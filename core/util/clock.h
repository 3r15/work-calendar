#pragma once

#include "core/util/date.h"

#include <memory>

namespace util {

// "지금" 을 주입할 수 있게 하는 인터페이스.
//
// 시각에 따라 동작이 달라지는 코드(어느 시간대인가, 오늘 급휴인가)를 테스트하려면 시각을
// 정할 수 있어야 한다. 실제 시계를 쓰면 자정 근처에서만 깨지는 테스트가 된다.
class IClock {
public:
    virtual ~IClock() = default;
    virtual DateTime now() const = 0;
};

// 시스템 로컬 시각. std::chrono::current_zone() 을 쓰므로 플랫폼 독립이다.
class SystemClock : public IClock {
public:
    DateTime now() const override;
};

// 테스트용. 정해진 시각을 계속 돌려준다.
class FixedClock : public IClock {
public:
    explicit FixedClock(DateTime moment) : moment_(moment) {}
    DateTime now() const override { return moment_; }
    void set(DateTime moment) { moment_ = moment; }

private:
    DateTime moment_;
};

}  // namespace util
