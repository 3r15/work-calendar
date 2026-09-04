#pragma once

#include "core/domain/entities.h"
#include "core/util/date.h"
#include "core/util/result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace app {

// 휴무 등록·해제. absences.json 을 원자적으로 다시 쓴다.
//
// 정기 휴무(매주 반복)는 여기가 아니라 workers.json 의 weeklyOff 다. 별개의 개념이라
// 별개의 함수로 다룬다 (CLI-SPEC.md).

struct AbsenceChange {
    // 이 등록이 당일 급휴인가. 그날 배정 스냅샷이 이미 있는 상태에서 등록된 휴무다 (D-009).
    bool urgent{false};
    // urgent 일 때, 이미 이 사람에게 배정되어 있는 건수.
    int affectedAssignments{0};
};

// from <= to 여야 한다. 하루면 두 값을 같게 준다.
util::Result<AbsenceChange> addAbsence(const std::filesystem::path& dataDir,
                                       domain::Model& model, const domain::WorkerId& workerId,
                                       const util::Date& from, const util::Date& to,
                                       const std::string& reason, const util::DateTime& now);

// 그날을 포함하는 휴무를 지운다. 기간 휴무면 통째로 지운다 — 가운데를 뚫는 것은 지금 요구가 없다.
// 돌려주는 값은 지운 건수다. 0 이면 그날 등록된 휴무가 없었다는 뜻이다.
util::Result<int> removeAbsence(const std::filesystem::path& dataDir, domain::Model& model,
                                const domain::WorkerId& workerId, const util::Date& date);

// 정기 휴무 요일을 통째로 바꾼다. workers.json 을 다시 쓴다.
util::Result<void> setWeeklyOff(const std::filesystem::path& dataDir, domain::Model& model,
                                const domain::WorkerId& workerId,
                                const std::vector<domain::Weekday>& weekdays);

// 그날 적용되는 휴무 목록. 날짜를 주지 않으면 전부.
std::vector<domain::Absence> listAbsences(const domain::Model& model, const util::Date* date,
                                          const domain::WorkerId* workerId);

}  // namespace app
