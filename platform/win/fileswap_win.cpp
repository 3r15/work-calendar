#include "platform/fileswap.h"

#include "core/storage/atomic_write.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>

namespace platform {
namespace {

bool replaceWithWinApi(const std::filesystem::path& from, const std::filesystem::path& to) {
    // 대상이 아직 없으면 교체할 것이 없으므로 그냥 옮긴다.
    if (!std::filesystem::exists(to)) {
        return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
    }
    // 백업은 storage 가 이미 만들었으므로 여기서는 만들지 않는다.
    return ReplaceFileW(to.c_str(), from.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr,
                        nullptr) != 0;
}

}  // namespace

void installFileSwap() {
    storage::setReplaceFile(&replaceWithWinApi);
}

}  // namespace platform
