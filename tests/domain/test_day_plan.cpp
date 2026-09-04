#include "core/domain/day_plan.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using domain::SlotPlacement;
using util::Date;
using util::DateTime;
using util::TimeOfDay;

namespace {

// data-sample 과 같은 모양의 설정. 파일을 읽지 않고 손으로 만들어 이 테스트가 샘플 데이터 변경에
// 흔들리지 않게 한다.
domain::Config makeConfig() {
    domain::Config config;
    config.workCalendar.workWeekdays = {domain::Weekday::Tue, domain::Weekday::Wed,
                                        domain::Weekday::Thu, domain::Weekday::Fri,
                                        domain::Weekday::Sat};

    domain::TimeSlot amWeekday;
    amWeekday.id = domain::TimeSlotId{"am_weekday"};
    amWeekday.displayName = "평일 오전";
    amWeekday.start = "09:00";
    amWeekday.end = "12:00";
    amWeekday.weekdays = {domain::Weekday::Tue, domain::Weekday::Wed, domain::Weekday::Thu,
                          domain::Weekday::Fri};
    config.timeSlots.push_back(amWeekday);

    domain::TimeSlot pmWeekday;
    pmWeekday.id = domain::TimeSlotId{"pm_weekday"};
    pmWeekday.displayName = "평일 오후";
    pmWeekday.start = "13:00";
    pmWeekday.end = "18:00";
    pmWeekday.weekdays = {domain::Weekday::Tue, domain::Weekday::Wed, domain::Weekday::Thu,
                          domain::Weekday::Fri};
    config.timeSlots.push_back(pmWeekday);

    domain::TimeSlot amWeekend;
    amWeekend.id = domain::TimeSlotId{"am_weekend"};
    amWeekend.displayName = "주말 오전";
    amWeekend.start = "09:00";
    amWeekend.end = "12:00";
    amWeekend.weekdays = {domain::Weekday::Sat};
    config.timeSlots.push_back(amWeekend);

    domain::TimeSlot pmWeekend;
    pmWeekend.id = domain::TimeSlotId{"pm_weekend"};
    pmWeekend.displayName = "주말 오후";
    pmWeekend.start = "13:00";
    pmWeekend.end = "17:00";
    pmWeekend.weekdays = {domain::Weekday::Sat};
    config.timeSlots.push_back(pmWeekend);

    return config;
}

// 2026-09-04 는 금요일, 09-05 토, 09-06 일, 09-07 월, 09-08 화.
DateTime at(int year, unsigned month, unsigned day, int hour, int minute) {
    return DateTime{Date::fromYmd(year, month, day), TimeOfDay::fromHm(hour, minute)};
}

std::string slotIdOf(const domain::SlotLookup& lookup) {
    return (lookup.slot != nullptr) ? lookup.slot->id.str() : std::string{};
}

}  // namespace

TEST_CASE("knows which days are working days", "[day_plan]") {
    const domain::Config config = makeConfig();
    REQUIRE(domain::isWorkday(config, Date::fromYmd(2026, 9, 4)));   // 금
    REQUIRE(domain::isWorkday(config, Date::fromYmd(2026, 9, 5)));   // 토
    REQUIRE_FALSE(domain::isWorkday(config, Date::fromYmd(2026, 9, 6)));  // 일
    REQUIRE_FALSE(domain::isWorkday(config, Date::fromYmd(2026, 9, 7)));  // 월
    REQUIRE(domain::isWorkday(config, Date::fromYmd(2026, 9, 8)));   // 화
}

TEST_CASE("treats holidays as non working days", "[day_plan]") {
    domain::Config config = makeConfig();
    config.workCalendar.holidays.push_back("2026-09-04");
    REQUIRE_FALSE(domain::isWorkday(config, Date::fromYmd(2026, 9, 4)));
    // 다른 날은 영향받지 않는다.
    REQUIRE(domain::isWorkday(config, Date::fromYmd(2026, 9, 8)));
}

TEST_CASE("lists the slots that apply to a date", "[day_plan]") {
    const domain::Config config = makeConfig();

    SECTION("평일에는 평일 시간대만") {
        const auto slots = domain::slotsForDate(config, Date::fromYmd(2026, 9, 4));
        REQUIRE(slots.size() == 2);
        REQUIRE(slots[0]->id.str() == "am_weekday");
        REQUIRE(slots[1]->id.str() == "pm_weekday");
    }
    SECTION("토요일에는 주말 시간대만") {
        const auto slots = domain::slotsForDate(config, Date::fromYmd(2026, 9, 5));
        REQUIRE(slots.size() == 2);
        REQUIRE(slots[0]->id.str() == "am_weekend");
        REQUIRE(slots[1]->id.str() == "pm_weekend");
    }
    SECTION("근무일이 아니면 비어 있다") {
        REQUIRE(domain::slotsForDate(config, Date::fromYmd(2026, 9, 6)).empty());
        REQUIRE(domain::slotsForDate(config, Date::fromYmd(2026, 9, 7)).empty());
    }
    SECTION("시작 시각 순으로 정렬된다") {
        domain::Config reordered = makeConfig();
        std::swap(reordered.timeSlots[0], reordered.timeSlots[1]);  // 오후를 먼저 정의
        const auto slots = domain::slotsForDate(reordered, Date::fromYmd(2026, 9, 4));
        REQUIRE(slots[0]->id.str() == "am_weekday");
    }
}

// 임의의 시각 20개를 주입해 올바른 시간대를 돌려주는지 본다 (완료 기준 1번).
TEST_CASE("finds the slot for an injected moment", "[day_plan]") {
    const domain::Config config = makeConfig();

    struct Case {
        DateTime when;
        SlotPlacement placement;
        const char* slotId;
        const char* date;
    };

    const Case cases[] = {
        // 금요일 안쪽
        {at(2026, 9, 4, 9, 0), SlotPlacement::Inside, "am_weekday", "2026-09-04"},
        {at(2026, 9, 4, 10, 30), SlotPlacement::Inside, "am_weekday", "2026-09-04"},
        {at(2026, 9, 4, 11, 59), SlotPlacement::Inside, "am_weekday", "2026-09-04"},
        {at(2026, 9, 4, 13, 0), SlotPlacement::Inside, "pm_weekday", "2026-09-04"},
        {at(2026, 9, 4, 17, 59), SlotPlacement::Inside, "pm_weekday", "2026-09-04"},
        // 금요일 틈새 — 다음 시간대를 안내한다 (D-003)
        {at(2026, 9, 4, 8, 59), SlotPlacement::Upcoming, "am_weekday", "2026-09-04"},
        {at(2026, 9, 4, 0, 0), SlotPlacement::Upcoming, "am_weekday", "2026-09-04"},
        {at(2026, 9, 4, 12, 0), SlotPlacement::Upcoming, "pm_weekday", "2026-09-04"},
        {at(2026, 9, 4, 12, 40), SlotPlacement::Upcoming, "pm_weekday", "2026-09-04"},
        // 금요일 업무 종료 후 — 다음 근무일인 토요일로 넘어간다
        {at(2026, 9, 4, 18, 0), SlotPlacement::Upcoming, "am_weekend", "2026-09-05"},
        {at(2026, 9, 4, 23, 59), SlotPlacement::Upcoming, "am_weekend", "2026-09-05"},
        // 토요일
        {at(2026, 9, 5, 9, 0), SlotPlacement::Inside, "am_weekend", "2026-09-05"},
        {at(2026, 9, 5, 13, 0), SlotPlacement::Inside, "pm_weekend", "2026-09-05"},
        {at(2026, 9, 5, 16, 59), SlotPlacement::Inside, "pm_weekend", "2026-09-05"},
        // 토요일 종료 후 — 일요일과 월요일은 쉬므로 화요일로 건너뛴다
        {at(2026, 9, 5, 17, 0), SlotPlacement::Upcoming, "am_weekday", "2026-09-08"},
        // 쉬는 날에 실행해도 다음 근무일을 안내한다
        {at(2026, 9, 6, 10, 0), SlotPlacement::Upcoming, "am_weekday", "2026-09-08"},
        {at(2026, 9, 7, 10, 0), SlotPlacement::Upcoming, "am_weekday", "2026-09-08"},
        {at(2026, 9, 6, 0, 0), SlotPlacement::Upcoming, "am_weekday", "2026-09-08"},
        // 화요일
        {at(2026, 9, 8, 9, 0), SlotPlacement::Inside, "am_weekday", "2026-09-08"},
        {at(2026, 9, 8, 12, 30), SlotPlacement::Upcoming, "pm_weekday", "2026-09-08"},
    };

    for (const Case& c : cases) {
        const domain::SlotLookup lookup = domain::findSlot(config, c.when);
        INFO(c.when.date.toString() << " " << c.when.time.toString());
        REQUIRE(lookup.placement == c.placement);
        REQUIRE(slotIdOf(lookup) == std::string{c.slotId});
        REQUIRE(lookup.date.toString() == std::string{c.date});
    }
}

// 완료 기준 2번: 경계값이 문서와 일치한다.
TEST_CASE("slot boundaries are half open", "[day_plan]") {
    const domain::Config config = makeConfig();

    SECTION("시작 정각은 시간대 안이다") {
        const auto lookup = domain::findSlot(config, at(2026, 9, 4, 9, 0));
        REQUIRE(lookup.placement == SlotPlacement::Inside);
        REQUIRE(slotIdOf(lookup) == "am_weekday");
    }
    SECTION("종료 정각은 시간대 밖이다") {
        // 12:00 은 오전이 막 끝난 시각이지 오전이 아니다.
        const auto lookup = domain::findSlot(config, at(2026, 9, 4, 12, 0));
        REQUIRE(lookup.placement == SlotPlacement::Upcoming);
        REQUIRE(slotIdOf(lookup) == "pm_weekday");
    }
    SECTION("종료 1분 전은 안이다") {
        REQUIRE(domain::findSlot(config, at(2026, 9, 4, 11, 59)).placement ==
                SlotPlacement::Inside);
    }
    SECTION("자정은 그날 첫 시간대를 기다린다") {
        const auto lookup = domain::findSlot(config, at(2026, 9, 4, 0, 0));
        REQUIRE(lookup.placement == SlotPlacement::Upcoming);
        REQUIRE(lookup.date.toString() == "2026-09-04");
    }
    SECTION("자정 직전은 이미 다음 날을 본다") {
        const auto lookup = domain::findSlot(config, at(2026, 9, 4, 23, 59));
        REQUIRE(lookup.placement == SlotPlacement::Upcoming);
        REQUIRE(lookup.date.toString() == "2026-09-05");
    }
}

TEST_CASE("skips holidays when looking ahead", "[day_plan]") {
    domain::Config config = makeConfig();
    config.workCalendar.holidays.push_back("2026-09-05");  // 토요일을 공휴일로

    const auto lookup = domain::findSlot(config, at(2026, 9, 4, 20, 0));
    REQUIRE(lookup.placement == SlotPlacement::Upcoming);
    REQUIRE(lookup.date.toString() == "2026-09-08");  // 토·일·월을 건너뛴다
}

TEST_CASE("reports none when nothing is ever scheduled", "[day_plan]") {
    SECTION("근무 요일이 없다") {
        domain::Config config = makeConfig();
        config.workCalendar.workWeekdays.clear();
        const auto lookup = domain::findSlot(config, at(2026, 9, 4, 10, 0));
        REQUIRE(lookup.placement == SlotPlacement::None);
        REQUIRE(lookup.slot == nullptr);
    }
    SECTION("시간대가 없다") {
        domain::Config config = makeConfig();
        config.timeSlots.clear();
        const auto lookup = domain::findSlot(config, at(2026, 9, 4, 10, 0));
        REQUIRE(lookup.placement == SlotPlacement::None);
    }
}

// 시각 형식이 깨진 시간대는 건너뛴다. validator 가 따로 보고하므로 여기서 멈출 필요가 없다.
TEST_CASE("ignores slots with a broken time", "[day_plan]") {
    domain::Config config = makeConfig();
    config.timeSlots[0].start = "아홉시";

    const auto slots = domain::slotsForDate(config, Date::fromYmd(2026, 9, 4));
    REQUIRE(slots.size() == 1);
    REQUIRE(slots[0]->id.str() == "pm_weekday");
}

TEST_CASE("empty weekday list means every working day", "[day_plan]") {
    domain::Config config = makeConfig();
    config.timeSlots[0].weekdays.clear();  // 평일 오전을 근무일 전체로

    // 토요일에도 나타난다.
    const auto slots = domain::slotsForDate(config, Date::fromYmd(2026, 9, 5));
    REQUIRE(slots.size() == 3);
}
