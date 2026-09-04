#include "cli/render.h"

#include "core/util/display_width.h"
#include "core/util/korean.h"

#include <algorithm>

namespace cli {
namespace {

const char* weekdayLabelOf(const util::Date& date) {
    static const char* kLabels[] = {"일", "월", "화", "수", "목", "금", "토"};
    return kLabels[date.weekday().c_encoding()];
}

}  // namespace

std::string padTo(const std::string& text, std::size_t width) {
    const std::size_t used = util::displayWidth(text);
    if (used >= width) {
        return text;
    }
    return text + std::string(width - used, ' ');
}

void renderDayView(std::ostream& out, const app::DayView& view, const util::DateTime& now) {
    out << view.date.toString() << " (" << weekdayLabelOf(view.date) << ")";
    if (!view.workday) {
        out << "  근무일이 아닙니다.";
    }
    out << "\n";

    for (const app::SlotBlock& slot : view.slots) {
        out << "\n" << slot.start << "–" << slot.end << "  " << slot.displayName;
        if (slot.isCurrent) {
            out << "  ◀ 지금";
        } else if (slot.isPast) {
            out << "  (지남)";
        }
        out << "\n";

        // 분류가 하나뿐이면 이름을 한 번 더 보여줄 이유가 없다 (CLI-SPEC 출력 형식).
        const bool showCategoryNames = slot.categories.size() > 1;
        for (const app::CategoryBlock& category : slot.categories) {
            if (showCategoryNames) {
                out << "  " << category.displayName << "\n";
            }
            const std::string indent = showCategoryNames ? "    " : "  ";
            for (const app::TaskSetBlock& set : category.taskSets) {
                out << indent << "[" << set.displayName << "]\n";
                if (set.tasks.empty()) {
                    out << indent << "  (작업 없음)\n";
                    continue;
                }
                // 이름 열의 너비를 표시 폭 기준으로 맞춘다.
                std::size_t nameWidth = 0;
                for (const app::TaskLine& task : set.tasks) {
                    nameWidth = std::max(nameWidth, util::displayWidth(task.name));
                }
                for (const app::TaskLine& task : set.tasks) {
                    out << indent << "  " << padTo(task.name, nameWidth + 2);
                    if (task.workers.empty() && task.unassignedCount == 0) {
                        // 아직 배정하지 않은 날. 필요 인원만 보여준다.
                        out << task.requiredCount << "명";
                    } else {
                        // 미배정을 먼저 적는다. 눈에 먼저 들어와야 하는 정보다.
                        for (int i = 0; i < task.unassignedCount; ++i) {
                            out << "─ 미배정 ─  ";
                        }
                        for (std::size_t i = 0; i < task.workers.size(); ++i) {
                            if (i > 0) {
                                out << "  ";
                            }
                            out << task.workers[i].name;
                            if (task.workers[i].absent) {
                                out << "(휴무)";
                            }
                        }
                    }
                    out << "\n";
                }
            }
        }
    }

    if (view.slots.empty()) {
        out << "\n이 날에는 배정된 시간대가 없습니다.\n";
    }

    // 지금이 시간대 밖이면 다음에 오는 것을 안내한다 (D-003).
    if (view.upcoming.has_value()) {
        const app::UpcomingSlot& next = *view.upcoming;
        out << "\n지금은 " << now.time.toString() << ", 배정된 시간대가 아닙니다.\n";
        out << "다음: " << next.displayName;
        if (!next.isToday) {
            out << " (" << next.date.toString() << " " << next.start << " 시작)";
        } else {
            out << " (" << next.start << " 시작)";
        }
        out << "\n";
    }
}

void renderAssignOutcome(std::ostream& out, const app::AssignOutcome& outcome) {
    const domain::DaySnapshot& snapshot = outcome.snapshot;
    if (outcome.computed) {
        out << snapshot.date << " 배정을 계산해 저장했습니다.\n";
    } else {
        // 같은 날 결과가 달라지면 안 되므로 다시 계산하지 않는다 (DESIGN 3.1).
        out << snapshot.date << " 배정은 이미 있습니다 (" << snapshot.generatedAt << " 계산).\n";
    }

    const int unassigned = domain::countUnassigned(snapshot);
    if (unassigned > 0) {
        out << "\n⚠ 인원이 모자라 " << unassigned << "자리를 채우지 못했습니다.\n";
        out << "  누가 더 나올 수 있으면 등록한 뒤 다시 배정하세요.\n";
    }
    out << "\n오늘 무엇을 하는지 보려면: sched today\n";
}

void renderAbsences(std::ostream& out, const domain::Model& model,
                    const std::vector<domain::Absence>& absences) {
    if (absences.empty()) {
        out << "등록된 휴무가 없습니다.\n";
        return;
    }

    // 이름 열을 표시 폭 기준으로 맞춘다.
    std::size_t nameWidth = 0;
    const auto nameOf = [&model](const domain::WorkerId& id) {
        for (const domain::Worker& worker : model.workers.workers) {
            if (worker.id == id) {
                return worker.name;
            }
        }
        return id.str();
    };
    for (const domain::Absence& absence : absences) {
        nameWidth = std::max(nameWidth, util::displayWidth(nameOf(absence.workerId)));
    }

    for (const domain::Absence& absence : absences) {
        out << padTo(nameOf(absence.workerId), nameWidth + 2) << absence.from;
        if (absence.to != absence.from) {
            out << " ~ " << absence.to;
        }
        if (!absence.reason.empty()) {
            out << "  " << absence.reason;
        }
        out << "\n";
    }
}

void renderReport(std::ostream& out, const domain::Report& report) {
    if (report.empty()) {
        out << "문제를 찾지 못했습니다.\n";
        return;
    }

    out << "오류 " << report.errorCount() << "건, 경고 " << report.warningCount() << "건\n\n";
    for (const domain::Finding& finding : report.findings()) {
        out << ((finding.severity == domain::Severity::Error) ? "[오류] " : "[경고] ");
        if (!finding.where.empty()) {
            out << finding.where << " — ";
        }
        out << finding.message << "\n";
    }
}

void renderError(std::ostream& out, const util::Error& error) {
    out << error.message << "\n";
    if (!error.hint.empty()) {
        out << error.hint << "\n";
    }
}

}  // namespace cli
