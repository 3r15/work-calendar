#pragma once

namespace platform {

// storage::atomicWrite() 가 쓸 파일 교체 구현을 등록한다. 프로그램 시작 시 한 번 부른다.
//
// Windows 의 rename 은 대상 파일이 이미 있으면 실패한다. ReplaceFileW 는 교체를 원자적으로
// 처리하고 ACL 과 타임스탬프도 보존한다. 이 호출이 platform/ 에 있는 이유는 core/ 가
// <windows.h> 를 포함하면 안 되기 때문이다 (CLAUDE.md 1장).
void installFileSwap();

}  // namespace platform
