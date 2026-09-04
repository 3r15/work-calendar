#include "core/storage/json_io.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path samplePath(const char* name) {
    return fs::path{SCHED_DATA_SAMPLE_DIR} / name;
}

bool hasTaskId(const std::vector<domain::TaskId>& ids, const char* id) {
    return std::find(ids.begin(), ids.end(), domain::TaskId{id}) != ids.end();
}

const domain::Task* findTask(const domain::TaskList& tasks, const char* id) {
    for (const domain::Task& task : tasks.tasks) {
        if (task.id == domain::TaskId{id}) {
            return &task;
        }
    }
    return nullptr;
}

}  // namespace

// data-sample/ 은 테스트 픽스처를 겸한다 (CLAUDE.md 2장).
TEST_CASE("loads the sample data", "[json_io]") {
    const util::Result<storage::Loaded<domain::Config>> config =
        storage::loadConfig(samplePath("config.json"));
    REQUIRE(config.ok());
    REQUIRE(config.value().report.empty());
    REQUIRE(config.value().value.timeSlots.size() == 4);
    REQUIRE(config.value().value.categories.size() == 4);
    REQUIRE(config.value().value.workCalendar.workWeekdays.size() == 5);
    REQUIRE_FALSE(config.value().value.assignment.crossSetConflicts);

    const util::Result<storage::Loaded<domain::WorkerList>> workers =
        storage::loadWorkers(samplePath("workers.json"));
    REQUIRE(workers.ok());
    REQUIRE(workers.value().value.workers.size() == 4);
    // 배열 순서가 라운드 로빈 기준 순서다. 읽은 순서가 그대로 보존되어야 한다.
    REQUIRE(workers.value().value.workers[0].name == "김철수");
    REQUIRE(workers.value().value.workers[3].name == "최지우");

    const util::Result<storage::Loaded<domain::TaskList>> tasks =
        storage::loadTasks(samplePath("tasks.json"));
    REQUIRE(tasks.ok());
    REQUIRE(tasks.value().value.taskSets.size() == 4);
    REQUIRE(tasks.value().value.tasks.size() == 8);

    const util::Result<storage::Loaded<domain::AbsenceList>> absences =
        storage::loadAbsences(samplePath("absences.json"));
    REQUIRE(absences.ok());
    REQUIRE(absences.value().value.absences.size() == 1);
    REQUIRE(absences.value().value.absences[0].reason == "연차");
}

// priority 는 로드·저장만 되고 엔진은 읽지 않는다 (D-010).
TEST_CASE("keeps the priority field even though the engine ignores it", "[json_io]") {
    const util::Result<storage::Loaded<domain::TaskList>> tasks =
        storage::loadTasks(samplePath("tasks.json"));
    REQUIRE(tasks.ok());
    const domain::Task* vacuum = findTask(tasks.value().value, "vacuum");
    REQUIRE(vacuum != nullptr);
    REQUIRE_FALSE(vacuum->priority.has_value());

    // 저장할 때 필드 자체는 남아야 나중에 켤 때 마이그레이션이 필요 없다.
    const std::string text = storage::toJsonText(tasks.value().value);
    REQUIRE(text.find("\"priority\"") != std::string::npos);
}

// 완료 기준 1번: 읽고 다시 써도 의미가 같다.
TEST_CASE("round trips the sample data without drift", "[json_io]") {
    const util::Result<storage::Loaded<domain::TaskList>> first =
        storage::loadTasks(samplePath("tasks.json"));
    REQUIRE(first.ok());

    const fs::path temp = fs::temp_directory_path() / "sched_test_roundtrip";
    fs::remove_all(temp);
    fs::create_directories(temp);
    const fs::path written = temp / "tasks.json";

    REQUIRE(storage::saveTasks(written, first.value().value).ok());

    const util::Result<storage::Loaded<domain::TaskList>> second = storage::loadTasks(written);
    REQUIRE(second.ok());

    // 두 번째 직렬화가 첫 번째와 바이트까지 같으면 정보가 새지 않았다는 뜻이다.
    REQUIRE(storage::toJsonText(second.value().value) ==
            storage::toJsonText(first.value().value));

    std::error_code ec;
    fs::remove_all(temp, ec);
}

TEST_CASE("round trips every sample file", "[json_io]") {
    const fs::path temp = fs::temp_directory_path() / "sched_test_roundtrip_all";
    fs::remove_all(temp);
    fs::create_directories(temp);

    SECTION("config.json") {
        const auto loaded = storage::loadConfig(samplePath("config.json"));
        REQUIRE(loaded.ok());
        REQUIRE(storage::saveConfig(temp / "config.json", loaded.value().value).ok());
        const auto again = storage::loadConfig(temp / "config.json");
        REQUIRE(again.ok());
        REQUIRE(storage::toJsonText(again.value().value) ==
                storage::toJsonText(loaded.value().value));
    }
    SECTION("workers.json") {
        const auto loaded = storage::loadWorkers(samplePath("workers.json"));
        REQUIRE(loaded.ok());
        REQUIRE(storage::saveWorkers(temp / "workers.json", loaded.value().value).ok());
        const auto again = storage::loadWorkers(temp / "workers.json");
        REQUIRE(again.ok());
        REQUIRE(storage::toJsonText(again.value().value) ==
                storage::toJsonText(loaded.value().value));
    }
    SECTION("absences.json") {
        const auto loaded = storage::loadAbsences(samplePath("absences.json"));
        REQUIRE(loaded.ok());
        REQUIRE(storage::saveAbsences(temp / "absences.json", loaded.value().value).ok());
        const auto again = storage::loadAbsences(temp / "absences.json");
        REQUIRE(again.ok());
        REQUIRE(storage::toJsonText(again.value().value) ==
                storage::toJsonText(loaded.value().value));
    }

    std::error_code ec;
    fs::remove_all(temp, ec);
}

// 배타는 대칭이다. 한쪽에만 적어도 양쪽에 적용된다.
TEST_CASE("normalizes conflicts to be symmetric", "[json_io]") {
    domain::TaskList tasks;
    tasks.taskSets.push_back(domain::TaskSet{domain::TaskSetId{"cleaning"}, "청소"});

    domain::Task vacuum;
    vacuum.id = domain::TaskId{"vacuum"};
    vacuum.name = "청소기";
    vacuum.conflictsWith.push_back(domain::TaskId{"mop"});  // 한쪽에만 적었다
    tasks.tasks.push_back(vacuum);

    domain::Task mop;
    mop.id = domain::TaskId{"mop"};
    mop.name = "밀대";
    tasks.tasks.push_back(mop);

    storage::normalizeConflicts(tasks);

    REQUIRE(hasTaskId(findTask(tasks, "vacuum")->conflictsWith, "mop"));
    REQUIRE(hasTaskId(findTask(tasks, "mop")->conflictsWith, "vacuum"));

    SECTION("두 번 정규화해도 중복이 생기지 않는다") {
        storage::normalizeConflicts(tasks);
        REQUIRE(findTask(tasks, "mop")->conflictsWith.size() == 1);
    }

    SECTION("자기 자신은 넣지 않는다 — 암묵적이다") {
        domain::TaskList selfRef;
        domain::Task task;
        task.id = domain::TaskId{"solo"};
        task.conflictsWith.push_back(domain::TaskId{"solo"});
        selfRef.tasks.push_back(task);
        storage::normalizeConflicts(selfRef);
        REQUIRE(selfRef.tasks[0].conflictsWith.empty());
    }
}

TEST_CASE("sample conflicts are already symmetric after load", "[json_io]") {
    const auto tasks = storage::loadTasks(samplePath("tasks.json"));
    REQUIRE(tasks.ok());
    REQUIRE(hasTaskId(findTask(tasks.value().value, "vacuum")->conflictsWith, "mop"));
    REQUIRE(hasTaskId(findTask(tasks.value().value, "mop")->conflictsWith, "vacuum"));
}

// 오류 메시지는 기술 용어 대신 사람 말로, 줄 번호와 함께 (CLI-SPEC.md).
TEST_CASE("reports broken json with a line number", "[json_io]") {
    const fs::path temp = fs::temp_directory_path() / "sched_test_broken_json";
    fs::remove_all(temp);
    fs::create_directories(temp);
    const fs::path path = temp / "tasks.json";
    {
        std::ofstream out(path);
        out << "{\n";
        out << "  \"version\": 1,\n";
        out << "  \"taskSets\": [\n";
        out << "    { \"id\": \"a\" \"displayName\": \"청소\" }\n";  // 쉼표 빠짐
        out << "  ]\n";
        out << "}\n";
    }

    const util::Result<storage::Loaded<domain::TaskList>> result = storage::loadTasks(path);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == util::ErrorCode::DataFile);
    REQUIRE(result.error().message.find("4번째 줄") != std::string::npos);
    REQUIRE(result.error().hint.find("쉼표") != std::string::npos);

    std::error_code ec;
    fs::remove_all(temp, ec);
}

TEST_CASE("reports a missing file in plain language", "[json_io]") {
    const util::Result<storage::Loaded<domain::Config>> result =
        storage::loadConfig(fs::temp_directory_path() / "sched_no_such_dir" / "config.json");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == util::ErrorCode::NotFound);
    REQUIRE(result.error().message.find("찾을 수 없습니다") != std::string::npos);
}

// 편집기가 붙이는 BOM 때문에 파일을 못 읽는 일이 없어야 한다.
TEST_CASE("tolerates a utf8 bom", "[json_io]") {
    const fs::path temp = fs::temp_directory_path() / "sched_test_bom";
    fs::remove_all(temp);
    fs::create_directories(temp);
    const fs::path path = temp / "absences.json";
    {
        std::ofstream out(path, std::ios::binary);
        out << "\xEF\xBB\xBF" << "{\"version\": 1, \"absences\": []}\n";
    }

    const util::Result<storage::Loaded<domain::AbsenceList>> result = storage::loadAbsences(path);
    REQUIRE(result.ok());

    std::error_code ec;
    fs::remove_all(temp, ec);
}

// rr-cursor.json 은 ROADMAP 이 말하는 "5개 파일" 의 다섯 번째다. 커서를 실제로 쓰는 것은
// Phase 3 이지만 읽고 쓰는 것은 여기 속하므로 형식만 고정해 둔다.
TEST_CASE("round trips the round robin cursor file", "[json_io]") {
    const fs::path temp = fs::temp_directory_path() / "sched_test_cursor";
    fs::remove_all(temp);
    fs::create_directories(temp);
    const fs::path path = temp / "state" / "rr-cursor.json";

    domain::CursorState state;
    state.cursors.emplace(domain::TaskSetId{"cleaning_am"}, 2);
    state.cursors.emplace(domain::TaskSetId{"cleaning_pm"}, 0);
    state.cursors.emplace(domain::TaskSetId{"zone"}, 3);

    REQUIRE(storage::saveCursors(path, state).ok());

    const util::Result<storage::Loaded<domain::CursorState>> loaded = storage::loadCursors(path);
    REQUIRE(loaded.ok());
    REQUIRE(loaded.value().report.empty());
    REQUIRE(loaded.value().value.cursors.size() == 3);
    REQUIRE(loaded.value().value.cursors.at(domain::TaskSetId{"cleaning_am"}) == 2);
    REQUIRE(loaded.value().value.cursors.at(domain::TaskSetId{"zone"}) == 3);
    REQUIRE(storage::toJsonText(loaded.value().value) == storage::toJsonText(state));

    std::error_code ec;
    fs::remove_all(temp, ec);
}

// 커서 파일은 처음 실행할 때 없다. 없는 것은 오류가 아니라 "아직 아무것도 안 돌았다" 는 뜻이므로,
// 부르는 쪽이 NotFound 를 빈 상태로 바꿔 쓸 수 있어야 한다.
TEST_CASE("missing cursor file is reported as not found", "[json_io]") {
    const util::Result<storage::Loaded<domain::CursorState>> loaded =
        storage::loadCursors(fs::temp_directory_path() / "sched_no_cursor" / "rr-cursor.json");
    REQUIRE_FALSE(loaded.ok());
    REQUIRE(loaded.error().code == util::ErrorCode::NotFound);
}
