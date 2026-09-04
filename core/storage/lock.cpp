#include "core/storage/lock.h"

#include "core/storage/atomic_write.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>

namespace storage {
namespace {

using nlohmann::json;

std::optional<LockInfo> readLock(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    try {
        const json root = json::parse(buffer.str());
        LockInfo info;
        if (root.contains("pid") && root["pid"].is_number_integer()) {
            info.pid = root["pid"].get<long>();
        }
        if (root.contains("acquiredAt") && root["acquiredAt"].is_string()) {
            info.acquiredAt = root["acquiredAt"].get<std::string>();
        }
        return info;
    } catch (const json::parse_error&) {
        // 깨진 락 파일은 없는 것으로 본다. 락 때문에 프로그램을 못 쓰게 되면 안 된다.
        return std::nullopt;
    }
}

// ISO 8601 문자열 두 개를 비교해 몇 분 지났는지 센다.
// 문자열이 이상하면 "아주 오래됐다" 고 보고 락을 무시하게 한다.
long minutesSince(const std::string& iso, const util::DateTime& now) {
    if (iso.size() < 16) {
        return kStaleLockMinutes + 1;
    }
    const std::optional<util::Date> date = util::Date::parse(iso.substr(0, 10));
    const std::optional<util::TimeOfDay> time = util::TimeOfDay::parse(iso.substr(11, 5));
    if (!date.has_value() || !time.has_value()) {
        return kStaleLockMinutes + 1;
    }
    const auto toDays = [](const util::Date& value) {
        return std::chrono::sys_days{value.ymd()}.time_since_epoch().count();
    };
    const long days = static_cast<long>(toDays(now.date) - toDays(*date));
    return days * 24 * 60 + static_cast<long>(now.time.minutes() - time->minutes());
}

}  // namespace

std::filesystem::path lockPath(const std::filesystem::path& dataDir) {
    return dataDir / "state" / ".lock";
}

ScopedLock::~ScopedLock() {
    release();
}

util::Result<void> ScopedLock::acquire(const std::filesystem::path& dataDir, long pid,
                                       const util::DateTime& now, AliveFn isAlive) {
    const std::filesystem::path path = lockPath(dataDir);

    if (const std::optional<LockInfo> existing = readLock(path)) {
        const long age = minutesSince(existing->acquiredAt, now);
        const bool stale = age > kStaleLockMinutes || age < 0;
        const bool alive = (isAlive != nullptr) && isAlive(existing->pid);

        // 자기 자신이 남긴 락은 이어받는다. 같은 프로세스가 두 번 잡는 것은 문제가 아니다.
        if (existing->pid != pid && alive && !stale) {
            return util::makeError(
                util::ErrorCode::Conflict,
                "다른 창에서 이 데이터 폴더를 쓰고 있습니다 (프로세스 " +
                    std::to_string(existing->pid) + ", " + existing->acquiredAt + " 부터).",
                "그 창의 작업이 끝난 뒤 다시 실행해 주세요.\n"
                "그 창을 이미 닫으셨다면 " + path.string() + " 파일을 지우면 됩니다.");
        }
    }

    json root;
    root["pid"] = pid;
    root["acquiredAt"] = util::formatIso8601(now);
    if (const util::Result<void> written = atomicWrite(path, root.dump(2) + "\n"); !written) {
        return written.error();
    }

    path_ = path;
    held_ = true;
    return {};
}

void ScopedLock::release() {
    if (!held_) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(path_, ec);
    // 지우지 못해도 조용히 넘어간다. 10분 뒤에는 오래된 락으로 무시된다.
    held_ = false;
}

}  // namespace storage
