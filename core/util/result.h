#pragma once

#include <cassert>
#include <string>
#include <utility>
#include <variant>

namespace util {

// 종료 코드는 CLI-SPEC.md 의 표를 따른다. 매핑은 cli/ 에서 하고 core 는 원인만 분류한다.
enum class ErrorCode {
    Unknown,       // 분류되지 않은 실패
    InvalidUsage,  // 인자가 잘못됨 (종료 코드 2)
    DataFile,      // 파싱 실패, 검증 실패 (종료 코드 3)
    NotFound,      // 참조한 ID 나 파일이 없음
    Conflict,      // 이미 존재하거나 잠겨 있음
    Io,            // 읽기·쓰기 실패
};

// 사용자에게 그대로 보여줄 수 있는 오류. 기술 용어 대신 사람 말로 씁니다 (CLI-SPEC.md 오류 메시지).
struct Error {
    ErrorCode code{ErrorCode::Unknown};
    std::string message;  // 무엇이 잘못됐는지
    std::string hint;     // 어떻게 고치는지. 비어 있을 수 있다
};

inline Error makeError(ErrorCode code, std::string message, std::string hint = {}) {
    return Error{code, std::move(message), std::move(hint)};
}

// 예외 대신 쓰는 반환 타입 (CLAUDE.md 7장).
// 배정 실패처럼 "정상적인 실패" 는 Result 가 아니라 결과 자료구조로 표현한다 — 미배정 슬롯이 그 예다.
template <typename T>
class Result {
public:
    Result(T value) : store_(std::move(value)) {}
    Result(Error error) : store_(std::move(error)) {}

    bool ok() const noexcept { return std::holds_alternative<T>(store_); }
    explicit operator bool() const noexcept { return ok(); }

    // ok() 가 true 일 때만 부른다. 아니면 프로그래밍 오류다.
    T& value() & {
        assert(ok());
        return std::get<T>(store_);
    }
    const T& value() const& {
        assert(ok());
        return std::get<T>(store_);
    }
    T&& value() && {
        assert(ok());
        return std::get<T>(std::move(store_));
    }

    const Error& error() const& {
        assert(!ok());
        return std::get<Error>(store_);
    }

    // 실패했을 때 쓸 대체값. 오류를 삼키므로 정말 기본값이 맞는 자리에서만 쓴다.
    T valueOr(T fallback) const& { return ok() ? std::get<T>(store_) : std::move(fallback); }

private:
    std::variant<T, Error> store_;
};

// 성공에 값이 없는 경우.
template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)), failed_(true) {}

    bool ok() const noexcept { return !failed_; }
    explicit operator bool() const noexcept { return ok(); }

    const Error& error() const& {
        assert(!ok());
        return error_;
    }

private:
    Error error_{};
    bool failed_{false};
};

}  // namespace util
