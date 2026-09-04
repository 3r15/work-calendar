#pragma once

#include <filesystem>

namespace platform {

// 실행 파일이 있는 폴더. 기본 데이터 폴더가 그 옆의 ./data 이기 때문에 필요하다 (D-011).
//
// argv[0] 로는 부족하다. PATH 를 통해 실행되면 파일 이름만 들어오고, 심볼릭 링크를 거치면
// 엉뚱한 곳을 가리킨다. Windows 는 GetModuleFileNameW 가 정확한 답을 준다.
//
// 실패하면 현재 작업 폴더를 돌려준다. 프로그램을 멈추지 않는다.
std::filesystem::path executableDir();

}  // namespace platform
