#include "core/storage/atomic_write.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

// 테스트마다 고유한 폴더를 쓰고 끝나면 지운다.
class TempDir {
public:
    explicit TempDir(const std::string& name)
        : path_(fs::temp_directory_path() / ("sched_test_" + name)) {
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

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeRaw(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

// 교체 단계에서 죽은 것처럼 흉내 낸다.
bool alwaysFail(const fs::path&, const fs::path&) {
    return false;
}

// setReplaceFile 을 건드린 테스트가 다른 테스트를 오염시키지 않게 되돌린다.
class ScopedReplaceFile {
public:
    explicit ScopedReplaceFile(storage::ReplaceFileFn fn) : previous_(storage::replaceFile()) {
        storage::setReplaceFile(fn);
    }
    ~ScopedReplaceFile() { storage::setReplaceFile(previous_); }
    ScopedReplaceFile(const ScopedReplaceFile&) = delete;
    ScopedReplaceFile& operator=(const ScopedReplaceFile&) = delete;

private:
    storage::ReplaceFileFn previous_;
};

}  // namespace

// 새 파일을 만든다.
TEST_CASE("creates a new file", "[atomic_write]") {
    TempDir dir{"atomic_new"};
    const fs::path target = dir.path() / "config.json";

    const util::Result<void> result = storage::atomicWrite(target, "{\"버전\": 1}\n");

    REQUIRE(result.ok());
    REQUIRE(fs::exists(target));
    REQUIRE(readAll(target) == "{\"버전\": 1}\n");
    // 원본이 없었으므로 백업도 없다.
    REQUIRE_FALSE(fs::exists(dir.path() / "config.json.bak"));
    // 임시 파일은 남지 않는다.
    REQUIRE_FALSE(fs::exists(dir.path() / "config.json.tmp"));
}

// 덮어쓸 때 .bak 한 세대를 남긴다 (D-008).
TEST_CASE("keeps one generation of backup", "[atomic_write]") {
    TempDir dir{"atomic_backup"};
    const fs::path target = dir.path() / "workers.json";
    writeRaw(target, "첫 번째");

    REQUIRE(storage::atomicWrite(target, "두 번째").ok());
    REQUIRE(readAll(target) == "두 번째");
    REQUIRE(readAll(dir.path() / "workers.json.bak") == "첫 번째");

    SECTION("다시 쓰면 백업도 한 세대만 유지된다") {
        REQUIRE(storage::atomicWrite(target, "세 번째").ok());
        REQUIRE(readAll(target) == "세 번째");
        REQUIRE(readAll(dir.path() / "workers.json.bak") == "두 번째");
    }
}

// 완료 기준 3번: 쓰기 도중 중단되어도 원본이 손상되지 않는다.
TEST_CASE("original survives an interrupted write", "[atomic_write]") {
    TempDir dir{"atomic_interrupt"};
    const fs::path target = dir.path() / "tasks.json";
    const std::string original = "{\"tasks\": [\"청소기\"]}\n";
    writeRaw(target, original);

    SECTION("교체 직전에 죽으면 원본과 백업이 모두 온전하다") {
        const ScopedReplaceFile guard{&alwaysFail};
        const util::Result<void> result = storage::atomicWrite(target, "망가진 내용");

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.error().code == util::ErrorCode::Io);
        REQUIRE_FALSE(result.error().message.empty());
        REQUIRE_FALSE(result.error().hint.empty());

        // 원본은 한 글자도 바뀌지 않았다.
        REQUIRE(readAll(target) == original);
        // 실패한 임시 파일이 남아 다음 실행을 헷갈리게 하지 않는다.
        REQUIRE_FALSE(fs::exists(dir.path() / "tasks.json.tmp"));
        // 백업은 원본과 같은 내용이므로 되돌릴 수 있다.
        REQUIRE(readAll(dir.path() / "tasks.json.bak") == original);
    }

    SECTION("교체가 성공하면 원본이 새 내용으로 바뀐다") {
        REQUIRE(storage::atomicWrite(target, "새 내용").ok());
        REQUIRE(readAll(target) == "새 내용");
    }
}

// 없는 폴더에도 쓸 수 있어야 한다. state/ 는 처음 실행 때 없다.
TEST_CASE("creates missing parent directories", "[atomic_write]") {
    TempDir dir{"atomic_mkdir"};
    const fs::path target = dir.path() / "state" / "rr-cursor.json";

    REQUIRE(storage::atomicWrite(target, "{}\n").ok());
    REQUIRE(fs::exists(target));
}

// 한글이 섞인 내용을 바이트 그대로 보존한다.
TEST_CASE("preserves utf8 bytes exactly", "[atomic_write]") {
    TempDir dir{"atomic_utf8"};
    const fs::path target = dir.path() / "absences.json";
    const std::string content = "{\"reason\": \"연차\", \"name\": \"김철수\"}\n";

    REQUIRE(storage::atomicWrite(target, content).ok());
    REQUIRE(readAll(target) == content);
    REQUIRE(readAll(target).size() == content.size());
}
