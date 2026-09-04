#include "core/util/result.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using util::Error;
using util::ErrorCode;
using util::makeError;
using util::Result;

namespace {

Result<int> parsePositive(int input) {
    if (input <= 0) {
        return makeError(ErrorCode::InvalidUsage, "0보다 큰 수를 넣어 주세요.",
                         "예: sched task edit --count 2");
    }
    return input;
}

Result<void> checkEmpty(const std::vector<int>& values) {
    if (values.empty()) {
        return makeError(ErrorCode::DataFile, "목록이 비어 있습니다.");
    }
    return {};
}

}  // namespace

// 성공은 값을, 실패는 오류를 담는다.
TEST_CASE("holds either a value or an error", "[result]") {
    const Result<int> good = parsePositive(3);
    REQUIRE(good.ok());
    REQUIRE(static_cast<bool>(good));
    REQUIRE(good.value() == 3);

    const Result<int> bad = parsePositive(0);
    REQUIRE_FALSE(bad.ok());
    REQUIRE_FALSE(static_cast<bool>(bad));
    REQUIRE(bad.error().code == ErrorCode::InvalidUsage);
}

// 오류 메시지는 무엇이 잘못됐고(message) 어떻게 고치는지(hint)를 담는다.
TEST_CASE("error carries message and hint", "[result]") {
    const Result<int> bad = parsePositive(-1);
    REQUIRE(bad.error().message == "0보다 큰 수를 넣어 주세요.");
    REQUIRE(bad.error().hint == "예: sched task edit --count 2");
}

// hint 는 없어도 된다.
TEST_CASE("hint is optional", "[result]") {
    const Result<void> bad = checkEmpty({});
    REQUIRE_FALSE(bad.ok());
    REQUIRE(bad.error().hint.empty());

    const Result<void> good = checkEmpty({1});
    REQUIRE(good.ok());
}

// 값을 옮길 수 있다.
TEST_CASE("value can be moved out", "[result]") {
    Result<std::string> r{std::string("청소기")};
    REQUIRE(r.ok());
    const std::string taken = std::move(r).value();
    REQUIRE(taken == "청소기");
}

// valueOr 은 실패했을 때 대체값을 준다.
TEST_CASE("valueOr falls back on error", "[result]") {
    REQUIRE(parsePositive(7).valueOr(1) == 7);
    REQUIRE(parsePositive(0).valueOr(1) == 1);
}
