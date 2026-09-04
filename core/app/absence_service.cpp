#include "core/app/absence_service.h"

#include "core/domain/snapshot.h"
#include "core/storage/json_io.h"

#include <algorithm>

namespace app {
namespace {

// 그날 이 사람에게 배정된 건수. 스냅샷이 없으면 0.
int countAssignmentsFor(const std::filesystem::path& dataDir, const domain::WorkerId& workerId,
                        const util::Date& date) {
    const std::filesystem::path path = storage::snapshotPath(dataDir, date.toString());
    if (!std::filesystem::exists(path)) {
        return 0;
    }
    const util::Result<storage::Loaded<domain::DaySnapshot>> loaded = storage::loadSnapshot(path);
    if (!loaded) {
        return 0;  // 읽을 수 없으면 셀 수 없다. 등록 자체를 막지는 않는다.
    }

    int count = 0;
    for (const domain::SnapshotSlot& slot : loaded.value().value.slots) {
        for (const domain::SnapshotTaskSet& set : slot.taskSets) {
            for (const domain::SnapshotAssignment& assignment : set.assignments) {
                if (assignment.workerId == workerId) {
                    count += 1;
                }
            }
        }
    }
    return count;
}

}  // namespace

util::Result<AbsenceChange> addAbsence(const std::filesystem::path& dataDir,
                                       domain::Model& model, const domain::WorkerId& workerId,
                                       const util::Date& from, const util::Date& to,
                                       const std::string& reason, const util::DateTime& now) {
    if (to < from) {
        return util::makeError(util::ErrorCode::InvalidUsage,
                               "휴무 시작일이 종료일보다 늦습니다 (" + from.toString() + " ~ " +
                                   to.toString() + ").",
                               "--from 과 --to 를 바꿔 적으셨는지 확인해 주세요.");
    }

    domain::Absence absence;
    absence.workerId = workerId;
    absence.from = from.toString();
    absence.to = to.toString();
    absence.reason = reason;
    absence.createdAt = util::formatIso8601(now);

    // 기간이 겹쳐도 오류가 아니다. 합집합으로 처리한다 (DATA-SCHEMA.md).
    model.absences.absences.push_back(absence);

    if (const util::Result<void> written =
            storage::saveAbsences(dataDir / storage::filenames::kAbsences, model.absences);
        !written) {
        model.absences.absences.pop_back();  // 저장 못 했으면 메모리도 되돌린다
        return written.error();
    }

    AbsenceChange change;
    // 당일 급휴 판정: 등록한 날이 휴무 시작일과 같고, 그날 배정이 이미 나가 있는 경우 (D-009).
    if (now.date == from) {
        change.affectedAssignments = countAssignmentsFor(dataDir, workerId, from);
        change.urgent = change.affectedAssignments > 0;
    }
    return change;
}

util::Result<int> removeAbsence(const std::filesystem::path& dataDir, domain::Model& model,
                                const domain::WorkerId& workerId, const util::Date& date) {
    const std::string text = date.toString();
    const domain::AbsenceList before = model.absences;

    std::vector<domain::Absence> kept;
    int removed = 0;
    for (const domain::Absence& absence : model.absences.absences) {
        const bool matches =
            absence.workerId == workerId && absence.from <= text && text <= absence.to;
        if (matches) {
            removed += 1;
        } else {
            kept.push_back(absence);
        }
    }

    if (removed == 0) {
        return 0;  // 지울 것이 없으면 파일도 건드리지 않는다
    }

    model.absences.absences = std::move(kept);
    if (const util::Result<void> written =
            storage::saveAbsences(dataDir / storage::filenames::kAbsences, model.absences);
        !written) {
        model.absences = before;
        return written.error();
    }
    return removed;
}

util::Result<void> setWeeklyOff(const std::filesystem::path& dataDir, domain::Model& model,
                                const domain::WorkerId& workerId,
                                const std::vector<domain::Weekday>& weekdays) {
    domain::Worker* target = nullptr;
    for (domain::Worker& worker : model.workers.workers) {
        if (worker.id == workerId) {
            target = &worker;
            break;
        }
    }
    if (target == nullptr) {
        return util::makeError(util::ErrorCode::NotFound,
                               "작업자 \"" + workerId.str() + "\" 를 찾을 수 없습니다.");
    }

    const std::vector<domain::Weekday> before = target->weeklyOff;
    target->weeklyOff = weekdays;

    if (const util::Result<void> written =
            storage::saveWorkers(dataDir / storage::filenames::kWorkers, model.workers);
        !written) {
        target->weeklyOff = before;
        return written.error();
    }
    return {};
}

std::vector<domain::Absence> listAbsences(const domain::Model& model, const util::Date* date,
                                          const domain::WorkerId* workerId) {
    std::vector<domain::Absence> out;
    for (const domain::Absence& absence : model.absences.absences) {
        if (workerId != nullptr && !(absence.workerId == *workerId)) {
            continue;
        }
        if (date != nullptr) {
            const std::string text = date->toString();
            if (!(absence.from <= text && text <= absence.to)) {
                continue;
            }
        }
        out.push_back(absence);
    }
    return out;
}

}  // namespace app
