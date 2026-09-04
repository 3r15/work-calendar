#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace app {

// 데이터 폴더를 정한다. 우선순위는 --data-dir > SCHED_DATA_DIR > 실행 파일 옆 ./data (D-011).
//
// 인자는 CLI 가 파싱한 값과 프로그램 위치다. 환경 변수 읽기는 표준 라이브러리로 되므로
// platform/ 없이 core 에 둘 수 있다.
std::filesystem::path resolveDataDir(const std::optional<std::string>& fromOption,
                                     const std::filesystem::path& executableDir);

}  // namespace app
