#include "core/scheduling/policy.h"

namespace scheduling {

int RoundRobinPolicy::score(const domain::Worker& worker, const domain::Task& task,
                            const PolicyContext& context) const {
    // 라운드 로빈은 사람과 작업의 성격을 보지 않는다. 공정성 기능이 켜지면 여기가 늘어난다.
    (void)worker;
    (void)task;
    return context.slotLoad;
}

}  // namespace scheduling
