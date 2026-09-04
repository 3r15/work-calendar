#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace domain {

// 요일 코드는 "SUN" | "MON" | ... | "SAT" (DATA-SCHEMA.md 공통 규칙).
enum class Weekday { Sun = 0, Mon, Tue, Wed, Thu, Fri, Sat };

// 알 수 없는 코드면 비어 있는 값을 돌려준다. 예외를 던지지 않는다.
std::optional<Weekday> parseWeekday(std::string_view code);

// 저장할 때 쓰는 대문자 세 글자.
std::string formatWeekday(Weekday day);

// 화면에 보여줄 한 글자 (일 월 화 수 목 금 토).
std::string weekdayLabel(Weekday day);

bool contains(const std::vector<Weekday>& days, Weekday day);

// std::chrono 의 요일을 도메인 요일로. 이 변환이 domain 쪽에 있는 이유는 util 이 domain 을
// 알면 의존 방향이 뒤집히기 때문이다.
Weekday fromChrono(std::chrono::weekday day);

}  // namespace domain
