#pragma once

#include "core/domain/entities.h"
#include "core/domain/snapshot.h"
#include "core/util/date.h"
#include "core/util/result.h"

#include <filesystem>

namespace app {

// 하루치 배정을 계산한다. 파일을 건드리지 않는다 — 커서는 인자로 받고 갱신해서 돌려준다.
//
// 시간대 → 분류 → 작업집합 순으로 돌며 작업집합마다 엔진을 한 번 부른다.
// 같은 시간대에서 먼저 채운 결과는 다음 작업집합에 넘겨, crossSetConflicts 가 켜졌을 때
// 배타 검사가 시간대 전체로 넓어지게 한다 (D-002).
domain::DaySnapshot computeDay(const domain::Model& model, const util::Date& date,
                               const util::DateTime& generatedAt, domain::CursorState& cursors);

struct AssignOutcome {
    domain::DaySnapshot snapshot;
    // 이번 호출이 계산해서 저장했는가. false 면 기존 스냅샷을 읽기만 했다.
    bool computed{false};
};

// 그날 스냅샷이 없으면 계산해서 저장하고, 있으면 읽기만 한다 (DESIGN 3.1).
// 조회할 때마다 다시 계산하면 같은 날 결과가 달라진다.
//
// force 를 주면 기존 스냅샷을 버리고 다시 계산한다. 당일 급휴 뒤 재배정이 이 경로다 (D-009).
// 자동으로는 절대 하지 않는다 — 사람이 이미 움직이고 있는 상태에서 화면만 바뀌면 위험하다.
util::Result<AssignOutcome> ensureSnapshot(const std::filesystem::path& dataDir,
                                           const domain::Model& model, const util::Date& date,
                                           const util::DateTime& now, bool force = false);

}  // namespace app
