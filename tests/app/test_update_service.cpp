#include "core/app/update_service.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
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

// GitHub Releases API 응답의 필요한 부분만 흉내 낸다.
std::string releaseJson(const char* tag) {
    return std::string{R"({
      "tag_name": ")"} + tag + R"(",
      "name": "릴리스",
      "assets": [
        {"name": "sched-win-x64.zip",
         "browser_download_url": "https://example.invalid/sched-win-x64.zip"},
        {"name": "SHA256SUMS.txt",
         "browser_download_url": "https://example.invalid/SHA256SUMS.txt"}
      ]
    })";
}

domain::Config configWith(bool enabled, const char* repo) {
    domain::Config config;
    config.update.enabled = enabled;
    config.update.repo = repo;
    return config;
}

class TempDir {
public:
    explicit TempDir(const std::string& name)
        : path_(fs::temp_directory_path() / ("sched_update_" + name)) {
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

}  // namespace

TEST_CASE("reads the tag and asset urls from a release", "[update]") {
    const util::Result<app::ReleaseInfo> parsed = app::parseLatestRelease(releaseJson("v0.1.1"));
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value().tag == "v0.1.1");
    REQUIRE(parsed.value().version == util::Version{0, 1, 1});
    REQUIRE(parsed.value().assetUrl == "https://example.invalid/sched-win-x64.zip");
    REQUIRE(parsed.value().checksumUrl == "https://example.invalid/SHA256SUMS.txt");
}

TEST_CASE("rejects a release we cannot act on", "[update]") {
    SECTION("JSON 이 아니다") {
        REQUIRE_FALSE(app::parseLatestRelease("<html>404</html>").ok());
    }
    SECTION("태그가 없다") {
        REQUIRE_FALSE(app::parseLatestRelease(R"({"name": "릴리스"})").ok());
    }
    SECTION("태그가 버전이 아니다") {
        const util::Result<app::ReleaseInfo> parsed = app::parseLatestRelease(releaseJson("latest"));
        REQUIRE_FALSE(parsed.ok());
        REQUIRE(parsed.error().message.find("latest") != std::string::npos);
    }
    SECTION("우리 zip 이 없다") {
        const std::string noAsset = R"({"tag_name": "v0.1.1", "assets": []})";
        const util::Result<app::ReleaseInfo> parsed = app::parseLatestRelease(noAsset);
        REQUIRE_FALSE(parsed.ok());
        REQUIRE(parsed.error().code == util::ErrorCode::NotFound);
    }
}

TEST_CASE("compares the release against the running version", "[update]") {
    const util::Result<app::ReleaseInfo> release = app::parseLatestRelease(releaseJson("v0.1.1"));
    REQUIRE(release.ok());

    SECTION("새 버전이면 알린다") {
        const app::UpdateStatus status =
            app::decideUpdate(configWith(true, "owner/repo"), util::Version{0, 1, 0},
                              release.value());
        REQUIRE(status.decision == app::UpdateDecision::Available);
        REQUIRE(status.release.has_value());
    }
    SECTION("같거나 더 높으면 최신이다") {
        REQUIRE(app::decideUpdate(configWith(true, "owner/repo"), util::Version{0, 1, 1},
                                  release.value())
                    .decision == app::UpdateDecision::UpToDate);
        REQUIRE(app::decideUpdate(configWith(true, "owner/repo"), util::Version{0, 2, 0},
                                  release.value())
                    .decision == app::UpdateDecision::UpToDate);
    }
    SECTION("config 에서 껐으면 아무것도 하지 않는다") {
        REQUIRE(app::decideUpdate(configWith(false, "owner/repo"), util::Version{0, 1, 0},
                                  release.value())
                    .decision == app::UpdateDecision::Disabled);
    }
    SECTION("repo 가 비어 있으면 자동 업데이트 자체가 없다") {
        REQUIRE(app::decideUpdate(configWith(true, ""), util::Version{0, 1, 0}, release.value())
                    .decision == app::UpdateDecision::Disabled);
    }
}

TEST_CASE("builds the api url only from owner/name", "[update]") {
    REQUIRE(app::latestReleaseUrl("3r15/work-calendar").value() ==
            "https://api.github.com/repos/3r15/work-calendar/releases/latest");
    REQUIRE_FALSE(app::latestReleaseUrl("").has_value());
    REQUIRE_FALSE(app::latestReleaseUrl("work-calendar").has_value());
    REQUIRE_FALSE(app::latestReleaseUrl("a/b/c").has_value());
    REQUIRE_FALSE(app::latestReleaseUrl("/repo").has_value());
    REQUIRE_FALSE(app::latestReleaseUrl("owner/").has_value());
}

TEST_CASE("finds a checksum line for the asset", "[update]") {
    const std::string sums =
        "aaaa1111  other-file.zip\n"
        "bbbb2222  sched-win-x64.zip\n";
    REQUIRE(app::findChecksumFor(sums, "sched-win-x64.zip").value() == "bbbb2222");
    REQUIRE_FALSE(app::findChecksumFor(sums, "missing.zip").has_value());

    SECTION("바이너리 표시(*)와 공백 개수가 달라도 읽는다") {
        REQUIRE(app::findChecksumFor("cccc3333 *sched-win-x64.zip\n", "sched-win-x64.zip")
                    .value() == "cccc3333");
        REQUIRE(app::findChecksumFor("dddd4444\tsched-win-x64.zip", "sched-win-x64.zip").value() ==
                "dddd4444");
    }
    SECTION("빈 파일에서는 찾지 못한다") {
        REQUIRE_FALSE(app::findChecksumFor("", "sched-win-x64.zip").has_value());
    }
}

// GitHub API 비인증 한도는 시간당 60회다. 하루 1회로 제한한다 (DESIGN 7.2).
TEST_CASE("limits how often we ask github", "[update]") {
    TempDir dir{"throttle"};

    SECTION("확인한 적이 없으면 확인한다") {
        REQUIRE(app::shouldCheckNow(dir.path(), moment(4, 9, 0), 24));
    }

    REQUIRE(app::recordCheck(dir.path(), moment(4, 9, 0)).ok());

    SECTION("24시간이 지나지 않았으면 건너뛴다") {
        REQUIRE_FALSE(app::shouldCheckNow(dir.path(), moment(4, 10, 0), 24));
        REQUIRE_FALSE(app::shouldCheckNow(dir.path(), moment(4, 23, 59), 24));
        REQUIRE_FALSE(app::shouldCheckNow(dir.path(), moment(5, 8, 59), 24));
    }
    SECTION("24시간이 지나면 확인한다") {
        REQUIRE(app::shouldCheckNow(dir.path(), moment(5, 9, 0), 24));
        REQUIRE(app::shouldCheckNow(dir.path(), moment(6, 9, 0), 24));
    }
    SECTION("간격을 짧게 주면 그만큼만 기다린다") {
        REQUIRE_FALSE(app::shouldCheckNow(dir.path(), moment(4, 9, 59), 1));
        REQUIRE(app::shouldCheckNow(dir.path(), moment(4, 10, 0), 1));
    }
    SECTION("시계가 뒤로 갔으면 막지 않는다") {
        // 시간대 변경이나 수동 조정으로 과거가 될 수 있다. 그 때문에 영영 확인을 못 하면 안 된다.
        REQUIRE(app::shouldCheckNow(dir.path(), moment(3, 9, 0), 24));
    }
}

TEST_CASE("a broken state file does not block checking", "[update]") {
    TempDir dir{"broken"};
    fs::create_directories(dir.path() / "state");
    {
        std::ofstream out(dir.path() / "state" / "update.json");
        out << "이건 JSON 이 아니다";
    }
    REQUIRE(app::shouldCheckNow(dir.path(), moment(4, 9, 0), 24));
}
