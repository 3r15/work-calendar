#include "core/storage/lock.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

util::DateTime moment(unsigned day, int hour, int minute) {
    util::DateTime out;
    out.date = util::Date::fromYmd(2026, 9, day);
    out.time = util::TimeOfDay::fromHm(hour, minute);
    out.utcOffsetMinutes = 540;
    return out;
}

class TempDir {
public:
    explicit TempDir(const std::string& name)
        : path_(fs::temp_directory_path() / ("sched_lock_" + name)) {
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

bool alwaysAlive(long) { return true; }
bool neverAlive(long) { return false; }

}  // namespace

TEST_CASE("acquires and releases a lock", "[lock]") {
    TempDir dir{"basic"};
    storage::ScopedLock lock;

    REQUIRE(lock.acquire(dir.path(), 1234, moment(4, 9, 0), &alwaysAlive).ok());
    REQUIRE(lock.held());
    REQUIRE(fs::exists(storage::lockPath(dir.path())));

    lock.release();
    REQUIRE_FALSE(lock.held());
    REQUIRE_FALSE(fs::exists(storage::lockPath(dir.path())));
}

TEST_CASE("scope exit releases the lock", "[lock]") {
    TempDir dir{"scope"};
    {
        storage::ScopedLock lock;
        REQUIRE(lock.acquire(dir.path(), 1234, moment(4, 9, 0), &alwaysAlive).ok());
        REQUIRE(fs::exists(storage::lockPath(dir.path())));
    }
    REQUIRE_FALSE(fs::exists(storage::lockPath(dir.path())));
}

// 완료 기준 2번: 두 프로세스가 동시에 쓰기를 시도하면 두 번째가 안전하게 중단.
TEST_CASE("a second live process is refused", "[lock]") {
    TempDir dir{"conflict"};
    storage::ScopedLock first;
    REQUIRE(first.acquire(dir.path(), 1111, moment(4, 9, 0), &alwaysAlive).ok());

    storage::ScopedLock second;
    const util::Result<void> refused =
        second.acquire(dir.path(), 2222, moment(4, 9, 1), &alwaysAlive);

    REQUIRE_FALSE(refused.ok());
    REQUIRE(refused.error().code == util::ErrorCode::Conflict);
    // 누가 잡고 있는지와 어떻게 풀지를 함께 알려준다.
    REQUIRE(refused.error().message.find("1111") != std::string::npos);
    REQUIRE(refused.error().hint.find(".lock") != std::string::npos);
    REQUIRE_FALSE(second.held());
    // 남의 락을 지우지 않았다.
    REQUIRE(fs::exists(storage::lockPath(dir.path())));
}

TEST_CASE("the same process can take its own lock again", "[lock]") {
    TempDir dir{"same_pid"};
    storage::ScopedLock first;
    REQUIRE(first.acquire(dir.path(), 1111, moment(4, 9, 0), &alwaysAlive).ok());

    storage::ScopedLock again;
    REQUIRE(again.acquire(dir.path(), 1111, moment(4, 9, 1), &alwaysAlive).ok());
}

// 강제 종료로 남은 락 때문에 프로그램을 영영 못 쓰게 되면 안 된다.
TEST_CASE("a stale lock is ignored", "[lock]") {
    TempDir dir{"stale"};
    storage::ScopedLock old;
    REQUIRE(old.acquire(dir.path(), 1111, moment(4, 9, 0), &alwaysAlive).ok());

    storage::ScopedLock fresh;
    SECTION("10분이 지나면 무시한다") {
        REQUIRE(fresh.acquire(dir.path(), 2222, moment(4, 9, 11), &alwaysAlive).ok());
    }
    SECTION("10분 안이면 아직 유효하다") {
        REQUIRE_FALSE(fresh.acquire(dir.path(), 2222, moment(4, 9, 9), &alwaysAlive).ok());
    }
    SECTION("프로세스가 죽었으면 시간과 무관하게 무시한다") {
        REQUIRE(fresh.acquire(dir.path(), 2222, moment(4, 9, 1), &neverAlive).ok());
    }
}

TEST_CASE("a broken lock file does not block", "[lock]") {
    TempDir dir{"broken"};
    const fs::path path = storage::lockPath(dir.path());
    fs::create_directories(path.parent_path());
    {
        std::ofstream out(path);
        out << "이건 JSON 이 아니다";
    }

    storage::ScopedLock lock;
    REQUIRE(lock.acquire(dir.path(), 1234, moment(4, 9, 0), &alwaysAlive).ok());
}
