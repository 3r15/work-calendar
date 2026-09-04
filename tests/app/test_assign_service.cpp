#include "core/app/assign_service.h"

#include "core/domain/availability.h"
#include "core/storage/json_io.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path sampleDir() {
    return fs::path{SCHED_DATA_SAMPLE_DIR};
}

domain::Model loadSample() {
    const util::Result<storage::LoadedModel> loaded = storage::loadModel(sampleDir());
    REQUIRE(loaded.ok());
    return loaded.value().model;
}

util::DateTime moment(unsigned day, int hour, int minute) {
    util::DateTime out;
    out.date = util::Date::fromYmd(2026, 9, day);
    out.time = util::TimeOfDay::fromHm(hour, minute);
    out.utcOffsetMinutes = 540;  // 한국
    return out;
}

// 데이터 폴더를 복사해 쓰고 끝나면 지운다. 스냅샷을 쓰므로 원본을 건드리면 안 된다.
class Sandbox {
public:
    explicit Sandbox(const std::string& name)
        : dir_(fs::temp_directory_path() / ("sched_assign_" + name)) {
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        for (const char* file : {"config.json", "workers.json", "tasks.json", "absences.json"}) {
            fs::copy_file(sampleDir() / file, dir_ / file);
        }
    }
    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    const fs::path& dir() const { return dir_; }

private:
    fs::path dir_;
};

// 같은 작업집합이 오전·오후 분류 양쪽에 붙어 있을 수 있으므로(샘플의 배치구역이 그렇다)
// 시간대를 지정해 좁힌다. timeSlotId 가 비어 있으면 그날 전체를 본다.
std::vector<std::string> workersOf(const domain::DaySnapshot& snapshot, const char* taskSetId,
                                   const char* taskId, const char* timeSlotId = "") {
    std::vector<std::string> out;
    for (const domain::SnapshotSlot& slot : snapshot.slots) {
        if (timeSlotId[0] != '\0' && !(slot.timeSlotId == domain::TimeSlotId{timeSlotId})) {
            continue;
        }
        for (const domain::SnapshotTaskSet& set : slot.taskSets) {
            if (!(set.taskSetId == domain::TaskSetId{taskSetId})) {
                continue;
            }
            for (const domain::SnapshotAssignment& assignment : set.assignments) {
                if (assignment.taskId == domain::TaskId{taskId}) {
                    out.push_back(assignment.workerId.str());
                }
            }
        }
    }
    return out;
}

}  // namespace

// 샘플 데이터의 평일 오전은 청소기·밀대·소독·정리 각 2명에 작업자 4명 — DESIGN 3.3 과 같은 모양이다.
TEST_CASE("computes a day from the sample data", "[assign_service]") {
    const domain::Model model = loadSample();
    domain::CursorState cursors;

    const domain::DaySnapshot snapshot =
        app::computeDay(model, util::Date::fromYmd(2026, 9, 4), moment(4, 9, 1), cursors);

    REQUIRE(snapshot.date == "2026-09-04");
    REQUIRE(snapshot.weekday == domain::Weekday::Fri);
    REQUIRE(snapshot.generatedAt == "2026-09-04T09:01:00+09:00");
    // 평일 오전·오후 두 시간대, 각각 분류 하나.
    REQUIRE(snapshot.slots.size() == 2);

    SECTION("평일 오전 청소 집합이 기준 시나리오와 같다") {
        REQUIRE(workersOf(snapshot, "cleaning_am", "vacuum") ==
                std::vector<std::string>{"w_kim", "w_lee"});
        REQUIRE(workersOf(snapshot, "cleaning_am", "mop") ==
                std::vector<std::string>{"w_park", "w_choi"});
        REQUIRE(workersOf(snapshot, "cleaning_am", "sanitize") ==
                std::vector<std::string>{"w_kim", "w_lee"});
        REQUIRE(workersOf(snapshot, "cleaning_am", "tidy") ==
                std::vector<std::string>{"w_park", "w_choi"});
    }
}

// 같은 날을 두 번 계산하면 같은 결과여야 한다 (DESIGN 3.1).
TEST_CASE("computing the same day twice gives the same result", "[assign_service]") {
    const domain::Model model = loadSample();

    domain::CursorState first;
    domain::CursorState second;
    const domain::DaySnapshot a =
        app::computeDay(model, util::Date::fromYmd(2026, 9, 4), moment(4, 9, 1), first);
    const domain::DaySnapshot b =
        app::computeDay(model, util::Date::fromYmd(2026, 9, 4), moment(4, 9, 1), second);

    REQUIRE(storage::toJsonText(a) == storage::toJsonText(b));
    REQUIRE(first.cursors == second.cursors);
}

// 커서는 작업집합마다 하나이고 계산하면 전진한다.
TEST_CASE("cursors advance per task set", "[assign_service]") {
    const domain::Model model = loadSample();
    domain::CursorState cursors;

    app::computeDay(model, util::Date::fromYmd(2026, 9, 4), moment(4, 9, 1), cursors);

    REQUIRE(cursors.cursors.count(domain::TaskSetId{"cleaning_am"}) == 1);
    REQUIRE(cursors.cursors.count(domain::TaskSetId{"zone"}) == 1);
    // 주말 집합은 금요일에 등장하지 않으므로 커서가 생기지 않는다.
    REQUIRE(cursors.cursors.count(domain::TaskSetId{"cleaning_weekend"}) == 0);
}

// 스냅샷이 없으면 계산해서 저장하고, 있으면 읽기만 한다.
TEST_CASE("snapshot is written once and read back afterwards", "[assign_service]") {
    Sandbox sandbox{"once"};
    const util::Result<storage::LoadedModel> loaded = storage::loadModel(sandbox.dir());
    REQUIRE(loaded.ok());
    const domain::Model& model = loaded.value().model;
    const util::Date date = util::Date::fromYmd(2026, 9, 4);

    const util::Result<app::AssignOutcome> first =
        app::ensureSnapshot(sandbox.dir(), model, date, moment(4, 9, 1));
    REQUIRE(first.ok());
    REQUIRE(first.value().computed);
    REQUIRE(fs::exists(storage::snapshotPath(sandbox.dir(), "2026-09-04")));
    REQUIRE(fs::exists(sandbox.dir() / "state" / "rr-cursor.json"));

    SECTION("두 번째 호출은 계산하지 않고 읽기만 한다") {
        // 다른 시각을 줘도 저장된 generatedAt 이 그대로여야 한다 — 다시 계산하지 않았다는 증거.
        const util::Result<app::AssignOutcome> second =
            app::ensureSnapshot(sandbox.dir(), model, date, moment(4, 15, 30));
        REQUIRE(second.ok());
        REQUIRE_FALSE(second.value().computed);
        REQUIRE(second.value().snapshot.generatedAt == "2026-09-04T09:01:00+09:00");
        REQUIRE(storage::toJsonText(second.value().snapshot) ==
                storage::toJsonText(first.value().snapshot));
    }
}

// 커서가 파일을 거쳐 다음 근무일로 이어진다.
//
// 평일 오후(cleaning_pm)는 청소기2+밀대2+정리2+쓰레기1 = 슬롯 7개인데 작업자는 4명이다.
// 한 바퀴가 딱 떨어지지 않으므로 다음 날 시작 인원이 달라진다 — 커서 이월을 눈으로 확인할 수 있는
// 자리다. (배치구역은 하루에 4슬롯을 써서 정확히 한 바퀴를 돌므로 이 검증에 쓸 수 없다.)
TEST_CASE("cursor carries across days through the file", "[assign_service]") {
    Sandbox sandbox{"carry"};
    const util::Result<storage::LoadedModel> loaded = storage::loadModel(sandbox.dir());
    REQUIRE(loaded.ok());
    const domain::Model& model = loaded.value().model;

    const util::Result<app::AssignOutcome> friday = app::ensureSnapshot(
        sandbox.dir(), model, util::Date::fromYmd(2026, 9, 4), moment(4, 9, 1));
    REQUIRE(friday.ok());
    REQUIRE(friday.value().computed);

    const std::vector<std::string> fridayVacuum =
        workersOf(friday.value().snapshot, "cleaning_pm", "vacuum", "pm_weekday");
    REQUIRE(fridayVacuum == std::vector<std::string>{"w_kim", "w_lee"});

    SECTION("커서가 파일에 남는다") {
        const util::Result<storage::Loaded<domain::CursorState>> cursors =
            storage::loadCursors(sandbox.dir() / "state" / "rr-cursor.json");
        REQUIRE(cursors.ok());
        // 슬롯 7개를 4명이 나눠 가지면 커서는 3에서 멈춘다.
        REQUIRE(cursors.value().value.cursors.at(domain::TaskSetId{"cleaning_pm"}) == 3);
    }

    SECTION("다음 근무일은 이어받은 커서에서 시작한다") {
        // 2026-09-08 은 화요일. 토·일·월을 건너뛰어도 커서는 파일에 남아 있다.
        const util::Result<app::AssignOutcome> tuesday = app::ensureSnapshot(
            sandbox.dir(), model, util::Date::fromYmd(2026, 9, 8), moment(8, 9, 1));
        REQUIRE(tuesday.ok());

        const std::vector<std::string> tuesdayVacuum =
            workersOf(tuesday.value().snapshot, "cleaning_pm", "vacuum", "pm_weekday");
        // 커서 3 에서 시작하므로 최지우부터다. 어제 먼저 시작한 김철수가 아니다.
        REQUIRE(tuesdayVacuum == std::vector<std::string>{"w_choi", "w_kim"});
        REQUIRE(tuesdayVacuum != fridayVacuum);
    }
}

// 사전 휴무자는 애초에 배정되지 않는다.
TEST_CASE("workers absent on the day are not assigned", "[assign_service]") {
    domain::Model model = loadSample();
    // 샘플의 최지우는 2026-09-10 ~ 09-12 연차다.
    const util::Date during = util::Date::fromYmd(2026, 9, 10);
    REQUIRE(domain::isAbsentOn(model, domain::WorkerId{"w_choi"}, during));

    domain::CursorState cursors;
    const domain::DaySnapshot snapshot =
        app::computeDay(model, during, moment(10, 9, 1), cursors);

    for (const domain::SnapshotSlot& slot : snapshot.slots) {
        for (const domain::SnapshotTaskSet& set : slot.taskSets) {
            for (const domain::SnapshotAssignment& assignment : set.assignments) {
                INFO("배정: " << assignment.taskId.str() << " -> "
                              << assignment.workerId.str());
                REQUIRE_FALSE(assignment.workerId == domain::WorkerId{"w_choi"});
            }
        }
    }
}

// 인원이 모자라면 미배정으로 기록되고 예외를 던지지 않는다.
TEST_CASE("shortage becomes unassigned slots not an error", "[assign_service]") {
    domain::Model model = loadSample();
    // 4명 중 3명을 빼면 청소 집합 슬롯 8개에 1명뿐이다.
    for (domain::Worker& worker : model.workers.workers) {
        if (!(worker.id == domain::WorkerId{"w_kim"})) {
            worker.active = false;
        }
    }

    domain::CursorState cursors;
    const domain::DaySnapshot snapshot =
        app::computeDay(model, util::Date::fromYmd(2026, 9, 4), moment(4, 9, 1), cursors);

    REQUIRE(domain::countUnassigned(snapshot) > 0);
    REQUIRE(domain::countAbsentAssignees(snapshot) == 0);
}

// 스냅샷을 쓰고 다시 읽어도 의미가 같다.
TEST_CASE("snapshot round trips", "[assign_service]") {
    Sandbox sandbox{"roundtrip"};
    const util::Result<storage::LoadedModel> loaded = storage::loadModel(sandbox.dir());
    REQUIRE(loaded.ok());

    domain::CursorState cursors;
    const domain::DaySnapshot original = app::computeDay(
        loaded.value().model, util::Date::fromYmd(2026, 9, 4), moment(4, 9, 1), cursors);

    const fs::path path = storage::snapshotPath(sandbox.dir(), "2026-09-04");
    REQUIRE(storage::saveSnapshot(path, original).ok());

    const util::Result<storage::Loaded<domain::DaySnapshot>> reloaded = storage::loadSnapshot(path);
    REQUIRE(reloaded.ok());
    REQUIRE(storage::toJsonText(reloaded.value().value) == storage::toJsonText(original));
}
