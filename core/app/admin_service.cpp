#include "core/app/admin_service.h"

#include "core/domain/validator.h"
#include "core/storage/json_io.h"
#include "core/util/korean.h"

#include <algorithm>

namespace app {
namespace {

util::Error invalidId(const std::string& id) {
    return util::makeError(util::ErrorCode::InvalidUsage,
                           "ID \"" + id + "\"" + std::string{util::josaEunNeun(id)} +
                               " 쓸 수 없습니다.",
                           "소문자 영문, 숫자, 밑줄(_)만 쓸 수 있습니다. 예: w_kim, cleaning_am");
}

util::Error duplicateId(const std::string& what, const std::string& id) {
    return util::makeError(util::ErrorCode::Conflict,
                           what + " ID \"" + id + "\"" + std::string{util::josaEunNeun(id)} +
                               " 이미 있습니다.",
                           "다른 ID 를 쓰거나 기존 것을 먼저 지워 주세요.");
}

util::Error notFound(const std::string& what, const std::string& id) {
    return util::makeError(util::ErrorCode::NotFound,
                           what + " \"" + id + "\"" + std::string{util::josaEulReul(id)} +
                               " 찾을 수 없습니다.");
}

// 무엇이 이 대상을 가리키고 있는지 모아 알려준다. 지우면 끊어질 참조들이다.
util::Error stillReferenced(const std::string& what, const std::string& id,
                            const std::vector<std::string>& referrers) {
    std::string list;
    for (const std::string& referrer : referrers) {
        list += "  " + referrer + "\n";
    }
    return util::makeError(util::ErrorCode::Conflict,
                           what + " \"" + id + "\"" + std::string{util::josaEulReul(id)} +
                               " 가리키는 곳이 남아 있어 지울 수 없습니다.",
                           "먼저 아래를 정리해 주세요.\n" + list);
}

}  // namespace

bool isValidId(const std::string& id) {
    if (id.empty()) {
        return false;
    }
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

// --- 작업자 ---

util::Result<domain::Worker> addWorker(const std::filesystem::path& dataDir, domain::Model& model,
                                       const std::string& name, const std::string& idHint,
                                       const std::vector<domain::Weekday>& weeklyOff) {
    if (name.empty()) {
        return util::makeError(util::ErrorCode::InvalidUsage, "이름을 적어 주세요.",
                               "예: sched worker add --name \"김철수\"");
    }

    std::string id = idHint;
    if (id.empty()) {
        // 이름이 한글이라 ID 로 쓸 수 없다. 비어 있는 번호를 찾아 붙인다.
        for (int n = 1;; ++n) {
            const std::string candidate = "w_" + std::to_string(n);
            const bool taken =
                std::any_of(model.workers.workers.begin(), model.workers.workers.end(),
                            [&candidate](const domain::Worker& worker) {
                                return worker.id.str() == candidate;
                            });
            if (!taken) {
                id = candidate;
                break;
            }
        }
    } else if (!isValidId(id)) {
        return invalidId(id);
    }

    for (const domain::Worker& worker : model.workers.workers) {
        if (worker.id.str() == id) {
            return duplicateId("작업자", id);
        }
    }

    domain::Worker worker;
    worker.id = domain::WorkerId{id};
    worker.name = name;
    worker.active = true;
    worker.weeklyOff = weeklyOff;

    // 배열 끝에 붙인다. 순서가 라운드 로빈 기준이라 중간에 끼워 넣으면 기존 배정 흐름이 바뀐다.
    model.workers.workers.push_back(worker);
    if (const util::Result<void> written =
            storage::saveWorkers(dataDir / storage::filenames::kWorkers, model.workers);
        !written) {
        model.workers.workers.pop_back();
        return written.error();
    }
    return worker;
}

util::Result<void> editWorker(const std::filesystem::path& dataDir, domain::Model& model,
                              const domain::WorkerId& id, const std::optional<std::string>& name,
                              const std::optional<bool>& active) {
    domain::Worker* target = nullptr;
    for (domain::Worker& worker : model.workers.workers) {
        if (worker.id == id) {
            target = &worker;
            break;
        }
    }
    if (target == nullptr) {
        return notFound("작업자", id.str());
    }

    const domain::Worker before = *target;
    if (name.has_value()) {
        target->name = *name;
    }
    if (active.has_value()) {
        target->active = *active;
    }

    if (const util::Result<void> written =
            storage::saveWorkers(dataDir / storage::filenames::kWorkers, model.workers);
        !written) {
        *target = before;
        return written.error();
    }
    return {};
}

util::Result<void> removeWorker(const std::filesystem::path& dataDir, domain::Model& model,
                                const domain::WorkerId& id, bool hard) {
    const auto it = std::find_if(model.workers.workers.begin(), model.workers.workers.end(),
                                 [&id](const domain::Worker& worker) { return worker.id == id; });
    if (it == model.workers.workers.end()) {
        return notFound("작업자", id.str());
    }

    if (!hard) {
        return editWorker(dataDir, model, id, std::nullopt, false);
    }

    const domain::WorkerList before = model.workers;
    model.workers.workers.erase(it);
    if (const util::Result<void> written =
            storage::saveWorkers(dataDir / storage::filenames::kWorkers, model.workers);
        !written) {
        model.workers = before;
        return written.error();
    }
    return {};
}

// --- 작업 ---

util::Result<void> addTask(const std::filesystem::path& dataDir, domain::Model& model,
                           const TaskSpec& spec) {
    if (!isValidId(spec.id)) {
        return invalidId(spec.id);
    }
    for (const domain::Task& task : model.tasks.tasks) {
        if (task.id.str() == spec.id) {
            return duplicateId("작업", spec.id);
        }
    }
    if (spec.taskSetIds.empty()) {
        return util::makeError(util::ErrorCode::InvalidUsage,
                               "작업이 속할 작업집합을 하나 이상 지정해 주세요.",
                               "예: --set cleaning_am,cleaning_pm\n"
                               "작업집합 목록을 보려면: sched set list");
    }
    for (const domain::TaskSetId& setId : spec.taskSetIds) {
        const bool exists =
            std::any_of(model.tasks.taskSets.begin(), model.tasks.taskSets.end(),
                        [&setId](const domain::TaskSet& set) { return set.id == setId; });
        if (!exists) {
            return notFound("작업집합", setId.str());
        }
    }
    if (spec.requiredCount <= 0) {
        return util::makeError(util::ErrorCode::InvalidUsage, "필요 인원은 1 이상이어야 합니다.");
    }
    for (const domain::TaskId& other : spec.conflictsWith) {
        const bool exists =
            std::any_of(model.tasks.tasks.begin(), model.tasks.tasks.end(),
                        [&other](const domain::Task& task) { return task.id == other; });
        if (!exists) {
            return notFound("배타로 지정한 작업", other.str());
        }
    }

    domain::Task task;
    task.id = domain::TaskId{spec.id};
    task.name = spec.name.empty() ? spec.id : spec.name;
    task.taskSetIds = spec.taskSetIds;
    task.requiredCount = spec.requiredCount;
    task.conflictsWith = spec.conflictsWith;
    task.weekdays = spec.weekdays;

    const domain::TaskList before = model.tasks;
    model.tasks.tasks.push_back(task);
    // 배타는 대칭이어야 한다. 저장 전에 양방향으로 맞춘다.
    storage::normalizeConflicts(model.tasks);

    if (const util::Result<void> written =
            storage::saveTasks(dataDir / storage::filenames::kTasks, model.tasks);
        !written) {
        model.tasks = before;
        return written.error();
    }
    return {};
}

util::Result<void> editTask(const std::filesystem::path& dataDir, domain::Model& model,
                            const domain::TaskId& id, const std::optional<std::string>& name,
                            const std::optional<int>& requiredCount,
                            const std::optional<std::vector<domain::TaskId>>& conflictsWith,
                            const std::optional<std::vector<domain::Weekday>>& weekdays,
                            const std::optional<std::vector<domain::TaskSetId>>& taskSetIds) {
    domain::Task* target = nullptr;
    for (domain::Task& task : model.tasks.tasks) {
        if (task.id == id) {
            target = &task;
            break;
        }
    }
    if (target == nullptr) {
        return notFound("작업", id.str());
    }
    if (requiredCount.has_value() && *requiredCount <= 0) {
        return util::makeError(util::ErrorCode::InvalidUsage, "필요 인원은 1 이상이어야 합니다.");
    }

    const domain::TaskList before = model.tasks;
    if (name.has_value()) {
        target->name = *name;
    }
    if (requiredCount.has_value()) {
        target->requiredCount = *requiredCount;
    }
    if (weekdays.has_value()) {
        target->weekdays = *weekdays;
    }
    if (taskSetIds.has_value()) {
        target->taskSetIds = *taskSetIds;
    }
    if (conflictsWith.has_value()) {
        // 이 작업을 가리키던 반대 방향 간선을 먼저 끊는다. 안 그러면 정규화가 되살린다.
        for (domain::Task& task : model.tasks.tasks) {
            if (task.id == id) {
                continue;
            }
            task.conflictsWith.erase(
                std::remove(task.conflictsWith.begin(), task.conflictsWith.end(), id),
                task.conflictsWith.end());
        }
        target->conflictsWith = *conflictsWith;
    }
    storage::normalizeConflicts(model.tasks);

    if (const util::Result<void> written =
            storage::saveTasks(dataDir / storage::filenames::kTasks, model.tasks);
        !written) {
        model.tasks = before;
        return written.error();
    }
    return {};
}

util::Result<void> removeTask(const std::filesystem::path& dataDir, domain::Model& model,
                              const domain::TaskId& id) {
    const auto it = std::find_if(model.tasks.tasks.begin(), model.tasks.tasks.end(),
                                 [&id](const domain::Task& task) { return task.id == id; });
    if (it == model.tasks.tasks.end()) {
        return notFound("작업", id.str());
    }

    const domain::TaskList before = model.tasks;
    model.tasks.tasks.erase(it);
    // 다른 작업의 배타 목록에서도 지운다. 남겨두면 끊어진 참조가 된다.
    for (domain::Task& task : model.tasks.tasks) {
        task.conflictsWith.erase(
            std::remove(task.conflictsWith.begin(), task.conflictsWith.end(), id),
            task.conflictsWith.end());
    }

    if (const util::Result<void> written =
            storage::saveTasks(dataDir / storage::filenames::kTasks, model.tasks);
        !written) {
        model.tasks = before;
        return written.error();
    }
    return {};
}

// --- 작업집합 ---

util::Result<void> addTaskSet(const std::filesystem::path& dataDir, domain::Model& model,
                              const std::string& id, const std::string& displayName) {
    if (!isValidId(id)) {
        return invalidId(id);
    }
    for (const domain::TaskSet& set : model.tasks.taskSets) {
        if (set.id.str() == id) {
            return duplicateId("작업집합", id);
        }
    }

    const domain::TaskList before = model.tasks;
    model.tasks.taskSets.push_back(
        domain::TaskSet{domain::TaskSetId{id}, displayName.empty() ? id : displayName});
    if (const util::Result<void> written =
            storage::saveTasks(dataDir / storage::filenames::kTasks, model.tasks);
        !written) {
        model.tasks = before;
        return written.error();
    }
    return {};
}

util::Result<void> removeTaskSet(const std::filesystem::path& dataDir, domain::Model& model,
                                 const domain::TaskSetId& id) {
    const auto it = std::find_if(model.tasks.taskSets.begin(), model.tasks.taskSets.end(),
                                 [&id](const domain::TaskSet& set) { return set.id == id; });
    if (it == model.tasks.taskSets.end()) {
        return notFound("작업집합", id.str());
    }

    std::vector<std::string> referrers;
    for (const domain::Task& task : model.tasks.tasks) {
        if (std::find(task.taskSetIds.begin(), task.taskSetIds.end(), id) !=
            task.taskSetIds.end()) {
            referrers.push_back("작업 \"" + task.name + "\" (sched task rm --id " + task.id.str() +
                                ")");
        }
    }
    for (const domain::Category& category : model.config.categories) {
        if (std::find(category.taskSetIds.begin(), category.taskSetIds.end(), id) !=
            category.taskSetIds.end()) {
            referrers.push_back("분류 \"" + category.displayName + "\" (sched cat rm --id " +
                                category.id.str() + ")");
        }
    }
    if (!referrers.empty()) {
        return stillReferenced("작업집합", id.str(), referrers);
    }

    const domain::TaskList before = model.tasks;
    model.tasks.taskSets.erase(it);
    if (const util::Result<void> written =
            storage::saveTasks(dataDir / storage::filenames::kTasks, model.tasks);
        !written) {
        model.tasks = before;
        return written.error();
    }
    return {};
}

// --- 작업분류 ---

util::Result<void> addCategory(const std::filesystem::path& dataDir, domain::Model& model,
                               const std::string& id, const std::string& displayName,
                               const std::vector<domain::TaskSetId>& taskSetIds) {
    if (!isValidId(id)) {
        return invalidId(id);
    }
    for (const domain::Category& category : model.config.categories) {
        if (category.id.str() == id) {
            return duplicateId("분류", id);
        }
    }
    if (taskSetIds.empty()) {
        return util::makeError(util::ErrorCode::InvalidUsage,
                               "분류에 넣을 작업집합을 하나 이상 지정해 주세요.",
                               "예: --set cleaning_am,zone");
    }
    for (const domain::TaskSetId& setId : taskSetIds) {
        const bool exists =
            std::any_of(model.tasks.taskSets.begin(), model.tasks.taskSets.end(),
                        [&setId](const domain::TaskSet& set) { return set.id == setId; });
        if (!exists) {
            return notFound("작업집합", setId.str());
        }
    }

    const domain::Config before = model.config;
    model.config.categories.push_back(
        domain::Category{domain::CategoryId{id}, displayName.empty() ? id : displayName,
                         taskSetIds});
    if (const util::Result<void> written =
            storage::saveConfig(dataDir / storage::filenames::kConfig, model.config);
        !written) {
        model.config = before;
        return written.error();
    }
    return {};
}

util::Result<void> removeCategory(const std::filesystem::path& dataDir, domain::Model& model,
                                  const domain::CategoryId& id) {
    const auto it =
        std::find_if(model.config.categories.begin(), model.config.categories.end(),
                     [&id](const domain::Category& category) { return category.id == id; });
    if (it == model.config.categories.end()) {
        return notFound("분류", id.str());
    }

    std::vector<std::string> referrers;
    for (const domain::TimeSlot& slot : model.config.timeSlots) {
        if (std::find(slot.categoryIds.begin(), slot.categoryIds.end(), id) !=
            slot.categoryIds.end()) {
            referrers.push_back("시간대 \"" + slot.displayName + "\" (sched slot rm --id " +
                                slot.id.str() + ")");
        }
    }
    if (!referrers.empty()) {
        return stillReferenced("분류", id.str(), referrers);
    }

    const domain::Config before = model.config;
    model.config.categories.erase(it);
    if (const util::Result<void> written =
            storage::saveConfig(dataDir / storage::filenames::kConfig, model.config);
        !written) {
        model.config = before;
        return written.error();
    }
    return {};
}

// --- 시간대 ---

util::Result<void> addTimeSlot(const std::filesystem::path& dataDir, domain::Model& model,
                               const TimeSlotSpec& spec) {
    if (!isValidId(spec.id)) {
        return invalidId(spec.id);
    }
    for (const domain::TimeSlot& slot : model.config.timeSlots) {
        if (slot.id.str() == spec.id) {
            return duplicateId("시간대", spec.id);
        }
    }
    if (!domain::isTimeString(spec.start) || !domain::isTimeString(spec.end)) {
        return util::makeError(util::ErrorCode::InvalidUsage,
                               "시각 형식이 잘못됐습니다 (" + spec.start + " ~ " + spec.end + ").",
                               "24시간제 \"HH:MM\" 으로 적어 주세요. 예: --start 09:00 --end 12:00");
    }
    if (spec.start >= spec.end) {
        return util::makeError(util::ErrorCode::InvalidUsage,
                               "시작이 끝보다 늦거나 같습니다 (" + spec.start + " ~ " + spec.end +
                                   ").");
    }
    if (spec.categoryIds.empty()) {
        return util::makeError(util::ErrorCode::InvalidUsage,
                               "시간대에 붙일 분류를 하나 이상 지정해 주세요.",
                               "예: --cat weekday_am");
    }
    for (const domain::CategoryId& categoryId : spec.categoryIds) {
        const bool exists = std::any_of(
            model.config.categories.begin(), model.config.categories.end(),
            [&categoryId](const domain::Category& category) { return category.id == categoryId; });
        if (!exists) {
            return notFound("분류", categoryId.str());
        }
    }

    domain::TimeSlot slot;
    slot.id = domain::TimeSlotId{spec.id};
    slot.displayName = spec.displayName.empty() ? spec.id : spec.displayName;
    slot.start = spec.start;
    slot.end = spec.end;
    slot.weekdays = spec.weekdays;
    slot.categoryIds = spec.categoryIds;

    const domain::Config before = model.config;
    model.config.timeSlots.push_back(slot);

    // 같은 요일에 구간이 겹치면 "지금 어느 시간대인가" 에 답이 두 개가 된다. 저장 전에 막는다.
    // validator 와 같은 판정 함수를 쓴다 — 메시지 문구로 판정하면 문구가 바뀔 때 조용히 통과한다.
    const std::vector<domain::TimeSlotOverlap> overlaps =
        domain::findTimeSlotOverlaps(model.config);
    if (!overlaps.empty()) {
        const std::string message = domain::describeOverlap(overlaps.front());
        model.config = before;
        return util::makeError(util::ErrorCode::Conflict, message,
                               "겹치지 않게 시각을 조정하거나 요일을 나눠 주세요.");
    }

    if (const util::Result<void> written =
            storage::saveConfig(dataDir / storage::filenames::kConfig, model.config);
        !written) {
        model.config = before;
        return written.error();
    }
    return {};
}

util::Result<void> removeTimeSlot(const std::filesystem::path& dataDir, domain::Model& model,
                                  const domain::TimeSlotId& id) {
    const auto it = std::find_if(model.config.timeSlots.begin(), model.config.timeSlots.end(),
                                 [&id](const domain::TimeSlot& slot) { return slot.id == id; });
    if (it == model.config.timeSlots.end()) {
        return notFound("시간대", id.str());
    }

    const domain::Config before = model.config;
    model.config.timeSlots.erase(it);
    if (const util::Result<void> written =
            storage::saveConfig(dataDir / storage::filenames::kConfig, model.config);
        !written) {
        model.config = before;
        return written.error();
    }
    return {};
}

}  // namespace app
