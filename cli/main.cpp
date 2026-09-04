#include "cli/color.h"
#include "cli/render.h"
#include "core/app/absence_service.h"
#include "core/app/assign_service.h"
#include "core/app/data_dir.h"
#include "core/app/prune_service.h"
#include "core/app/day_view.h"
#include "core/app/validate_service.h"
#include "core/app/worker_lookup.h"
#include "core/domain/day_plan.h"
#include "core/domain/finding.h"
#include "core/storage/json_io.h"
#include "core/storage/lock.h"
#include "core/util/clock.h"
#include "core/util/korean.h"
#include "platform/console.h"
#include "platform/fileswap.h"
#include "platform/paths.h"
#include "platform/process.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

// CLI-SPEC.md 의 종료 코드 표.
constexpr int kExitSuccess = 0;
constexpr int kExitGeneral = 1;
constexpr int kExitInvalidUsage = 2;
constexpr int kExitDataFile = 3;
// 성공했으나 미배정 슬롯이 존재 (--strict 를 준 경우에만).
constexpr int kExitUnassigned = 4;

// --worker 를 해석한다. 실패하면 무엇이 문제인지 출력하고 종료 코드를 돌려준다.
// 이름이 여러 명과 일치하면 후보를 보여주고 멈춘다 — 조용히 한 명을 고르면 엉뚱한 사람이 쉰다.
int resolveWorkerOrExplain(const domain::Model& model, const std::string& query,
                           domain::WorkerId& out) {
    const app::WorkerMatch match = app::resolveWorker(model, query);
    switch (match.kind) {
        case app::WorkerMatch::Kind::Found:
            out = match.worker.id;
            return kExitSuccess;
        case app::WorkerMatch::Kind::NotFound:
            std::cerr << "작업자 \"" << query << "\"" << util::josaEulReul(query)
                      << " 찾을 수 없습니다.\n";
            std::cerr << "이름 대신 ID 로도 지정할 수 있습니다.\n";
            return kExitInvalidUsage;
        case app::WorkerMatch::Kind::Ambiguous:
            std::cerr << "\"" << query << "\"라는 이름이 " << match.candidates.size()
                      << "명 있습니다. ID 로 지정해 주세요.\n";
            for (const domain::Worker& candidate : match.candidates) {
                std::cerr << "  " << candidate.id.str() << "  " << candidate.name << "\n";
            }
            return kExitInvalidUsage;
    }
    return kExitGeneral;
}

// 요일 목록을 "MON,TUE" 형태로 받는다.
bool parseWeekdayList(const std::string& text, std::vector<domain::Weekday>& out) {
    std::istringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (token.empty()) {
            continue;
        }
        const std::optional<domain::Weekday> day = domain::parseWeekday(token);
        if (!day.has_value()) {
            std::cerr << "요일 \"" << token << "\"" << util::josaEulReul(token)
                  << " 알 수 없습니다.\n";
            std::cerr << "SUN MON TUE WED THU FRI SAT 중에서 쉼표로 이어 적어 주세요.\n";
            return false;
        }
        out.push_back(*day);
    }
    return true;
}

int exitCodeFor(const util::Error& error) {
    switch (error.code) {
        case util::ErrorCode::InvalidUsage:
            return kExitInvalidUsage;
        case util::ErrorCode::DataFile:
        case util::ErrorCode::NotFound:
            return kExitDataFile;
        default:
            return kExitGeneral;
    }
}

}  // namespace

int main(int argc, char** argv) {
    // 무엇보다 먼저. 이 뒤로 출력되는 한글이 깨지지 않는다.
    platform::initConsole();
    // 데이터 쓰기가 Windows 에서도 원자적이 되도록 교체 구현을 꽂는다 (D-008).
    platform::installFileSwap();

    CLI::App app{"작업 스케줄러 — 매일의 작업을 작업자에게 자동으로 배정합니다."};
    app.set_version_flag("--version", std::string{"sched "} + SCHED_VERSION + " — 작업 스케줄러");
    app.require_subcommand(0, 1);

    std::string dataDirOption;
    bool noColorFlag = false;
    bool jsonFlag = false;
    bool quietFlag = false;
    app.add_option("--data-dir", dataDirOption,
                   "데이터 폴더. 기본값은 실행 파일 옆 ./data 입니다.");
    app.add_flag("--no-color", noColorFlag, "색상을 쓰지 않습니다.");
    app.add_flag("--json", jsonFlag, "사람이 읽는 표 대신 JSON 으로 출력합니다.");
    app.add_flag("--quiet", quietFlag, "경고와 안내를 숨기고 결과만 보여줍니다.");

    CLI::App* validate = app.add_subcommand("validate", "설정 파일의 정합성을 검사합니다.");

    CLI::App* prune = app.add_subcommand("prune", "오래된 배정 기록을 한 달치씩 합쳐 보관합니다.");
    int keepDays = app::kDefaultKeepDays;
    bool dryRunFlag = false;
    prune->add_option("--keep-days", keepDays, "이 일수보다 오래된 것만 옮깁니다 (기본 90).");
    prune->add_flag("--dry-run", dryRunFlag, "옮기지 않고 몇 건인지만 셉니다.");
    CLI::App* now_ = app.add_subcommand("now", "지금 시간대의 작업을 보여줍니다.");
    CLI::App* today = app.add_subcommand("today", "오늘의 배정을 보여줍니다.");
    CLI::App* show = app.add_subcommand("show", "지정한 날짜와 시간대의 배정을 보여줍니다.");

    std::string slotOption;
    bool strictFlag = false;
    show->add_option("--slot", slotOption, "시간대 ID 로 좁혀 봅니다.");
    for (CLI::App* command : {now_, today, show}) {
        command->add_flag("--strict", strictFlag,
                          "미배정 슬롯이 있으면 종료 코드 4 로 끝냅니다.");
    }
    CLI::App* assign = app.add_subcommand("assign", "그날의 배정을 계산해 저장합니다.");

    std::string dateOption;
    today->add_option("--date", dateOption, "조회할 날짜 (YYYY-MM-DD). 기본값은 오늘입니다.");
    show->add_option("--date", dateOption, "조회할 날짜 (YYYY-MM-DD). 기본값은 오늘입니다.");
    assign->add_option("--date", dateOption, "배정할 날짜 (YYYY-MM-DD). 기본값은 오늘입니다.");

    bool forceFlag = false;
    bool yesFlag = false;
    assign->add_flag("--force", forceFlag, "기존 배정을 버리고 다시 계산합니다.");
    assign->add_flag("--yes", yesFlag, "--force 실행 전 확인을 건너뜁니다.");

    // 휴무 명령군.
    CLI::App* off = app.add_subcommand("off", "휴무를 등록하거나 해제합니다.");
    off->require_subcommand(1);

    std::string workerOption;
    std::string reasonOption;
    std::string fromOption;
    std::string toOption;
    bool todayFlag = false;

    CLI::App* offAdd = off->add_subcommand("add", "휴무를 등록합니다.");
    offAdd->add_option("--worker", workerOption, "작업자 이름 또는 ID")->required();
    offAdd->add_flag("--today", todayFlag, "오늘 하루");
    offAdd->add_option("--date", dateOption, "하루 휴무 날짜 (YYYY-MM-DD)");
    offAdd->add_option("--from", fromOption, "기간 시작일 (YYYY-MM-DD)");
    offAdd->add_option("--to", toOption, "기간 종료일 (YYYY-MM-DD)");
    offAdd->add_option("--reason", reasonOption, "사유 (예: 연차)");

    CLI::App* offRm = off->add_subcommand("rm", "등록된 휴무를 해제합니다.");
    offRm->add_option("--worker", workerOption, "작업자 이름 또는 ID")->required();
    offRm->add_flag("--today", todayFlag, "오늘");
    offRm->add_option("--date", dateOption, "해제할 날짜 (YYYY-MM-DD)");

    CLI::App* offList = off->add_subcommand("list", "등록된 휴무를 보여줍니다.");
    offList->add_option("--date", dateOption, "그날 적용되는 것만 (YYYY-MM-DD)");
    offList->add_option("--worker", workerOption, "한 사람만");

    // 정기 휴무는 absences.json 이 아니라 workers.json 의 weeklyOff 다 (CLI-SPEC.md).
    CLI::App* worker = app.add_subcommand("worker", "작업자를 관리합니다.");
    worker->require_subcommand(1);
    CLI::App* workerOff = worker->add_subcommand("off", "정기 휴무 요일을 지정합니다.");
    std::string weeklyOption;
    workerOff->add_option("--worker", workerOption, "작업자 이름 또는 ID")->required();
    workerOff->add_option("--weekly", weeklyOption, "요일 목록 (예: SUN,MON). 비우면 해제")
        ->required();

    // 전역 옵션은 서브명령 앞뒤 어디에 와도 받아야 한다. fallthrough 가 없으면
    // "sched validate --data-dir X" 가 인자 오류로 떨어진다.
    validate->fallthrough();
    prune->fallthrough();
    now_->fallthrough();
    today->fallthrough();
    show->fallthrough();
    assign->fallthrough();
    off->fallthrough();
    offAdd->fallthrough();
    offRm->fallthrough();
    offList->fallthrough();
    worker->fallthrough();
    workerOff->fallthrough();

    app.footer("매일 쓰는 것: sched now / sched today / sched off add --worker <이름> --today");

    // CLI11 은 파싱 결과를 예외로 알린다. 예외는 여기서 끝내고 안쪽으로 넘기지 않는다.
    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp& e) {
        return app.exit(e);
    } catch (const CLI::CallForVersion& e) {
        return app.exit(e);
    } catch (const CLI::ParseError& e) {
        app.exit(e);
        return kExitInvalidUsage;
    }

    // 색을 쓸 수 있는지 여기서 한 번 정한다. 끄는 조건이 셋이라 출력 코드마다 판단하면
    // 어긋난다: --no-color, NO_COLOR 환경 변수, 콘솔이 ANSI 를 못 켜는 경우 (CLI-SPEC.md).
    const bool colorEnabled = !noColorFlag && !cli::noColorRequested() && platform::enableAnsi();
    const cli::Palette palette{colorEnabled && !jsonFlag};

    const std::optional<std::string> dataDirFromOption =
        dataDirOption.empty() ? std::nullopt : std::optional<std::string>{dataDirOption};
    const std::filesystem::path dataDir =
        app::resolveDataDir(dataDirFromOption, platform::executableDir());

    // 데이터를 고치는 명령은 쓰기 락을 잡는다 (D-007). 조회만 하는 명령은 잡지 않는다 —
    // 두 창에서 동시에 들여다보는 것은 막을 이유가 없다.
    //
    // today/now/show 도 그날 스냅샷이 없으면 계산해서 저장하므로 여기에 포함된다.
    storage::ScopedLock writeLock;
    const bool needsLock = off->parsed() || workerOff->parsed() || assign->parsed() ||
                           prune->parsed() || now_->parsed() || today->parsed() || show->parsed();
    if (needsLock) {
        const util::Result<void> locked =
            writeLock.acquire(dataDir, platform::processId(), util::SystemClock{}.now(),
                              &platform::processAlive);
        if (!locked) {
            cli::renderError(std::cerr, locked.error(), palette);
            return exitCodeFor(locked.error());
        }
    }

    if (prune->parsed()) {
        const util::Result<app::PruneResult> pruned = app::pruneSnapshots(
            dataDir, util::SystemClock{}.now().date, keepDays, dryRunFlag);
        if (!pruned) {
            cli::renderError(std::cerr, pruned.error(), palette);
            return exitCodeFor(pruned.error());
        }
        if (pruned.value().archived == 0) {
            std::cout << "옮길 기록이 없습니다. " << keepDays << "일이 지난 배정 기록이 아직 "
                      << "없습니다.\n";
            return kExitSuccess;
        }
        std::cout << (dryRunFlag ? "옮길 수 있는 배정 기록: " : "배정 기록 ")
                  << pruned.value().archived << "건" << (dryRunFlag ? "" : "을 보관했습니다")
                  << "\n";
        for (const std::string& file : pruned.value().touched) {
            std::cout << "  state/archive/" << file << "\n";
        }
        return kExitSuccess;
    }

    // 휴무와 정기 휴무는 데이터를 고치므로 먼저 모델을 읽는다.
    if (off->parsed() || workerOff->parsed()) {
        const util::DateTime now = util::SystemClock{}.now();

        util::Result<storage::LoadedModel> loaded = storage::loadModel(dataDir);
        if (!loaded) {
            cli::renderError(std::cerr, loaded.error(), palette);
            return exitCodeFor(loaded.error());
        }
        domain::Model model = std::move(loaded).value().model;

        // --date 를 준 경우의 날짜 해석. --today 면 오늘이다.
        const auto resolveDate = [&](util::Date& out) -> int {
            if (todayFlag || dateOption.empty()) {
                out = now.date;
                return kExitSuccess;
            }
            const std::optional<util::Date> parsed = util::Date::parse(dateOption);
            if (!parsed.has_value()) {
                std::cerr << "날짜 \"" << dateOption << "\"" << util::josaEulReul(dateOption)
                          << " 읽을 수 없습니다.\n";
                std::cerr << "\"2026-09-04\" 처럼 YYYY-MM-DD 형식으로 적어 주세요.\n";
                return kExitInvalidUsage;
            }
            out = *parsed;
            return kExitSuccess;
        };

        if (offList->parsed()) {
            util::Date date;
            const bool filterByDate = todayFlag || !dateOption.empty();
            if (filterByDate) {
                if (const int code = resolveDate(date); code != kExitSuccess) {
                    return code;
                }
            }

            domain::WorkerId workerId;
            const bool filterByWorker = !workerOption.empty();
            if (filterByWorker) {
                if (const int code = resolveWorkerOrExplain(model, workerOption, workerId);
                    code != kExitSuccess) {
                    return code;
                }
            }

            cli::renderAbsences(std::cout, model,
                                app::listAbsences(model, filterByDate ? &date : nullptr,
                                                  filterByWorker ? &workerId : nullptr));
            return kExitSuccess;
        }

        domain::WorkerId workerId;
        if (const int code = resolveWorkerOrExplain(model, workerOption, workerId);
            code != kExitSuccess) {
            return code;
        }

        if (workerOff->parsed()) {
            std::vector<domain::Weekday> weekdays;
            if (!parseWeekdayList(weeklyOption, weekdays)) {
                return kExitInvalidUsage;
            }
            if (const util::Result<void> saved =
                    app::setWeeklyOff(dataDir, model, workerId, weekdays);
                !saved) {
                cli::renderError(std::cerr, saved.error(), palette);
                return exitCodeFor(saved.error());
            }
            std::cout << "정기 휴무를 저장했습니다.\n";
            return kExitSuccess;
        }

        if (offAdd->parsed()) {
            util::Date from;
            util::Date to;
            if (!fromOption.empty() || !toOption.empty()) {
                const std::optional<util::Date> parsedFrom = util::Date::parse(fromOption);
                const std::optional<util::Date> parsedTo = util::Date::parse(toOption);
                if (!parsedFrom.has_value() || !parsedTo.has_value()) {
                    std::cerr << "--from 과 --to 를 \"2026-09-10\" 처럼 YYYY-MM-DD 형식으로 "
                                 "함께 적어 주세요.\n";
                    return kExitInvalidUsage;
                }
                from = *parsedFrom;
                to = *parsedTo;
            } else {
                if (const int code = resolveDate(from); code != kExitSuccess) {
                    return code;
                }
                to = from;
            }

            const util::Result<app::AbsenceChange> change =
                app::addAbsence(dataDir, model, workerId, from, to, reasonOption, now);
            if (!change) {
                cli::renderError(std::cerr, change.error(), palette);
                return exitCodeFor(change.error());
            }

            std::cout << "휴무를 등록했습니다: " << from.toString();
            if (!(to == from)) {
                std::cout << " ~ " << to.toString();
            }
            std::cout << "\n";

            // 당일 급휴는 배정을 지우지 않는다. 재배정 여부는 사람이 판단한다 (D-009).
            if (change.value().urgent) {
                std::cout << "\n⚠ 이미 나간 배정 " << change.value().affectedAssignments
                          << "건이 이 사람에게 있습니다. 자동으로 바꾸지 않았습니다.\n";
                std::cout << "  재배정하려면: sched assign --date " << from.toString()
                          << " --force\n";
            }
            return kExitSuccess;
        }

        if (offRm->parsed()) {
            util::Date date;
            if (const int code = resolveDate(date); code != kExitSuccess) {
                return code;
            }
            const util::Result<int> removed = app::removeAbsence(dataDir, model, workerId, date);
            if (!removed) {
                cli::renderError(std::cerr, removed.error(), palette);
                return exitCodeFor(removed.error());
            }
            if (removed.value() == 0) {
                std::cout << date.toString() << " 에 등록된 휴무가 없습니다.\n";
            } else {
                std::cout << "휴무 " << removed.value() << "건을 해제했습니다.\n";
            }
            return kExitSuccess;
        }
    }

    if (validate->parsed()) {
        const util::Result<domain::Report> result = app::validateDataDir(dataDir);
        if (!result) {
            cli::renderError(std::cerr, result.error(), palette);
            return exitCodeFor(result.error());
        }
        cli::renderReport(std::cout, result.value(), palette);
        // 경고만 있으면 실행은 가능하므로 성공으로 끝낸다.
        return result.value().hasErrors() ? kExitDataFile : kExitSuccess;
    }

    if (now_->parsed() || today->parsed() || show->parsed() || assign->parsed()) {
        const util::DateTime now = util::SystemClock{}.now();

        util::Date date = now.date;
        if (!dateOption.empty()) {
            const std::optional<util::Date> parsed = util::Date::parse(dateOption);
            if (!parsed.has_value()) {
                std::cerr << "날짜 \"" << dateOption << "\"" << util::josaEulReul(dateOption)
                          << " 읽을 수 없습니다.\n";
                std::cerr << "\"2026-09-04\" 처럼 YYYY-MM-DD 형식으로 적어 주세요.\n";
                return kExitInvalidUsage;
            }
            date = *parsed;
        }

        const util::Result<storage::LoadedModel> loaded = storage::loadModel(dataDir);
        if (!loaded) {
            cli::renderError(std::cerr, loaded.error(), palette);
            return exitCodeFor(loaded.error());
        }
        const domain::Model& model = loaded.value().model;

        // 그날 스냅샷이 없으면 계산해서 저장하고, 있으면 읽기만 한다 (DESIGN 3.1).
        if (assign->parsed() && forceFlag && !yesFlag) {
            // 이미 나간 배정을 갈아엎는 일이다. 사람이 이미 움직이고 있을 수 있다 (D-009).
            std::cout << date.toString() << " 의 기존 배정을 버리고 다시 계산합니다.\n";
            std::cout << "이미 자기 일을 시작한 사람의 작업이 바뀔 수 있습니다. 계속할까요? [y/N] ";
            std::string answer;
            std::getline(std::cin, answer);
            if (answer != "y" && answer != "Y") {
                std::cout << "취소했습니다.\n";
                return kExitSuccess;
            }
        }

        const util::Result<app::AssignOutcome> outcome =
            app::ensureSnapshot(dataDir, model, date, now, assign->parsed() && forceFlag);
        if (!outcome) {
            cli::renderError(std::cerr, outcome.error(), palette);
            return exitCodeFor(outcome.error());
        }

        const domain::DaySnapshot& snapshot = outcome.value().snapshot;

        // --strict 는 미배정이 있으면 종료 코드 4 로 끝낸다. 스크립트가 결과를 알 수 있게 하려는 것.
        const int strictCode =
            (strictFlag && domain::countUnassigned(snapshot) > 0) ? kExitUnassigned : kExitSuccess;

        if (jsonFlag) {
            // 스냅샷 스키마를 그대로 내보내되 이름을 덧붙인다 (CLI-SPEC.md --json).
            std::cout << cli::toJsonWithNames(model, snapshot);
            return strictCode;
        }

        if (assign->parsed()) {
            cli::renderAssignOutcome(std::cout, outcome.value(), palette, quietFlag);
            return kExitSuccess;
        }

        app::DayView view = app::buildDayView(model, date, now, &snapshot);

        // now 는 지금 시간대만, show --slot 은 지정한 시간대만 남긴다.
        if (now_->parsed() || !slotOption.empty()) {
            std::vector<app::SlotBlock> kept;
            for (const app::SlotBlock& slot : view.slots) {
                const bool wanted = slotOption.empty() ? slot.isCurrent
                                                       : slot.id == domain::TimeSlotId{slotOption};
                if (wanted) {
                    kept.push_back(slot);
                }
            }
            if (!slotOption.empty() && kept.empty()) {
                std::cerr << "시간대 \"" << slotOption << "\"" << util::josaEulReul(slotOption)
                          << " 찾을 수 없습니다.\n";
                std::cerr << "오늘 있는 시간대를 보려면: sched today\n";
                return kExitInvalidUsage;
            }
            view.slots = std::move(kept);
        }

        cli::renderDayView(std::cout, view, now, palette);
        return strictCode;
    }

    std::cout << app.help() << std::endl;
    return kExitSuccess;
}
