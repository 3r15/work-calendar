#pragma once

#include "core/domain/entities.h"
#include "core/domain/snapshot.h"
#include "core/util/date.h"

#include <optional>
#include <string>
#include <vector>

namespace app {

// 하루의 구조를 화면에 그릴 수 있는 형태로 정리한 것.
//
// 문자열 조립은 여기서 하지 않는다. CLI 와 GUI 가 같은 구조를 받아 각자 그린다 (DESIGN 6장).
// Phase 2 에서는 배정 없이 구조만 담는다. 누가 무엇을 맡는지는 Phase 3 이 채운다.

// 배정된 사람 하나. absent 는 저장된 값이 아니라 **조회 시점에 계산**한다 (D-009) —
// 휴무를 취소하면 다음 조회에서 false 로 돌아가야 한다.
struct AssignedWorker {
    std::string name;
    bool absent{false};
};

struct TaskLine {
    std::string name;
    int requiredCount{1};
    std::vector<AssignedWorker> workers;
    // 인원 부족으로 채우지 못한 자리 수.
    int unassignedCount{0};
};

struct TaskSetBlock {
    std::string displayName;
    std::vector<TaskLine> tasks;
};

struct CategoryBlock {
    domain::CategoryId id;
    std::string displayName;
    std::vector<TaskSetBlock> taskSets;
};

struct SlotBlock {
    domain::TimeSlotId id;
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
//
// snapshot 이 null 이면 배정 없이 구조만 담는다. 넘기면 누가 무엇을 맡았는지가 채워진다.
DayView buildDayView(const domain::Model& model, const util::Date& date,
                     const util::DateTime& now,
                     const domain::DaySnapshot* snapshot = nullptr);

}  // namespace app
