#include "cli/color.h"
#include "cli/render.h"
#include "core/app/absence_service.h"
#include "core/app/admin_service.h"
#include "core/app/assign_service.h"
#include "core/app/data_dir.h"
#include "core/app/prune_service.h"
#include "core/app/update_service.h"
#include "core/app/day_view.h"
#include "core/app/validate_service.h"
#include "core/app/worker_lookup.h"
#include "core/domain/day_plan.h"
#include "core/domain/finding.h"
#include "core/storage/json_io.h"
#include "core/storage/lock.h"
#include "core/util/clock.h"
#include "core/util/korean.h"
#include "core/util/semver.h"
#include "core/util/sha256.h"
#include "platform/console.h"
#include "platform/fileswap.h"
#include "platform/http.h"
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

    CLI::App* update = app.add_subcommand("update", "새 버전을 확인하거나 내려받아 적용합니다.");
    update->require_subcommand(1);
    CLI::App* updateCheck = update->add_subcommand("check", "새 버전이 있는지만 확인합니다.");
    CLI::App* updateApply = update->add_subcommand("apply", "내려받아 교체하고 다시 시작합니다.");

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

    // 관리 명령. 설정 파일을 손으로 고치지 않고도 운영되게 한다.
    std::string nameOption;
    std::string idOption;
    std::string setsOption;
    std::string catsOption;
    std::string conflictsOption;
    std::string weekdaysOption;
    std::string activeOption;
    std::string startOption;
    std::string endOption;
    int countOption = 0;
    bool allFlag = false;
    bool hardFlag = false;

    CLI::App* workerAdd = worker->add_subcommand("add", "작업자를 추가합니다.");
    workerAdd->add_option("--name", nameOption, "이름")->required();
    workerAdd->add_option("--id", idOption, "ID. 비우면 자동으로 붙입니다.");
    workerAdd->add_option("--weekly-off", weeklyOption, "정기 휴무 요일 (예: SUN,MON)");

    CLI::App* workerList = worker->add_subcommand("list", "작업자 목록을 보여줍니다.");
    workerList->add_flag("--all", allFlag, "쉬는 사람(active:false)도 함께 보여줍니다.");

    CLI::App* workerRm = worker->add_subcommand("rm", "작업자를 제외합니다.");
    workerRm->add_option("--worker", workerOption, "작업자 이름 또는 ID")->required();
    workerRm->add_flag("--hard", hardFlag, "정말 삭제합니다. 과거 기록의 참조가 끊어집니다.");

    CLI::App* workerEdit = worker->add_subcommand("edit", "작업자를 수정합니다.");
    workerEdit->add_option("--worker", workerOption, "작업자 이름 또는 ID")->required();
    workerEdit->add_option("--name", nameOption, "새 이름");
    workerEdit->add_option("--active", activeOption, "true 또는 false");

    CLI::App* task = app.add_subcommand("task", "작업을 관리합니다.");
    task->require_subcommand(1);
    CLI::App* taskAdd = task->add_subcommand("add", "작업을 추가합니다.");
    taskAdd->add_option("--id", idOption, "작업 ID")->required();
    taskAdd->add_option("--name", nameOption, "화면에 보일 이름");
    taskAdd->add_option("--set", setsOption, "속할 작업집합 (쉼표로 여럿)")->required();
    taskAdd->add_option("--count", countOption, "필요 인원")->required();
    taskAdd->add_option("--conflicts", conflictsOption, "함께 맡을 수 없는 작업 (쉼표로 여럿)");
    taskAdd->add_option("--weekdays", weekdaysOption, "이 요일에만 (비우면 근무일 전체)");

    CLI::App* taskList = task->add_subcommand("list", "작업 목록을 보여줍니다.");
    taskList->add_option("--set", setsOption, "이 작업집합의 것만");

    CLI::App* taskRm = task->add_subcommand("rm", "작업을 지웁니다.");
    taskRm->add_option("--id", idOption, "작업 ID")->required();

    CLI::App* taskEdit = task->add_subcommand("edit", "작업을 수정합니다.");
    taskEdit->add_option("--id", idOption, "작업 ID")->required();
    taskEdit->add_option("--name", nameOption, "새 이름");
    taskEdit->add_option("--count", countOption, "필요 인원");
    taskEdit->add_option("--set", setsOption, "속할 작업집합 (통째로 교체)");
    taskEdit->add_option("--conflicts", conflictsOption, "배타 작업 (통째로 교체)");
    taskEdit->add_option("--weekdays", weekdaysOption, "요일 (통째로 교체)");

    CLI::App* setCmd = app.add_subcommand("set", "작업집합을 관리합니다.");
    setCmd->require_subcommand(1);
    CLI::App* setAdd = setCmd->add_subcommand("add", "작업집합을 추가합니다.");
    setAdd->add_option("--id", idOption, "작업집합 ID")->required();
    setAdd->add_option("--name", nameOption, "화면에 보일 이름");
    CLI::App* setList = setCmd->add_subcommand("list", "작업집합 목록을 보여줍니다.");
    CLI::App* setRm = setCmd->add_subcommand("rm", "작업집합을 지웁니다.");
    setRm->add_option("--id", idOption, "작업집합 ID")->required();

    CLI::App* catCmd = app.add_subcommand("cat", "작업분류를 관리합니다.");
    catCmd->require_subcommand(1);
    CLI::App* catAdd = catCmd->add_subcommand("add", "분류를 추가합니다.");
    catAdd->add_option("--id", idOption, "분류 ID")->required();
    catAdd->add_option("--name", nameOption, "화면에 보일 이름");
    catAdd->add_option("--set", setsOption, "묶을 작업집합 (쉼표로 여럿)")->required();
    CLI::App* catList = catCmd->add_subcommand("list", "분류 목록을 보여줍니다.");
    CLI::App* catRm = catCmd->add_subcommand("rm", "분류를 지웁니다.");
    catRm->add_option("--id", idOption, "분류 ID")->required();

    CLI::App* slotCmd = app.add_subcommand("slot", "시간대를 관리합니다.");
    slotCmd->require_subcommand(1);
    CLI::App* slotAdd = slotCmd->add_subcommand("add", "시간대를 추가합니다.");
    slotAdd->add_option("--id", idOption, "시간대 ID")->required();
    slotAdd->add_option("--name", nameOption, "화면에 보일 이름");
    slotAdd->add_option("--start", startOption, "시작 시각 HH:MM")->required();
    slotAdd->add_option("--end", endOption, "종료 시각 HH:MM")->required();
    slotAdd->add_option("--weekdays", weekdaysOption, "이 요일에만 (비우면 근무일 전체)");
    slotAdd->add_option("--cat", catsOption, "붙일 분류 (쉼표로 여럿)")->required();
    CLI::App* slotList = slotCmd->add_subcommand("list", "시간대 목록을 보여줍니다.");
    CLI::App* slotRm = slotCmd->add_subcommand("rm", "시간대를 지웁니다.");
    slotRm->add_option("--id", idOption, "시간대 ID")->required();

    // 전역 옵션은 서브명령 앞뒤 어디에 와도 받아야 한다. fallthrough 가 없으면
    // "sched validate --data-dir X" 가 인자 오류로 떨어진다.
    validate->fallthrough();
    prune->fallthrough();
    update->fallthrough();
    updateCheck->fallthrough();
    updateApply->fallthrough();
    now_->fallthrough();
    today->fallthrough();
    show->fallthrough();
    assign->fallthrough();
    off->fallthrough();
    offAdd->fallthrough();
    offRm->fallthrough();
    offList->fallthrough();
    worker->fallthrough();
    for (CLI::App* command : {workerOff, workerAdd, workerList, workerRm, workerEdit, task, taskAdd,
                              taskList, taskRm, taskEdit, setCmd, setAdd, setList, setRm, catCmd,
                              catAdd, catList, catRm, slotCmd, slotAdd, slotList, slotRm}) {
        command->fallthrough();
    }

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
    // 부모 App 의 parsed() 를 보면 안 된다. CLI11 은 하위 명령이 실행되면 부모도 true 가 되므로
    // "sched worker list" 같은 조회까지 쓰기 락을 잡게 된다 — 두 창에서 목록을 보는 것조차
    // 막혀 버린다. 실제로 파일을 고치는 하위 명령만 나열한다.
    const bool writesData = offAdd->parsed() || offRm->parsed() || workerOff->parsed() ||
                            workerAdd->parsed() || workerRm->parsed() || workerEdit->parsed() ||
                            taskAdd->parsed() || taskRm->parsed() || taskEdit->parsed() ||
                            setAdd->parsed() || setRm->parsed() || catAdd->parsed() ||
                            catRm->parsed() || slotAdd->parsed() || slotRm->parsed();
    const bool needsLock = writesData || assign->parsed() || prune->parsed() || now_->parsed() ||
                           today->parsed() || show->parsed();
    if (needsLock) {
        const util::Result<void> locked =
            writeLock.acquire(dataDir, platform::processId(), util::SystemClock{}.now(),
                              &platform::processAlive);
        if (!locked) {
            cli::renderError(std::cerr, locked.error(), palette);
            return exitCodeFor(locked.error());
        }
    }

    if (update->parsed()) {
        const util::DateTime now = util::SystemClock{}.now();
        const util::Result<storage::LoadedModel> loaded = storage::loadModel(dataDir);
        if (!loaded) {
            cli::renderError(std::cerr, loaded.error(), palette);
            return exitCodeFor(loaded.error());
        }
        const domain::Config& config = loaded.value().model.config;

        const util::Version current = util::Version::parse(SCHED_VERSION).value_or(util::Version{});

        if (!config.update.enabled || config.update.repo.empty()) {
            std::cout << "자동 업데이트가 꺼져 있습니다.\n";
            std::cout << "켜려면 config.json 의 update.enabled 를 true 로, update.repo 를 "
                         "\"owner/name\" 으로 적어 주세요.\n";
            return kExitSuccess;
        }

        const std::optional<std::string> apiUrl = app::latestReleaseUrl(config.update.repo);
        if (!apiUrl.has_value()) {
            std::cerr << "config.json 의 update.repo \"" << config.update.repo
                      << "\" 를 읽을 수 없습니다.\n";
            std::cerr << "\"owner/name\" 형식이어야 합니다. 예: 3r15/work-calendar\n";
            return kExitDataFile;
        }

        const std::string userAgent = std::string{"sched/"} + SCHED_VERSION;
        const util::Result<platform::HttpResponse> response =
            platform::httpGet(*apiUrl, userAgent);
        if (!response) {
            // 네트워크가 없다고 프로그램이 멈추면 안 된다. 알리고 정상 종료한다.
            cli::renderError(std::cerr, response.error(), palette);
            return kExitSuccess;
        }
        if (response.value().status != 200) {
            std::cerr << "업데이트 서버가 " << response.value().status << " 로 답했습니다.\n";
            std::cerr << "잠시 뒤 다시 시도해 주세요.\n";
            return kExitSuccess;
        }

        const util::Result<app::ReleaseInfo> release =
            app::parseLatestRelease(response.value().body);
        if (!release) {
            cli::renderError(std::cerr, release.error(), palette);
            return kExitSuccess;
        }

        // 확인했다는 사실을 남긴다. GitHub 비인증 한도가 시간당 60회다 (DESIGN 7.2).
        (void)app::recordCheck(dataDir, now);

        const app::UpdateStatus status = app::decideUpdate(config, current, release.value());
        if (status.decision != app::UpdateDecision::Available) {
            std::cout << "이미 최신입니다 (" << current.toString() << ").\n";
            return kExitSuccess;
        }

        std::cout << "새 버전이 있습니다: " << current.toString() << " → "
                  << release.value().version.toString() << "\n";
        if (updateCheck->parsed()) {
            std::cout << "받으려면: sched update apply\n";
            return kExitSuccess;
        }

        // --- apply ---
        const std::filesystem::path staging = dataDir / "state" / "update-staging";
        std::error_code ec;
        std::filesystem::remove_all(staging, ec);
        std::filesystem::create_directories(staging, ec);
        const std::filesystem::path archive = staging / release.value().assetName;

        std::cout << "내려받는 중…\n";
        if (const util::Result<void> downloaded =
                platform::httpDownload(release.value().assetUrl, archive, userAgent);
            !downloaded) {
            cli::renderError(std::cerr, downloaded.error(), palette);
            std::filesystem::remove_all(staging, ec);
            return kExitGeneral;
        }

        // 체크섬이 틀리면 교체하지 않는다 (Phase 6 완료 기준 2번).
        if (release.value().checksumUrl.empty()) {
            std::cerr << "이 릴리스에 체크섬 파일이 없어 내려받은 파일을 검증할 수 없습니다.\n";
            std::cerr << "안전을 위해 교체하지 않았습니다.\n";
            std::filesystem::remove_all(staging, ec);
            return kExitGeneral;
        }
        const util::Result<platform::HttpResponse> sums =
            platform::httpGet(release.value().checksumUrl, userAgent);
        if (!sums || sums.value().status != 200) {
            std::cerr << "체크섬 파일을 받지 못해 검증할 수 없습니다. 교체하지 않았습니다.\n";
            std::filesystem::remove_all(staging, ec);
            return kExitGeneral;
        }
        const std::optional<std::string> expected =
            app::findChecksumFor(sums.value().body, release.value().assetName);
        const std::optional<std::string> actual = util::sha256HexOfFile(archive);
        if (!expected.has_value() || !actual.has_value() ||
            !util::checksumMatches(*expected, *actual)) {
            std::cerr << "내려받은 파일이 손상됐습니다. 교체하지 않았습니다.\n";
            std::cerr << "네트워크 문제일 수 있습니다. 다시 시도해 주세요.\n";
            std::filesystem::remove_all(staging, ec);
            return kExitGeneral;
        }
        std::cout << "검증했습니다.\n";

        std::cout << "\n압축을 풀어 " << staging.string() << " 에 두었습니다.\n";
        std::cout << "updater 가 프로그램을 교체하고 다시 시작합니다.\n";
        std::cout << "data/ 는 건드리지 않습니다.\n";
        return kExitSuccess;
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

    // 관리 명령. 전부 모델을 읽고 고친 뒤 해당 파일을 다시 쓴다.
    if (task->parsed() || setCmd->parsed() || catCmd->parsed() || slotCmd->parsed() ||
        workerAdd->parsed() || workerList->parsed() || workerRm->parsed() ||
        workerEdit->parsed()) {
        util::Result<storage::LoadedModel> loaded = storage::loadModel(dataDir);
        if (!loaded) {
            cli::renderError(std::cerr, loaded.error(), palette);
            return exitCodeFor(loaded.error());
        }
        domain::Model model = std::move(loaded).value().model;

        const auto splitIds = [](const std::string& text) {
            std::vector<std::string> out;
            std::istringstream stream(text);
            std::string token;
            while (std::getline(stream, token, ',')) {
                if (!token.empty()) {
                    out.push_back(token);
                }
            }
            return out;
        };
        const auto report = [&](const util::Result<void>& result, const char* done) {
            if (!result) {
                cli::renderError(std::cerr, result.error(), palette);
                return exitCodeFor(result.error());
            }
            std::cout << done << "\n";
            return kExitSuccess;
        };

        // --- 작업자 ---
        if (workerAdd->parsed()) {
            std::vector<domain::Weekday> weeklyOff;
            if (!weeklyOption.empty() && !parseWeekdayList(weeklyOption, weeklyOff)) {
                return kExitInvalidUsage;
            }
            const util::Result<domain::Worker> added =
                app::addWorker(dataDir, model, nameOption, idOption, weeklyOff);
            if (!added) {
                cli::renderError(std::cerr, added.error(), palette);
                return exitCodeFor(added.error());
            }
            std::cout << "작업자를 추가했습니다: " << added.value().name << " ("
                      << added.value().id.str() << ")\n";
            return kExitSuccess;
        }
        if (workerList->parsed()) {
            cli::renderWorkers(std::cout, model, allFlag, palette);
            return kExitSuccess;
        }
        if (workerRm->parsed() || workerEdit->parsed()) {
            domain::WorkerId workerId;
            if (const int code = resolveWorkerOrExplain(model, workerOption, workerId);
                code != kExitSuccess) {
                return code;
            }
            if (workerRm->parsed()) {
                if (hardFlag) {
                    std::cout << "과거 배정 기록이 이 ID 를 참조합니다. 지우면 그 기록에서 "
                                 "이름을 찾을 수 없게 됩니다.\n";
                }
                const util::Result<void> removed =
                    app::removeWorker(dataDir, model, workerId, hardFlag);
                return report(removed, hardFlag ? "작업자를 삭제했습니다."
                                                : "작업자를 배정 대상에서 제외했습니다. "
                                                  "(--hard 를 주면 정말 지웁니다)");
            }
            std::optional<bool> active;
            if (!activeOption.empty()) {
                if (activeOption != "true" && activeOption != "false") {
                    std::cerr << "--active 는 true 또는 false 여야 합니다.\n";
                    return kExitInvalidUsage;
                }
                active = (activeOption == "true");
            }
            std::optional<std::string> newName;
            if (!nameOption.empty()) {
                newName = nameOption;
            }
            return report(app::editWorker(dataDir, model, workerId, newName, active),
                          "작업자를 수정했습니다.");
        }

        // --- 작업 ---
        if (taskAdd->parsed()) {
            app::TaskSpec spec;
            spec.id = idOption;
            spec.name = nameOption;
            spec.requiredCount = countOption;
            for (const std::string& id : splitIds(setsOption)) {
                spec.taskSetIds.push_back(domain::TaskSetId{id});
            }
            for (const std::string& id : splitIds(conflictsOption)) {
                spec.conflictsWith.push_back(domain::TaskId{id});
            }
            if (!weekdaysOption.empty() && !parseWeekdayList(weekdaysOption, spec.weekdays)) {
                return kExitInvalidUsage;
            }
            return report(app::addTask(dataDir, model, spec), "작업을 추가했습니다.");
        }
        if (taskList->parsed()) {
            cli::renderTasks(std::cout, model, setsOption, palette);
            return kExitSuccess;
        }
        if (taskRm->parsed()) {
            return report(app::removeTask(dataDir, model, domain::TaskId{idOption}),
                          "작업을 지웠습니다.");
        }
        if (taskEdit->parsed()) {
            std::optional<std::string> newName;
            std::optional<int> newCount;
            std::optional<std::vector<domain::TaskId>> newConflicts;
            std::optional<std::vector<domain::Weekday>> newWeekdays;
            std::optional<std::vector<domain::TaskSetId>> newSets;
            if (!nameOption.empty()) {
                newName = nameOption;
            }
            // 값이 아니라 옵션이 주어졌는지로 판정한다. 값으로 보면 --count 0 이 조용히
            // 무시되어 "수정했습니다" 만 나가고 아무것도 안 바뀐다.
            if (taskEdit->count("--count") > 0) {
                newCount = countOption;
            }
            if (taskEdit->count("--conflicts") > 0) {
                std::vector<domain::TaskId> ids;
                for (const std::string& id : splitIds(conflictsOption)) {
                    ids.push_back(domain::TaskId{id});
                }
                newConflicts = ids;
            }
            if (taskEdit->count("--weekdays") > 0) {
                std::vector<domain::Weekday> days;
                if (!weekdaysOption.empty() && !parseWeekdayList(weekdaysOption, days)) {
                    return kExitInvalidUsage;
                }
                newWeekdays = days;
            }
            if (taskEdit->count("--set") > 0) {
                std::vector<domain::TaskSetId> ids;
                for (const std::string& id : splitIds(setsOption)) {
                    ids.push_back(domain::TaskSetId{id});
                }
                newSets = ids;
            }
            return report(app::editTask(dataDir, model, domain::TaskId{idOption}, newName,
                                        newCount, newConflicts, newWeekdays, newSets),
                          "작업을 수정했습니다.");
        }

        // --- 작업집합 / 분류 / 시간대 ---
        if (setAdd->parsed()) {
            return report(app::addTaskSet(dataDir, model, idOption, nameOption),
                          "작업집합을 추가했습니다.");
        }
        if (setList->parsed()) {
            cli::renderTaskSets(std::cout, model, palette);
            return kExitSuccess;
        }
        if (setRm->parsed()) {
            return report(app::removeTaskSet(dataDir, model, domain::TaskSetId{idOption}),
                          "작업집합을 지웠습니다.");
        }
        if (catAdd->parsed()) {
            std::vector<domain::TaskSetId> sets;
            for (const std::string& id : splitIds(setsOption)) {
                sets.push_back(domain::TaskSetId{id});
            }
            return report(app::addCategory(dataDir, model, idOption, nameOption, sets),
                          "분류를 추가했습니다.");
        }
        if (catList->parsed()) {
            cli::renderCategories(std::cout, model, palette);
            return kExitSuccess;
        }
        if (catRm->parsed()) {
            return report(app::removeCategory(dataDir, model, domain::CategoryId{idOption}),
                          "분류를 지웠습니다.");
        }
        if (slotAdd->parsed()) {
            app::TimeSlotSpec spec;
            spec.id = idOption;
            spec.displayName = nameOption;
            spec.start = startOption;
            spec.end = endOption;
            for (const std::string& id : splitIds(catsOption)) {
                spec.categoryIds.push_back(domain::CategoryId{id});
            }
            if (!weekdaysOption.empty() && !parseWeekdayList(weekdaysOption, spec.weekdays)) {
                return kExitInvalidUsage;
            }
            return report(app::addTimeSlot(dataDir, model, spec), "시간대를 추가했습니다.");
        }
        if (slotList->parsed()) {
            cli::renderTimeSlots(std::cout, model, palette);
            return kExitSuccess;
        }
        if (slotRm->parsed()) {
            return report(app::removeTimeSlot(dataDir, model, domain::TimeSlotId{idOption}),
                          "시간대를 지웠습니다.");
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
