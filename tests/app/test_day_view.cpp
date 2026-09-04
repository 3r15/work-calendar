#include "core/app/day_view.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using util::Date;
using util::DateTime;
using util::TimeOfDay;

namespace {

// 시간대 두 개(오전·오후)와 작업집합 두 개를 가진 최소 설정.
domain::Model makeModel() {
    domain::Model model;

    model.config.workCalendar.workWeekdays = {domain::Weekday::Tue, domain::Weekday::Wed,
                                              domain::Weekday::Thu, domain::Weekday::Fri,
                                              domain::Weekday::Sat};

    domain::TimeSlot am;
    am.id = domain::TimeSlotId{"am"};
    am.displayName = "평일 오전";
    am.start = "09:00";
    am.end = "12:00";
    am.weekdays = {domain::Weekday::Tue, domain::Weekday::Wed, domain::Weekday::Thu,
                   domain::Weekday::Fri};
    am.categoryIds = {domain::CategoryId{"cat_am"}};
    model.config.timeSlots.push_back(am);

    domain::TimeSlot pm;
    pm.id = domain::TimeSlotId{"pm"};
    pm.displayName = "평일 오후";
    pm.start = "13:00";
    pm.end = "18:00";
    pm.weekdays = {domain::Weekday::Tue, domain::Weekday::Wed, domain::Weekday::Thu,
                   domain::Weekday::Fri};
    pm.categoryIds = {domain::CategoryId{"cat_pm"}};
    model.config.timeSlots.push_back(pm);

    domain::Category catAm;
    catAm.id = domain::CategoryId{"cat_am"};
    catAm.displayName = "평일 오전";
    catAm.taskSetIds = {domain::TaskSetId{"cleaning_am"}, domain::TaskSetId{"zone"}};
    model.config.categories.push_back(catAm);

    domain::Category catPm;
    catPm.id = domain::CategoryId{"cat_pm"};
    catPm.displayName = "평일 오후";
    catPm.taskSetIds = {domain::TaskSetId{"cleaning_pm"}};
    model.config.categories.push_back(catPm);

    model.tasks.taskSets = {
        domain::TaskSet{domain::TaskSetId{"cleaning_am"}, "청소"},
        domain::TaskSet{domain::TaskSetId{"cleaning_pm"}, "청소"},
        domain::TaskSet{domain::TaskSetId{"zone"}, "배치구역"},
    };

    domain::Task vacuum;
    vacuum.id = domain::TaskId{"vacuum"};
    vacuum.name = "청소기";
    vacuum.taskSetIds = {domain::TaskSetId{"cleaning_am"}, domain::TaskSetId{"cleaning_pm"}};
    vacuum.requiredCount = 2;
    model.tasks.tasks.push_back(vacuum);

    domain::Task sanitize;
    sanitize.id = domain::TaskId{"sanitize"};
    sanitize.name = "소독";
    sanitize.taskSetIds = {domain::TaskSetId{"cleaning_am"}};
    sanitize.requiredCount = 2;
    model.tasks.tasks.push_back(sanitize);

    domain::Task deepClean;
    deepClean.id = domain::TaskId{"deep_clean"};
    deepClean.name = "대청소";
    deepClean.taskSetIds = {domain::TaskSetId{"cleaning_am"}};
    deepClean.requiredCount = 2;
    deepClean.weekdays = {domain::Weekday::Sat};  // 토요일에만
    model.tasks.tasks.push_back(deepClean);

    domain::Task zoneA;
    zoneA.id = domain::TaskId{"zone_a"};
    zoneA.name = "A구역";
    zoneA.taskSetIds = {domain::TaskSetId{"zone"}};
    zoneA.requiredCount = 1;
    model.tasks.tasks.push_back(zoneA);

    return model;
}

DateTime at(unsigned day, int hour, int minute) {
    return DateTime{Date::fromYmd(2026, 9, day), TimeOfDay::fromHm(hour, minute)};
}

}  // namespace

TEST_CASE("builds the structure of a working day", "[day_view]") {
    const domain::Model model = makeModel();
    const app::DayView view = app::buildDayView(model, Date::fromYmd(2026, 9, 4), at(4, 10, 0));

    REQUIRE(view.workday);
    REQUIRE(view.slots.size() == 2);
    REQUIRE(view.slots[0].displayName == "평일 오전");
    REQUIRE(view.slots[0].categories.size() == 1);
    REQUIRE(view.slots[0].categories[0].taskSets.size() == 2);
    REQUIRE(view.slots[0].categories[0].taskSets[0].displayName == "청소");
    REQUIRE(view.slots[0].categories[0].taskSets[1].displayName == "배치구역");
}

// 작업집합 안의 작업 순서는 tasks 배열의 정의 순서다.
TEST_CASE("keeps the defined task order", "[day_view]") {
    const domain::Model model = makeModel();
    const app::DayView view = app::buildDayView(model, Date::fromYmd(2026, 9, 4), at(4, 10, 0));

    const auto& tasks = view.slots[0].categories[0].taskSets[0].tasks;
    REQUIRE(tasks.size() == 2);  // 청소기, 소독 — 대청소는 토요일에만
    REQUIRE(tasks[0].name == "청소기");
    REQUIRE(tasks[1].name == "소독");
    REQUIRE(tasks[0].requiredCount == 2);
}

TEST_CASE("applies the weekday filter of a task", "[day_view]") {
    const domain::Model model = makeModel();

    SECTION("금요일에는 대청소가 없다") {
        const app::DayView view =
            app::buildDayView(model, Date::fromYmd(2026, 9, 4), at(4, 10, 0));
        const auto& tasks = view.slots[0].categories[0].taskSets[0].tasks;
        REQUIRE(tasks.size() == 2);
    }
    SECTION("토요일 오전 시간대는 이 설정에 없다") {
        // am 시간대는 화~금만 붙어 있으므로 토요일에는 시간대 자체가 없다.
        const app::DayView view =
            app::buildDayView(model, Date::fromYmd(2026, 9, 5), at(5, 10, 0));
        REQUIRE(view.workday);
        REQUIRE(view.slots.empty());
    }
}

TEST_CASE("marks the current and past slots", "[day_view]") {
    const domain::Model model = makeModel();

    SECTION("오전 안에 있을 때") {
        const app::DayView view =
            app::buildDayView(model, Date::fromYmd(2026, 9, 4), at(4, 10, 0));
        REQUIRE(view.slots[0].isCurrent);
        REQUIRE_FALSE(view.slots[0].isPast);
        REQUIRE_FALSE(view.slots[1].isCurrent);
        REQUIRE_FALSE(view.upcoming.has_value());
    }
    SECTION("오후 안에 있을 때 오전은 지난 것으로 표시된다") {
        const app::DayView view =
            app::buildDayView(model, Date::fromYmd(2026, 9, 4), at(4, 14, 0));
        REQUIRE(view.slots[0].isPast);
        REQUIRE_FALSE(view.slots[0].isCurrent);
        REQUIRE(view.slots[1].isCurrent);
    }
    SECTION("다른 날을 조회하면 지금 표시를 붙이지 않는다") {
        const app::DayView view =
            app::buildDayView(model, Date::fromYmd(2026, 9, 8), at(4, 10, 0));
        REQUIRE_FALSE(view.slots[0].isCurrent);
        REQUIRE_FALSE(view.slots[0].isPast);
        REQUIRE_FALSE(view.upcoming.has_value());
    }
}

// 완료 기준 3번: 점심시간에 실행하면 다음 시간대 안내가 나온다 (D-003).
TEST_CASE("announces the next slot during the lunch gap", "[day_view]") {
    const domain::Model model = makeModel();
    const app::DayView view = app::buildDayView(model, Date::fromYmd(2026, 9, 4), at(4, 12, 40));

    REQUIRE(view.upcoming.has_value());
    REQUIRE(view.upcoming->displayName == "평일 오후");
    REQUIRE(view.upcoming->start == "13:00");
    REQUIRE(view.upcoming->isToday);
    // 안내가 나온다고 해서 그날 구조가 사라지지는 않는다.
    REQUIRE(view.slots.size() == 2);
}

TEST_CASE("announces the next working day after hours", "[day_view]") {
    const domain::Model model = makeModel();
    const app::DayView view = app::buildDayView(model, Date::fromYmd(2026, 9, 4), at(4, 19, 0));

    REQUIRE(view.upcoming.has_value());
    REQUIRE_FALSE(view.upcoming->isToday);
    // 토요일에는 이 설정의 시간대가 없으므로 다음 화요일이다.
    REQUIRE(view.upcoming->date.toString() == "2026-09-08");
}

TEST_CASE("reports a non working day as empty", "[day_view]") {
    const domain::Model model = makeModel();
    const app::DayView view = app::buildDayView(model, Date::fromYmd(2026, 9, 6), at(6, 10, 0));

    REQUIRE_FALSE(view.workday);
    REQUIRE(view.slots.empty());
    REQUIRE(view.upcoming.has_value());  // 그래도 다음을 안내한다
}

TEST_CASE("skips references that do not resolve", "[day_view]") {
    domain::Model model = makeModel();
    // 없는 분류를 가리키게 만든다. validator 가 오류로 보고하지만 화면은 그려져야 한다.
    model.config.timeSlots[0].categoryIds.push_back(domain::CategoryId{"없음"});

    const app::DayView view = app::buildDayView(model, Date::fromYmd(2026, 9, 4), at(4, 10, 0));
    REQUIRE(view.slots[0].categories.size() == 1);
}
