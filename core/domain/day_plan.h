#pragma once

#include "core/domain/entities.h"
#include "core/util/date.h"

#include <optional>
#include <vector>

namespace domain {

// 그날이 근무일인가. 근무 요일에 들어 있고 공휴일이 아니어야 한다.
bool isWorkday(const Config& config, const util::Date& date);

// 그날 적용되는 시간대. 시작 시각 순으로 정렬해 돌려준다.
// 근무일이 아니면 비어 있다. 시각 형식이 깨진 시간대는 건너뛴다 — validator 가 따로 보고한다.
std::vector<const TimeSlot*> slotsForDate(const Config& config, const util::Date& date);

enum class SlotPlacement {
    Inside,    // 지금이 이 시간대 안이다
    Upcoming,  // 지금은 어느 시간대도 아니다. 다음에 오는 시간대를 돌려준다 (D-003)
    None,      // 앞으로 근무일이 없다. 설정이 비었거나 근무 요일이 하나도 없는 경우다
};

struct SlotLookup {
    SlotPlacement placement{SlotPlacement::None};
    const TimeSlot* slot{nullptr};
    util::Date date;  // slot 이 놓인 날. Upcoming 이면 오늘이 아닐 수 있다
};

// 지금이 어느 시간대인가.
//
// 시간대 구간은 [start, end) 로 본다. 시작 정각은 안에 들어가고 종료 정각은 들어가지 않는다.
// 09:00~12:00 과 13:00~18:00 이 있을 때 12:00 은 "오전이 막 끝난 시각" 이지 오전이 아니다.
//
// 어느 시간대에도 속하지 않으면 다음에 오는 시간대를 돌려준다. 그날 남은 것이 없으면 다음
// 근무일의 첫 시간대다 (D-003). 빈 화면은 프로그램이 고장난 것처럼 보인다.
SlotLookup findSlot(const Config& config, const util::DateTime& now);

// 그날 이후로 근무일이면서 시간대가 하나라도 있는 첫 날. 못 찾으면 비어 있는 값.
std::optional<util::Date> nextWorkdayWithSlots(const Config& config, const util::Date& after);

}  // namespace domain
