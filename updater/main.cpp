// 실행 중인 exe 를 교체하는 별도 프로그램.
//
// Windows 는 실행 중인 exe 를 덮어쓸 수 없다. 그래서 sched.exe 가 자기 자신을 갈아끼울 수 없고,
// 이 작은 프로그램이 부모가 끝나기를 기다렸다가 교체한다 (DESIGN 7.2).
//
// 순서:
//   1. 부모 프로세스(sched.exe)가 끝나기를 기다린다
//   2. 새 파일들을 제자리로 옮긴다
//   3. sched.exe 를 다시 띄운다
//
// data/ 는 절대 건드리지 않는다. 교체 대상은 실행 파일과 리소스뿐이다.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// 부모가 끝나기를 기다린다. 영원히 기다리지는 않는다 — 부모가 멈춰 있으면 사용자가 알아야 한다.
constexpr DWORD kWaitMillis = 30000;

bool waitForParent(DWORD pid) {
    const HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (handle == nullptr) {
        // 이미 끝났거나 볼 수 없다. 어느 쪽이든 교체를 막을 이유는 아니다.
        return true;
    }
    const DWORD result = WaitForSingleObject(handle, kWaitMillis);
    CloseHandle(handle);
    return result == WAIT_OBJECT_0;
}

std::string utf8Of(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), length,
                        nullptr, nullptr);
    return out;
}

// 새 파일을 제자리로. 실패하면 되돌릴 수 있도록 원본을 .old 로 밀어둔다.
struct Replacement {
    fs::path target;
    fs::path staged;
    fs::path backup;
};

bool applyOne(Replacement& item) {
    std::error_code ec;
    if (fs::exists(item.target)) {
        item.backup = item.target;
        item.backup += ".old";
        fs::remove(item.backup, ec);
        fs::rename(item.target, item.backup, ec);
        if (ec) {
            return false;
        }
    }
    fs::rename(item.staged, item.target, ec);
    if (ec) {
        // 되돌린다. 반쯤 바뀐 상태로 두면 프로그램이 아예 안 뜬다.
        if (!item.backup.empty()) {
            std::error_code rollback;
            fs::rename(item.backup, item.target, rollback);
        }
        return false;
    }
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);

    // 인자: <부모 PID> <새 파일이 있는 폴더> <설치 폴더> [재실행할 exe]
    if (argc < 4) {
        std::cout << "이 프로그램은 sched 가 직접 실행합니다.\n";
        std::cout << "사용법: updater <부모PID> <새파일폴더> <설치폴더> [재실행할exe]\n";
        return 2;
    }

    const DWORD parentPid = static_cast<DWORD>(std::stoul(argv[1]));
    const fs::path stagingDir{argv[2]};
    const fs::path installDir{argv[3]};
    const fs::path relaunch = (argc >= 5) ? fs::path{argv[4]} : (installDir / L"sched.exe");

    if (!waitForParent(parentPid)) {
        std::cout << "sched 가 아직 실행 중입니다. 창을 모두 닫은 뒤 다시 시도해 주세요.\n";
        return 1;
    }

    std::error_code ec;
    if (!fs::exists(stagingDir, ec)) {
        std::cout << "받아 둔 새 파일을 찾을 수 없습니다: " << utf8Of(stagingDir.wstring())
                  << "\n";
        return 1;
    }

    std::vector<Replacement> applied;
    for (const auto& entry : fs::directory_iterator(stagingDir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        Replacement item;
        item.staged = entry.path();
        item.target = installDir / entry.path().filename();
        if (!applyOne(item)) {
            std::cout << "교체에 실패했습니다: " << utf8Of(item.target.filename().wstring())
                      << "\n";
            std::cout << "이전 파일은 그대로 있습니다. 다시 시도해 주세요.\n";
            return 1;
        }
        applied.push_back(item);
    }

    if (applied.empty()) {
        std::cout << "옮길 파일이 없습니다.\n";
        return 1;
    }

    // 교체가 끝난 뒤에 .old 를 치운다. 중간에 실패하면 되돌릴 것이 있어야 한다.
    for (const Replacement& item : applied) {
        if (!item.backup.empty()) {
            fs::remove(item.backup, ec);
        }
    }
    fs::remove_all(stagingDir, ec);

    std::cout << "업데이트를 마쳤습니다. 프로그램을 다시 시작합니다.\n";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + relaunch.wstring() + L"\"";
    if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       installDir.c_str(), &startup, &process) != 0) {
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
    } else {
        std::cout << "다시 시작하지 못했습니다. sched 를 직접 실행해 주세요.\n";
    }
    return 0;
}
