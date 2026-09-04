#pragma once

#include "core/domain/entities.h"
#include "core/util/date.h"
#include "core/util/result.h"
#include "core/util/semver.h"

#include <filesystem>
#include <optional>
#include <string>

namespace app {

// 자동 업데이트의 판단 부분. 네트워크는 건드리지 않는다 — HTTP 는 platform/ 뒤에 있고,
// 여기서는 받아 온 문자열을 해석하고 무엇을 할지 정하기만 한다. 그래야 리눅스에서 테스트된다.

// 릴리스 zip 의 이름. release.yml 이 만드는 것과 같아야 한다.
inline constexpr const char* kAssetName = "sched-win-x64.zip";

struct ReleaseInfo {
    util::Version version;
    std::string tag;
    std::string assetName;
    std::string assetUrl;
    std::string checksumUrl;  // SHA256SUMS.txt. 없으면 비어 있다
};

// GitHub Releases API 응답에서 필요한 것만 뽑는다.
util::Result<ReleaseInfo> parseLatestRelease(const std::string& json,
                                             const std::string& assetName = kAssetName);

// SHA256SUMS.txt 에서 그 파일의 체크섬을 찾는다. "<hex>  <파일명>" 형식이다.
std::optional<std::string> findChecksumFor(const std::string& sumsFile,
                                           const std::string& assetName);

enum class UpdateDecision {
    Disabled,   // config 에서 껐다
    UpToDate,   // 이미 최신
    Available,  // 새 버전이 있다
};

struct UpdateStatus {
    UpdateDecision decision{UpdateDecision::UpToDate};
    util::Version current;
    std::optional<ReleaseInfo> release;
};

// 현재 버전과 릴리스를 견준다.
UpdateStatus decideUpdate(const domain::Config& config, const util::Version& current,
                          const std::optional<ReleaseInfo>& release);

// GitHub API 비인증 한도는 시간당 60회다. 마지막 확인 시각을 남겨 하루 1회로 제한한다 (DESIGN 7.2).
// 파일이 없으면 "확인한 적 없음" 이므로 true.
bool shouldCheckNow(const std::filesystem::path& dataDir, const util::DateTime& now,
                    int intervalHours);
util::Result<void> recordCheck(const std::filesystem::path& dataDir, const util::DateTime& now);

// {owner}/{repo} 에서 최신 릴리스 API 주소를 만든다. repo 형식이 아니면 비어 있는 값.
std::optional<std::string> latestReleaseUrl(const std::string& repo);

}  // namespace app
