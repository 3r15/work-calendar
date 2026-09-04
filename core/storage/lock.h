#pragma once

#include "core/util/date.h"
#include "core/util/result.h"

#include <filesystem>
#include <string>

namespace storage {

// 한 데이터 폴더를 여러 프로세스가 동시에 쓰는 것은 지원하지 않는다. 다만 감지는 한다 (D-007).
//
// 완전한 동시성 제어는 1인 사용에 과하다. 하지만 CLI 창을 두 개 열어놓는 실수는 충분히
// 일어나고, 그때 데이터가 깨지면 복구가 어렵다. 락 파일 하나면 그 실수의 대부분을 막는다.
//
// 락이 오래되면 죽은 프로세스가 남긴 것으로 보고 무시한다. 프로그램이 강제 종료되면 락 파일이
// 남는데, 그것 때문에 영영 못 쓰게 되면 안 되기 때문이다.
inline constexpr int kStaleLockMinutes = 10;

// 락 파일 내용. 사람이 열어봐도 알아볼 수 있게 JSON 으로 적는다.
struct LockInfo {
    long pid{0};
    std::string acquiredAt;  // ISO 8601
};

// 락을 잡는다. 이미 유효한 락이 있으면 실패하고 누가 잡고 있는지 알린다.
//
// isAlive 는 PID 가 살아 있는지 판단하는 함수다. 플랫폼 호출이라 주입받는다 —
// 덕분에 core 가 platform 을 모르고도 죽은 락을 걸러낼 수 있고, 테스트가 이 판정을 흉내 낼 수 있다.
class ScopedLock {
public:
    using AliveFn = bool (*)(long pid);

    ScopedLock() = default;
    ~ScopedLock();
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;

    util::Result<void> acquire(const std::filesystem::path& dataDir, long pid,
                               const util::DateTime& now, AliveFn isAlive);
    void release();

    bool held() const { return held_; }

private:
    std::filesystem::path path_;
    bool held_{false};
};

// state/.lock
std::filesystem::path lockPath(const std::filesystem::path& dataDir);

}  // namespace storage
