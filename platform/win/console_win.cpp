#include "platform/console.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace platform {

bool initConsole() {
    // 출력과 입력을 모두 바꿔야 한다. 출력만 바꾸면 한글 인자를 받을 때 깨진다.
    const BOOL out = SetConsoleOutputCP(CP_UTF8);
    const BOOL in = SetConsoleCP(CP_UTF8);
    return out != 0 && in != 0;
}

bool enableAnsi() {
    const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD mode = 0;
    // 출력이 파일이나 파이프로 리다이렉트되면 여기서 실패한다. 색을 끄면 될 뿐 오류가 아니다.
    if (GetConsoleMode(handle, &mode) == 0) {
        return false;
    }
    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
        return true;
    }
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

}  // namespace platform
