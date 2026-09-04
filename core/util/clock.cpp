#include "core/util/clock.h"

#include <chrono>

namespace util {

DateTime SystemClock::now() const {
    const auto instant = std::chrono::system_clock::now();

    std::chrono::local_time<std::chrono::system_clock::duration> local;
    try {
        local = std::chrono::current_zone()->to_local(instant);
    } catch (const std::exception&) {
        // 시간대 데이터베이스가 없는 환경에서는 UTC 로 떨어진다. 프로그램을 멈추지는 않는다 —
        // 시각이 몇 시간 어긋나는 것이 아예 실행되지 않는 것보다 낫다.
        local = std::chrono::local_time<std::chrono::system_clock::duration>{
            instant.time_since_epoch()};
    }

    const auto days = std::chrono::floor<std::chrono::days>(local);
    const auto sinceMidnight = std::chrono::floor<std::chrono::minutes>(local - days);

    DateTime out;
    out.date = Date{std::chrono::year_month_day{std::chrono::sys_days{days.time_since_epoch()}}};
    out.time = TimeOfDay{static_cast<int>(sinceMidnight.count())};

    // 로컬 시각에서 UTC 를 빼면 오프셋이다. 시간대 정보를 못 얻었으면 0(UTC)이 남는다.
    const auto utc = std::chrono::floor<std::chrono::minutes>(instant.time_since_epoch());
    const auto localMinutes = std::chrono::floor<std::chrono::minutes>(local.time_since_epoch());
    out.utcOffsetMinutes = static_cast<int>((localMinutes - utc).count());
    return out;
}

}  // namespace util
