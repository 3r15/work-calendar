#include "core/app/day_view.h"

#include "core/domain/day_plan.h"

#include <algorithm>

namespace app {
namespace {

const domain::TaskSet* findTaskSet(const domain::TaskList& tasks, const domain::TaskSetId& id) {
    for (const domain::TaskSet& set : tasks.taskSets) {
        if (set.id == id) {
            return &set;
        }
    }
    return nullptr;
}

const domain::Category* findCategory(const domain::Config& config,
                                     const domain::CategoryId& id) {
    for (const domain::Category& category : config.categories) {
        if (category.id == id) {
            return &category;
        }
    }
    return nullptr;
}

// 작업집합에 속하면서 그날 요일에 해당하는 작업. 정의 순서를 지킨다 — 배정 순서와 같아야 한다.
std::vector<TaskLine> tasksIn(const domain::TaskList& tasks, const domain::TaskSetId& setId,
                              domain::Weekday day) {
    std::vector<TaskLine> out;
    for (const domain::Task& task : tasks.tasks) {
        const bool inSet = std::find(task.taskSetIds.begin(), task.taskSetIds.end(), setId) !=
                           task.taskSetIds.end();
        if (!inSet) {
            continue;
        }
        // 비어 있으면 근무일 전체라는 뜻이다.
        if (!task.weekdays.empty() && !domain::contains(task.weekdays, day)) {
            continue;
        }
        out.push_back(TaskLine{task.name, task.requiredCount});
    }
    return out;
}

}  // namespace

DayView buildDayView(const domain::Model& model, const util::Date& date,
                     const util::DateTime& now) {
    DayView view;
    view.date = date;
    view.workday = domain::isWorkday(model.config, date);

    const domain::Weekday day = domain::fromChrono(date.weekday());
    const domain::SlotLookup lookup = domain::findSlot(model.config, now);
    const bool viewingToday = (date == now.date);

    for (const domain::TimeSlot* slot : domain::slotsForDate(model.config, date)) {
        SlotBlock block;
        block.displayName = slot->displayName;
        block.start = slot->start;
        block.end = slot->end;

        if (viewingToday) {
            block.isCurrent = (lookup.placement == domain::SlotPlacement::Inside &&
                               lookup.slot == slot);
            const std::optional<util::TimeOfDay> end = util::TimeOfDay::parse(slot->end);
            block.isPast = end.has_value() && now.time >= *end;
        }

        for (const domain::CategoryId& categoryId : slot->categoryIds) {
            const domain::Category* category = findCategory(model.config, categoryId);
            if (category == nullptr) {
                continue;  // 없는 참조는 validator 가 오류로 보고한다
            }
            CategoryBlock categoryBlock;
            categoryBlock.displayName = category->displayName;
            for (const domain::TaskSetId& setId : category->taskSetIds) {
                const domain::TaskSet* set = findTaskSet(model.tasks, setId);
                if (set == nullptr) {
                    continue;
                }
                TaskSetBlock setBlock;
                setBlock.displayName = set->displayName;
                setBlock.tasks = tasksIn(model.tasks, setId, day);
                categoryBlock.taskSets.push_back(std::move(setBlock));
            }
            block.categories.push_back(std::move(categoryBlock));
        }
        view.slots.push_back(std::move(block));
    }

    // 지금이 시간대 밖이면 다음에 오는 것을 안내한다. 빈 화면은 고장난 것처럼 보인다 (D-003).
    if (viewingToday && lookup.placement == domain::SlotPlacement::Upcoming &&
        lookup.slot != nullptr) {
        UpcomingSlot upcoming;
        upcoming.displayName = lookup.slot->displayName;
        upcoming.start = lookup.slot->start;
        upcoming.date = lookup.date;
        upcoming.isToday = (lookup.date == now.date);
        view.upcoming = upcoming;
    }
    return view;
}

}  // namespace app
