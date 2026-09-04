#include "cli/render.h"
#include "core/app/absence_service.h"
#include "core/app/assign_service.h"
#include "core/app/data_dir.h"
#include "core/app/day_view.h"
#include "core/app/validate_service.h"
#include "core/app/worker_lookup.h"
#include "core/domain/finding.h"
#include "core/storage/json_io.h"
#include "core/util/clock.h"
#include "platform/console.h"
#include "platform/fileswap.h"
#include "platform/paths.h"

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
            std::cerr << "작업자 \"" << query << "\"를 찾을 수 없습니다.\n";
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
            std::cerr << "요일 \"" << token << "\"를 알 수 없습니다.\n";
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
    app.add_option("--data-dir", dataDirOption,
                   "데이터 폴더. 기본값은 실행 파일 옆 ./data 입니다.");

    CLI::App* validate = app.add_subcommand("validate", "설정 파일의 정합성을 검사합니다.");
    CLI::App* today = app.add_subcommand("today", "오늘의 배정을 보여줍니다.");
    CLI::App* assign = app.add_subcommand("assign", "그날의 배정을 계산해 저장합니다.");

    std::string dateOption;
    today->add_option("--date", dateOption, "조회할 날짜 (YYYY-MM-DD). 기본값은 오늘입니다.");
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
    today->fallthrough();
    assign->fallthrough();
    off->fallthrough();
    offAdd->fallthrough();
    offRm->fallthrough();
    offList->fallthrough();
    worker->fallthrough();
    workerOff->fallthrough();

    app.footer("아직 구현 중입니다. 현재는 validate, today, assign, off, worker off 가 동작합니다.");

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

    const std::optional<std::string> dataDirFromOption =
        dataDirOption.empty() ? std::nullopt : std::optional<std::string>{dataDirOption};
    const std::filesystem::path dataDir =
        app::resolveDataDir(dataDirFromOption, platform::executableDir());

    // 휴무와 정기 휴무는 데이터를 고치므로 먼저 모델을 읽는다.
    if (off->parsed() || workerOff->parsed()) {
        const util::DateTime now = util::SystemClock{}.now();

        util::Result<storage::LoadedModel> loaded = storage::loadModel(dataDir);
        if (!loaded) {
            cli::renderError(std::cerr, loaded.error());
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
                std::cerr << "날짜 \"" << dateOption << "\"를 읽을 수 없습니다.\n";
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
                cli::renderError(std::cerr, saved.error());
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
                cli::renderError(std::cerr, change.error());
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
                cli::renderError(std::cerr, removed.error());
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
            cli::renderError(std::cerr, result.error());
            return exitCodeFor(result.error());
        }
        cli::renderReport(std::cout, result.value());
        // 경고만 있으면 실행은 가능하므로 성공으로 끝낸다.
        return result.value().hasErrors() ? kExitDataFile : kExitSuccess;
    }

    if (today->parsed() || assign->parsed()) {
        const util::DateTime now = util::SystemClock{}.now();

        util::Date date = now.date;
        if (!dateOption.empty()) {
            const std::optional<util::Date> parsed = util::Date::parse(dateOption);
            if (!parsed.has_value()) {
                std::cerr << "날짜 \"" << dateOption << "\"를 읽을 수 없습니다.\n";
                std::cerr << "\"2026-09-04\" 처럼 YYYY-MM-DD 형식으로 적어 주세요.\n";
                return kExitInvalidUsage;
            }
            date = *parsed;
        }

        const util::Result<storage::LoadedModel> loaded = storage::loadModel(dataDir);
        if (!loaded) {
            cli::renderError(std::cerr, loaded.error());
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
            cli::renderError(std::cerr, outcome.error());
            return exitCodeFor(outcome.error());
        }

        if (assign->parsed()) {
            cli::renderAssignOutcome(std::cout, outcome.value());
            return kExitSuccess;
        }

        cli::renderDayView(std::cout,
                           app::buildDayView(model, date, now, &outcome.value().snapshot), now);
        return kExitSuccess;
    }

    std::cout << app.help() << std::endl;
    return kExitSuccess;
}
