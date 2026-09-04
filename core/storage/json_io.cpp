#include "core/storage/json_io.h"

#include "core/storage/atomic_write.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace storage {
namespace {

using nlohmann::json;

// 파싱 오류 위치를 바이트 오프셋 대신 줄 번호로 바꾼다.
// "14번째 줄" 이 "byte 312" 보다 고치기 쉽다 (CLI-SPEC.md 오류 메시지).
std::size_t lineAtByte(const std::string& text, std::size_t byte) {
    std::size_t line = 1;
    const std::size_t limit = std::min(byte, text.size());
    for (std::size_t i = 0; i < limit; ++i) {
        if (text[i] == '\n') {
            ++line;
        }
    }
    return line;
}

util::Result<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return util::makeError(util::ErrorCode::NotFound,
                               path.filename().string() + " 파일을 찾을 수 없습니다.",
                               "데이터 폴더 위치가 맞는지 확인해 주세요: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();

    // BOM 이 있으면 nlohmann 이 파싱에 실패한다. 사람이 편집기로 저장하다 붙이는 일이 흔하다.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

util::Result<json> parseJson(const std::filesystem::path& path) {
    util::Result<std::string> text = readFile(path);
    if (!text) {
        return text.error();
    }
    const std::string& body = text.value();
    try {
        return json::parse(body);
    } catch (const json::parse_error& e) {
        const std::size_t line = lineAtByte(body, e.byte > 0 ? e.byte - 1 : 0);
        return util::makeError(
            util::ErrorCode::DataFile,
            path.filename().string() + " " + std::to_string(line) + "번째 줄에서 형식 오류가 " +
                "발생했습니다.",
            "쉼표나 괄호가 빠졌는지 확인해 주세요.\n문제가 계속되면 " + path.string() +
                ".bak 으로 되돌릴 수 있습니다.");
    }
}

// 없는 필드와 null 과 빈 배열을 같게 취급한다 (DATA-SCHEMA.md 공통 규칙).
const json* field(const json& object, const char* key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return nullptr;
    }
    return &(*it);
}

std::string getString(const json& object, const char* key, std::string fallback = {}) {
    const json* value = field(object, key);
    return (value != nullptr && value->is_string()) ? value->get<std::string>()
                                                    : std::move(fallback);
}

int getInt(const json& object, const char* key, int fallback) {
    const json* value = field(object, key);
    return (value != nullptr && value->is_number_integer()) ? value->get<int>() : fallback;
}

bool getBool(const json& object, const char* key, bool fallback) {
    const json* value = field(object, key);
    return (value != nullptr && value->is_boolean()) ? value->get<bool>() : fallback;
}

std::vector<std::string> getStringArray(const json& object, const char* key) {
    std::vector<std::string> out;
    const json* value = field(object, key);
    if (value == nullptr || !value->is_array()) {
        return out;
    }
    for (const json& item : *value) {
        if (item.is_string()) {
            out.push_back(item.get<std::string>());
        }
    }
    return out;
}

// 알 수 없는 요일 코드는 버리고 오류로 기록한다. 파일은 계속 읽는다.
std::vector<domain::Weekday> getWeekdays(const json& object, const char* key,
                                         const std::string& where, const std::string& what,
                                         domain::Report& report) {
    std::vector<domain::Weekday> out;
    for (const std::string& code : getStringArray(object, key)) {
        if (const std::optional<domain::Weekday> day = domain::parseWeekday(code)) {
            out.push_back(*day);
        } else {
            report.error(where, what + " 의 요일 코드 \"" + code +
                                    "\" 를 알 수 없습니다. SUN MON TUE WED THU FRI SAT 중 하나여야 "
                                    "합니다.");
        }
    }
    return out;
}

json weekdaysToJson(const std::vector<domain::Weekday>& days) {
    json out = json::array();
    for (domain::Weekday day : days) {
        out.push_back(domain::formatWeekday(day));
    }
    return out;
}

void checkVersion(const json& root, const std::string& where, domain::Report& report) {
    const json* value = field(root, "version");
    if (value == nullptr || !value->is_number_integer()) {
        report.error(where, "version 필드가 없습니다. 이 파일이 이 프로그램의 데이터 파일이 "
                            "맞는지 확인해 주세요.");
        return;
    }
    const int version = value->get<int>();
    if (version != domain::kSchemaVersion) {
        report.error(where, "지원하지 않는 형식 버전입니다 (version " + std::to_string(version) +
                                "). 이 프로그램은 " + std::to_string(domain::kSchemaVersion) +
                                " 만 읽을 수 있습니다.");
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// 로드
// ---------------------------------------------------------------------------

util::Result<Loaded<domain::Config>> loadConfig(const std::filesystem::path& path) {
    util::Result<json> parsed = parseJson(path);
    if (!parsed) {
        return parsed.error();
    }
    const json& root = parsed.value();
    const std::string where = filenames::kConfig;

    Loaded<domain::Config> out;
    checkVersion(root, where, out.report);
    out.value.version = getInt(root, "version", domain::kSchemaVersion);

    if (const json* calendar = field(root, "workCalendar")) {
        out.value.workCalendar.workWeekdays =
            getWeekdays(*calendar, "workWeekdays", where, "근무 요일", out.report);
        out.value.workCalendar.holidays = getStringArray(*calendar, "holidays");
    }

    if (const json* slots = field(root, "timeSlots")) {
        for (const json& item : *slots) {
            domain::TimeSlot slot;
            slot.id = domain::TimeSlotId{getString(item, "id")};
            slot.displayName = getString(item, "displayName");
            slot.start = getString(item, "start");
            slot.end = getString(item, "end");
            slot.weekdays = getWeekdays(item, "weekdays", where,
                                        "시간대 \"" + slot.id.str() + "\"", out.report);
            for (const std::string& id : getStringArray(item, "categoryIds")) {
                slot.categoryIds.push_back(domain::CategoryId{id});
            }
            out.value.timeSlots.push_back(std::move(slot));
        }
    }

    if (const json* categories = field(root, "categories")) {
        for (const json& item : *categories) {
            domain::Category category;
            category.id = domain::CategoryId{getString(item, "id")};
            category.displayName = getString(item, "displayName");
            for (const std::string& id : getStringArray(item, "taskSetIds")) {
                category.taskSetIds.push_back(domain::TaskSetId{id});
            }
            out.value.categories.push_back(std::move(category));
        }
    }

    if (const json* assignment = field(root, "assignment")) {
        out.value.assignment.crossSetConflicts = getBool(*assignment, "crossSetConflicts", false);
    }

    if (const json* update = field(root, "update")) {
        out.value.update.enabled = getBool(*update, "enabled", true);
        out.value.update.repo = getString(*update, "repo");
        out.value.update.checkIntervalHours = getInt(*update, "checkIntervalHours", 24);
    }

    return out;
}

util::Result<Loaded<domain::WorkerList>> loadWorkers(const std::filesystem::path& path) {
    util::Result<json> parsed = parseJson(path);
    if (!parsed) {
        return parsed.error();
    }
    const json& root = parsed.value();
    const std::string where = filenames::kWorkers;

    Loaded<domain::WorkerList> out;
    checkVersion(root, where, out.report);
    out.value.version = getInt(root, "version", domain::kSchemaVersion);

    if (const json* workers = field(root, "workers")) {
        // 배열 순서가 라운드 로빈 기준 순서다. 읽는 순서를 그대로 유지한다.
        for (const json& item : *workers) {
            domain::Worker worker;
            worker.id = domain::WorkerId{getString(item, "id")};
            worker.name = getString(item, "name");
            worker.active = getBool(item, "active", true);
            worker.weeklyOff = getWeekdays(item, "weeklyOff", where,
                                           "작업자 \"" + worker.name + "\"", out.report);
            worker.note = getString(item, "note");
            out.value.workers.push_back(std::move(worker));
        }
    }
    return out;
}

util::Result<Loaded<domain::TaskList>> loadTasks(const std::filesystem::path& path) {
    util::Result<json> parsed = parseJson(path);
    if (!parsed) {
        return parsed.error();
    }
    const json& root = parsed.value();
    const std::string where = filenames::kTasks;

    Loaded<domain::TaskList> out;
    checkVersion(root, where, out.report);
    out.value.version = getInt(root, "version", domain::kSchemaVersion);

    if (const json* sets = field(root, "taskSets")) {
        for (const json& item : *sets) {
            domain::TaskSet set;
            set.id = domain::TaskSetId{getString(item, "id")};
            set.displayName = getString(item, "displayName");
            out.value.taskSets.push_back(std::move(set));
        }
    }

    if (const json* tasks = field(root, "tasks")) {
        for (const json& item : *tasks) {
            domain::Task task;
            task.id = domain::TaskId{getString(item, "id")};
            task.name = getString(item, "name");
            for (const std::string& id : getStringArray(item, "taskSetIds")) {
                task.taskSetIds.push_back(domain::TaskSetId{id});
            }
            task.requiredCount = getInt(item, "requiredCount", 1);
            for (const std::string& id : getStringArray(item, "conflictsWith")) {
                task.conflictsWith.push_back(domain::TaskId{id});
            }
            task.weekdays =
                getWeekdays(item, "weekdays", where, "작업 \"" + task.name + "\"", out.report);
            if (const json* priority = field(item, "priority")) {
                if (priority->is_number_integer()) {
                    task.priority = priority->get<int>();
                }
            }
            out.value.tasks.push_back(std::move(task));
        }
    }

    normalizeConflicts(out.value);
    return out;
}

util::Result<Loaded<domain::AbsenceList>> loadAbsences(const std::filesystem::path& path) {
    util::Result<json> parsed = parseJson(path);
    if (!parsed) {
        return parsed.error();
    }
    const json& root = parsed.value();

    Loaded<domain::AbsenceList> out;
    checkVersion(root, filenames::kAbsences, out.report);
    out.value.version = getInt(root, "version", domain::kSchemaVersion);

    if (const json* absences = field(root, "absences")) {
        for (const json& item : *absences) {
            domain::Absence absence;
            absence.workerId = domain::WorkerId{getString(item, "workerId")};
            absence.from = getString(item, "from");
            absence.to = getString(item, "to");
            absence.reason = getString(item, "reason");
            absence.createdAt = getString(item, "createdAt");
            out.value.absences.push_back(std::move(absence));
        }
    }
    return out;
}

util::Result<Loaded<domain::CursorState>> loadCursors(const std::filesystem::path& path) {
    util::Result<json> parsed = parseJson(path);
    if (!parsed) {
        return parsed.error();
    }
    const json& root = parsed.value();

    Loaded<domain::CursorState> out;
    checkVersion(root, filenames::kCursors, out.report);
    out.value.version = getInt(root, "version", domain::kSchemaVersion);

    if (const json* cursors = field(root, "cursors")) {
        if (cursors->is_object()) {
            for (const auto& [key, value] : cursors->items()) {
                if (value.is_number_integer()) {
                    out.value.cursors.emplace(domain::TaskSetId{key}, value.get<int>());
                }
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 저장
// ---------------------------------------------------------------------------

std::string toJsonText(const domain::Config& value) {
    json root;
    root["version"] = value.version;
    root["workCalendar"]["workWeekdays"] = weekdaysToJson(value.workCalendar.workWeekdays);
    root["workCalendar"]["holidays"] = value.workCalendar.holidays;

    root["timeSlots"] = json::array();
    for (const domain::TimeSlot& slot : value.timeSlots) {
        json item;
        item["id"] = slot.id.str();
        item["displayName"] = slot.displayName;
        item["start"] = slot.start;
        item["end"] = slot.end;
        item["weekdays"] = weekdaysToJson(slot.weekdays);
        item["categoryIds"] = json::array();
        for (const domain::CategoryId& id : slot.categoryIds) {
            item["categoryIds"].push_back(id.str());
        }
        root["timeSlots"].push_back(std::move(item));
    }

    root["categories"] = json::array();
    for (const domain::Category& category : value.categories) {
        json item;
        item["id"] = category.id.str();
        item["displayName"] = category.displayName;
        item["taskSetIds"] = json::array();
        for (const domain::TaskSetId& id : category.taskSetIds) {
            item["taskSetIds"].push_back(id.str());
        }
        root["categories"].push_back(std::move(item));
    }

    root["assignment"]["crossSetConflicts"] = value.assignment.crossSetConflicts;
    root["update"]["enabled"] = value.update.enabled;
    root["update"]["repo"] = value.update.repo;
    root["update"]["checkIntervalHours"] = value.update.checkIntervalHours;
    return root.dump(2) + "\n";
}

std::string toJsonText(const domain::WorkerList& value) {
    json root;
    root["version"] = value.version;
    root["workers"] = json::array();
    for (const domain::Worker& worker : value.workers) {
        json item;
        item["id"] = worker.id.str();
        item["name"] = worker.name;
        item["active"] = worker.active;
        item["weeklyOff"] = weekdaysToJson(worker.weeklyOff);
        item["note"] = worker.note;
        root["workers"].push_back(std::move(item));
    }
    return root.dump(2) + "\n";
}

std::string toJsonText(const domain::TaskList& value) {
    json root;
    root["version"] = value.version;
    root["taskSets"] = json::array();
    for (const domain::TaskSet& set : value.taskSets) {
        json item;
        item["id"] = set.id.str();
        item["displayName"] = set.displayName;
        root["taskSets"].push_back(std::move(item));
    }
    root["tasks"] = json::array();
    for (const domain::Task& task : value.tasks) {
        json item;
        item["id"] = task.id.str();
        item["name"] = task.name;
        item["taskSetIds"] = json::array();
        for (const domain::TaskSetId& id : task.taskSetIds) {
            item["taskSetIds"].push_back(id.str());
        }
        item["requiredCount"] = task.requiredCount;
        item["conflictsWith"] = json::array();
        for (const domain::TaskId& id : task.conflictsWith) {
            item["conflictsWith"].push_back(id.str());
        }
        item["weekdays"] = weekdaysToJson(task.weekdays);
        if (task.priority.has_value()) {
            item["priority"] = *task.priority;
        } else {
            item["priority"] = nullptr;
        }
        root["tasks"].push_back(std::move(item));
    }
    return root.dump(2) + "\n";
}

std::string toJsonText(const domain::AbsenceList& value) {
    json root;
    root["version"] = value.version;
    root["absences"] = json::array();
    for (const domain::Absence& absence : value.absences) {
        json item;
        item["workerId"] = absence.workerId.str();
        item["from"] = absence.from;
        item["to"] = absence.to;
        item["reason"] = absence.reason;
        item["createdAt"] = absence.createdAt;
        root["absences"].push_back(std::move(item));
    }
    return root.dump(2) + "\n";
}

std::string toJsonText(const domain::CursorState& value) {
    json root;
    root["version"] = value.version;
    root["cursors"] = json::object();
    for (const auto& [id, cursor] : value.cursors) {
        root["cursors"][id.str()] = cursor;
    }
    return root.dump(2) + "\n";
}

util::Result<void> saveConfig(const std::filesystem::path& path, const domain::Config& value) {
    return atomicWrite(path, toJsonText(value));
}
util::Result<void> saveWorkers(const std::filesystem::path& path, const domain::WorkerList& value) {
    return atomicWrite(path, toJsonText(value));
}
util::Result<void> saveTasks(const std::filesystem::path& path, const domain::TaskList& value) {
    return atomicWrite(path, toJsonText(value));
}
util::Result<void> saveAbsences(const std::filesystem::path& path,
                                const domain::AbsenceList& value) {
    return atomicWrite(path, toJsonText(value));
}
util::Result<void> saveCursors(const std::filesystem::path& path, const domain::CursorState& value) {
    return atomicWrite(path, toJsonText(value));
}

// ---------------------------------------------------------------------------

void normalizeConflicts(domain::TaskList& tasks) {
    // 한쪽에만 적혀 있어도 양쪽에 적용되어야 한다. 자기 자신은 암묵적이므로 넣지 않는다.
    std::vector<std::pair<domain::TaskId, domain::TaskId>> edges;
    for (const domain::Task& task : tasks.tasks) {
        for (const domain::TaskId& other : task.conflictsWith) {
            if (!(other == task.id)) {
                edges.emplace_back(task.id, other);
            }
        }
    }

    for (domain::Task& task : tasks.tasks) {
        std::vector<domain::TaskId> merged;
        const auto push = [&merged](const domain::TaskId& id) {
            if (std::find(merged.begin(), merged.end(), id) == merged.end()) {
                merged.push_back(id);
            }
        };
        // 원래 순서를 앞에 두고, 반대 방향에서 들어온 것을 뒤에 붙인다.
        // 정의 순서가 배정에 쓰이므로 임의로 정렬하지 않는다.
        for (const domain::TaskId& id : task.conflictsWith) {
            if (!(id == task.id)) {
                push(id);
            }
        }
        for (const auto& [from, to] : edges) {
            if (to == task.id) {
                push(from);
            }
        }
        task.conflictsWith = std::move(merged);
    }
}

util::Result<LoadedModel> loadModel(const std::filesystem::path& dataDir) {
    LoadedModel out;

    util::Result<Loaded<domain::Config>> config = loadConfig(dataDir / filenames::kConfig);
    if (!config) {
        return config.error();
    }
    util::Result<Loaded<domain::WorkerList>> workers = loadWorkers(dataDir / filenames::kWorkers);
    if (!workers) {
        return workers.error();
    }
    util::Result<Loaded<domain::TaskList>> tasks = loadTasks(dataDir / filenames::kTasks);
    if (!tasks) {
        return tasks.error();
    }
    util::Result<Loaded<domain::AbsenceList>> absences =
        loadAbsences(dataDir / filenames::kAbsences);
    if (!absences) {
        return absences.error();
    }

    out.report.merge(config.value().report);
    out.report.merge(workers.value().report);
    out.report.merge(tasks.value().report);
    out.report.merge(absences.value().report);

    out.model.config = std::move(config).value().value;
    out.model.workers = std::move(workers).value().value;
    out.model.tasks = std::move(tasks).value().value;
    out.model.absences = std::move(absences).value().value;
    return out;
}

}  // namespace storage
