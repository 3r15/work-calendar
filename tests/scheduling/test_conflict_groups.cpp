// 배타(conflictsWith) 그룹 처리 — ROADMAP Phase 3 테스트 목록 5, 6, 7.
//
//   5. 배타 그룹 2개 이상 공존
//   6. 배타가 한쪽에만 적혀도 대칭 동작
//   7. crossSetConflicts:true 일 때 집합 간 배타 적용 (D-002)
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

Task makeTask(const std::string& id, const std::string& name,
              const std::vector<TaskSetId>& sets, int required,
              const std::vector<TaskId>& conflicts = {}) {
    Task t;
    t.id = TaskId{id};
    t.name = name;
    t.taskSetIds = sets;
    t.requiredCount = required;
    t.conflictsWith = conflicts;
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

// 작업집합 하나 안에 서로 무관한 배타 그룹 두 개가 있을 때, 한 그룹에서의 배정이 다른
// 그룹의 배정을 막지 않아야 한다. pack1<->pack2 배타, label1<->label2 배타. 둘 사이는 무관.
TEST_CASE("two unrelated conflict groups coexist inside one task set", "[conflict_groups]") {
    const TaskSetId warehouse{"warehouse"};

    Task pack1 = makeTask("pack1", "포장1", {warehouse}, 1, {TaskId{"pack2"}});
    Task pack2 = makeTask("pack2", "포장2", {warehouse}, 1, {TaskId{"pack1"}});
    Task label1 = makeTask("label1", "라벨1", {warehouse}, 1, {TaskId{"label2"}});
    Task label2 = makeTask("label2", "라벨2", {warehouse}, 1, {TaskId{"label1"}});

    AssignInput input;
    input.date = util::Date::fromYmd(2026, 9, 4);
    input.taskSetId = warehouse;
    input.slotTasks = {pack1, pack2, label1, label2};
    input.availableWorkers = makeWorkers({"A", "B"});
    input.cursor = 0;

    const AssignResult result = AssignmentEngine{}.assign(input);
    INFO("배정 결과: " << dump(result));

    REQUIRE(result.unassigned.empty());
    REQUIRE(workersFor(result, "pack1") == std::vector<std::string>{"A"});
    REQUIRE(workersFor(result, "pack2") == std::vector<std::string>{"B"});
    REQUIRE(workersFor(result, "label1") == std::vector<std::string>{"A"});
    REQUIRE(workersFor(result, "label2") == std::vector<std::string>{"B"});

    // pack 그룹에서 A 를 썼다고 해서 label 그룹에서 A 가 막히지 않는다 — 그룹이 독립적이라는 증거.
    REQUIRE(workersFor(result, "pack1") == workersFor(result, "label1"));
}

// X.conflictsWith 에만 Y 가 적혀 있고 Y.conflictsWith 는 비어 있어도, 엔진은 이를 대칭으로
// 취급해야 한다. 작업자를 1명만 둬서, 대칭이 깨지면 Y 에도 같은 사람이 다시 배정되어 버린다.
TEST_CASE("conflict declared on only one side still blocks the other side", "[conflict_groups]") {
    const TaskSetId zone{"zone"};

    Task x = makeTask("x", "X작업", {zone}, 1, {TaskId{"y"}});  // X -> Y 로만 선언
    Task y = makeTask("y", "Y작업", {zone}, 1, {});             // Y 쪽은 비어 있다

    AssignInput input;
    input.date = util::Date::fromYmd(2026, 9, 4);
    input.taskSetId = zone;
    input.slotTasks = {x, y};
    input.availableWorkers = makeWorkers({"A"});  // 단 한 명
    input.cursor = 0;

    const AssignResult result = AssignmentEngine{}.assign(input);
    INFO("배정 결과: " << dump(result));

    REQUIRE(workersFor(result, "x") == std::vector<std::string>{"A"});
    // Y 쪽에 배타가 적혀 있지 않아도, X 에 이미 배정된 A 는 Y 에 배정될 수 없다.
    REQUIRE(workersFor(result, "y").empty());
    REQUIRE(result.unassigned.size() == 1);
    REQUIRE(result.unassigned[0].taskId.str() == "y");
}

// crossSetConflicts (D-002) — 기본은 작업집합 내부만 검사하고, true 면 시간대 전체로 넓어진다.
// security 집합의 guard 와 desk 집합의 register 가 서로 배타로 선언되어 있을 때,
// guard 를 먼저 채운 뒤 register 를 채우는 두 번째 호출에서 옵션에 따라 결과가 달라져야 한다.
TEST_CASE("crossSetConflicts controls whether conflicts reach across task sets",
          "[conflict_groups]") {
    const TaskSetId security{"security"};
    const TaskSetId desk{"desk"};

    Task guard = makeTask("guard", "경비", {security}, 1, {TaskId{"register"}});
    Task registerTask = makeTask("register", "카운터", {desk}, 1, {TaskId{"guard"}});

    // 1단계: security 집합의 guard 를 먼저 채운다. A 가 뽑힌다.
    AssignInput fillGuard;
    fillGuard.date = util::Date::fromYmd(2026, 9, 4);
    fillGuard.taskSetId = security;
    fillGuard.slotTasks = {guard, registerTask};
    fillGuard.availableWorkers = makeWorkers({"A", "B"});
    fillGuard.cursor = 0;

    const AssignResult guardResult = AssignmentEngine{}.assign(fillGuard);
    INFO("guard 배정: " << dump(guardResult));
    REQUIRE(workersFor(guardResult, "guard") == std::vector<std::string>{"A"});

    SECTION("crossSetConflicts:true 면 desk 집합도 guard 배정을 피한다") {
        AssignInput fillRegister;
        fillRegister.date = util::Date::fromYmd(2026, 9, 4);
        fillRegister.taskSetId = desk;
        fillRegister.slotTasks = {guard, registerTask};
        fillRegister.availableWorkers = makeWorkers({"A", "B"});
        fillRegister.cursor = 0;
        fillRegister.options.crossSetConflicts = true;
        fillRegister.priorAssignments = guardResult.assignments;

        const AssignResult result = AssignmentEngine{}.assign(fillRegister);
        INFO("register 배정: " << dump(result));

        // A 는 guard 로 이미 쓰였고 guard<->register 는 배타이므로 B 가 뽑힌다.
        REQUIRE(workersFor(result, "register") == std::vector<std::string>{"B"});
    }

    SECTION("crossSetConflicts:false(기본값) 면 desk 집합은 guard 배정을 모른다") {
        AssignInput fillRegister;
        fillRegister.date = util::Date::fromYmd(2026, 9, 4);
        fillRegister.taskSetId = desk;
        fillRegister.slotTasks = {guard, registerTask};
        fillRegister.availableWorkers = makeWorkers({"A", "B"});
        fillRegister.cursor = 0;
        fillRegister.options.crossSetConflicts = false;
        fillRegister.priorAssignments = guardResult.assignments;

        const AssignResult result = AssignmentEngine{}.assign(fillRegister);
        INFO("register 배정: " << dump(result));

        // 검사 범위가 desk 집합 안으로 좁혀지므로 guard 의 A 는 register 를 막지 못한다.
        REQUIRE(workersFor(result, "register") == std::vector<std::string>{"A"});
    }
}
