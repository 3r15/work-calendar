#pragma once

#include "core/domain/entities.h"
#include "core/domain/snapshot.h"
#include "core/domain/finding.h"
#include "core/util/result.h"

#include <filesystem>
#include <string>

namespace storage {

// 데이터 폴더 안의 파일 이름. 경로 조립을 한 곳에 모아 둔다.
namespace filenames {
inline constexpr const char* kConfig = "config.json";
inline constexpr const char* kWorkers = "workers.json";
inline constexpr const char* kTasks = "tasks.json";
inline constexpr const char* kAbsences = "absences.json";
inline constexpr const char* kCursors = "state/rr-cursor.json";
}  // namespace filenames

// state/assign-YYYY-MM-DD.json
std::filesystem::path snapshotPath(const std::filesystem::path& dataDir, const std::string& date);

// 로드 결과. 파일을 읽는 도중 발견한 문제는 예외가 아니라 findings 로 모은다.
//
// 왜 findings 를 여기서 들고 나오는가: "알 수 없는 요일 코드" 나 "version 범위 밖" 같은 문제는
// 파싱 단계에서만 보인다. 그런데 sched validate 는 첫 오류에서 멈추지 않고 전부 모아 보고해야
// 하므로(DATA-SCHEMA.md), 파싱 단계의 발견도 같은 보고서에 합쳐야 한다.
template <typename T>
struct Loaded {
    T value;
    domain::Report report;
};

// JSON 자체가 깨져 읽을 수 없으면 Result 가 실패한다 (종료 코드 3).
// 읽히기는 하지만 값이 이상한 경우는 report 에 쌓인다.
util::Result<Loaded<domain::Config>> loadConfig(const std::filesystem::path& path);
util::Result<Loaded<domain::WorkerList>> loadWorkers(const std::filesystem::path& path);
util::Result<Loaded<domain::TaskList>> loadTasks(const std::filesystem::path& path);
util::Result<Loaded<domain::AbsenceList>> loadAbsences(const std::filesystem::path& path);
util::Result<Loaded<domain::CursorState>> loadCursors(const std::filesystem::path& path);
util::Result<Loaded<domain::DaySnapshot>> loadSnapshot(const std::filesystem::path& path);

util::Result<void> saveConfig(const std::filesystem::path& path, const domain::Config& value);
util::Result<void> saveWorkers(const std::filesystem::path& path, const domain::WorkerList& value);
util::Result<void> saveTasks(const std::filesystem::path& path, const domain::TaskList& value);
util::Result<void> saveAbsences(const std::filesystem::path& path, const domain::AbsenceList& value);
util::Result<void> saveCursors(const std::filesystem::path& path, const domain::CursorState& value);
util::Result<void> saveSnapshot(const std::filesystem::path& path, const domain::DaySnapshot& value);

// 데이터 폴더 하나를 통째로 읽는다. rr-cursor.json 은 없어도 정상이므로 포함하지 않는다.
struct LoadedModel {
    domain::Model model;
    domain::Report report;
};
util::Result<LoadedModel> loadModel(const std::filesystem::path& dataDir);

// conflictsWith 를 양방향 대칭으로 맞춘다. 로드 직후 한 번 부른다.
// 존재하지 않는 작업을 가리키는 항목은 손대지 않는다 — 그건 validator 가 오류로 잡는다.
void normalizeConflicts(domain::TaskList& tasks);

// 직렬화만 따로 쓰고 싶을 때 (테스트, --json 출력).
std::string toJsonText(const domain::Config& value);
std::string toJsonText(const domain::WorkerList& value);
std::string toJsonText(const domain::TaskList& value);
std::string toJsonText(const domain::AbsenceList& value);
std::string toJsonText(const domain::CursorState& value);
std::string toJsonText(const domain::DaySnapshot& value);

}  // namespace storage
