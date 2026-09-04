#include "core/app/day_view.h"

#include "core/domain/availability.h"
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

// 스냅샷에서 이 시간대·분류·작업집합에 해당하는 항목을 찾는다.
const domain::SnapshotTaskSet* findSnapshotSet(const domain::DaySnapshot* snapshot,
                                               const domain::TimeSlotId& slotId,
                                               const domain::CategoryId& categoryId,
                                               const domain::TaskSetId& setId) {
    if (snapshot == nullptr) {
        return nullptr;
    }
    for (const domain::SnapshotSlot& slot : snapshot->slots) {
        if (!(slot.timeSlotId == slotId) || !(slot.categoryId == categoryId)) {
            continue;
        }
        for (const domain::SnapshotTaskSet& set : slot.taskSets) {
            if (set.taskSetId == setId) {
                return &set;
            }
        }
    }
    return nullptr;
}

std::string workerNameOf(const domain::Model& model, const domain::WorkerId& id) {
    for (const domain::Worker& worker : model.workers.workers) {
        if (worker.id == id) {
            return worker.name;
        }
    }
    // 지워진 작업자를 과거 스냅샷이 가리킬 수 있다. ID 라도 보여준다.
    return id.str();
}

// 작업집합에 속하면서 그날 요일에 해당하는 작업. 정의 순서를 지킨다 — 배정 순서와 같아야 한다.
std::vector<TaskLine> tasksIn(const domain::Model& model, const domain::TaskSetId& setId,
                              domain::Weekday day, const util::Date& date,
                              const domain::SnapshotTaskSet* snapshotSet) {
    std::vector<TaskLine> out;
    for (const domain::Task& task : model.tasks.tasks) {
        const bool inSet = std::find(task.taskSetIds.begin(), task.taskSetIds.end(), setId) !=
                           task.taskSetIds.end();
        if (!inSet) {
            continue;
        }
        // 비어 있으면 근무일 전체라는 뜻이다.
        if (!task.weekdays.empty() && !domain::contains(task.weekdays, day)) {
            continue;
        }

        TaskLine line;
        line.name = task.name;
        line.requiredCount = task.requiredCount;

        if (snapshotSet != nullptr) {
            for (const domain::SnapshotAssignment& assignment : snapshotSet->assignments) {
                if (!(assignment.taskId == task.id)) {
                    continue;
                }
                AssignedWorker worker;
                worker.name = workerNameOf(model, assignment.workerId);
                // 저장된 absentAssignee 를 믿지 않고 지금 다시 판정한다 (D-009).
                worker.absent = domain::isAbsentOn(model, assignment.workerId, date);
                line.workers.push_back(std::move(worker));
            }
            for (const domain::SnapshotUnassigned& item : snapshotSet->unassigned) {
                if (item.taskId == task.id) {
                    line.unassignedCount += item.count;
                }
            }
        }
        out.push_back(std::move(line));
    }
    return out;
}

}  // namespace

DayView buildDayView(const domain::Model& model, const util::Date& date,
                     const util::DateTime& now, const domain::DaySnapshot* snapshot) {
    DayView view;
    view.date = date;
    view.workday = domain::isWorkday(model.config, date);

    const domain::Weekday day = domain::fromChrono(date.weekday());
    const domain::SlotLookup lookup = domain::findSlot(model.config, now);
    const bool viewingToday = (date == now.date);

    for (const domain::TimeSlot* slot : domain::slotsForDate(model.config, date)) {
        SlotBlock block;
        block.id = slot->id;
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
            categoryBlock.id = category->id;
            categoryBlock.displayName = category->displayName;
            for (const domain::TaskSetId& setId : category->taskSetIds) {
                const domain::TaskSet* set = findTaskSet(model.tasks, setId);
                if (set == nullptr) {
                    continue;
                }
                TaskSetBlock setBlock;
                setBlock.displayName = set->displayName;
                setBlock.tasks = tasksIn(model, setId, day, date,
                                         findSnapshotSet(snapshot, slot->id, category->id, setId));
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
