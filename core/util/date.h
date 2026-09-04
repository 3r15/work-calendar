#pragma once

#include <chrono>
#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace util {

// 날짜. std::chrono::year_month_day 를 감싸 파싱·포맷·요일 판정을 한곳에 모은다.
//
// 요일은 std::chrono::weekday 로 돌려준다. 도메인의 Weekday 로 바꾸는 것은 domain 이 한다 —
// util 이 domain 을 알면 의존 방향이 뒤집힌다.
class Date {
public:
    Date() = default;
    explicit Date(std::chrono::year_month_day ymd) : ymd_(ymd) {}
    static Date fromYmd(int year, unsigned month, unsigned day);

    // "YYYY-MM-DD". 형식이 맞아도 2026-02-30 처럼 없는 날짜면 비어 있는 값을 돌려준다.
    static std::optional<Date> parse(std::string_view text);

    std::string toString() const;
    bool valid() const { return ymd_.ok(); }

    std::chrono::year_month_day ymd() const { return ymd_; }
    std::chrono::weekday weekday() const;

    Date plusDays(int days) const;

    friend bool operator==(const Date& a, const Date& b) { return a.ymd_ == b.ymd_; }
    friend std::strong_ordering operator<=>(const Date& a, const Date& b);

private:
    std::chrono::year_month_day ymd_{};
};

// 하루 안의 시각. 자정부터 흐른 분으로 다룬다. 초는 쓰지 않는다 — 시간대 경계가 분 단위다.
class TimeOfDay {
public:
    TimeOfDay() = default;
    explicit TimeOfDay(int minutesSinceMidnight) : minutes_(minutesSinceMidnight) {}
    static TimeOfDay fromHm(int hour, int minute) { return TimeOfDay{hour * 60 + minute}; }

    // "HH:MM" (24시간제).
    static std::optional<TimeOfDay> parse(std::string_view text);

    std::string toString() const;
    int minutes() const { return minutes_; }
    int hour() const { return minutes_ / 60; }
    int minute() const { return minutes_ % 60; }

    friend bool operator==(const TimeOfDay&, const TimeOfDay&) = default;
    friend std::strong_ordering operator<=>(const TimeOfDay&, const TimeOfDay&) = default;

private:
    int minutes_{0};
};

struct DateTime {
    Date date;
    TimeOfDay time;
};

}  // namespace util
