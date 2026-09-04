#pragma once

#include "core/app/absence_service.h"
#include "core/app/assign_service.h"
#include "core/app/day_view.h"
#include "core/domain/finding.h"
#include "core/util/date.h"
#include "core/util/result.h"

#include <ostream>

namespace cli {

// 렌더링만 한다. 계산은 core/app 이 끝낸 상태로 들어온다 (CLAUDE.md 2장).

void renderDayView(std::ostream& out, const app::DayView& view, const util::DateTime& now);
void renderAssignOutcome(std::ostream& out, const app::AssignOutcome& outcome);
void renderAbsences(std::ostream& out, const domain::Model& model,
                    const std::vector<domain::Absence>& absences);
void renderReport(std::ostream& out, const domain::Report& report);
void renderError(std::ostream& out, const util::Error& error);

// 표 정렬용. 표시 폭 기준으로 오른쪽을 공백으로 채운다.
// std::setw 는 문자 수로 세기 때문에 한글이 섞이면 어긋난다 (CLAUDE.md 1장).
std::string padTo(const std::string& text, std::size_t width);

}  // namespace cli
