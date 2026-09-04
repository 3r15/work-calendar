#include "platform/process.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace platform {

long processId() {
    return static_cast<long>(GetCurrentProcessId());
}

bool processAlive(long pid) {
    if (pid <= 0) {
        return false;
    }
    const HANDLE handle =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (handle == nullptr) {
        // 권한이 없어서 못 여는 경우도 있다. 그때는 살아 있다고 본다 —
        // 남의 락을 함부로 깨는 것보다 한 번 더 물어보게 하는 편이 안전하다.
        return GetLastError() == ERROR_ACCESS_DENIED;
    }

    DWORD exitCode = 0;
    const bool running =
        GetExitCodeProcess(handle, &exitCode) != 0 && exitCode == STILL_ACTIVE;
    CloseHandle(handle);
    return running;
}

}  // namespace platform
