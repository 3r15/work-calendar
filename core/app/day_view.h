#pragma once

#include "core/domain/entities.h"
#include "core/util/date.h"

#include <optional>
#include <string>
#include <vector>

namespace app {

// 하루의 구조를 화면에 그릴 수 있는 형태로 정리한 것.
//
// 문자열 조립은 여기서 하지 않는다. CLI 와 GUI 가 같은 구조를 받아 각자 그린다 (DESIGN 6장).
// Phase 2 에서는 배정 없이 구조만 담는다. 누가 무엇을 맡는지는 Phase 3 이 채운다.

struct TaskLine {
    std::string name;
    int requiredCount{1};
};

struct TaskSetBlock {
    std::string displayName;
    std::vector<TaskLine> tasks;
};

struct CategoryBlock {
    std::string displayName;
    std::vector<TaskSetBlock> taskSets;
};

struct SlotBlock {
    std::string displayName;
    std::string start;
    std::string end;
    bool isCurrent{false};  // 지금이 이 시간대 안이다
    bool isPast{false};     // 이미 지났다
    std::vector<CategoryBlock> categories;
};

// 지금이 어느 시간대에도 속하지 않을 때 안내할 다음 시간대 (D-003).
struct UpcomingSlot {
    std::string displayName;
    std::string start;
    util::Date date;
    bool isToday{false};
};

struct DayView {
    util::Date date;
    bool workday{false};
    std::vector<SlotBlock> slots;
    std::optional<UpcomingSlot> upcoming;
};

// now 는 "지금" 을 표시하는 데만 쓴다. date 와 다른 날이어도 된다 — 지난 날짜를 조회할 때다.
DayView buildDayView(const domain::Model& model, const util::Date& date,
                     const util::DateTime& now);

}  // namespace app
