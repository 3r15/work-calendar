#include "core/app/data_dir.h"

#include <cstdlib>

namespace app {

std::filesystem::path resolveDataDir(const std::optional<std::string>& fromOption,
                                     const std::filesystem::path& executableDir) {
    if (fromOption.has_value() && !fromOption->empty()) {
        return std::filesystem::path{*fromOption};
    }
    if (const char* fromEnv = std::getenv("SCHED_DATA_DIR");
        fromEnv != nullptr && fromEnv[0] != '\0') {
        return std::filesystem::path{fromEnv};
    }
    // %APPDATA% 를 쓰지 않는 이유는 D-011 에 있다. 압축을 풀면 바로 쓰고, 폴더째 백업한다.
    return executableDir / "data";
}

}  // namespace app
