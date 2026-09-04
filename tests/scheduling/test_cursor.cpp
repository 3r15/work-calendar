// 커서 이월 — ROADMAP Phase 3 테스트 목록 8.
//
// 라운드 로빈 커서는 작업집합마다 하나이고, 다음 실행에 그대로 넘겨받는다 (SKILL.md).
// 커서가 실제로 이월되면 이틀 연속 실행했을 때 시작 인원이 달라져야 한다.
#include "core/scheduling/assignment_engine.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

using domain::Task;
using domain::TaskSetId;
using domain::Worker;
using domain::WorkerId;
using scheduling::AssignInput;
using scheduling::AssignmentEngine;
using scheduling::AssignResult;

namespace {

const TaskSetId kZone{"zone"};

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

Task makeVacuumTask() {
    Task t;
    t.id = domain::TaskId{"vacuum"};
    t.name = "청소기";
    t.taskSetIds = {kZone};
    t.requiredCount = 2;
    return t;
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
    os << "| nextCursor=" << result.nextCursor;
    return os.str();
}

}  // namespace

// 작업자 4명, 청소기(2)만 있는 작업집합. 매일 2명씩 뽑으면 이틀에 한 번 전체를 한 바퀴 돈다.
TEST_CASE("cursor carries over between consecutive runs", "[cursor]") {
    const AssignmentEngine engine;

    AssignInput day1;
    day1.date = util::Date::fromYmd(2026, 9, 4);  // 금
    day1.taskSetId = kZone;
    day1.slotTasks = {makeVacuumTask()};
    day1.availableWorkers = makeWorkers({"A", "B", "C", "D"});
    day1.cursor = 0;  // 첫 실행

    const AssignResult r1 = engine.assign(day1);
    INFO("1일차: " << dump(r1));
    REQUIRE(workersFor(r1, "vacuum") == std::vector<std::string>{"A", "B"});

    AssignInput day2 = day1;
    day2.date = util::Date::fromYmd(2026, 9, 5);  // 토
    day2.cursor = r1.nextCursor;                  // 어제 남긴 커서를 그대로 이어받는다

    const AssignResult r2 = engine.assign(day2);
    INFO("2일차: " << dump(r2));

    // 커서가 실제로 이월됐다면 이틀째는 A·B 가 아니라 C·D 로 시작해야 한다.
    REQUIRE(workersFor(r2, "vacuum") == std::vector<std::string>{"C", "D"});
    REQUIRE(workersFor(r1, "vacuum")[0] != workersFor(r2, "vacuum")[0]);

    AssignInput day3 = day1;
    day3.date = util::Date::fromYmd(2026, 9, 8);  // 화 (그 다음 근무일)
    day3.cursor = r2.nextCursor;

    const AssignResult r3 = engine.assign(day3);
    INFO("3일차: " << dump(r3));

    // 4명을 2명씩 두 번 돌면 정확히 한 바퀴 — 사흘째는 다시 A·B 로 돌아온다.
    REQUIRE(workersFor(r3, "vacuum") == std::vector<std::string>{"A", "B"});
}
