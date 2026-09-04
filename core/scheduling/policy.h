#pragma once

#include "core/domain/entities.h"

namespace scheduling {

// 후보를 고르는 기준을 갈아 끼울 수 있게 하는 자리 (DESIGN 3.4).
//
// 지금은 RoundRobinPolicy 하나뿐입니다. 나중에 누적 배정 횟수, 최근 N일 기피 작업 이력,
// 숙련도 가중치를 score 에 넣으면 엔진 본체를 건드리지 않고 확장됩니다.

struct PolicyContext {
    // 커서에서 몇 칸 떨어져 있는가. 0 이 커서가 가리키는 사람이다.
    int cursorDistance{0};
    // 이 시간대에서 이 사람이 이미 맡은 건수.
    int slotLoad{0};
};

class IAssignmentPolicy {
public:
    virtual ~IAssignmentPolicy() = default;

    // 점수가 **낮을수록** 먼저 뽑힌다. 같은 점수면 커서에 가까운 쪽이 이긴다.
    // (DESIGN 3.4 는 방향을 정하지 않았으므로 여기서 정한다.)
    virtual int score(const domain::Worker& worker, const domain::Task& task,
                      const PolicyContext& context) const = 0;
};

// 기본 정책. 이 시간대에 적게 맡은 사람을 먼저 (DESIGN 3.2 의 "동점 후보 중 부하가 적은 사람").
// 커서 순서는 엔진이 순회 방향으로 이미 반영하므로 점수에 넣지 않는다.
class RoundRobinPolicy : public IAssignmentPolicy {
public:
    int score(const domain::Worker& worker, const domain::Task& task,
              const PolicyContext& context) const override;
};

}  // namespace scheduling
