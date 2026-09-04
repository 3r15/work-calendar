#include "core/app/update_service.h"

#include "core/storage/atomic_write.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <sstream>

namespace app {
namespace {

using nlohmann::json;

constexpr const char* kStateFile = "state/update.json";

}  // namespace

util::Result<ReleaseInfo> parseLatestRelease(const std::string& body,
                                             const std::string& assetName) {
    json root;
    try {
        root = json::parse(body);
    } catch (const json::parse_error&) {
        return util::makeError(util::ErrorCode::DataFile,
                               "업데이트 서버의 답을 이해할 수 없습니다.",
                               "잠시 뒤 다시 시도해 주세요.");
    }

    if (!root.is_object() || !root.contains("tag_name") || !root["tag_name"].is_string()) {
        return util::makeError(util::ErrorCode::DataFile,
                               "업데이트 서버의 답에 버전 정보가 없습니다.",
                               "저장소에 아직 릴리스가 없을 수 있습니다.");
    }

    ReleaseInfo info;
    info.tag = root["tag_name"].get<std::string>();
    const std::optional<util::Version> version = util::Version::parse(info.tag);
    if (!version.has_value()) {
        return util::makeError(util::ErrorCode::DataFile,
                               "릴리스 태그 \"" + info.tag + "\" 를 버전으로 읽을 수 없습니다.",
                               "태그는 v1.2.3 형식이어야 합니다.");
    }
    info.version = *version;

    if (root.contains("assets") && root["assets"].is_array()) {
        for (const json& asset : root["assets"]) {
            if (!asset.is_object() || !asset.contains("name") || !asset["name"].is_string()) {
                continue;
            }
            const std::string name = asset["name"].get<std::string>();
            const std::string url =
                (asset.contains("browser_download_url") && asset["browser_download_url"].is_string())
                    ? asset["browser_download_url"].get<std::string>()
                    : std::string{};
            if (name == assetName) {
                info.assetName = name;
                info.assetUrl = url;
            } else if (name == "SHA256SUMS.txt") {
                info.checksumUrl = url;
            }
        }
    }

    if (info.assetUrl.empty()) {
        return util::makeError(util::ErrorCode::NotFound,
                               "릴리스 " + info.tag + " 에 " + assetName + " 파일이 없습니다.",
                               "릴리스가 아직 올라가는 중일 수 있습니다.");
    }
    return info;
}

std::optional<std::string> findChecksumFor(const std::string& sumsFile,
                                           const std::string& assetName) {
    std::istringstream stream(sumsFile);
    std::string line;
    while (std::getline(stream, line)) {
        // "<hex>  <파일명>" — 공백 개수는 도구마다 다르고 앞에 * 가 붙기도 한다.
        const std::size_t space = line.find_first_of(" \t");
        if (space == std::string::npos) {
            continue;
        }
        const std::string digest = line.substr(0, space);
        std::string name = line.substr(space);
        const std::size_t start = name.find_first_not_of(" \t*");
        if (start == std::string::npos) {
            continue;
        }
        name = name.substr(start);
        while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' ')) {
            name.pop_back();
        }
        if (name == assetName) {
            return digest;
        }
    }
    return std::nullopt;
}

UpdateStatus decideUpdate(const domain::Config& config, const util::Version& current,
                          const std::optional<ReleaseInfo>& release) {
    UpdateStatus status;
    status.current = current;

    if (!config.update.enabled || config.update.repo.empty()) {
        status.decision = UpdateDecision::Disabled;
        return status;
    }
    if (!release.has_value()) {
        status.decision = UpdateDecision::UpToDate;
        return status;
    }

    status.release = release;
    status.decision =
        (release->version > current) ? UpdateDecision::Available : UpdateDecision::UpToDate;
    return status;
}

bool shouldCheckNow(const std::filesystem::path& dataDir, const util::DateTime& now,
                    int intervalHours) {
    const std::filesystem::path path = dataDir / kStateFile;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return true;  // 확인한 적이 없다
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    std::string lastChecked;
    try {
        const json root = json::parse(buffer.str());
        if (root.contains("lastCheckedAt") && root["lastCheckedAt"].is_string()) {
            lastChecked = root["lastCheckedAt"].get<std::string>();
        }
    } catch (const json::parse_error&) {
        return true;  // 깨진 파일 때문에 업데이트 확인을 못 하게 되면 안 된다
    }
    if (lastChecked.size() < 16) {
        return true;
    }

    const std::optional<util::Date> date = util::Date::parse(lastChecked.substr(0, 10));
    const std::optional<util::TimeOfDay> time = util::TimeOfDay::parse(lastChecked.substr(11, 5));
    if (!date.has_value() || !time.has_value()) {
        return true;
    }

    const auto toDays = [](const util::Date& value) {
        return std::chrono::sys_days{value.ymd()}.time_since_epoch().count();
    };
    const long minutes = static_cast<long>(toDays(now.date) - toDays(*date)) * 24 * 60 +
                         static_cast<long>(now.time.minutes() - time->minutes());
    // 시계가 뒤로 갔으면(시간대 변경, 수동 조정) 확인하게 둔다. 못 하게 막을 이유가 없다.
    return minutes < 0 || minutes >= static_cast<long>(intervalHours) * 60;
}

util::Result<void> recordCheck(const std::filesystem::path& dataDir, const util::DateTime& now) {
    json root;
    root["version"] = domain::kSchemaVersion;
    root["lastCheckedAt"] = util::formatIso8601(now);
    return storage::atomicWrite(dataDir / kStateFile, root.dump(2) + "\n");
}

std::optional<std::string> latestReleaseUrl(const std::string& repo) {
    const std::size_t slash = repo.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= repo.size()) {
        return std::nullopt;
    }
    if (repo.find('/', slash + 1) != std::string::npos) {
        return std::nullopt;  // owner/name 두 조각이어야 한다
    }
    return "https://api.github.com/repos/" + repo + "/releases/latest";
}

}  // namespace app
