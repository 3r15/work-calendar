#pragma once

#include "core/util/result.h"

#include <filesystem>
#include <string_view>

namespace storage {

// 데이터 파일 쓰기는 전부 이 함수를 거친다 (D-008, CLAUDE.md 1장).
// ofstream 으로 데이터 파일을 직접 덮어쓰지 말 것.
//
// 순서:
//   1. 같은 폴더의 <target>.tmp 에 쓰고 닫는다 (같은 볼륨이어야 교체가 원자적이다)
//   2. 기존 파일이 있으면 <target>.bak 으로 한 세대 남긴다
//   3. tmp 를 target 으로 교체한다
//
// 어느 단계에서 실패해도 원본은 그대로 남는다. 실패하면 tmp 를 지우고 오류를 돌려준다.
util::Result<void> atomicWrite(const std::filesystem::path& target, std::string_view content);

// 파일 교체 구현. 기본은 std::filesystem::rename 이다 (D-008 의 "그 외" 경로).
//
// Windows 는 대상 파일이 있으면 rename 이 실패하므로 ReplaceFileW 를 써야 한다. 그 호출은
// platform/ 뒤에 있으므로, cli 가 시작할 때 platform::installFileSwap() 으로 여기에 꽂는다.
// core 가 platform 을 include 하지 않고도 플랫폼 구현을 쓰는 방법이다.
using ReplaceFileFn = bool (*)(const std::filesystem::path& from, const std::filesystem::path& to);
void setReplaceFile(ReplaceFileFn fn);

// 테스트에서 교체 실패를 흉내 낼 때 되돌리기 위해 현재 구현을 꺼내 본다.
ReplaceFileFn replaceFile();

}  // namespace storage
