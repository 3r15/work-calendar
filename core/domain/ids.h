#pragma once

#include <compare>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace domain {

// 강타입 ID 래퍼. WorkerId 자리에 TaskId 를 넣으면 컴파일러가 막는다 (CLAUDE.md 7장).
// 태그 타입만 다른 별개의 타입이므로 서로 대입되지 않는다.
template <typename Tag>
class Id {
public:
    Id() = default;
    explicit Id(std::string value) : value_(std::move(value)) {}

    const std::string& str() const noexcept { return value_; }
    bool empty() const noexcept { return value_.empty(); }

    friend bool operator==(const Id&, const Id&) = default;
    friend std::strong_ordering operator<=>(const Id&, const Id&) = default;

private:
    std::string value_;
};

struct WorkerTag {};
struct TaskTag {};
struct TaskSetTag {};
struct CategoryTag {};
struct TimeSlotTag {};

using WorkerId = Id<WorkerTag>;
using TaskId = Id<TaskTag>;
using TaskSetId = Id<TaskSetTag>;
using CategoryId = Id<CategoryTag>;
using TimeSlotId = Id<TimeSlotTag>;

}  // namespace domain

namespace std {

template <typename Tag>
struct hash<domain::Id<Tag>> {
    std::size_t operator()(const domain::Id<Tag>& id) const noexcept {
        return std::hash<std::string>{}(id.str());
    }
};

}  // namespace std
