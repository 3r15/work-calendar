#include "cli/render.h"
#include "core/app/data_dir.h"
#include "core/app/day_view.h"
#include "core/app/validate_service.h"
#include "core/domain/finding.h"
#include "core/storage/json_io.h"
#include "core/util/clock.h"
#include "platform/console.h"
#include "platform/fileswap.h"
#include "platform/paths.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <optional>
#include <string>

namespace {

// CLI-SPEC.md 의 종료 코드 표.
constexpr int kExitSuccess = 0;
constexpr int kExitGeneral = 1;
constexpr int kExitInvalidUsage = 2;
constexpr int kExitDataFile = 3;

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
    CLI::App* today = app.add_subcommand("today", "오늘의 시간대와 작업 구조를 보여줍니다.");

    std::string dateOption;
    today->add_option("--date", dateOption, "조회할 날짜 (YYYY-MM-DD). 기본값은 오늘입니다.");

    // 전역 옵션은 서브명령 앞뒤 어디에 와도 받아야 한다. fallthrough 가 없으면
    // "sched validate --data-dir X" 가 인자 오류로 떨어진다.
    validate->fallthrough();
    today->fallthrough();

    app.footer("아직 구현 중입니다. 현재는 validate 와 today 만 동작합니다.");

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

    const std::optional<std::string> fromOption =
        dataDirOption.empty() ? std::nullopt : std::optional<std::string>{dataDirOption};
    const std::filesystem::path dataDir =
        app::resolveDataDir(fromOption, platform::executableDir());

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

    if (today->parsed()) {
        const util::DateTime now = util::SystemClock{}.now();

        util::Date date = now.date;
        if (!dateOption.empty()) {
            const std::optional<util::Date> parsed = util::Date::parse(dateOption);
            if (!parsed.has_value()) {
                std::cerr << "날짜 \"" << dateOption << "\" 를 읽을 수 없습니다.\n";
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

        cli::renderDayView(std::cout, app::buildDayView(loaded.value().model, date, now), now);
        return kExitSuccess;
    }

    std::cout << app.help() << std::endl;
    return kExitSuccess;
}
