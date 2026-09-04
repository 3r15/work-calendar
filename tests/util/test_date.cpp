#include "core/util/clock.h"
#include "core/util/date.h"

#include <catch2/catch_test_macros.hpp>

using util::Date;
using util::TimeOfDay;

// "YYYY-MM-DD" 왕복.
TEST_CASE("parses and formats dates", "[date]") {
    const std::optional<Date> date = Date::parse("2026-09-04");
    REQUIRE(date.has_value());
    REQUIRE(date->toString() == "2026-09-04");
    REQUIRE(static_cast<int>(date->ymd().year()) == 2026);
    REQUIRE(static_cast<unsigned>(date->ymd().month()) == 9);
    REQUIRE(static_cast<unsigned>(date->ymd().day()) == 4);
}

TEST_CASE("rejects malformed dates", "[date]") {
    REQUIRE_FALSE(Date::parse("2026-9-4").has_value());     // 0 을 채우지 않음
    REQUIRE_FALSE(Date::parse("2026/09/04").has_value());   // 구분자가 다름
    REQUIRE_FALSE(Date::parse("2026-09-4").has_value());
    REQUIRE_FALSE(Date::parse("").has_value());
    REQUIRE_FALSE(Date::parse("20260904").has_value());
    REQUIRE_FALSE(Date::parse("2026-09-04T09:00").has_value());
}

// 형식이 맞아도 달력에 없는 날은 거른다.
TEST_CASE("rejects dates that do not exist", "[date]") {
    REQUIRE_FALSE(Date::parse("2026-02-30").has_value());
    REQUIRE_FALSE(Date::parse("2026-13-01").has_value());
    REQUIRE_FALSE(Date::parse("2026-00-10").has_value());
    REQUIRE_FALSE(Date::parse("2026-04-31").has_value());  // 4월은 30일까지
    REQUIRE_FALSE(Date::parse("2026-02-29").has_value());  // 2026은 평년
    REQUIRE(Date::parse("2024-02-29").has_value());        // 2024는 윤년
}

TEST_CASE("knows the weekday", "[date]") {
    // 일요일이 0 이다.
    REQUIRE(Date::fromYmd(2026, 9, 4).weekday().c_encoding() == 5);   // 금
    REQUIRE(Date::fromYmd(2026, 9, 5).weekday().c_encoding() == 6);   // 토
    REQUIRE(Date::fromYmd(2026, 9, 6).weekday().c_encoding() == 0);   // 일
    REQUIRE(Date::fromYmd(2026, 9, 7).weekday().c_encoding() == 1);   // 월
}

TEST_CASE("moves across month and year boundaries", "[date]") {
    REQUIRE(Date::fromYmd(2026, 9, 30).plusDays(1).toString() == "2026-10-01");
    REQUIRE(Date::fromYmd(2026, 12, 31).plusDays(1).toString() == "2027-01-01");
    REQUIRE(Date::fromYmd(2026, 1, 1).plusDays(-1).toString() == "2025-12-31");
    REQUIRE(Date::fromYmd(2024, 2, 28).plusDays(1).toString() == "2024-02-29");
    REQUIRE(Date::fromYmd(2026, 2, 28).plusDays(1).toString() == "2026-03-01");
}

TEST_CASE("orders dates", "[date]") {
    REQUIRE(Date::fromYmd(2026, 9, 4) < Date::fromYmd(2026, 9, 5));
    REQUIRE(Date::fromYmd(2026, 9, 4) < Date::fromYmd(2026, 10, 1));
    REQUIRE(Date::fromYmd(2025, 12, 31) < Date::fromYmd(2026, 1, 1));
    REQUIRE(Date::fromYmd(2026, 9, 4) == Date::fromYmd(2026, 9, 4));
    REQUIRE_FALSE(Date::fromYmd(2026, 9, 4) < Date::fromYmd(2026, 9, 4));
}

TEST_CASE("parses and formats times", "[date]") {
    const std::optional<TimeOfDay> time = TimeOfDay::parse("09:05");
    REQUIRE(time.has_value());
    REQUIRE(time->hour() == 9);
    REQUIRE(time->minute() == 5);
    REQUIRE(time->minutes() == 545);
    REQUIRE(time->toString() == "09:05");

    REQUIRE(TimeOfDay::parse("00:00")->minutes() == 0);
    REQUIRE(TimeOfDay::parse("23:59")->minutes() == 1439);
}

TEST_CASE("rejects malformed times", "[date]") {
    REQUIRE_FALSE(TimeOfDay::parse("24:00").has_value());
    REQUIRE_FALSE(TimeOfDay::parse("09:60").has_value());
    REQUIRE_FALSE(TimeOfDay::parse("9:00").has_value());
    REQUIRE_FALSE(TimeOfDay::parse("09-00").has_value());
    REQUIRE_FALSE(TimeOfDay::parse("").has_value());
    REQUIRE_FALSE(TimeOfDay::parse("09:00:00").has_value());
}

TEST_CASE("orders times", "[date]") {
    REQUIRE(TimeOfDay::fromHm(9, 0) < TimeOfDay::fromHm(12, 0));
    REQUIRE(TimeOfDay::fromHm(0, 0) < TimeOfDay::fromHm(0, 1));
    REQUIRE(TimeOfDay::fromHm(23, 59) > TimeOfDay::fromHm(13, 0));
    REQUIRE(TimeOfDay::fromHm(9, 0) == TimeOfDay::fromHm(9, 0));
}

// 테스트가 "지금" 을 정할 수 있어야 한다. 실제 시계를 쓰면 자정 근처에서만 깨지는 테스트가 된다.
TEST_CASE("fixed clock returns what it was given", "[clock]") {
    util::FixedClock clock{util::DateTime{Date::fromYmd(2026, 9, 4), TimeOfDay::fromHm(14, 20)}};
    REQUIRE(clock.now().date.toString() == "2026-09-04");
    REQUIRE(clock.now().time.toString() == "14:20");

    clock.set(util::DateTime{Date::fromYmd(2026, 9, 5), TimeOfDay::fromHm(9, 0)});
    REQUIRE(clock.now().date.toString() == "2026-09-05");
}

// 시스템 시계는 값을 확인할 수 없지만, 말이 되는 값을 주는지는 볼 수 있다.
TEST_CASE("system clock returns a sane moment", "[clock]") {
    const util::DateTime now = util::SystemClock{}.now();
    REQUIRE(now.date.valid());
    REQUIRE(static_cast<int>(now.date.ymd().year()) >= 2024);
    REQUIRE(now.time.minutes() >= 0);
    REQUIRE(now.time.minutes() < 24 * 60);
}
