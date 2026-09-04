#include "platform/paths.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace platform {

std::filesystem::path executableDir() {
    // 경로 길이 제한이 풀린 환경에서는 MAX_PATH 를 넘을 수 있으므로 버퍼를 늘려 가며 시도한다.
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD written =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            std::error_code ec;
            return std::filesystem::current_path(ec);
        }
        if (written < buffer.size()) {
            return std::filesystem::path{std::wstring{buffer.data(), written}}.parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}

}  // namespace platform
