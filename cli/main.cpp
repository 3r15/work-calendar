#include "platform/console.h"

#include <CLI/CLI.hpp>

#include <iostream>

namespace {

// CLI-SPEC.md 의 종료 코드 표.
constexpr int kExitSuccess = 0;
constexpr int kExitInvalidUsage = 2;

}  // namespace

int main(int argc, char** argv) {
    // 무엇보다 먼저. 이 뒤로 출력되는 한글이 깨지지 않는다.
    platform::initConsole();

    CLI::App app{"작업 스케줄러 — 매일의 작업을 작업자에게 자동으로 배정합니다."};
    // 버전 문자열에 한글을 넣어 둔다. 이게 깨지지 않고 나오는 것이 콘솔 UTF-8 설정이
    // 실제로 먹었다는 증거다 (ROADMAP Phase 0 완료 기준 5번).
    app.set_version_flag("--version", std::string{"sched "} + SCHED_VERSION + " — 작업 스케줄러");

    // Phase 0 은 골격만 세운다. 명령은 Phase 2 부터 하나씩 붙는다 (docs/ROADMAP.md).
    app.footer("아직 구현 중입니다. 현재는 --version 과 --help 만 동작합니다.");

    // CLI11 은 파싱 결과를 예외로 알린다. 예외는 여기서 끝내고 안쪽으로 넘기지 않는다 (CLAUDE.md 7장).
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

    // 인자 없이 실행하면 무엇을 할 수 있는지 보여준다. 빈 화면은 고장난 것처럼 보인다 (D-003 과 같은 취지).
    std::cout << app.help() << std::endl;
    return kExitSuccess;
}
