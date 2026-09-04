#include "core/app/admin_service.h"

#include "core/domain/validator.h"
#include "core/storage/json_io.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

class Sandbox {
public:
    explicit Sandbox(const std::string& name)
        : dir_(fs::temp_directory_path() / ("sched_admin_" + name)) {
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

const domain::Task* findTask(const domain::Model& model, const char* id) {
    for (const domain::Task& task : model.tasks.tasks) {
        if (task.id == domain::TaskId{id}) {
            return &task;
        }
    }
    return nullptr;
}

bool hasConflict(const domain::Task& task, const char* id) {
    return std::find(task.conflictsWith.begin(), task.conflictsWith.end(), domain::TaskId{id}) !=
           task.conflictsWith.end();
}

}  // namespace

TEST_CASE("id rules follow the schema", "[admin]") {
    REQUIRE(app::isValidId("w_kim"));
    REQUIRE(app::isValidId("cleaning_am"));
    REQUIRE(app::isValidId("zone1"));
    REQUIRE_FALSE(app::isValidId(""));
    REQUIRE_FALSE(app::isValidId("W_kim"));    // 대문자
    REQUIRE_FALSE(app::isValidId("김철수"));    // 한글
    REQUIRE_FALSE(app::isValidId("w-kim"));    // 하이픈
    REQUIRE_FALSE(app::isValidId("w kim"));    // 공백
}

TEST_CASE("adds a worker and gives it an id when none is given", "[admin]") {
    Sandbox sandbox{"worker_add"};
    domain::Model model = sandbox.load();

    const util::Result<domain::Worker> added =
        app::addWorker(sandbox.dir(), model, "정소연", "", {});
    REQUIRE(added.ok());
    // 이름이 한글이라 ID 로 쓸 수 없다. 자동으로 붙인다.
    REQUIRE(app::isValidId(added.value().id.str()));

    const domain::Model reloaded = sandbox.load();
    REQUIRE(reloaded.workers.workers.size() == 5);
    // 배열 끝에 붙는다. 순서가 라운드 로빈 기준이라 중간에 끼우면 안 된다.
    REQUIRE(reloaded.workers.workers.back().name == "정소연");

    SECTION("ID 를 직접 주면 그걸 쓴다") {
        const util::Result<domain::Worker> withId =
            app::addWorker(sandbox.dir(), model, "한지민", "w_han", {});
        REQUIRE(withId.ok());
        REQUIRE(withId.value().id.str() == "w_han");
    }
    SECTION("잘못된 ID 는 거부한다") {
        const util::Result<domain::Worker> bad =
            app::addWorker(sandbox.dir(), model, "한지민", "W-Han", {});
        REQUIRE_FALSE(bad.ok());
        REQUIRE(bad.error().code == util::ErrorCode::InvalidUsage);
    }
    SECTION("ID 가 겹치면 거부한다") {
        const util::Result<domain::Worker> dup =
            app::addWorker(sandbox.dir(), model, "한지민", "w_kim", {});
        REQUIRE_FALSE(dup.ok());
        REQUIRE(dup.error().code == util::ErrorCode::Conflict);
    }
}

// 과거 스냅샷이 ID 를 참조하므로 기본은 삭제가 아니라 제외다.
TEST_CASE("removing a worker deactivates instead of deleting", "[admin]") {
    Sandbox sandbox{"worker_rm"};
    domain::Model model = sandbox.load();

    REQUIRE(app::removeWorker(sandbox.dir(), model, domain::WorkerId{"w_kim"}, /*hard=*/false)
                .ok());
    const domain::Model soft = sandbox.load();
    REQUIRE(soft.workers.workers.size() == 4);
    REQUIRE_FALSE(soft.workers.workers[0].active);

    SECTION("--hard 면 정말 지운다") {
        REQUIRE(app::removeWorker(sandbox.dir(), model, domain::WorkerId{"w_kim"}, true).ok());
        const domain::Model hard = sandbox.load();
        REQUIRE(hard.workers.workers.size() == 3);
    }
}

TEST_CASE("adds a task and keeps conflicts symmetric", "[admin]") {
    Sandbox sandbox{"task_add"};
    domain::Model model = sandbox.load();

    app::TaskSpec spec;
    spec.id = "windows";
    spec.name = "창닦기";
    spec.taskSetIds = {domain::TaskSetId{"cleaning_am"}};
    spec.requiredCount = 1;
    spec.conflictsWith = {domain::TaskId{"vacuum"}};  // 한쪽에만 적는다

    REQUIRE(app::addTask(sandbox.dir(), model, spec).ok());

    const domain::Model reloaded = sandbox.load();
    REQUIRE(hasConflict(*findTask(reloaded, "windows"), "vacuum"));
    // 반대 방향도 생겨야 한다.
    REQUIRE(hasConflict(*findTask(reloaded, "vacuum"), "windows"));
}

TEST_CASE("rejects a task that points at nothing", "[admin]") {
    Sandbox sandbox{"task_bad"};
    domain::Model model = sandbox.load();

    app::TaskSpec spec;
    spec.id = "ghost";
    spec.taskSetIds = {domain::TaskSetId{"no_such_set"}};
    spec.requiredCount = 1;
    const util::Result<void> missingSet = app::addTask(sandbox.dir(), model, spec);
    REQUIRE_FALSE(missingSet.ok());
    REQUIRE(missingSet.error().code == util::ErrorCode::NotFound);

    spec.taskSetIds = {domain::TaskSetId{"cleaning_am"}};
    spec.requiredCount = 0;
    const util::Result<void> zeroCount = app::addTask(sandbox.dir(), model, spec);
    REQUIRE_FALSE(zeroCount.ok());

    spec.requiredCount = 1;
    spec.conflictsWith = {domain::TaskId{"no_such_task"}};
    const util::Result<void> missingConflict = app::addTask(sandbox.dir(), model, spec);
    REQUIRE_FALSE(missingConflict.ok());
}

// 지운 작업이 다른 작업의 배타 목록에 남으면 끊어진 참조가 된다.
TEST_CASE("removing a task clears it from other conflict lists", "[admin]") {
    Sandbox sandbox{"task_rm"};
    domain::Model model = sandbox.load();
    REQUIRE(hasConflict(*findTask(model, "mop"), "vacuum"));

    REQUIRE(app::removeTask(sandbox.dir(), model, domain::TaskId{"vacuum"}).ok());

    const domain::Model reloaded = sandbox.load();
    REQUIRE(findTask(reloaded, "vacuum") == nullptr);
    REQUIRE_FALSE(hasConflict(*findTask(reloaded, "mop"), "vacuum"));
}

TEST_CASE("editing a task replaces the whole conflict list", "[admin]") {
    Sandbox sandbox{"task_edit"};
    domain::Model model = sandbox.load();

    // 청소기의 배타를 비운다. 밀대 쪽의 반대 방향도 함께 사라져야 한다.
    REQUIRE(app::editTask(sandbox.dir(), model, domain::TaskId{"vacuum"}, std::nullopt,
                          std::nullopt, std::vector<domain::TaskId>{}, std::nullopt, std::nullopt)
                .ok());

    const domain::Model reloaded = sandbox.load();
    REQUIRE(findTask(reloaded, "vacuum")->conflictsWith.empty());
    REQUIRE_FALSE(hasConflict(*findTask(reloaded, "mop"), "vacuum"));
}

TEST_CASE("editing changes only what was given", "[admin]") {
    Sandbox sandbox{"task_partial"};
    domain::Model model = sandbox.load();
    const int before = findTask(model, "vacuum")->requiredCount;

    REQUIRE(app::editTask(sandbox.dir(), model, domain::TaskId{"vacuum"},
                          std::optional<std::string>{"진공청소기"}, std::nullopt, std::nullopt,
                          std::nullopt, std::nullopt)
                .ok());

    const domain::Model reloaded = sandbox.load();
    REQUIRE(reloaded.tasks.tasks.size() == model.tasks.tasks.size());
    REQUIRE(findTask(reloaded, "vacuum")->name == "진공청소기");
    REQUIRE(findTask(reloaded, "vacuum")->requiredCount == before);
    // 배타는 건드리지 않았다.
    REQUIRE(hasConflict(*findTask(reloaded, "vacuum"), "mop"));
}

// CLI 로 운영하는데 CLI 가 깨진 데이터를 만들면 안 된다.
TEST_CASE("refuses to delete something still referenced", "[admin]") {
    Sandbox sandbox{"refs"};
    domain::Model model = sandbox.load();

    SECTION("작업이 속한 작업집합") {
        const util::Result<void> refused =
            app::removeTaskSet(sandbox.dir(), model, domain::TaskSetId{"cleaning_am"});
        REQUIRE_FALSE(refused.ok());
        REQUIRE(refused.error().code == util::ErrorCode::Conflict);
        // 무엇을 먼저 정리해야 하는지 알려준다.
        REQUIRE(refused.error().hint.find("sched task rm") != std::string::npos);
    }
    SECTION("시간대가 쓰는 분류") {
        const util::Result<void> refused =
            app::removeCategory(sandbox.dir(), model, domain::CategoryId{"weekday_am"});
        REQUIRE_FALSE(refused.ok());
        REQUIRE(refused.error().hint.find("sched slot rm") != std::string::npos);
    }
    SECTION("참조가 없어지면 지울 수 있다") {
        REQUIRE(app::removeTimeSlot(sandbox.dir(), model, domain::TimeSlotId{"am_weekday"}).ok());
        REQUIRE(app::removeCategory(sandbox.dir(), model, domain::CategoryId{"weekday_am"}).ok());
    }
}

TEST_CASE("adds a time slot and refuses an overlapping one", "[admin]") {
    Sandbox sandbox{"slot"};
    domain::Model model = sandbox.load();

    app::TimeSlotSpec spec;
    spec.id = "evening";
    spec.displayName = "저녁";
    spec.start = "19:00";
    spec.end = "21:00";
    spec.categoryIds = {domain::CategoryId{"weekday_am"}};
    REQUIRE(app::addTimeSlot(sandbox.dir(), model, spec).ok());

    SECTION("같은 요일에 겹치면 거부한다 — 지금 어느 시간대인가에 답이 둘이 된다") {
        app::TimeSlotSpec overlap;
        overlap.id = "evening2";
        overlap.displayName = "저녁2";
        overlap.start = "20:00";
        overlap.end = "22:00";
        overlap.categoryIds = {domain::CategoryId{"weekday_am"}};
        const util::Result<void> refused = app::addTimeSlot(sandbox.dir(), model, overlap);
        REQUIRE_FALSE(refused.ok());
        REQUIRE(refused.error().message.find("겹칩니다") != std::string::npos);

        // 거부했으면 파일에도 들어가지 않아야 한다.
        const domain::Model reloaded = sandbox.load();
        const bool saved = std::any_of(
            reloaded.config.timeSlots.begin(), reloaded.config.timeSlots.end(),
            [](const domain::TimeSlot& s) { return s.id == domain::TimeSlotId{"evening2"}; });
        REQUIRE_FALSE(saved);
    }
    SECTION("시각 형식이 틀리면 거부한다") {
        app::TimeSlotSpec bad;
        bad.id = "bad";
        bad.start = "9:00";
        bad.end = "12:00";
        bad.categoryIds = {domain::CategoryId{"weekday_am"}};
        REQUIRE_FALSE(app::addTimeSlot(sandbox.dir(), model, bad).ok());
    }
    SECTION("시작이 끝보다 늦으면 거부한다") {
        app::TimeSlotSpec bad;
        bad.id = "bad";
        bad.start = "12:00";
        bad.end = "09:00";
        bad.categoryIds = {domain::CategoryId{"weekday_am"}};
        REQUIRE_FALSE(app::addTimeSlot(sandbox.dir(), model, bad).ok());
    }
}

// 완료 기준 1번: 설정 파일을 손으로 고치지 않고 CLI 만으로 운영할 수 있다.
TEST_CASE("a working configuration can be built through the service alone", "[admin]") {
    Sandbox sandbox{"scratch"};
    domain::Model model = sandbox.load();
    // 샘플을 비우고 처음부터 쌓는다.
    model.tasks.tasks.clear();
    model.tasks.taskSets.clear();
    model.config.categories.clear();
    model.config.timeSlots.clear();
    REQUIRE(storage::saveTasks(sandbox.dir() / "tasks.json", model.tasks).ok());
    REQUIRE(storage::saveConfig(sandbox.dir() / "config.json", model.config).ok());

    REQUIRE(app::addTaskSet(sandbox.dir(), model, "morning", "아침청소").ok());

    app::TaskSpec vacuum;
    vacuum.id = "vacuum";
    vacuum.name = "청소기";
    vacuum.taskSetIds = {domain::TaskSetId{"morning"}};
    vacuum.requiredCount = 2;
    REQUIRE(app::addTask(sandbox.dir(), model, vacuum).ok());

    REQUIRE(app::addCategory(sandbox.dir(), model, "am", "오전",
                             {domain::TaskSetId{"morning"}})
                .ok());

    app::TimeSlotSpec slot;
    slot.id = "am_slot";
    slot.displayName = "오전";
    slot.start = "09:00";
    slot.end = "12:00";
    slot.categoryIds = {domain::CategoryId{"am"}};
    REQUIRE(app::addTimeSlot(sandbox.dir(), model, slot).ok());

    // 손으로 고친 것 없이 검증을 통과해야 한다.
    const domain::Model built = sandbox.load();
    const domain::Report report = domain::validate(built);
    for (const domain::Finding& finding : report.findings()) {
        INFO(finding.where << " — " << finding.message);
        REQUIRE(finding.severity != domain::Severity::Error);
    }
    REQUIRE_FALSE(report.hasErrors());
}

// 겹침 판정을 validator 와 admin_service 가 같은 함수로 한다.
// 예전에는 admin_service 가 오류 메시지에 "겹칩니다" 가 들어 있는지로 판정했다. 문구가 바뀌면
// 겹침 검사가 조용히 통과해 버린다.
TEST_CASE("overlap detection is shared, not message matching", "[admin]") {
    Sandbox sandbox{"overlap_shared"};
    const domain::Model model = sandbox.load();

    // 샘플은 겹치지 않는다.
    REQUIRE(domain::findTimeSlotOverlaps(model.config).empty());

    domain::Config broken = model.config;
    domain::TimeSlot extra;
    extra.id = domain::TimeSlotId{"overlapping"};
    extra.displayName = "겹치는 시간대";
    extra.start = "10:00";
    extra.end = "11:00";
    extra.weekdays = {domain::Weekday::Tue};
    extra.categoryIds = {domain::CategoryId{"weekday_am"}};
    broken.timeSlots.push_back(extra);

    const std::vector<domain::TimeSlotOverlap> overlaps = domain::findTimeSlotOverlaps(broken);
    REQUIRE(overlaps.size() == 1);
    REQUIRE(overlaps.front().a != nullptr);
    REQUIRE(overlaps.front().b != nullptr);
    REQUIRE(domain::contains(overlaps.front().weekdays, domain::Weekday::Tue));
    // 설명 문장은 판정과 분리되어 있다.
    REQUIRE(domain::describeOverlap(overlaps.front()).find("겹칩니다") != std::string::npos);

    SECTION("반열림 구간이라 맞닿은 시간대는 겹치지 않는다 (D-012)") {
        domain::Config touching = model.config;
        domain::TimeSlot next;
        next.id = domain::TimeSlotId{"touching"};
        next.displayName = "이어지는 시간대";
        next.start = "12:00";  // 평일 오전이 12:00 에 끝난다
        next.end = "13:00";
        next.weekdays = {domain::Weekday::Tue};
        next.categoryIds = {domain::CategoryId{"weekday_am"}};
        touching.timeSlots.push_back(next);
        REQUIRE(domain::findTimeSlotOverlaps(touching).empty());
    }
}

// 필요 인원을 0 으로 고치려는 시도는 오류여야 한다.
TEST_CASE("editing required count to zero is rejected", "[admin]") {
    Sandbox sandbox{"zero_count"};
    domain::Model model = sandbox.load();
    const int before = findTask(model, "vacuum")->requiredCount;

    const util::Result<void> refused =
        app::editTask(sandbox.dir(), model, domain::TaskId{"vacuum"}, std::nullopt,
                      std::optional<int>{0}, std::nullopt, std::nullopt, std::nullopt);
    REQUIRE_FALSE(refused.ok());
    REQUIRE(refused.error().code == util::ErrorCode::InvalidUsage);

    // 파일도 그대로다.
    REQUIRE(sandbox.load().tasks.tasks.size() == model.tasks.tasks.size());
    REQUIRE(findTask(sandbox.load(), "vacuum")->requiredCount == before);
}
