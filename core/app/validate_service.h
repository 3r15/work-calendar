#pragma once

#include "core/domain/finding.h"
#include "core/util/result.h"

#include <filesystem>

namespace app {

// sched validate 의 알맹이. CLI 는 이 결과를 화면에 그리기만 한다 (CLAUDE.md 2장).
// GUI 도 같은 함수를 부른다.
//
// 파일이 아예 읽히지 않으면(없거나 JSON 이 깨졌으면) Result 가 실패한다.
// 읽히기는 하는데 내용이 이상한 경우는 Report 에 담겨 성공으로 돌아온다.
util::Result<domain::Report> validateDataDir(const std::filesystem::path& dataDir);

}  // namespace app
