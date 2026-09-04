#include "core/app/prune_service.h"

#include "core/storage/atomic_write.h"
#include "core/storage/json_io.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>

namespace app {
namespace {

// "assign-2026-09-04.json" 에서 날짜만 뽑는다. 형식이 다르면 비어 있는 값.
std::optional<util::Date> dateOfSnapshotFile(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    const std::string prefix = "assign-";
    const std::string suffix = ".json";
    if (name.size() != prefix.size() + 10 + suffix.size()) {
        return std::nullopt;
    }
    if (name.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return std::nullopt;
    }
    return util::Date::parse(name.substr(prefix.size(), 10));
}

long daysBetween(const util::Date& from, const util::Date& to) {
    const auto toDays = [](const util::Date& value) {
        return std::chrono::sys_days{value.ymd()}.time_since_epoch().count();
    };
    return static_cast<long>(toDays(to) - toDays(from));
}

std::string readWhole(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// 여러 줄 JSON 을 한 줄로 만든다. jsonl 은 한 줄에 스냅샷 하나여야 한다.
std::string toSingleLine(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (escaped) {
            out += c;
            escaped = false;
            continue;
        }
        if (inString && c == '\\') {
            out += c;
            escaped = true;
            continue;
        }
        if (c == '"') {
            inString = !inString;
            out += c;
            continue;
        }
        // 문자열 밖의 줄바꿈과 들여쓰기만 걷어낸다. 문자열 안의 공백은 데이터다.
        if (!inString && (c == '\n' || c == '\r')) {
            continue;
        }
        if (!inString && (c == ' ' || c == '\t')) {
            continue;
        }
        out += c;
    }
    return out;
}

}  // namespace

util::Result<PruneResult> pruneSnapshots(const std::filesystem::path& dataDir,
                                         const util::Date& today, int keepDays, bool dryRun) {
    PruneResult result;
    const std::filesystem::path stateDir = dataDir / "state";
    if (!std::filesystem::exists(stateDir)) {
        return result;  // 아직 아무것도 쌓이지 않았다. 오류가 아니다.
    }

    // 옮길 것을 월별로 모은다. 파일 이름 순서가 곧 날짜 순서다.
    std::map<std::string, std::vector<std::filesystem::path>> byMonth;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(stateDir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::optional<util::Date> date = dateOfSnapshotFile(entry.path());
        if (!date.has_value()) {
            continue;
        }
        if (daysBetween(*date, today) <= keepDays) {
            continue;  // 아직 보관 기간 안이다
        }
        byMonth[date->toString().substr(0, 7)].push_back(entry.path());
    }
    if (ec) {
        return util::makeError(util::ErrorCode::Io, stateDir.string() + " 폴더를 읽을 수 없습니다.");
    }

    for (auto& [month, files] : byMonth) {
        std::sort(files.begin(), files.end());
        if (dryRun) {
            result.archived += static_cast<int>(files.size());
            result.touched.push_back(month + ".jsonl");
            continue;
        }

        const std::filesystem::path archive = stateDir / "archive" / (month + ".jsonl");
        // 이미 있던 내용 뒤에 붙인다. atomicWrite 는 통째로 쓰므로 먼저 읽어 이어붙인다.
        std::string body;
        if (std::filesystem::exists(archive)) {
            body = readWhole(archive);
            if (!body.empty() && body.back() != '\n') {
                body += '\n';
            }
        }
        for (const std::filesystem::path& file : files) {
            body += toSingleLine(readWhole(file));
            body += '\n';
        }

        if (const util::Result<void> written = storage::atomicWrite(archive, body); !written) {
            return written.error();
        }
        // 아카이브를 먼저 쓰고 원본을 지운다. 반대면 중간에 죽었을 때 이력이 사라진다.
        for (const std::filesystem::path& file : files) {
            std::filesystem::remove(file, ec);
        }
        result.archived += static_cast<int>(files.size());
        result.touched.push_back(month + ".jsonl");
    }
    return result;
}

}  // namespace app
