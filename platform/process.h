#pragma once

namespace platform {

// 현재 프로세스 ID. 쓰기 락 파일에 남겨 어느 프로세스가 잡고 있는지 알린다 (D-007).
// GetCurrentProcessId 는 Windows API 라서 core/ 에 둘 수 없다.
long processId();

// 그 PID 의 프로세스가 아직 살아 있는가.
// 확실하지 않으면 true 를 돌려준다 — 살아 있는 것을 죽었다고 보는 쪽이 더 위험하다.
bool processAlive(long pid);

}  // namespace platform
