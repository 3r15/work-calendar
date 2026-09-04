#pragma once

#include "core/domain/entities.h"
#include "core/util/result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace app {

// 설정 파일을 손으로 고치지 않고도 전체 운영이 되게 하는 명령들 (ROADMAP Phase 5 완료 기준 1).
//
// 모든 함수는 model 을 고치고 해당 JSON 파일을 원자적으로 다시 쓴다. 저장에 실패하면 model 도
// 원래대로 되돌린다 — 메모리와 파일이 어긋나면 다음 명령이 이상하게 동작한다.
//
// 참조를 끊는 삭제는 거부한다. 무엇이 가리키고 있는지 알려주고 멈춘다. CLI 만으로 운영하는데
// CLI 가 깨진 데이터를 만들 수 있으면 안 되기 때문이다.

// ID 규칙: 소문자 영숫자와 밑줄만 (DATA-SCHEMA.md 공통 규칙).
bool isValidId(const std::string& id);

// --- 작업자 ---

util::Result<domain::Worker> addWorker(const std::filesystem::path& dataDir, domain::Model& model,
                                       const std::string& name, const std::string& idHint,
                                       const std::vector<domain::Weekday>& weeklyOff);

util::Result<void> editWorker(const std::filesystem::path& dataDir, domain::Model& model,
                              const domain::WorkerId& id, const std::optional<std::string>& name,
                              const std::optional<bool>& active);

// hard 가 false 면 active: false 로 바꾼다. 과거 스냅샷이 ID 를 참조하므로 이쪽이 기본이다.
util::Result<void> removeWorker(const std::filesystem::path& dataDir, domain::Model& model,
                                const domain::WorkerId& id, bool hard);

// --- 작업 ---

struct TaskSpec {
    std::string id;
    std::string name;
    std::vector<domain::TaskSetId> taskSetIds;
    int requiredCount{1};
    std::vector<domain::TaskId> conflictsWith;
    std::vector<domain::Weekday> weekdays;
};

util::Result<void> addTask(const std::filesystem::path& dataDir, domain::Model& model,
                           const TaskSpec& spec);

util::Result<void> editTask(const std::filesystem::path& dataDir, domain::Model& model,
                            const domain::TaskId& id, const std::optional<std::string>& name,
                            const std::optional<int>& requiredCount,
                            const std::optional<std::vector<domain::TaskId>>& conflictsWith,
                            const std::optional<std::vector<domain::Weekday>>& weekdays,
                            const std::optional<std::vector<domain::TaskSetId>>& taskSetIds);

// 다른 작업의 conflictsWith 에서도 함께 지운다. 안 그러면 끊어진 참조가 남는다.
util::Result<void> removeTask(const std::filesystem::path& dataDir, domain::Model& model,
                              const domain::TaskId& id);

// --- 작업집합 ---

util::Result<void> addTaskSet(const std::filesystem::path& dataDir, domain::Model& model,
                              const std::string& id, const std::string& displayName);
util::Result<void> removeTaskSet(const std::filesystem::path& dataDir, domain::Model& model,
                                 const domain::TaskSetId& id);

// --- 작업분류 ---

util::Result<void> addCategory(const std::filesystem::path& dataDir, domain::Model& model,
                               const std::string& id, const std::string& displayName,
                               const std::vector<domain::TaskSetId>& taskSetIds);
util::Result<void> removeCategory(const std::filesystem::path& dataDir, domain::Model& model,
                                  const domain::CategoryId& id);

// --- 시간대 ---

struct TimeSlotSpec {
    std::string id;
    std::string displayName;
    std::string start;  // "HH:MM"
    std::string end;
    std::vector<domain::Weekday> weekdays;
    std::vector<domain::CategoryId> categoryIds;
};

util::Result<void> addTimeSlot(const std::filesystem::path& dataDir, domain::Model& model,
                               const TimeSlotSpec& spec);
util::Result<void> removeTimeSlot(const std::filesystem::path& dataDir, domain::Model& model,
                                  const domain::TimeSlotId& id);

}  // namespace app
