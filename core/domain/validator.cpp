#include "core/domain/validator.h"

#include "core/util/korean.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace domain {
namespace {

constexpr const char* kConfigFile = "config.json";
constexpr const char* kWorkersFile = "workers.json";
constexpr const char* kTasksFile = "tasks.json";
constexpr const char* kAbsencesFile = "absences.json";

bool allDigits(const std::string& text, std::size_t from, std::size_t count) {
    for (std::size_t i = from; i < from + count; ++i) {
        if (i >= text.size() || text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    return true;
}

int twoDigits(const std::string& text, std::size_t at) {
    return (text[at] - '0') * 10 + (text[at + 1] - '0');
}

// 같은 ID 가 두 번 나오면 오류. 어느 쪽이 맞는지 프로그램이 정할 수 없다.
template <typename IdType, typename Entity, typename GetId>
void checkDuplicateIds(const std::vector<Entity>& items, GetId getId, const char* where,
                       const char* what, Report& report) {
    std::set<IdType> seen;
    for (const Entity& item : items) {
        const IdType& id = getId(item);
        if (!seen.insert(id).second) {
            report.error(where, std::string{what} + " ID \"" + id.str() + "\"" +
                                    std::string{util::josaIGa(id.str())} +
                                    " 두 번 정의되어 있습니다. 하나만 남기세요.");
        }
    }
}

// 시간대가 실제로 적용되는 요일. 비어 있으면 근무일 전체다.
std::vector<Weekday> effectiveWeekdays(const std::vector<Weekday>& own,
                                       const std::vector<Weekday>& workWeekdays) {
    return own.empty() ? workWeekdays : own;
}

std::vector<Weekday> intersect(const std::vector<Weekday>& a, const std::vector<Weekday>& b) {
    std::vector<Weekday> out;
    for (Weekday day : a) {
        if (contains(b, day) && !contains(out, day)) {
            out.push_back(day);
        }
    }
    return out;
}

std::string joinWeekdays(const std::vector<Weekday>& days) {
    std::string out;
    for (std::size_t i = 0; i < days.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += weekdayLabel(days[i]);
    }
    return out;
}

// 배타 관계를 간선으로 보는 그래프의 연결 요소. 배정 엔진이 쓰는 것과 같은 분해다.
std::vector<std::vector<const Task*>> conflictComponents(const std::vector<const Task*>& members) {
    std::unordered_map<std::string, const Task*> byId;
    for (const Task* task : members) {
        byId.emplace(task->id.str(), task);
    }

    std::unordered_set<std::string> visited;
    std::vector<std::vector<const Task*>> components;
    for (const Task* start : members) {
        if (visited.count(start->id.str()) > 0) {
            continue;
        }
        std::vector<const Task*> component;
        std::vector<const Task*> stack{start};
        while (!stack.empty()) {
            const Task* current = stack.back();
            stack.pop_back();
            if (!visited.insert(current->id.str()).second) {
                continue;
            }
            component.push_back(current);
            for (const TaskId& neighbour : current->conflictsWith) {
                const auto it = byId.find(neighbour.str());
                // 같은 작업집합 안에 있는 상대만 따라간다.
                if (it != byId.end() && visited.count(neighbour.str()) == 0) {
                    stack.push_back(it->second);
                }
            }
        }
        components.push_back(std::move(component));
    }
    return components;
}

void checkReferences(const Model& model, Report& report) {
    std::set<TaskSetId> taskSetIds;
    for (const TaskSet& set : model.tasks.taskSets) {
        taskSetIds.insert(set.id);
    }
    std::set<CategoryId> categoryIds;
    for (const Category& category : model.config.categories) {
        categoryIds.insert(category.id);
    }
    std::set<TaskId> taskIds;
    for (const Task& task : model.tasks.tasks) {
        taskIds.insert(task.id);
    }
    std::set<WorkerId> workerIds;
    for (const Worker& worker : model.workers.workers) {
        workerIds.insert(worker.id);
    }

    for (const TimeSlot& slot : model.config.timeSlots) {
        if (slot.categoryIds.empty()) {
            report.error(kConfigFile, "시간대 \"" + slot.displayName +
                                          "\" 에 분류가 하나도 연결되어 있지 않습니다.");
        }
        for (const CategoryId& id : slot.categoryIds) {
            if (categoryIds.count(id) == 0) {
                report.error(kConfigFile, "시간대 \"" + slot.displayName + "\"" +
                                              std::string{util::josaIGa(slot.displayName)} +
                                              " 존재하지 않는 분류 \"" + id.str() + "\"" +
                                              std::string{util::josaEulReul(id.str())} +
                                              " 참조합니다.");
            }
        }
    }

    for (const Category& category : model.config.categories) {
        if (category.taskSetIds.empty()) {
            report.error(kConfigFile, "분류 \"" + category.displayName +
                                          "\" 에 작업집합이 하나도 없습니다.");
        }
        for (const TaskSetId& id : category.taskSetIds) {
            if (taskSetIds.count(id) == 0) {
                report.error(kConfigFile, "분류 \"" + category.displayName + "\"" +
                                              std::string{util::josaIGa(category.displayName)} +
                                              " 존재하지 않는 작업집합 \"" + id.str() + "\"" +
                                              std::string{util::josaEulReul(id.str())} +
                                              " 참조합니다.");
            }
        }
    }

    for (const Task& task : model.tasks.tasks) {
        if (task.taskSetIds.empty()) {
            report.error(kTasksFile, "작업 \"" + task.name + "\"" +
                                         std::string{util::josaIGa(task.name)} +
                                         " 어느 작업집합에도 속해 있지 않습니다.");
        }
        for (const TaskSetId& id : task.taskSetIds) {
            if (taskSetIds.count(id) == 0) {
                report.error(kTasksFile, "작업 \"" + task.name + "\"" +
                                             std::string{util::josaIGa(task.name)} +
                                             " 존재하지 않는 작업집합 \"" + id.str() +
                                             "\" 에 속해 있습니다.");
            }
        }
        for (const TaskId& id : task.conflictsWith) {
            if (taskIds.count(id) == 0) {
                report.error(kTasksFile, "작업 \"" + task.name + "\"" +
                                             std::string{util::josaIGa(task.name)} +
                                             " 존재하지 않는 작업 \"" + id.str() + "\"" +
                                             std::string{util::josaWaGwa(id.str())} +
                                             " 배타 관계로 지정되어 있습니다.");
            }
        }
    }

    for (const Absence& absence : model.absences.absences) {
        if (workerIds.count(absence.workerId) == 0) {
            report.error(kAbsencesFile, "휴무가 존재하지 않는 작업자 \"" + absence.workerId.str() +
                                            "\" 를 가리킵니다.");
        }
    }
}

void checkTimeSlots(const Model& model, Report& report) {
    const std::vector<Weekday>& workWeekdays = model.config.workCalendar.workWeekdays;
    if (workWeekdays.empty()) {
        report.error(kConfigFile, "근무 요일이 하나도 지정되어 있지 않습니다. "
                                  "workCalendar.workWeekdays 에 최소 하나를 넣어 주세요.");
    }

    for (const TimeSlot& slot : model.config.timeSlots) {
        if (!isTimeString(slot.start) || !isTimeString(slot.end)) {
            report.error(kConfigFile, "시간대 \"" + slot.displayName + "\" 의 시각 형식이 "
                                      "잘못됐습니다 (" + slot.start + " ~ " + slot.end +
                                      "). 24시간제 \"HH:MM\" 이어야 합니다.");
            continue;
        }
        if (slot.start >= slot.end) {
            report.error(kConfigFile, "시간대 \"" + slot.displayName + "\" 의 시작이 끝보다 " +
                                          "늦거나 같습니다 (" + slot.start + " ~ " + slot.end +
                                          ").");
        }
    }

    // 같은 요일에 구간이 겹치면 "지금 어느 시간대인가" 가 하나로 정해지지 않는다.
    for (std::size_t i = 0; i < model.config.timeSlots.size(); ++i) {
        for (std::size_t j = i + 1; j < model.config.timeSlots.size(); ++j) {
            const TimeSlot& a = model.config.timeSlots[i];
            const TimeSlot& b = model.config.timeSlots[j];
            if (!isTimeString(a.start) || !isTimeString(a.end) || !isTimeString(b.start) ||
                !isTimeString(b.end)) {
                continue;
            }
            const std::vector<Weekday> shared =
                intersect(effectiveWeekdays(a.weekdays, model.config.workCalendar.workWeekdays),
                          effectiveWeekdays(b.weekdays, model.config.workCalendar.workWeekdays));
            if (shared.empty()) {
                continue;
            }
            if (a.start < b.end && b.start < a.end) {
                report.error(kConfigFile,
                             "시간대 \"" + a.displayName + "\"" +
                                 std::string{util::josaWaGwa(a.displayName)} + " \"" +
                                 b.displayName + "\"" +
                                 std::string{util::josaIGa(b.displayName)} + " " +
                                 joinWeekdays(shared) + "요일에 겹칩니다.");
            }
        }
    }
}

void checkTasks(const Model& model, Report& report) {
    for (const Task& task : model.tasks.tasks) {
        if (task.requiredCount <= 0) {
            report.error(kTasksFile, "작업 \"" + task.name + "\" 의 필요 인원이 " +
                                         std::to_string(task.requiredCount) +
                                         " 명입니다. 1 이상이어야 합니다.");
        }
    }
}

void checkAbsences(const Model& model, Report& report) {
    for (const Absence& absence : model.absences.absences) {
        if (!isDateString(absence.from) || !isDateString(absence.to)) {
            report.error(kAbsencesFile, "휴무의 날짜 형식이 잘못됐습니다 (" + absence.from +
                                            " ~ " + absence.to + "). \"YYYY-MM-DD\" 여야 합니다.");
            continue;
        }
        if (absence.from > absence.to) {
            report.error(kAbsencesFile, "휴무의 시작일이 종료일보다 늦습니다 (" + absence.from +
                                            " ~ " + absence.to + ").");
        }
    }
    for (const std::string& holiday : model.config.workCalendar.holidays) {
        if (!isDateString(holiday)) {
            report.error(kConfigFile, "공휴일 \"" + holiday +
                                          "\" 의 날짜 형식이 잘못됐습니다. \"YYYY-MM-DD\" 여야 "
                                          "합니다.");
        }
    }
}

void checkWarnings(const Model& model, Report& report) {
    // 어떤 분류에도 속하지 않은 작업집합
    std::set<TaskSetId> setsInCategories;
    for (const Category& category : model.config.categories) {
        for (const TaskSetId& id : category.taskSetIds) {
            setsInCategories.insert(id);
        }
    }
    // 어떤 시간대에도 연결되지 않은 분류
    std::set<CategoryId> categoriesInSlots;
    for (const TimeSlot& slot : model.config.timeSlots) {
        for (const CategoryId& id : slot.categoryIds) {
            categoriesInSlots.insert(id);
        }
    }

    for (const TaskSet& set : model.tasks.taskSets) {
        if (setsInCategories.count(set.id) == 0) {
            report.warn(kTasksFile, "작업집합 \"" + set.displayName + "\"" +
                                        std::string{util::josaIGa(set.displayName)} +
                                        " 어떤 분류에도 속해 있지 않습니다. 화면에 나오지 "
                                        "않습니다.");
        }
        const bool hasTask =
            std::any_of(model.tasks.tasks.begin(), model.tasks.tasks.end(),
                        [&set](const Task& task) {
                            return std::find(task.taskSetIds.begin(), task.taskSetIds.end(),
                                             set.id) != task.taskSetIds.end();
                        });
        if (!hasTask) {
            report.warn(kTasksFile,
                        "작업집합 \"" + set.displayName + "\" 에 속한 작업이 없습니다.");
        }
    }

    for (const Category& category : model.config.categories) {
        if (categoriesInSlots.count(category.id) == 0) {
            report.warn(kConfigFile, "분류 \"" + category.displayName + "\"" +
                                         std::string{util::josaIGa(category.displayName)} +
                                         " 어떤 시간대에도 연결되어 있지 않습니다.");
        }
    }

    // 이름이 겹치면 --worker 로 사람을 지정할 때 모호해진다.
    std::map<std::string, int> nameCount;
    for (const Worker& worker : model.workers.workers) {
        nameCount[worker.name] += 1;
    }
    for (const auto& [name, count] : nameCount) {
        if (count > 1) {
            report.warn(kWorkersFile, "작업자 이름 \"" + name + "\" 이 " + std::to_string(count) +
                                          "명 있습니다. 이름으로 지정할 때 ID 를 써야 합니다.");
        }
    }

    std::vector<const Worker*> active;
    for (const Worker& worker : model.workers.workers) {
        if (worker.active) {
            active.push_back(&worker);
        }
    }
    if (active.empty()) {
        report.warn(kWorkersFile, "배정할 수 있는 작업자가 없습니다. 모두 active: false 이거나 "
                                  "작업자가 등록되어 있지 않습니다.");
    }

    // 근무일이 아닌 요일만 지정된 작업은 영원히 실행되지 않는다.
    const std::vector<Weekday>& workWeekdays = model.config.workCalendar.workWeekdays;
    for (const Task& task : model.tasks.tasks) {
        if (!task.weekdays.empty() && intersect(task.weekdays, workWeekdays).empty()) {
            report.warn(kTasksFile, "작업 \"" + task.name + "\"" +
                                        std::string{util::josaEunNeun(task.name)} +
                                        " 근무일이 아닌 요일에만 지정되어 있어 실행되지 "
                                        "않습니다.");
        }
    }

    // 배타 그룹의 필요 인원 합이 활성 인원보다 많으면 매일 미배정이 생긴다.
    const int activeCount = static_cast<int>(active.size());
    for (const TaskSet& set : model.tasks.taskSets) {
        std::vector<const Task*> members;
        for (const Task& task : model.tasks.tasks) {
            if (std::find(task.taskSetIds.begin(), task.taskSetIds.end(), set.id) !=
                task.taskSetIds.end()) {
                members.push_back(&task);
            }
        }
        for (const std::vector<const Task*>& component : conflictComponents(members)) {
            int required = 0;
            std::string names;
            for (const Task* task : component) {
                required += std::max(task->requiredCount, 0);
                if (!names.empty()) {
                    names += ", ";
                }
                names += task->name;
            }
            if (required > activeCount) {
                report.warn(kTasksFile, "작업집합 \"" + set.displayName + "\" 의 배타 그룹 (" +
                                            names + ") 은 " + std::to_string(required) +
                                            "명이 필요한데 활성 작업자는 " +
                                            std::to_string(activeCount) +
                                            "명입니다. 매일 미배정이 생깁니다.");
            }
        }
    }
}

}  // namespace

bool isDateString(const std::string& text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return false;
    }
    if (!allDigits(text, 0, 4) || !allDigits(text, 5, 2) || !allDigits(text, 8, 2)) {
        return false;
    }
    const int month = twoDigits(text, 5);
    const int day = twoDigits(text, 8);
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

bool isTimeString(const std::string& text) {
    if (text.size() != 5 || text[2] != ':') {
        return false;
    }
    if (!allDigits(text, 0, 2) || !allDigits(text, 3, 2)) {
        return false;
    }
    return twoDigits(text, 0) <= 23 && twoDigits(text, 3) <= 59;
}

Report validate(const Model& model) {
    Report report;

    checkDuplicateIds<TimeSlotId>(
        model.config.timeSlots, [](const TimeSlot& s) -> const TimeSlotId& { return s.id; },
        kConfigFile, "시간대", report);
    checkDuplicateIds<CategoryId>(
        model.config.categories, [](const Category& c) -> const CategoryId& { return c.id; },
        kConfigFile, "분류", report);
    checkDuplicateIds<TaskSetId>(
        model.tasks.taskSets, [](const TaskSet& s) -> const TaskSetId& { return s.id; },
        kTasksFile, "작업집합", report);
    checkDuplicateIds<TaskId>(
        model.tasks.tasks, [](const Task& t) -> const TaskId& { return t.id; }, kTasksFile,
        "작업", report);
    checkDuplicateIds<WorkerId>(
        model.workers.workers, [](const Worker& w) -> const WorkerId& { return w.id; },
        kWorkersFile, "작업자", report);

    checkReferences(model, report);
    checkTimeSlots(model, report);
    checkTasks(model, report);
    checkAbsences(model, report);
    checkWarnings(model, report);
    return report;
}

}  // namespace domain
