#pragma once

#include <string>
#include <string_view>

namespace cli {

// ANSI 색상. 색을 쓸 수 없는 곳(파이프, 색을 끈 경우)에서는 전부 빈 문자열이 되어
// 출력 코드를 두 벌로 나누지 않아도 된다.
//
// 끄는 조건 (CLI-SPEC.md 전역 옵션):
//   --no-color 를 줬거나, NO_COLOR 환경 변수가 있거나, 콘솔이 ANSI 를 못 켜는 경우
class Palette {
public:
    // enabled 가 false 면 모든 코드가 빈 문자열이 된다.
    explicit Palette(bool enabled) : enabled_(enabled) {}

    std::string_view reset() const { return pick("\033[0m"); }
    std::string_view dim() const { return pick("\033[2m"); }
    std::string_view bold() const { return pick("\033[1m"); }
    std::string_view red() const { return pick("\033[31m"); }
    std::string_view yellow() const { return pick("\033[33m"); }
    std::string_view cyan() const { return pick("\033[36m"); }
    std::string_view gray() const { return pick("\033[90m"); }

    bool enabled() const { return enabled_; }

private:
    std::string_view pick(std::string_view code) const { return enabled_ ? code : ""; }

    bool enabled_;
};

// NO_COLOR 환경 변수가 설정되어 있는가. 값은 보지 않는다 — 있으면 끈다 (no-color.org).
bool noColorRequested();

}  // namespace cli
