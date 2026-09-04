#include "core/app/prune_service.h"

#include "core/storage/atomic_write.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

class TempDir {
public:
    explicit TempDir(const std::string& name)
        : path_(fs::temp_directory_path() / ("sched_prune_" + name)) {
        fs::remove_all(path_);
        fs::create_directories(path_ / "state");
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const fs::path& path() const { return path_; }

    // 여러 줄로 들여쓴 스냅샷을 흉내 낸다. 아카이브는 이것을 한 줄로 만들어야 한다.
    void putSnapshot(const std::string& date) {
        std::ofstream out(path_ / "state" / ("assign-" + date + ".json"));
        out << "{\n  \"version\": 1,\n  \"date\": \"" << date
            << "\",\n  \"note\": \"줄 바꿈 유지 확인\"\n}\n";
    }

private:
    fs::path path_;
};

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

int countLines(const std::string& text) {
    int lines = 0;
    for (const char c : text) {
        if (c == '\n') {
            lines += 1;
        }
    }
    return lines;
}

}  // namespace

TEST_CASE("keeps recent snapshots and archives old ones", "[prune]") {
    TempDir dir{"basic"};
    dir.putSnapshot("2026-09-01");  // 최근
    dir.putSnapshot("2026-05-02");  // 90일보다 오래됨
    dir.putSnapshot("2026-05-03");

    const util::Result<app::PruneResult> result = app::pruneSnapshots(
        dir.path(), util::Date::fromYmd(2026, 9, 4), app::kDefaultKeepDays, /*dryRun=*/false);
    REQUIRE(result.ok());
    REQUIRE(result.value().archived == 2);

    // 최근 것은 그대로 있다.
    REQUIRE(fs::exists(dir.path() / "state" / "assign-2026-09-01.json"));
    // 오래된 것은 옮겨졌다.
    REQUIRE_FALSE(fs::exists(dir.path() / "state" / "assign-2026-05-02.json"));
    REQUIRE(fs::exists(dir.path() / "state" / "archive" / "2026-05.jsonl"));

    SECTION("한 줄에 스냅샷 하나씩 들어간다") {
        const std::string archive = readAll(dir.path() / "state" / "archive" / "2026-05.jsonl");
        REQUIRE(countLines(archive) == 2);
        // 문자열 안의 한글은 그대로 남는다.
        REQUIRE(archive.find("줄 바꿈 유지 확인") != std::string::npos);
        // 들여쓰기는 걷혔다.
        REQUIRE(archive.find("\n  ") == std::string::npos);
    }
}

TEST_CASE("dry run counts without touching files", "[prune]") {
    TempDir dir{"dry"};
    dir.putSnapshot("2026-05-02");

    const util::Result<app::PruneResult> result = app::pruneSnapshots(
        dir.path(), util::Date::fromYmd(2026, 9, 4), app::kDefaultKeepDays, /*dryRun=*/true);
    REQUIRE(result.ok());
    REQUIRE(result.value().archived == 1);
    REQUIRE(fs::exists(dir.path() / "state" / "assign-2026-05-02.json"));
    REQUIRE_FALSE(fs::exists(dir.path() / "state" / "archive" / "2026-05.jsonl"));
}

TEST_CASE("appends to an existing archive", "[prune]") {
    TempDir dir{"append"};
    dir.putSnapshot("2026-05-02");
    REQUIRE(app::pruneSnapshots(dir.path(), util::Date::fromYmd(2026, 9, 4),
                                app::kDefaultKeepDays, false)
                .ok());

    dir.putSnapshot("2026-05-09");
    REQUIRE(app::pruneSnapshots(dir.path(), util::Date::fromYmd(2026, 9, 4),
                                app::kDefaultKeepDays, false)
                .ok());

    const std::string archive = readAll(dir.path() / "state" / "archive" / "2026-05.jsonl");
    REQUIRE(countLines(archive) == 2);
    REQUIRE(archive.find("2026-05-02") != std::string::npos);
    REQUIRE(archive.find("2026-05-09") != std::string::npos);
}

TEST_CASE("splits archives by month", "[prune]") {
    TempDir dir{"months"};
    dir.putSnapshot("2026-04-30");
    dir.putSnapshot("2026-05-01");

    REQUIRE(app::pruneSnapshots(dir.path(), util::Date::fromYmd(2026, 9, 4),
                                app::kDefaultKeepDays, false)
                .ok());

    REQUIRE(fs::exists(dir.path() / "state" / "archive" / "2026-04.jsonl"));
    REQUIRE(fs::exists(dir.path() / "state" / "archive" / "2026-05.jsonl"));
}

TEST_CASE("ignores files that are not snapshots", "[prune]") {
    TempDir dir{"other"};
    {
        std::ofstream out(dir.path() / "state" / "rr-cursor.json");
        out << "{}";
    }
    dir.putSnapshot("2026-05-02");

    REQUIRE(app::pruneSnapshots(dir.path(), util::Date::fromYmd(2026, 9, 4),
                                app::kDefaultKeepDays, false)
                .ok());
    // 커서 파일은 건드리지 않는다.
    REQUIRE(fs::exists(dir.path() / "state" / "rr-cursor.json"));
}

TEST_CASE("an empty state folder is not an error", "[prune]") {
    TempDir dir{"empty"};
    const util::Result<app::PruneResult> result = app::pruneSnapshots(
        dir.path(), util::Date::fromYmd(2026, 9, 4), app::kDefaultKeepDays, false);
    REQUIRE(result.ok());
    REQUIRE(result.value().archived == 0);
}
