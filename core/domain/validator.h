#pragma once

#include "core/domain/entities.h"
#include "core/domain/finding.h"

namespace domain {

// 설정 정합성 검사 (DATA-SCHEMA.md 검증 규칙).
//
// 첫 오류에서 멈추지 않는다. 발견한 것을 전부 모아 한 번에 돌려준다 — 사용자가 파일을 열고
// 고치는 왕복 횟수를 줄이려는 것이다.
//
// 파싱 단계에서만 보이는 문제(알 수 없는 요일 코드, version 범위 밖)는 storage 가 이미
// 보고서에 넣어 두었으므로 여기서 다시 검사하지 않는다.
Report validate(const Model& model);

// 날짜·시각 문자열의 모양 검사. "from > to" 같은 비교가 뜻을 가지려면 형식이 먼저 맞아야 한다.
bool isDateString(const std::string& text);  // "YYYY-MM-DD"
bool isTimeString(const std::string& text);  // "HH:MM"

}  // namespace domain
