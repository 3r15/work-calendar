#include "core/app/absence_service.h"

#include "core/app/assign_service.h"
#include "core/app/day_view.h"
#include "core/app/worker_lookup.h"
#include "core/domain/availability.h"
#include "core/storage/json_io.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

util::DateTime moment(unsigned day, int hour, int minute) {
    util::DateTime out;
    out.date = util::Date::fromYmd(2026, 9, day);
    out.time = util::TimeOfDay::fromHm(hour, minute);
    out.utcOffsetMinutes = 540;
    return out;
}

class Sandbox {
public:
    explicit Sandbox(const std::string& name)
        : dir_(fs::temp_directory_path() / ("sched_off_" + name)) {
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        for (const char* file : {"config.json", "workers.json", "tasks.json", "absences.json"}) {
            fs::copy_file(fs::path{SCHED_DATA_SAMPLE_DIR} / file, dir_ / file);
        }
    }
    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    const fs::path& dir() const { return dir_; }

    domain::Model load() const {
        const util::Result<storage::LoadedModel> loaded = storage::loadModel(dir_);
        REQUIRE(loaded.ok());
        return loaded.value().model;
    }

private:
    fs::path dir_;
};

// 이 사람이 그날 (휴무) 로 보이는 건수.
int absentMarks(const domain::Model& model, const util::Date& date,
                const domain::DaySnapshot& snapshot) {
    const app::DayView view =
        app::buildDayView(model, date, util::DateTime{date, util::TimeOfDay::fromHm(9, 0)},
                          &snapshot);
    int marks = 0;
    for (const app::SlotBlock& slot : view.slots) {
        for (const app::CategoryBlock& category : slot.categories) {
            for (const app::TaskSetBlock& set : category.taskSets) {
                for (const app::TaskLine& task : set.tasks) {
                    for (const app::AssignedWorker& worker : task.workers) {
                        if (worker.absent) {
                            marks += 1;
                        }
                    }
                }
            }
        }
    }
    return marks;
}

}  // namespace

TEST_CASE("resolves a worker by id or by name", "[worker_lookup]") {
    Sandbox sandbox{"lookup"};
    domain::Model model = sandbox.load();

    SECTION("ID 로 찾는다") {
        const app::WorkerMatch match = app::resolveWorker(model, "w_kim");
        REQUIRE(match.kind == app::WorkerMatch::Kind::Found);
        REQUIRE(match.worker.name == "김철수");
    }
    SECTION("이름으로 찾는다") {
        const app::WorkerMatch match = app::resolveWorker(model, "이영희");
        REQUIRE(match.kind == app::WorkerMatch::Kind::Found);
        REQUIRE(match.worker.id.str() == "w_lee");
    }
    SECTION("없으면 NotFound") {
        REQUIRE(app::resolveWorker(model, "홍길동").kind == app::WorkerMatch::Kind::NotFound);
        REQUIRE(app::resolveWorker(model, "").kind == app::WorkerMatch::Kind::NotFound);
    }
    SECTION("이름이 겹치면 후보를 돌려주고 고르지 않는다") {
        model.workers.workers[1].name = "김철수";  // 이영희를 김철수로
        const app::WorkerMatch match = app::resolveWorker(model, "김철수");
        REQUIRE(match.kind == app::WorkerMatch::Kind::Ambiguous);
        REQUIRE(match.candidates.size() == 2);
    }
    SECTION("ID 가 이름보다 우선한다") {
        model.workers.workers[1].name = "w_kim";  // 이영희의 이름을 김철수의 ID 로
        const app::WorkerMatch match = app::resolveWorker(model, "w_kim");
        REQUIRE(match.kind == app::WorkerMatch::Kind::Found);
        REQUIRE(match.worker.id.str() == "w_kim");
    }
}

// 완료 기준 3번: 사전 휴무자는 애초에 배정되지 않는다.
TEST_CASE("a worker absent before assignment is never picked", "[absence]") {
    Sandbox sandbox{"before"};
    domain::Model model = sandbox.load();
    const util::Date date = util::Date::fromYmd(2026, 9, 8);

    REQUIRE(app::addAbsence(sandbox.dir(), model, domain::WorkerId{"w_choi"}, date, date, "연차",
                            moment(4, 10, 0))
                .ok());
    REQUIRE(domain::isAbsentOn(model, domain::WorkerId{"w_choi"}, date));

    domain::CursorState cursors;
    const domain::DaySnapshot snapshot = app::computeDay(model, date, moment(8, 9, 1), cursors);

    for (const domain::SnapshotSlot& slot : snapshot.slots) {
        for (const domain::SnapshotTaskSet& set : slot.taskSets) {
            for (const domain::SnapshotAssignment& assignment : set.assignments) {
                REQUIRE_FALSE(assignment.workerId == domain::WorkerId{"w_choi"});
            }
        }
    }
    // 4명이 3명이 되면 슬롯을 다 못 채운다. 실패가 아니라 미배정으로 기록된다.
    REQUIRE(domain::countUnassigned(snapshot) > 0);
}

// 완료 기준 1·2번: 당일 급휴는 배정을 유지하고 표시만 바꾼다. 취소하면 되돌아온다 (D-009).
TEST_CASE("same day absence keeps assignments and only changes the display", "[absence]") {
    Sandbox sandbox{"sameday"};
    domain::Model model = sandbox.load();
    const util::Date date = util::Date::fromYmd(2026, 9, 4);

    const util::Result<app::AssignOutcome> assigned =
        app::ensureSnapshot(sandbox.dir(), model, date, moment(4, 9, 1));
    REQUIRE(assigned.ok());
    const domain::DaySnapshot& snapshot = assigned.value().snapshot;
    const int assignmentsBefore = static_cast<int>(snapshot.slots.size());
    REQUIRE(assignmentsBefore > 0);
    REQUIRE(absentMarks(model, date, snapshot) == 0);

    // 배정이 이미 나간 뒤에 휴무를 등록한다.
    const util::Result<app::AbsenceChange> change =
        app::addAbsence(sandbox.dir(), model, domain::WorkerId{"w_choi"}, date, date,
                        "당일 휴무", moment(4, 10, 0));
    REQUIRE(change.ok());

    SECTION("급휴로 판정되고 영향받는 건수를 알려준다") {
        REQUIRE(change.value().urgent);
        REQUIRE(change.value().affectedAssignments > 0);
    }

    SECTION("배정은 그대로 남고 표시만 (휴무) 가 된다") {
        const util::Result<app::AssignOutcome> after =
            app::ensureSnapshot(sandbox.dir(), model, date, moment(4, 10, 1));
        REQUIRE(after.ok());
        // 다시 계산하지 않았다.
        REQUIRE_FALSE(after.value().computed);
        REQUIRE(storage::toJsonText(after.value().snapshot) == storage::toJsonText(snapshot));
        // 그런데 조회하면 (휴무) 로 보인다.
        REQUIRE(absentMarks(model, date, after.value().snapshot) ==
                change.value().affectedAssignments);
    }

    SECTION("휴무를 취소하면 (휴무) 가 사라진다") {
        const util::Result<int> removed =
            app::removeAbsence(sandbox.dir(), model, domain::WorkerId{"w_choi"}, date);
        REQUIRE(removed.ok());
        REQUIRE(removed.value() == 1);
        REQUIRE(absentMarks(model, date, snapshot) == 0);
    }
}

// 완료 기준 4번: --force 로 다시 계산하면 휴무자가 빠진다.
TEST_CASE("force recompute drops the absent worker", "[absence]") {
    Sandbox sandbox{"force"};
    domain::Model model = sandbox.load();
    const util::Date date = util::Date::fromYmd(2026, 9, 4);

    REQUIRE(app::ensureSnapshot(sandbox.dir(), model, date, moment(4, 9, 1)).ok());
    const util::Result<app::AbsenceChange> change =
        app::addAbsence(sandbox.dir(), model, domain::WorkerId{"w_choi"}, date, date,
                        "당일 휴무", moment(4, 10, 0));
    REQUIRE(change.ok());
    const int heldByAbsentee = change.value().affectedAssignments;
    REQUIRE(heldByAbsentee > 0);

    const util::Result<app::AssignOutcome> forced =
        app::ensureSnapshot(sandbox.dir(), model, date, moment(4, 10, 5), /*force=*/true);
    REQUIRE(forced.ok());
    REQUIRE(forced.value().computed);

    // 휴무자에게 남은 배정이 없다.
    REQUIRE(absentMarks(model, date, forced.value().snapshot) == 0);
    // 실제로 사람이 없는 자리는 줄어든다: 휴무자가 붙들고 있던 자리보다 미배정이 적다.
    REQUIRE(domain::countUnassigned(forced.value().snapshot) < heldByAbsentee);
}

TEST_CASE("absence range covers every day inside it", "[absence]") {
    Sandbox sandbox{"range"};
    domain::Model model = sandbox.load();

    REQUIRE(app::addAbsence(sandbox.dir(), model, domain::WorkerId{"w_kim"},
                            util::Date::fromYmd(2026, 9, 8), util::Date::fromYmd(2026, 9, 10),
                            "연차", moment(4, 10, 0))
                .ok());

    REQUIRE_FALSE(domain::isAbsentOn(model, domain::WorkerId{"w_kim"},
                                     util::Date::fromYmd(2026, 9, 7)));
    REQUIRE(domain::isAbsentOn(model, domain::WorkerId{"w_kim"}, util::Date::fromYmd(2026, 9, 8)));
    REQUIRE(domain::isAbsentOn(model, domain::WorkerId{"w_kim"}, util::Date::fromYmd(2026, 9, 9)));
    REQUIRE(domain::isAbsentOn(model, domain::WorkerId{"w_kim"}, util::Date::fromYmd(2026, 9, 10)));
    REQUIRE_FALSE(domain::isAbsentOn(model, domain::WorkerId{"w_kim"},
                                     util::Date::fromYmd(2026, 9, 11)));

    SECTION("가운데 하루를 지우면 기간 전체가 사라진다") {
        const util::Result<int> removed = app::removeAbsence(
            sandbox.dir(), model, domain::WorkerId{"w_kim"}, util::Date::fromYmd(2026, 9, 9));
        REQUIRE(removed.ok());
        REQUIRE(removed.value() == 1);
        REQUIRE_FALSE(
            domain::isAbsentOn(model, domain::WorkerId{"w_kim"}, util::Date::fromYmd(2026, 9, 8)));
    }
}

TEST_CASE("rejects a range that runs backwards", "[absence]") {
    Sandbox sandbox{"backwards"};
    domain::Model model = sandbox.load();

    const util::Result<app::AbsenceChange> change =
        app::addAbsence(sandbox.dir(), model, domain::WorkerId{"w_kim"},
                        util::Date::fromYmd(2026, 9, 10), util::Date::fromYmd(2026, 9, 8), "",
                        moment(4, 10, 0));
    REQUIRE_FALSE(change.ok());
    REQUIRE(change.error().code == util::ErrorCode::InvalidUsage);
    // 저장까지 가지 않았다.
    REQUIRE(model.absences.absences.size() == 1);  // 샘플에 원래 있던 한 건뿐
}

TEST_CASE("removing a day with no absence changes nothing", "[absence]") {
    Sandbox sandbox{"noop"};
    domain::Model model = sandbox.load();
    const std::size_t before = model.absences.absences.size();

    const util::Result<int> removed = app::removeAbsence(
        sandbox.dir(), model, domain::WorkerId{"w_kim"}, util::Date::fromYmd(2026, 9, 4));
    REQUIRE(removed.ok());
    REQUIRE(removed.value() == 0);
    REQUIRE(model.absences.absences.size() == before);
}

// 정기 휴무는 absences.json 이 아니라 workers.json 이다.
TEST_CASE("weekly off is stored on the worker", "[absence]") {
    Sandbox sandbox{"weekly"};
    domain::Model model = sandbox.load();

    REQUIRE(app::setWeeklyOff(sandbox.dir(), model, domain::WorkerId{"w_kim"},
                              {domain::Weekday::Wed, domain::Weekday::Thu})
                .ok());

    const domain::Model reloaded = sandbox.load();
    REQUIRE(reloaded.workers.workers[0].weeklyOff.size() == 2);
    REQUIRE(domain::contains(reloaded.workers.workers[0].weeklyOff, domain::Weekday::Wed));

    // 2026-09-09 는 수요일 — 정기 휴무라 가용 인원에서 빠진다.
    const std::vector<domain::Worker> available =
        domain::availableWorkers(reloaded, util::Date::fromYmd(2026, 9, 9));
    for (const domain::Worker& worker : available) {
        REQUIRE_FALSE(worker.id == domain::WorkerId{"w_kim"});
    }
}

TEST_CASE("listing filters by date and worker", "[absence]") {
    Sandbox sandbox{"list"};
    domain::Model model = sandbox.load();
    // 샘플에는 최지우의 2026-09-10 ~ 09-12 연차가 하나 있다.

    REQUIRE(app::listAbsences(model, nullptr, nullptr).size() == 1);

    const util::Date inside = util::Date::fromYmd(2026, 9, 11);
    const util::Date outside = util::Date::fromYmd(2026, 9, 4);
    REQUIRE(app::listAbsences(model, &inside, nullptr).size() == 1);
    REQUIRE(app::listAbsences(model, &outside, nullptr).empty());

    const domain::WorkerId choi{"w_choi"};
    const domain::WorkerId kim{"w_kim"};
    REQUIRE(app::listAbsences(model, nullptr, &choi).size() == 1);
    REQUIRE(app::listAbsences(model, nullptr, &kim).empty());
}
