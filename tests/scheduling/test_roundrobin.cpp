// 기준 시나리오 (docs/DESIGN.md 3.3, scheduler-domain SKILL.md).
//
// 이 파일의 기대값은 변경 금지입니다 (CLAUDE.md 5장). 이 결과가 바뀌는 코드 변경은 설계 변경이므로
// 구현이 이 값을 못 맞추면 코드를 고치지 말고 먼저 물어보세요.
//
// 필요한 시그니처 (아직 core/scheduling/ 이 없어 컴파일이 실패합니다 — 구현자에게 주는 사양):
//   core/scheduling/assignment_engine.h
//     namespace scheduling {
//       struct Assignment { domain::TaskId taskId; domain::WorkerId workerId; };
//       struct Unassigned { domain::TaskId taskId; };
//       struct AssignResult {
//         std::vector<Assignment> assignments;
//         std::vector<Unassigned> unassigned;
//         int nextCursor{0};  // 다음 실행에 그대로 넘기는 커서. availableWorkers 인덱스 기준.
//       };
//       struct AssignInput {
//         util::Date date;                          // 요일 필터에 쓴다
//         domain::TaskSetId taskSetId;               // 이번 호출로 채우는 작업집합
//         std::vector<domain::Task> slotTasks;       // 같은 시간대의 모든 작업(다른 작업집합 포함 가능).
//                                                     // crossSetConflicts 검사 범위. taskSetId 소속이
//                                                     // 아닌 작업은 채워지지 않고 배타 검사에만 쓰인다.
//         std::vector<domain::Worker> availableWorkers;  // 이미 필터링된 가용 작업자, 정의 순서 유지
//         int cursor{0};                             // 이전 실행에서 이어받은 커서
//         domain::AssignmentOptions options;          // crossSetConflicts (D-002)
//         std::vector<Assignment> priorAssignments;   // 같은 시간대에서 먼저 채운 다른 작업집합의 결과.
//                                                      // crossSetConflicts:true 일 때만 적격성 검사에 쓴다.
//       };
//       class AssignmentEngine {
//        public:
//         AssignResult assign(const AssignInput& input) const;  // 파일 IO 없음
//       };
//     }
#include "core/scheduling/assignment_engine.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

using domain::Task;
using domain::TaskId;
using domain::TaskSetId;
using domain::Worker;
using domain::WorkerId;
using scheduling::AssignInput;
using scheduling::AssignmentEngine;
using scheduling::AssignResult;

namespace {

const TaskSetId kCleaning{"cleaning"};

std::vector<Worker> makeWorkers(const std::vector<std::string>& names) {
    std::vector<Worker> workers;
    for (const auto& n : names) {
        Worker w;
        w.id = WorkerId{n};
        w.name = n;
        workers.push_back(w);
    }
    return workers;
}

Task makeTask(const std::string& id, const std::string& name, int required,
              const std::vector<TaskId>& conflicts = {}) {
    Task t;
    t.id = TaskId{id};
    t.name = name;
    t.taskSetIds = {kCleaning};
    t.requiredCount = required;
    t.conflictsWith = conflicts;
    return t;
}

// 청소기(2)·밀대(2)·소독(2)·정리(2), 청소기<->밀대 배타.
std::vector<Task> makeBaselineTasks() {
    Task vacuum = makeTask("vacuum", "청소기", 2, {TaskId{"mop"}});
    Task mop = makeTask("mop", "밀대", 2, {TaskId{"vacuum"}});
    Task sanitize = makeTask("sanitize", "소독", 2);
    Task tidy = makeTask("tidy", "정리", 2);
    return {vacuum, mop, sanitize, tidy};
}

std::vector<std::string> workersFor(const AssignResult& result, const std::string& taskId) {
    std::vector<std::string> out;
    for (const auto& a : result.assignments) {
        if (a.taskId.str() == taskId) out.push_back(a.workerId.str());
    }
    return out;
}

std::string dump(const AssignResult& result) {
    std::ostringstream os;
    os << "assigned: ";
    for (const auto& a : result.assignments) {
        os << a.taskId.str() << "=" << a.workerId.str() << " ";
    }
    os << "| unassigned: ";
    for (const auto& u : result.unassigned) os << u.taskId.str() << " ";
    os << "| nextCursor=" << result.nextCursor;
    return os.str();
}

}  // namespace

// docs/DESIGN.md 3.3 / SKILL.md 기준 시나리오.
// 작업자 4명(A B C D), 청소기(2)·밀대(2)·소독(2)·정리(2), 청소기<->밀대 배타
// -> 청소기 A·B / 밀대 C·D / 소독 A·B / 정리 C·D
TEST_CASE("baseline round robin scenario matches the locked table", "[roundrobin]") {
    AssignInput input;
    input.date = util::Date::fromYmd(2026, 9, 4);
    input.taskSetId = kCleaning;
    input.slotTasks = makeBaselineTasks();
    input.availableWorkers = makeWorkers({"A", "B", "C", "D"});
    input.cursor = 0;

    const AssignResult result = AssignmentEngine{}.assign(input);
    INFO("배정 결과: " << dump(result));

    REQUIRE(result.unassigned.empty());
    REQUIRE(workersFor(result, "vacuum") == std::vector<std::string>{"A", "B"});
    REQUIRE(workersFor(result, "mop") == std::vector<std::string>{"C", "D"});
    REQUIRE(workersFor(result, "sanitize") == std::vector<std::string>{"A", "B"});
    REQUIRE(workersFor(result, "tidy") == std::vector<std::string>{"C", "D"});
}
