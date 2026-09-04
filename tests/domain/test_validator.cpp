#include "core/app/validate_service.h"
#include "core/domain/validator.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// data-sample/ 을 복사해 한 군데만 망가뜨린다. 나머지는 정상이므로 나온 오류가 그 한 군데 때문임이
// 분명해진다.
class BrokenData {
public:
    explicit BrokenData(const std::string& name)
        : dir_(fs::temp_directory_path() / ("sched_broken_" + name)) {
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        for (const char* file : {"config.json", "workers.json", "tasks.json", "absences.json"}) {
            fs::copy_file(fs::path{SCHED_DATA_SAMPLE_DIR} / file, dir_ / file);
        }
    }
    ~BrokenData() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    BrokenData(const BrokenData&) = delete;
    BrokenData& operator=(const BrokenData&) = delete;

    // 파일 안의 문자열 하나를 바꾼다. 찾지 못하면 테스트가 조용히 통과하지 않도록 실패시킨다.
    void patch(const char* file, const std::string& from, const std::string& to) {
        const fs::path path = dir_ / file;
        std::string text = readAll(path);
        const std::size_t at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }

    const fs::path& dir() const { return dir_; }

private:
    fs::path dir_;
};

bool mentions(const domain::Report& report, domain::Severity severity, const std::string& needle) {
    for (const domain::Finding& finding : report.findings()) {
        if (finding.severity == severity && finding.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

domain::Report validateOf(const BrokenData& data) {
    const util::Result<domain::Report> result = app::validateDataDir(data.dir());
    REQUIRE(result.ok());
    return result.value();
}

}  // namespace

// 기준점. 샘플 데이터는 아무 문제도 없어야 한다.
TEST_CASE("sample data is clean", "[validator]") {
    const util::Result<domain::Report> result =
        app::validateDataDir(fs::path{SCHED_DATA_SAMPLE_DIR});
    REQUIRE(result.ok());
    for (const domain::Finding& finding : result.value().findings()) {
        INFO(finding.where << " — " << finding.message);
        REQUIRE(false);
    }
    REQUIRE(result.value().errorCount() == 0);
    REQUIRE(result.value().warningCount() == 0);
}

// 완료 기준 2번: 일부러 망가뜨린 데이터에 대해 정확한 오류를 낸다.

TEST_CASE("catches a reference to a missing task set", "[validator]") {
    BrokenData data{"missing_set"};
    data.patch("tasks.json", "\"cleaning_am\", \"cleaning_pm\", \"cleaning_weekend\"",
               "\"cleaning_am\", \"cleaning_없음\"");
    const domain::Report report = validateOf(data);
    REQUIRE(report.hasErrors());
    REQUIRE(mentions(report, domain::Severity::Error, "존재하지 않는 작업집합"));
}

TEST_CASE("catches a conflict with a missing task", "[validator]") {
    BrokenData data{"missing_conflict"};
    data.patch("tasks.json", "\"conflictsWith\": [\"mop\"]", "\"conflictsWith\": [\"vacuum2\"]");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Error, "존재하지 않는 작업"));
    REQUIRE(mentions(report, domain::Severity::Error, "vacuum2"));
}

TEST_CASE("catches an absence for a missing worker", "[validator]") {
    BrokenData data{"missing_worker"};
    data.patch("absences.json", "\"w_choi\"", "\"w_ghost\"");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Error, "존재하지 않는 작업자"));
}

TEST_CASE("catches a missing category reference", "[validator]") {
    BrokenData data{"missing_category"};
    data.patch("config.json", "\"categoryIds\": [\"weekday_am\"]",
               "\"categoryIds\": [\"weekday_none\"]");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Error, "존재하지 않는 분류"));
}

TEST_CASE("catches duplicate ids", "[validator]") {
    BrokenData data{"duplicate_id"};
    data.patch("workers.json", "\"id\": \"w_lee\"", "\"id\": \"w_kim\"");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Error, "두 번 정의"));
}

TEST_CASE("catches a non positive required count", "[validator]") {
    BrokenData data{"bad_count"};
    data.patch("tasks.json", "\"requiredCount\": 2", "\"requiredCount\": 0");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Error, "필요 인원"));
}

TEST_CASE("catches a time slot that ends before it starts", "[validator]") {
    BrokenData data{"bad_range"};
    data.patch("config.json", "\"start\": \"09:00\",\n      \"end\": \"12:00\"",
               "\"start\": \"12:00\",\n      \"end\": \"09:00\"");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Error, "시작이 끝보다"));
}

TEST_CASE("catches overlapping time slots on the same weekday", "[validator]") {
    BrokenData data{"overlap"};
    // 평일 오후를 11:00 에 시작하게 만들면 평일 오전(09:00~12:00)과 겹친다.
    data.patch("config.json", "\"start\": \"13:00\",\n      \"end\": \"18:00\"",
               "\"start\": \"11:00\",\n      \"end\": \"18:00\"");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Error, "겹칩니다"));
}

TEST_CASE("catches an absence range that runs backwards", "[validator]") {
    BrokenData data{"backwards"};
    data.patch("absences.json", "\"from\": \"2026-09-10\"", "\"from\": \"2026-09-20\"");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Error, "시작일이 종료일보다"));
}

TEST_CASE("catches an unknown weekday code", "[validator]") {
    BrokenData data{"bad_weekday"};
    data.patch("workers.json", "[\"SUN\", \"MON\"]", "[\"SUN\", \"MONDAY\"]");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Error, "요일 코드"));
    REQUIRE(mentions(report, domain::Severity::Error, "MONDAY"));
}

TEST_CASE("catches an unsupported schema version", "[validator]") {
    BrokenData data{"bad_version"};
    data.patch("config.json", "\"version\": 1", "\"version\": 99");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Error, "지원하지 않는 형식 버전"));
}

// 첫 오류에서 멈추면 안 된다 (DATA-SCHEMA.md 검증 규칙).
TEST_CASE("collects every problem instead of stopping at the first", "[validator]") {
    BrokenData data{"many"};
    data.patch("workers.json", "\"id\": \"w_lee\"", "\"id\": \"w_kim\"");
    data.patch("tasks.json", "\"requiredCount\": 2", "\"requiredCount\": 0");
    data.patch("absences.json", "\"w_choi\"", "\"w_ghost\"");

    const domain::Report report = validateOf(data);
    REQUIRE(report.errorCount() >= 3);
}

// 경고는 실행을 막지 않는다.

TEST_CASE("warns about a task set no category uses", "[validator]") {
    BrokenData data{"orphan_set"};
    data.patch("tasks.json", "{ \"id\": \"zone\",             \"displayName\": \"배치구역\" }",
               "{ \"id\": \"zone\", \"displayName\": \"배치구역\" },\n"
               "    { \"id\": \"orphan\", \"displayName\": \"주말청소\" }");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Warning, "어떤 분류에도 속해 있지 않습니다"));
    REQUIRE(mentions(report, domain::Severity::Warning, "속한 작업이 없습니다"));
    REQUIRE_FALSE(report.hasErrors());
}

TEST_CASE("warns about duplicate worker names", "[validator]") {
    BrokenData data{"dup_name"};
    data.patch("workers.json", "\"name\": \"이영희\"", "\"name\": \"김철수\"");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Warning, "작업자 이름"));
    REQUIRE_FALSE(report.hasErrors());
}

TEST_CASE("warns when every worker is inactive", "[validator]") {
    BrokenData data{"all_inactive"};
    std::string text = readAll(data.dir() / "workers.json");
    std::size_t at = 0;
    while ((at = text.find("\"active\": true", at)) != std::string::npos) {
        text.replace(at, std::string{"\"active\": true"}.size(), "\"active\": false");
    }
    {
        std::ofstream out(data.dir() / "workers.json", std::ios::binary | std::ios::trunc);
        out << text;
    }
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Warning, "배정할 수 있는 작업자가 없습니다"));
}

TEST_CASE("warns when a conflict group needs more people than exist", "[validator]") {
    BrokenData data{"too_many"};
    // 청소기 5명 + 밀대 2명 = 배타 그룹 7명. 활성 작업자는 4명뿐이다.
    data.patch("tasks.json", "\"requiredCount\": 2", "\"requiredCount\": 5");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Warning, "매일 미배정이 생깁니다"));
}

TEST_CASE("warns about a task scheduled only on non working days", "[validator]") {
    BrokenData data{"never_runs"};
    data.patch("tasks.json", "\"weekdays\": [\"SAT\"]", "\"weekdays\": [\"SUN\"]");
    const domain::Report report = validateOf(data);
    REQUIRE(mentions(report, domain::Severity::Warning, "실행되지 않습니다"));
}

// 형식 검사 자체.
TEST_CASE("date and time shape checks", "[validator]") {
    REQUIRE(domain::isDateString("2026-09-04"));
    REQUIRE_FALSE(domain::isDateString("2026-9-4"));
    REQUIRE_FALSE(domain::isDateString("2026-13-01"));
    REQUIRE_FALSE(domain::isDateString("2026-09-32"));
    REQUIRE_FALSE(domain::isDateString(""));

    REQUIRE(domain::isTimeString("09:00"));
    REQUIRE(domain::isTimeString("23:59"));
    REQUIRE_FALSE(domain::isTimeString("24:00"));
    REQUIRE_FALSE(domain::isTimeString("9:00"));
    REQUIRE_FALSE(domain::isTimeString("09:60"));
}
