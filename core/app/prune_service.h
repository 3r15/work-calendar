#pragma once

#include "core/util/date.h"
#include "core/util/result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace app {

// 오래된 배정 스냅샷을 한 달치씩 합쳐 아카이브로 옮긴다 (D-005).
//
// 매일 파일이 하나씩 쌓이면 3년이면 천 개가 넘는다. 그렇다고 지울 수는 없다 — 공정성 기능을
// 나중에 켜려면 과거 이력이 필요하다. 그래서 줄 단위 JSON(jsonl)으로 합쳐 둔다. 나중에
// 집계할 때 스트리밍으로 읽을 수 있다.
inline constexpr int kDefaultKeepDays = 90;

struct PruneResult {
    int archived{0};                    // 아카이브로 옮긴 스냅샷 수
    std::vector<std::string> touched;   // 건드린 아카이브 파일 이름 (YYYY-MM.jsonl)
};

// today 기준으로 keepDays 보다 오래된 것만 옮긴다. dryRun 이면 세기만 하고 파일은 건드리지 않는다.
util::Result<PruneResult> pruneSnapshots(const std::filesystem::path& dataDir,
                                         const util::Date& today, int keepDays, bool dryRun);

}  // namespace app
