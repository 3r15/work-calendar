#include "core/storage/atomic_write.h"

#include <cstdio>
#include <fstream>
#include <system_error>

namespace storage {
namespace {

bool renameReplace(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
}

ReplaceFileFn g_replaceFile = &renameReplace;

util::Error ioError(const std::filesystem::path& path, std::string what, std::string hint = {}) {
    return util::makeError(util::ErrorCode::Io, path.string() + " " + std::move(what),
                           std::move(hint));
}

}  // namespace

void setReplaceFile(ReplaceFileFn fn) {
    g_replaceFile = (fn != nullptr) ? fn : &renameReplace;
}

ReplaceFileFn replaceFile() {
    return g_replaceFile;
}

util::Result<void> atomicWrite(const std::filesystem::path& target, std::string_view content) {
    std::filesystem::path tmp = target;
    tmp += ".tmp";
    std::filesystem::path bak = target;
    bak += ".bak";

    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            return ioError(target.parent_path(), "폴더를 만들 수 없습니다.",
                           "폴더 권한과 남은 디스크 공간을 확인해 주세요.");
        }
    }

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return ioError(tmp, "임시 파일을 열 수 없습니다.",
                           "폴더 권한과 남은 디스크 공간을 확인해 주세요.");
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return ioError(tmp, "임시 파일에 쓰지 못했습니다.", "남은 디스크 공간을 확인해 주세요.");
        }
    }

    // 교체 직전에 한 세대만 남긴다. 백업 실패로 저장 자체를 막지는 않되, 조용히 넘어가지도 않는다.
    if (std::filesystem::exists(target, ec)) {
        std::filesystem::remove(bak, ec);
        std::filesystem::copy_file(target, bak, std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return ioError(bak, "백업 파일을 만들 수 없습니다.",
                           "폴더 권한을 확인해 주세요. 원본은 그대로 남아 있습니다.");
        }
    }

    if (!g_replaceFile(tmp, target)) {
        // 교체에 실패했으면 원본은 손대지 않은 상태다. 임시 파일만 치운다.
        std::filesystem::remove(tmp, ec);
        return ioError(target, "파일을 교체하지 못했습니다.",
                       "다른 프로그램이 이 파일을 열고 있는지 확인해 주세요. "
                       "원본은 그대로 남아 있습니다.");
    }

    return {};
}

}  // namespace storage
