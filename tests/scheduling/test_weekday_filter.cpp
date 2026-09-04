// 요일 필터 — ROADMAP Phase 3 테스트 목록 4.
//
// Task.weekdays 가 비어 있으면 근무일 전체, 채워져 있으면 그 요일에만 대상이 된다
// (docs/DESIGN.md 3.2 step 1). 대상에서 빠진 작업은 배정도, 미배정 기록도 되지 않는다.
#include "core/scheduling/assignment_engine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

using domain::Task;
using domain::TaskId;
using domain::TaskSetId;
using domain::Weekday;
using domain::Worker;
using domain::WorkerId;
using scheduling::AssignInput;
using scheduling::AssignmentEngine;
using scheduling::AssignResult;
using scheduling::Unassigned;

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

Task makeTask(const std::string& id, const std::string& name, int required,
              const std::vector<Weekday>& weekdays) {
    Task t;
    t.id = TaskId{id};
    t.name = name;
    t.taskSetIds = {kZone};
    t.requiredCount = required;
    t.weekdays = weekdays;
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
    os << "| unassigned: ";
    for (const auto& u : result.unassigned) os << u.taskId.str() << " ";
    return os.str();
}

}  // namespace

// 2026-09-04 는 금, 09-05 는 토, 09-08 은 화 (tests/domain/test_day_plan.cpp 와 같은 기준일).
TEST_CASE("tasks restricted to specific weekdays are filtered by the given date",
          "[weekday_filter]") {
    Task everyday = makeTask("daily", "매일작업", 1, {});                  // 근무일 전체
    Task saturdayOnly = makeTask("sat_only", "토요전용", 1, {Weekday::Sat});  // 토요일만

    AssignInput input;
    input.taskSetId = kZone;
    input.slotTasks = {everyday, saturdayOnly};
    input.availableWorkers = makeWorkers({"A", "B"});
    input.cursor = 0;

    SECTION("화요일에는 토요일 전용 작업이 대상에서 빠진다") {
        input.date = util::Date::fromYmd(2026, 9, 8);  // 화

        const AssignResult result = AssignmentEngine{}.assign(input);
        INFO("배정 결과: " << dump(result));

        REQUIRE(workersFor(result, "daily") == std::vector<std::string>{"A"});
        REQUIRE(workersFor(result, "sat_only").empty());

        // 대상에서 아예 빠지므로 미배정 슬롯으로도 기록되지 않는다.
        const bool mentionsSaturdayOnly =
            std::any_of(result.unassigned.begin(), result.unassigned.end(),
                        [](const Unassigned& u) { return u.taskId.str() == "sat_only"; });
        REQUIRE_FALSE(mentionsSaturdayOnly);
    }

    SECTION("토요일에는 두 작업 모두 대상이 된다") {
        input.date = util::Date::fromYmd(2026, 9, 5);  // 토

        const AssignResult result = AssignmentEngine{}.assign(input);
        INFO("배정 결과: " << dump(result));

        REQUIRE(workersFor(result, "daily") == std::vector<std::string>{"A"});
        REQUIRE(workersFor(result, "sat_only") == std::vector<std::string>{"B"});
    }
}
