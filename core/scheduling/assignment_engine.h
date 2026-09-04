#pragma once

#include "core/domain/entities.h"
#include "core/scheduling/policy.h"
#include "core/util/date.h"

#include <vector>

namespace scheduling {

// 배정 엔진. 작업집합 하나를 채운다.
//
// 엔진은 파일 IO 를 하지 않는다. 입력은 전부 인자로 받는다 (CLAUDE.md 5장).
// 인원이 모자라도 예외를 던지지 않는다 — 미배정 슬롯으로 기록한다.

struct Assignment {
    domain::TaskId taskId;
    domain::WorkerId workerId;
};

// 채우지 못한 자리 하나. requiredCount 미달분만큼 같은 taskId 가 여러 번 나온다.
struct Unassigned {
    domain::TaskId taskId;
};

struct AssignResult {
    std::vector<Assignment> assignments;
    std::vector<Unassigned> unassigned;
    // 다음 실행에 그대로 넘긴다. availableWorkers 인덱스 기준.
    int nextCursor{0};
};

struct AssignInput {
    // 요일 필터에 쓴다. 근무일 판정은 부르는 쪽이 이미 했다고 본다.
    util::Date date;
    // 이번 호출로 채우는 작업집합.
    domain::TaskSetId taskSetId;
    // 같은 시간대의 모든 작업. taskSetId 소속이 아닌 것도 들어올 수 있고, 그런 작업은 채워지지
    // 않지만 crossSetConflicts 가 켜졌을 때 배타 검사에 쓰인다.
    std::vector<domain::Task> slotTasks;
    // 이미 걸러진 가용 작업자. workers.json 정의 순서를 그대로 유지해야 한다 — 정렬하면
    // 결정론이 깨진다.
    std::vector<domain::Worker> availableWorkers;
    // 이전 실행에서 이어받은 커서.
    int cursor{0};
    domain::AssignmentOptions options;
    // 같은 시간대에서 먼저 채운 다른 작업집합의 결과.
    // crossSetConflicts 가 true 일 때만 적격성과 부하 계산에 쓴다 (D-002).
    std::vector<Assignment> priorAssignments;
};

class AssignmentEngine {
public:
    // 기본은 RoundRobinPolicy.
    AssignmentEngine();
    // 정책을 갈아 끼울 때. policy 는 엔진보다 오래 살아야 한다.
    explicit AssignmentEngine(const IAssignmentPolicy& policy);

    AssignResult assign(const AssignInput& input) const;

private:
    const IAssignmentPolicy* policy_;
};

}  // namespace scheduling
