#include "core/util/sha256.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// FIPS 180-4 와 널리 쓰이는 표준 시험값. 직접 구현했으므로 여기서 어긋나면 전부 무의미하다.
TEST_CASE("matches the published test vectors", "[sha256]") {
    REQUIRE(util::sha256Hex("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(util::sha256Hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(util::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    REQUIRE(util::sha256Hex("The quick brown fox jumps over the lazy dog") ==
            "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

// 패딩 경계. 55/56/63/64 바이트에서 블록 처리가 갈린다.
TEST_CASE("handles padding boundaries", "[sha256]") {
    REQUIRE(util::sha256Hex(std::string(55, 'a')) ==
            "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    REQUIRE(util::sha256Hex(std::string(56, 'a')) ==
            "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    REQUIRE(util::sha256Hex(std::string(63, 'a')) ==
            "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
    REQUIRE(util::sha256Hex(std::string(64, 'a')) ==
            "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    REQUIRE(util::sha256Hex(std::string(1000, 'a')) ==
            "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

TEST_CASE("hashes utf8 bytes as bytes", "[sha256]") {
    // 한글이 섞여도 바이트열로만 다룬다.
    const std::string korean = "김철수";
    REQUIRE(util::sha256Hex(korean).size() == 64);
    REQUIRE(util::sha256Hex(korean) != util::sha256Hex("김철수 "));
}

TEST_CASE("hashes a file in chunks", "[sha256]") {
    const fs::path temp = fs::temp_directory_path() / "sched_sha_test.bin";
    {
        std::ofstream out(temp, std::ios::binary);
        // 64KB 조각 경계를 넘겨 조각 처리 자체를 검증한다.
        for (int i = 0; i < 5000; ++i) {
            out << "abcdefghijklmnopqrstuvwxyz";
        }
    }

    std::string expected;
    {
        std::ifstream in(temp, std::ios::binary);
        expected = util::sha256Hex(
            std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()});
    }

    const std::optional<std::string> actual = util::sha256HexOfFile(temp);
    REQUIRE(actual.has_value());
    REQUIRE(*actual == expected);

    std::error_code ec;
    fs::remove(temp, ec);
}

TEST_CASE("missing file gives no value", "[sha256]") {
    REQUIRE_FALSE(util::sha256HexOfFile(fs::temp_directory_path() / "sched_no_such_file").has_value());
}

// 체크섬 파일마다 대소문자와 공백 표기가 다르다.
TEST_CASE("checksum comparison ignores case and spacing", "[sha256]") {
    const std::string digest = util::sha256Hex("abc");
    REQUIRE(util::checksumMatches(digest, digest));
    REQUIRE(util::checksumMatches("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
                                  digest));
    REQUIRE(util::checksumMatches("  " + digest + "\n", digest));
    REQUIRE_FALSE(util::checksumMatches("", digest));
    REQUIRE_FALSE(util::checksumMatches(digest, ""));
    REQUIRE_FALSE(util::checksumMatches(util::sha256Hex("abd"), digest));
}
