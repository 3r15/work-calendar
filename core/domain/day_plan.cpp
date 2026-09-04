#include "core/domain/day_plan.h"

#include <algorithm>

namespace domain {
namespace {

// 앞으로 이만큼 안에 근무일이 없으면 설정이 잘못된 것이다. 무한 루프를 막는 상한.
constexpr int kSearchLimitDays = 366;

std::optional<util::TimeOfDay> slotStart(const TimeSlot& slot) {
    return util::TimeOfDay::parse(slot.start);
}

std::optional<util::TimeOfDay> slotEnd(const TimeSlot& slot) {
    return util::TimeOfDay::parse(slot.end);
}

}  // namespace

bool isWorkday(const Config& config, const util::Date& date) {
    const Weekday day = fromChrono(date.weekday());
    if (!contains(config.workCalendar.workWeekdays, day)) {
        return false;
    }
    const std::string text = date.toString();
    const auto& holidays = config.workCalendar.holidays;
    return std::find(holidays.begin(), holidays.end(), text) == holidays.end();
}

std::vector<const TimeSlot*> slotsForDate(const Config& config, const util::Date& date) {
    std::vector<const TimeSlot*> out;
    if (!isWorkday(config, date)) {
        return out;
    }

    const Weekday day = fromChrono(date.weekday());
    for (const TimeSlot& slot : config.timeSlots) {
        // 비어 있으면 근무일 전체라는 뜻이다.
        const bool applies = slot.weekdays.empty() || contains(slot.weekdays, day);
        if (!applies) {
            continue;
        }
        if (!slotStart(slot).has_value() || !slotEnd(slot).has_value()) {
            continue;
        }
        out.push_back(&slot);
    }

    std::stable_sort(out.begin(), out.end(), [](const TimeSlot* a, const TimeSlot* b) {
        return *slotStart(*a) < *slotStart(*b);
    });
    return out;
}

std::optional<util::Date> nextWorkdayWithSlots(const Config& config, const util::Date& after) {
    for (int offset = 1; offset <= kSearchLimitDays; ++offset) {
        const util::Date candidate = after.plusDays(offset);
        if (!slotsForDate(config, candidate).empty()) {
            return candidate;
        }
    }
    return std::nullopt;
}

SlotLookup findSlot(const Config& config, const util::DateTime& now) {
    SlotLookup out;
    out.date = now.date;

    const std::vector<const TimeSlot*> today = slotsForDate(config, now.date);
    for (const TimeSlot* slot : today) {
        const util::TimeOfDay start = *slotStart(*slot);
        const util::TimeOfDay end = *slotEnd(*slot);
        if (now.time >= start && now.time < end) {
            out.placement = SlotPlacement::Inside;
            out.slot = slot;
            return out;
        }
    }

    // 오늘 남은 시간대 중 가장 이른 것.
    for (const TimeSlot* slot : today) {
        if (now.time < *slotStart(*slot)) {
            out.placement = SlotPlacement::Upcoming;
            out.slot = slot;
            return out;
        }
    }

    // 오늘은 끝났다. 다음 근무일의 첫 시간대로 넘어간다.
    if (const std::optional<util::Date> next = nextWorkdayWithSlots(config, now.date)) {
        const std::vector<const TimeSlot*> slots = slotsForDate(config, *next);
        out.placement = SlotPlacement::Upcoming;
        out.slot = slots.front();
        out.date = *next;
        return out;
    }

    out.placement = SlotPlacement::None;
    out.slot = nullptr;
    return out;
}

}  // namespace domain
