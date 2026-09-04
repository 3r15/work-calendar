#include "core/app/data_dir.h"
#include "core/app/validate_service.h"
#include "core/domain/finding.h"
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

// 무엇이 잘못됐고 어떻게 고치는지를 한 줄씩 (CLI-SPEC.md 오류 메시지).
void printError(const util::Error& error) {
    std::cerr << error.message << "\n";
    if (!error.hint.empty()) {
        std::cerr << error.hint << "\n";
    }
}

void printReport(const domain::Report& report) {
    if (report.empty()) {
        std::cout << "문제를 찾지 못했습니다.\n";
        return;
    }

    std::cout << "오류 " << report.errorCount() << "건, 경고 " << report.warningCount() << "건\n\n";
    for (const domain::Finding& finding : report.findings()) {
        const char* label =
            (finding.severity == domain::Severity::Error) ? "[오류]" : "[경고]";
        std::cout << label << " ";
        if (!finding.where.empty()) {
            std::cout << finding.where << " — ";
        }
        std::cout << finding.message << "\n";
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
    // 전역 옵션은 서브명령 앞뒤 어디에 와도 받아야 한다. fallthrough 가 없으면
    // "sched validate --data-dir X" 가 인자 오류로 떨어진다.
    validate->fallthrough();

    app.footer("아직 구현 중입니다. 현재는 validate 만 동작합니다.");

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
            printError(result.error());
            return exitCodeFor(result.error());
        }
        printReport(result.value());
        // 경고만 있으면 실행은 가능하므로 성공으로 끝낸다.
        return result.value().hasErrors() ? kExitDataFile : kExitSuccess;
    }

    std::cout << app.help() << std::endl;
    return kExitSuccess;
}
