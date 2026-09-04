#pragma once

#include "core/domain/entities.h"
#include "core/domain/finding.h"

#include <optional>
#include <vector>

namespace domain {

// 설정 정합성 검사 (DATA-SCHEMA.md 검증 규칙).
//
// 첫 오류에서 멈추지 않는다. 발견한 것을 전부 모아 한 번에 돌려준다 — 사용자가 파일을 열고
// 고치는 왕복 횟수를 줄이려는 것이다.
//
// 파싱 단계에서만 보이는 문제(알 수 없는 요일 코드, version 범위 밖)는 storage 가 이미
// 보고서에 넣어 두었으므로 여기서 다시 검사하지 않는다.
Report validate(const Model& model);

// 같은 요일에 구간이 겹치는 시간대 쌍.
//
// validator 와 admin_service 가 **같은 판정**을 쓰게 하려고 노출한다. 오류 메시지 문자열로
// 판정하면 문구가 바뀌는 순간 겹침 검사가 조용히 통과해 버린다.
struct TimeSlotOverlap {
    const TimeSlot* a{nullptr};
    const TimeSlot* b{nullptr};
    std::vector<Weekday> weekdays;  // 겹치는 요일
};

std::vector<TimeSlotOverlap> findTimeSlotOverlaps(const Config& config);

// 사용자에게 보여줄 한 문장.
std::string describeOverlap(const TimeSlotOverlap& overlap);

// 날짜·시각 문자열의 모양 검사. "from > to" 같은 비교가 뜻을 가지려면 형식이 먼저 맞아야 한다.
bool isDateString(const std::string& text);  // "YYYY-MM-DD"
bool isTimeString(const std::string& text);  // "HH:MM"

}  // namespace domain
