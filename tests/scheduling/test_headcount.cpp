// 인원 부족·초과·전무 — ROADMAP Phase 3 테스트 목록 2, 3, 10.
//
// 미배정은 실패가 아니라 정상 결과다. 예외를 기대하지 않는다.
#include "core/scheduling/assignment_engine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

// 기준 시나리오와 같은 4개 작업. 청소기<->밀대 배타.
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

AssignInput baseInput() {
    AssignInput input;
    input.date = util::Date::fromYmd(2026, 9, 4);
    input.taskSetId = kCleaning;
    input.slotTasks = makeBaselineTasks();
    input.cursor = 0;
    return input;
}

}  // namespace

// scheduler-domain SKILL.md: 3명(A B C)이면
// 청소기 A·B / 밀대 C·미배정 1 / 소독 A·B / 정리 C·A
TEST_CASE("understaffed pool leaves exactly one unassigned slot", "[headcount]") {
    AssignInput input = baseInput();
    input.availableWorkers = makeWorkers({"A", "B", "C"});

    const AssignResult result = AssignmentEngine{}.assign(input);
    INFO("배정 결과: " << dump(result));

    REQUIRE(workersFor(result, "vacuum") == std::vector<std::string>{"A", "B"});
    REQUIRE(workersFor(result, "mop") == std::vector<std::string>{"C"});
    REQUIRE(workersFor(result, "sanitize") == std::vector<std::string>{"A", "B"});
    REQUIRE(workersFor(result, "tidy") == std::vector<std::string>{"C", "A"});

    REQUIRE(result.unassigned.size() == 1);
    REQUIRE(result.unassigned[0].taskId.str() == "mop");
}

// 슬롯 합계는 8(2+2+2+2). 10명을 넣으면 8명만 쓰이고 2명은 그대로 남는다 (D-004).
TEST_CASE("overstaffed pool has no duplicate assignment and leaves workers unused", "[headcount]") {
    AssignInput input = baseInput();
    input.availableWorkers =
        makeWorkers({"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"});

    const AssignResult result = AssignmentEngine{}.assign(input);
    INFO("배정 결과: " << dump(result));

    REQUIRE(result.unassigned.empty());
    REQUIRE(result.assignments.size() == 8);

    SECTION("아무도 두 번 이상 겹쳐 배정되지 않는다") {
        std::vector<std::string> used;
        for (const auto& a : result.assignments) used.push_back(a.workerId.str());
        std::sort(used.begin(), used.end());

        std::ostringstream usedList;
        for (const auto& n : used) usedList << n << " ";
        INFO("배정된 인원(정렬): " << usedList.str());

        REQUIRE(std::adjacent_find(used.begin(), used.end()) == used.end());
    }

    SECTION("커서 순서대로 채워지고 남는 사람이 존재한다") {
        REQUIRE(workersFor(result, "vacuum") == std::vector<std::string>{"A", "B"});
        REQUIRE(workersFor(result, "mop") == std::vector<std::string>{"C", "D"});
        REQUIRE(workersFor(result, "sanitize") == std::vector<std::string>{"E", "F"});
        REQUIRE(workersFor(result, "tidy") == std::vector<std::string>{"G", "H"});

        std::vector<std::string> used;
        for (const auto& a : result.assignments) used.push_back(a.workerId.str());
        REQUIRE(std::find(used.begin(), used.end(), "I") == used.end());
        REQUIRE(std::find(used.begin(), used.end(), "J") == used.end());
    }
}

// 가용 인원이 0명이어도 크래시 없이 전부 미배정으로 기록된다.
TEST_CASE("zero available workers leaves everything unassigned without crashing", "[headcount]") {
    AssignInput input = baseInput();
    input.availableWorkers = {};

    const AssignResult result = AssignmentEngine{}.assign(input);
    INFO("배정 결과: " << dump(result));

    REQUIRE(result.assignments.empty());
    REQUIRE(result.unassigned.size() == 8);  // 2+2+2+2, 전부 미배정
    REQUIRE(result.nextCursor == 0);         // 아무도 못 골랐으니 커서는 그대로다
}
