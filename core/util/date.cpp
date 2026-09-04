#include "core/util/date.h"

#include <cstdio>

namespace util {
namespace {

bool allDigits(std::string_view text, std::size_t from, std::size_t count) {
    for (std::size_t i = from; i < from + count; ++i) {
        if (i >= text.size() || text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    return true;
}

int toInt(std::string_view text, std::size_t from, std::size_t count) {
    int value = 0;
    for (std::size_t i = from; i < from + count; ++i) {
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

}  // namespace

Date Date::fromYmd(int year, unsigned month, unsigned day) {
    return Date{std::chrono::year_month_day{std::chrono::year{year}, std::chrono::month{month},
                                            std::chrono::day{day}}};
}

std::optional<Date> Date::parse(std::string_view text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return std::nullopt;
    }
    if (!allDigits(text, 0, 4) || !allDigits(text, 5, 2) || !allDigits(text, 8, 2)) {
        return std::nullopt;
    }
    const Date date = fromYmd(toInt(text, 0, 4), static_cast<unsigned>(toInt(text, 5, 2)),
                              static_cast<unsigned>(toInt(text, 8, 2)));
    // 2026-02-30 처럼 달력에 없는 날짜를 걸러낸다.
    if (!date.valid()) {
        return std::nullopt;
    }
    return date;
}

std::string Date::toString() const {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", static_cast<int>(ymd_.year()),
                  static_cast<unsigned>(ymd_.month()), static_cast<unsigned>(ymd_.day()));
    return std::string{buffer};
}

std::chrono::weekday Date::weekday() const {
    return std::chrono::weekday{std::chrono::sys_days{ymd_}};
}

Date Date::plusDays(int days) const {
    const std::chrono::sys_days moved = std::chrono::sys_days{ymd_} + std::chrono::days{days};
    return Date{std::chrono::year_month_day{moved}};
}

std::strong_ordering operator<=>(const Date& a, const Date& b) {
    // year_month_day 의 비교는 유효한 날짜에서만 뜻이 있으므로 일수로 환산해 비교한다.
    const auto left = std::chrono::sys_days{a.ymd_}.time_since_epoch().count();
    const auto right = std::chrono::sys_days{b.ymd_}.time_since_epoch().count();
    return left <=> right;
}

std::optional<TimeOfDay> TimeOfDay::parse(std::string_view text) {
    if (text.size() != 5 || text[2] != ':') {
        return std::nullopt;
    }
    if (!allDigits(text, 0, 2) || !allDigits(text, 3, 2)) {
        return std::nullopt;
    }
    const int hour = toInt(text, 0, 2);
    const int minute = toInt(text, 3, 2);
    if (hour > 23 || minute > 59) {
        return std::nullopt;
    }
    return TimeOfDay::fromHm(hour, minute);
}

std::string TimeOfDay::toString() const {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", hour(), minute());
    return std::string{buffer};
}

}  // namespace util
