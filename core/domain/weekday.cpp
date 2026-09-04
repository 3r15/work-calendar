#include "core/domain/weekday.h"

#include <algorithm>
#include <array>

namespace domain {
namespace {

// 배열 첨자가 enum 값과 같아야 한다. Weekday::Sun == 0 부터 시작한다.
constexpr std::array<std::string_view, 7> kCodes{"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
constexpr std::array<std::string_view, 7> kLabels{"일", "월", "화", "수", "목", "금", "토"};

}  // namespace

std::optional<Weekday> parseWeekday(std::string_view code) {
    for (std::size_t i = 0; i < kCodes.size(); ++i) {
        if (kCodes[i] == code) {
            return static_cast<Weekday>(i);
        }
    }
    return std::nullopt;
}

std::string formatWeekday(Weekday day) {
    return std::string{kCodes[static_cast<std::size_t>(day)]};
}

std::string weekdayLabel(Weekday day) {
    return std::string{kLabels[static_cast<std::size_t>(day)]};
}

bool contains(const std::vector<Weekday>& days, Weekday day) {
    return std::find(days.begin(), days.end(), day) != days.end();
}

Weekday fromChrono(std::chrono::weekday day) {
    // c_encoding() 은 일요일이 0 이다. Weekday::Sun 도 0 이므로 그대로 맞는다.
    return static_cast<Weekday>(day.c_encoding());
}

}  // namespace domain
