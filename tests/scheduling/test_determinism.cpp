// 결정론 — ROADMAP Phase 3 테스트 목록 9.
//
// 같은 날 같은 입력으로 여러 번 계산해도 배정이 달라지면 안 된다 (docs/DESIGN.md 3.1).
// 실제 운영에서는 스냅샷을 한 번만 계산하고 재조회 때는 파일을 읽지만, 엔진 자체가
// 파일 IO를 하지 않으므로 "같은 입력 -> 같은 출력" 을 엔진 수준에서 보장해야 그 위의
// 스냅샷 계층이 의미를 갖는다.
#include "core/scheduling/assignment_engine.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
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

// 일부러 인원을 부족하게 둬서(3명) 미배정이 섞인 경우에도 결정론이 지켜지는지 본다.
std::vector<Task> makeBaselineTasks() {
    Task vacuum = makeTask("vacuum", "청소기", 2, {TaskId{"mop"}});
    Task mop = makeTask("mop", "밀대", 2, {TaskId{"vacuum"}});
    Task sanitize = makeTask("sanitize", "소독", 2);
    Task tidy = makeTask("tidy", "정리", 2);
    return {vacuum, mop, sanitize, tidy};
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

TEST_CASE("same input produces the same result every time", "[determinism]") {
    AssignInput input;
    input.date = util::Date::fromYmd(2026, 9, 4);
    input.taskSetId = kCleaning;
    input.slotTasks = makeBaselineTasks();
    input.availableWorkers = makeWorkers({"A", "B", "C"});  // 인원 부족까지 포함해 검증
    input.cursor = 0;

    const AssignmentEngine engine;
    const AssignResult first = engine.assign(input);
    const AssignResult second = engine.assign(input);

    INFO("1회차: " << dump(first));
    INFO("2회차: " << dump(second));

    REQUIRE(first.assignments.size() == second.assignments.size());
    for (std::size_t i = 0; i < first.assignments.size(); ++i) {
        REQUIRE(first.assignments[i].taskId.str() == second.assignments[i].taskId.str());
        REQUIRE(first.assignments[i].workerId.str() == second.assignments[i].workerId.str());
    }

    REQUIRE(first.unassigned.size() == second.unassigned.size());
    for (std::size_t i = 0; i < first.unassigned.size(); ++i) {
        REQUIRE(first.unassigned[i].taskId.str() == second.unassigned[i].taskId.str());
    }

    REQUIRE(first.nextCursor == second.nextCursor);
}

// 결정론은 "같은 객체를 두 번 부른다" 뿐 아니라 "같은 값으로 새로 만들어도 같다" 는 뜻이다.
// 완전히 새로 구성한 입력으로도 결과가 같은지 확인한다.
TEST_CASE("freshly constructed identical input still yields identical output", "[determinism]") {
    auto buildInput = []() {
        AssignInput input;
        input.date = util::Date::fromYmd(2026, 9, 4);
        input.taskSetId = kCleaning;
        input.slotTasks = makeBaselineTasks();
        input.availableWorkers = makeWorkers({"A", "B", "C", "D"});
        input.cursor = 0;
        return input;
    };

    const AssignmentEngine engine;
    const AssignResult a = engine.assign(buildInput());
    const AssignResult b = engine.assign(buildInput());

    INFO("a: " << dump(a));
    INFO("b: " << dump(b));

    REQUIRE(a.assignments.size() == b.assignments.size());
    for (std::size_t i = 0; i < a.assignments.size(); ++i) {
        REQUIRE(a.assignments[i].taskId.str() == b.assignments[i].taskId.str());
        REQUIRE(a.assignments[i].workerId.str() == b.assignments[i].workerId.str());
    }
    REQUIRE(a.nextCursor == b.nextCursor);
}
