#pragma once

#include "core/domain/entities.h"

#include <optional>
#include <string>
#include <vector>

namespace domain {

// 그날의 배정 스냅샷. 한 번 쓰면 assign --force 없이는 다시 쓰지 않는다 (DESIGN 3.1).
// 조회할 때마다 다시 계산하면 같은 날 결과가 달라진다.

struct SnapshotAssignment {
    TaskId taskId;
    WorkerId workerId;
    // 당일 급휴로 이 사람이 쉬게 됐음. 배정은 유지하고 표시만 다르게 한다 (D-009).
    // 저장 시점이 아니라 **조회 시점에 계산**해서 갱신한다.
    bool absentAssignee{false};
    // 예약 필드. 현재 CLI 는 읽지도 쓰지도 않는다 (D-006).
    bool done{false};
    std::optional<std::string> doneAt;
};

// 인원 부족으로 채우지 못한 자리. count 는 남은 개수다.
struct SnapshotUnassigned {
    TaskId taskId;
    int count{0};
};

struct SnapshotTaskSet {
    TaskSetId taskSetId;
    std::vector<SnapshotAssignment> assignments;
    std::vector<SnapshotUnassigned> unassigned;
};

// 시간대 하나와 분류 하나의 짝. 시간대에 분류가 여럿이면 항목도 여럿이 된다.
struct SnapshotSlot {
    TimeSlotId timeSlotId;
    CategoryId categoryId;
    std::vector<SnapshotTaskSet> taskSets;
};

struct DaySnapshot {
    int version{kSchemaVersion};
    std::string date;  // "YYYY-MM-DD"
    Weekday weekday{Weekday::Sun};
    std::string generatedAt;  // ISO 8601, 오프셋 포함
    std::vector<SnapshotSlot> slots;
};

// 미배정이 몇 건인지. CLI 가 경고를 낼지 판단할 때 쓴다.
int countUnassigned(const DaySnapshot& snapshot);
// 휴무자에게 배정된 건수.
int countAbsentAssignees(const DaySnapshot& snapshot);

}  // namespace domain
