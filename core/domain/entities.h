#pragma once

#include "core/domain/ids.h"
#include "core/domain/weekday.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace domain {

// 현재 지원하는 스키마 버전. 파일을 읽을 때 이 값과 비교한다.
inline constexpr int kSchemaVersion = 1;

struct Worker {
    WorkerId id;
    std::string name;
    bool active{true};
    std::vector<Weekday> weeklyOff;  // 매주 반복되는 휴무
    std::string note;
};

struct Absence {
    WorkerId workerId;
    std::string from;       // "YYYY-MM-DD"
    std::string to;         // "YYYY-MM-DD", from 과 같으면 하루
    std::string reason;
    std::string createdAt;  // ISO 8601, 오프셋 포함. 당일 급휴 판정에 쓴다 (D-009)
};

struct Task {
    TaskId id;
    std::string name;
    // 작업집합 소속의 유일한 출처. TaskSet 이 작업 목록을 갖지 않는다 (D-001).
    std::vector<TaskSetId> taskSetIds;
    int requiredCount{1};
    // 로드 시 양방향 대칭으로 정규화된다.
    std::vector<TaskId> conflictsWith;
    std::vector<Weekday> weekdays;  // 비어 있으면 근무일 전체
    std::optional<int> priority;    // 로드·저장만 되고 엔진은 읽지 않는다 (D-010)
};

// id 와 displayName 만 갖는다. 작업 목록을 여기 넣으면 D-001 위반이다.
struct TaskSet {
    TaskSetId id;
    std::string displayName;
};

// 화면에 묶어 보여주는 단위. 배정 로직에 관여하지 않는다.
struct Category {
    CategoryId id;
    std::string displayName;
    std::vector<TaskSetId> taskSetIds;
};

struct TimeSlot {
    TimeSlotId id;
    std::string displayName;
    std::string start;  // "HH:MM"
    std::string end;    // "HH:MM"
    std::vector<Weekday> weekdays;  // 비어 있으면 근무일 전체
    std::vector<CategoryId> categoryIds;
};

struct WorkCalendar {
    std::vector<Weekday> workWeekdays;
    std::vector<std::string> holidays;  // "YYYY-MM-DD"
};

struct AssignmentOptions {
    // false 면 배타 검사가 작업집합 안에서만, true 면 시간대 전체로 (D-002).
    bool crossSetConflicts{false};
};

struct UpdateOptions {
    bool enabled{true};
    std::string repo;  // "owner/name"
    int checkIntervalHours{24};
};

struct Config {
    int version{kSchemaVersion};
    WorkCalendar workCalendar;
    std::vector<TimeSlot> timeSlots;
    std::vector<Category> categories;
    AssignmentOptions assignment;
    UpdateOptions update;
};

// 배열 순서가 라운드 로빈의 기준 순서다. 정렬하지 말 것.
struct WorkerList {
    int version{kSchemaVersion};
    std::vector<Worker> workers;
};

struct TaskList {
    int version{kSchemaVersion};
    std::vector<TaskSet> taskSets;
    // 작업집합 안의 작업 순서는 이 배열의 정의 순서다.
    std::vector<Task> tasks;
};

struct AbsenceList {
    int version{kSchemaVersion};
    std::vector<Absence> absences;
};

// 작업집합별 라운드 로빈 커서. 값은 WorkerList 배열의 인덱스.
struct CursorState {
    int version{kSchemaVersion};
    std::map<TaskSetId, int> cursors;
};

// 데이터 폴더 하나에서 읽은 전체. 검증과 배정이 이걸 통째로 받는다.
struct Model {
    Config config;
    WorkerList workers;
    TaskList tasks;
    AbsenceList absences;
};

}  // namespace domain
