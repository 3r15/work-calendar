#include "core/app/assign_service.h"

#include "core/domain/availability.h"
#include "core/domain/day_plan.h"
#include "core/scheduling/assignment_engine.h"
#include "core/storage/json_io.h"

#include <algorithm>
#include <map>

namespace app {
namespace {

const domain::Category* findCategory(const domain::Config& config, const domain::CategoryId& id) {
    for (const domain::Category& category : config.categories) {
        if (category.id == id) {
            return &category;
        }
    }
    return nullptr;
}

// 미배정 항목을 작업별로 묶는다. 스냅샷은 개수로 적는다 (DATA-SCHEMA.md).
std::vector<domain::SnapshotUnassigned> foldUnassigned(
    const std::vector<scheduling::Unassigned>& raw) {
    std::vector<domain::SnapshotUnassigned> out;
    for (const scheduling::Unassigned& item : raw) {
        const auto it = std::find_if(out.begin(), out.end(),
                                     [&item](const domain::SnapshotUnassigned& existing) {
                                         return existing.taskId == item.taskId;
                                     });
        if (it != out.end()) {
            it->count += 1;
        } else {
            out.push_back(domain::SnapshotUnassigned{item.taskId, 1});
        }
    }
    return out;
}

}  // namespace

domain::DaySnapshot computeDay(const domain::Model& model, const util::Date& date,
                               const util::DateTime& generatedAt, domain::CursorState& cursors) {
    domain::DaySnapshot snapshot;
    snapshot.date = date.toString();
    snapshot.weekday = domain::fromChrono(date.weekday());
    snapshot.generatedAt = util::formatIso8601(generatedAt);

    const std::vector<domain::Worker> available = domain::availableWorkers(model, date);
    const scheduling::AssignmentEngine engine;

    for (const domain::TimeSlot* timeSlot : domain::slotsForDate(model.config, date)) {
        // 같은 시간대에서 먼저 채운 결과. 분류가 여럿이어도 시간대 하나로 이어진다.
        std::vector<scheduling::Assignment> priorInSlot;

        for (const domain::CategoryId& categoryId : timeSlot->categoryIds) {
            const domain::Category* category = findCategory(model.config, categoryId);
            if (category == nullptr) {
                continue;  // 없는 참조는 validator 가 오류로 보고한다
            }

            domain::SnapshotSlot snapshotSlot;
            snapshotSlot.timeSlotId = timeSlot->id;
            snapshotSlot.categoryId = category->id;

            for (const domain::TaskSetId& taskSetId : category->taskSetIds) {
                scheduling::AssignInput input;
                input.date = date;
                input.taskSetId = taskSetId;
                input.slotTasks = model.tasks.tasks;
                input.availableWorkers = available;
                input.options = model.config.assignment;
                input.priorAssignments = priorInSlot;

                // 커서는 작업집합마다 하나다. 작업자를 추가·삭제하면 인덱스가 어긋나므로
                // 엔진이 작업자 수로 나눠 정규화한다 (DATA-SCHEMA.md rr-cursor.json).
                const auto found = cursors.cursors.find(taskSetId);
                input.cursor = (found != cursors.cursors.end()) ? found->second : 0;

                const scheduling::AssignResult result = engine.assign(input);
                cursors.cursors[taskSetId] = result.nextCursor;

                domain::SnapshotTaskSet snapshotSet;
                snapshotSet.taskSetId = taskSetId;
                for (const scheduling::Assignment& assignment : result.assignments) {
                    domain::SnapshotAssignment item;
                    item.taskId = assignment.taskId;
                    item.workerId = assignment.workerId;
                    // absentAssignee 는 저장 시점이 아니라 조회 시점에 계산한다 (D-009).
                    snapshotSet.assignments.push_back(std::move(item));
                }
                snapshotSet.unassigned = foldUnassigned(result.unassigned);
                snapshotSlot.taskSets.push_back(std::move(snapshotSet));

                priorInSlot.insert(priorInSlot.end(), result.assignments.begin(),
                                   result.assignments.end());
            }
            snapshot.slots.push_back(std::move(snapshotSlot));
        }
    }
    return snapshot;
}

util::Result<AssignOutcome> ensureSnapshot(const std::filesystem::path& dataDir,
                                           const domain::Model& model, const util::Date& date,
                                           const util::DateTime& now) {
    const std::filesystem::path path = storage::snapshotPath(dataDir, date.toString());

    if (std::filesystem::exists(path)) {
        util::Result<storage::Loaded<domain::DaySnapshot>> loaded = storage::loadSnapshot(path);
        if (!loaded) {
            return loaded.error();
        }
        AssignOutcome outcome;
        outcome.snapshot = std::move(loaded).value().value;
        outcome.computed = false;
        return outcome;
    }

    // 커서 파일은 첫 실행에 없다. 없는 것은 오류가 아니라 "아직 아무것도 안 돌았다" 는 뜻이다.
    domain::CursorState cursors;
    const std::filesystem::path cursorPath = dataDir / storage::filenames::kCursors;
    if (std::filesystem::exists(cursorPath)) {
        util::Result<storage::Loaded<domain::CursorState>> loaded = storage::loadCursors(cursorPath);
        if (!loaded) {
            return loaded.error();
        }
        cursors = std::move(loaded).value().value;
    }

    AssignOutcome outcome;
    outcome.snapshot = computeDay(model, date, now, cursors);
    outcome.computed = true;

    if (const util::Result<void> written = storage::saveSnapshot(path, outcome.snapshot);
        !written) {
        return written.error();
    }
    // 스냅샷을 먼저 쓴다. 커서 저장이 실패해도 그날 배정은 남는다 — 반대면 커서만 전진하고
    // 배정은 사라져 다음 날 배정이 어긋난다.
    if (const util::Result<void> written = storage::saveCursors(cursorPath, cursors); !written) {
        return written.error();
    }
    return outcome;
}

}  // namespace app
