#pragma once

#include "cli/color.h"
#include "core/app/absence_service.h"
#include "core/app/assign_service.h"
#include "core/app/day_view.h"
#include "core/domain/finding.h"
#include "core/domain/snapshot.h"
#include "core/util/date.h"
#include "core/util/result.h"

#include <ostream>

namespace cli {

// 렌더링만 한다. 계산은 core/app 이 끝낸 상태로 들어온다 (CLAUDE.md 2장).

void renderDayView(std::ostream& out, const app::DayView& view, const util::DateTime& now,
                   const Palette& palette);
void renderAssignOutcome(std::ostream& out, const app::AssignOutcome& outcome,
                         const Palette& palette, bool quiet);
void renderAbsences(std::ostream& out, const domain::Model& model,
                    const std::vector<domain::Absence>& absences);
void renderReport(std::ostream& out, const domain::Report& report, const Palette& palette);

// 목록 출력. 열 정렬은 전부 displayWidth() 기준이다 — 한글이 섞여도 어긋나지 않는다.
void renderWorkers(std::ostream& out, const domain::Model& model, bool includeInactive,
                   const Palette& palette);
void renderTasks(std::ostream& out, const domain::Model& model, const std::string& setFilter,
                 const Palette& palette);
void renderTaskSets(std::ostream& out, const domain::Model& model, const Palette& palette);
void renderCategories(std::ostream& out, const domain::Model& model, const Palette& palette);
void renderTimeSlots(std::ostream& out, const domain::Model& model, const Palette& palette);
void renderError(std::ostream& out, const util::Error& error, const Palette& palette);

// --json 모드. 스냅샷 스키마에 workerName 과 taskName 을 덧붙여 소비 측이 이름을 다시
// 조회하지 않아도 되게 한다 (CLI-SPEC.md).
std::string toJsonWithNames(const domain::Model& model, const domain::DaySnapshot& snapshot);

// 표 정렬용. 표시 폭 기준으로 오른쪽을 공백으로 채운다.
// std::setw 는 문자 수로 세기 때문에 한글이 섞이면 어긋난다 (CLAUDE.md 1장).
std::string padTo(const std::string& text, std::size_t width);

}  // namespace cli
