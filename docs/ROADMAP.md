# 로드맵

각 Phase 는 완료 기준을 전부 만족해야 다음으로 넘어갑니다. 완료하면 체크하고 `CLAUDE.md` 8장의 현재 Phase 를 갱신하세요.

---

## Phase 0 — 프로젝트 골격

- [x] CMakeLists.txt (core / platform / cli / tests 타겟)
- [x] CMakePresets.json 은 이미 제공됨. 프리셋 이름에 맞춰 구성
- [x] vcpkg.json 매니페스트 (nlohmann-json, cli11, catch2)
- [x] VSCode `.vscode/` 설정 (tasks, launch, c_cpp_properties)
- [x] MSVC `/utf-8` 플래그, GCC/Clang 경고 옵션
- [x] `platform/console.h` + Windows 구현 — `initConsole()`, `enableAnsi()`
- [x] `core/util/display_width.h` — East Asian Width 기반 폭 계산
- [x] `core/util/result.h` — 오류 반환 타입
- [x] `tests/util/test_display_width.cpp`
- [x] `sched --version` 이 동작

**완료 기준**

1. `cmake --build --preset debug` 성공
2. `cmake --build --preset debug-core-only` 가 리눅스에서 성공
3. `ctest --preset debug` 통과
4. `displayWidth("김철수") == 6`, `displayWidth("abc") == 3`, `displayWidth("김a") == 3` 이 테스트로 검증됨
5. Windows 콘솔에서 `sched --version` 이 한글 포함 문자열을 깨짐 없이 출력

**주의** — `core/` 에 `<windows.h>` 가 들어가면 2번이 깨집니다. 그게 이 단계의 핵심 검증입니다.

**현재 상태** — 2번과 3번(코어 전용 빌드·테스트 14건)은 리눅스에서 확인했습니다. 1번·3번(전체)·5번은
Windows 가 필요하므로 CI 의 windows 잡 결과로 확인합니다. 전부 초록이 된 뒤 `CLAUDE.md` 9장의
현재 Phase 를 1 로 올리세요.

---

## Phase 1 — 도메인 모델과 저장

- [ ] `core/domain/` 엔티티 (`Worker`, `Task`, `TaskSet`, `Category`, `TimeSlot`, `Absence`, `WorkCalendar`)
- [ ] 강타입 ID 래퍼 (`WorkerId`, `TaskId`, `TaskSetId`, `CategoryId`, `TimeSlotId`)
- [ ] `core/storage/json_io.h` — 5개 파일의 로드·저장
- [ ] `storage::atomicWrite()` + `.bak` 백업 (플랫폼 호출은 `platform/fileswap.h` 뒤로)
- [ ] `conflictsWith` 양방향 정규화
- [ ] `core/domain/validator.h` — `DATA-SCHEMA.md` 의 오류·경고 규칙 전부
- [ ] `sched validate` 명령
- [ ] `data-sample/` 을 픽스처로 쓰는 테스트

**완료 기준**

1. `data-sample/` 을 읽고 다시 써도 의미가 동일 (round-trip 테스트 통과)
2. 일부러 망가뜨린 데이터 8종에 대해 `validate` 가 정확한 오류를 냄
3. 쓰기 도중 중단을 시뮬레이션해도 원본이 손상되지 않음
4. `cpp-reviewer` 에이전트 검사에서 심각 항목 없음

---

## Phase 2 — 날짜와 시간대

- [ ] `core/util/date.h` — `std::chrono` 래퍼, 요일 판정, 파싱·포맷
- [ ] 시스템 로컬 시각 조회 (플랫폼 독립. `std::chrono::current_zone()`)
- [ ] "지금이 어느 시간대인가" 판정
- [ ] 시간대에 속하지 않는 시각 → 다음 시간대 반환 (D-003)
- [ ] 근무일·공휴일 판정
- [ ] `sched today` — 배정 없이 구조만 출력
- [ ] 시각을 주입할 수 있는 인터페이스 (테스트용 `IClock`)

**완료 기준**

1. 임의 시각 20개를 주입해 올바른 시간대를 반환하는 테스트 통과
2. 경계값(시간대 시작 정각, 종료 정각, 자정) 동작이 문서와 일치
3. 점심시간에 `today` 를 실행하면 다음 시간대 안내가 나옴

---

## Phase 3 — 배정 엔진 ★ 가장 위험

**반드시 `test-first` 에이전트로 테스트를 먼저 작성합니다.**

- [ ] `IAssignmentPolicy` 인터페이스 + `RoundRobinPolicy`
- [ ] 배타 그래프 연결 요소 분해, 필요 인원 합 내림차순 정렬
- [ ] 커서 순회 + 부하 균형 tiebreak
- [ ] 미배정 슬롯 기록 (예외를 던지지 않음)
- [ ] `crossSetConflicts` 옵션 지원 (D-002)
- [ ] 커서 영속화 (`rr-cursor.json`)
- [ ] 스냅샷 저장·로드
- [ ] 남는 인원 처리 (D-004)

**테스트 목록**

- [ ] 기준 시나리오 4명 → 청소기 A·B / 밀대 C·D / 소독 A·B / 정리 C·D
- [ ] 인원 부족 3명 → 미배정 정확히 1건
- [ ] 인원 초과 8명 → 중복 배정 없음, 남는 사람 존재
- [ ] 요일 필터로 작업이 제외됨
- [ ] 배타 그룹 2개 이상 공존
- [ ] 배타가 한쪽에만 적혀도 대칭 동작
- [ ] `crossSetConflicts: true` 일 때 집합 간 배타 적용
- [ ] 커서 이월 — 이틀 연속 실행 시 시작 인원이 달라짐
- [ ] 결정론 — 같은 입력 두 번 계산 시 동일 결과
- [ ] 가용 인원 0명 → 전부 미배정, 크래시 없음

**완료 기준**

1. 위 테스트 전부 통과
2. 기준 시나리오 결과가 `DESIGN.md` 3.3 표와 정확히 일치
3. 엔진이 파일 IO 를 직접 하지 않음 (입력은 전부 인자로 받음)

---

## Phase 4 — 휴무

- [ ] 단발·기간·정기 휴무 등록과 해제
- [ ] 사전 휴무 = 가용 인원에서 제외
- [ ] 당일 급휴 = 배정 유지 + `absentAssignee` 플래그 (D-009)
- [ ] 급휴 판정 로직 (`createdAt` 과 스냅샷 존재 여부)
- [ ] `assign --force` 재배정 경로
- [ ] `sched off` 명령군

**완료 기준**

1. 당일 휴무 등록 직후 `sched now` 에 `(휴무)` 가 즉시 반영
2. 휴무 취소 시 `(휴무)` 가 사라짐
3. 사전 휴무자는 애초에 배정되지 않음
4. `--force` 재배정 시 휴무자가 빠지고 미배정이 줄어듦

---

## Phase 5 — CLI 완성

- [ ] `CLI-SPEC.md` 의 전체 명령
- [ ] 색상 출력 + `--no-color` + `NO_COLOR`
- [ ] `displayWidth()` 기반 표 정렬
- [ ] `--json` 출력 모드
- [ ] 사람 말로 쓴 오류 메시지 (`CLI-SPEC.md` 참고)
- [ ] `state/.lock` 파일 락 (D-007)
- [ ] `sched prune` 아카이브 (D-005)
- [ ] 이름으로 작업자 지정 시 모호성 처리

**완료 기준**

1. 설정 파일을 손으로 고치지 않고 CLI 만으로 전체 운영 가능
2. 두 프로세스가 동시에 쓰기를 시도하면 두 번째가 안전하게 중단
3. 한글이 섞인 표가 어긋나지 않음 (실제 Windows 콘솔에서 눈으로 확인)
4. 잘못된 입력 10종에 대해 오류 메시지가 원인과 해결책을 모두 제시

---

## Phase 6 — 자동 업데이트와 릴리스

- [ ] `platform/http.h` + WinHTTP 구현
- [ ] GitHub Releases API 호출, semver 비교
- [ ] asset 다운로드 + SHA256 검증
- [ ] `updater.exe` — 부모 프로세스 종료 대기 → 교체 → 재실행
- [ ] 하루 1회 확인 제한 (`state/` 에 마지막 확인 시각)
- [ ] `sched update check|apply`
- [ ] 스키마 마이그레이션 훅
- [ ] `.github/workflows/release.yml` 로 `v0.1.0` 첫 릴리스

**완료 기준**

1. v0.1.0 설치본이 v0.1.1 을 감지·다운로드·검증·교체·재실행
2. 체크섬이 틀리면 교체하지 않고 중단
3. 네트워크가 없어도 프로그램이 정상 시작
4. 업데이트 후 `data/` 가 그대로 남아 있음

**주의** — Windows 는 실행 중인 exe 를 덮어쓸 수 없습니다. `updater.exe` 를 별도 프로세스로 두는 구조를 처음부터 잡으세요.

---

## Phase 7 — 사용설명서

- [ ] `README.md` 전면 작성 (`README-PLAN.md` 의 구조와 톤 규칙 준수)
- [ ] 스크린샷 또는 실제 출력 예시
- [ ] `data-sample/` 을 최종 스키마에 맞춰 갱신
- [ ] 릴리스 zip 에 샘플 데이터 포함

**완료 기준**

1. 이 프로젝트를 모르는 사람이 README 만 보고 설치부터 첫 조회까지 성공
2. "작업집합이 왜 필요한가" 가 설명되어 있음
3. 자주 겪는 문제 3가지(미배정, 급휴, 표 깨짐)에 대한 답이 있음

---

## Phase 8 — GUI 이식

프레임워크는 이 시점에 다시 판단합니다 (`DESIGN.md` 10장).

- [ ] 프레임워크 선택과 근거를 `DECISIONS.md` 에 기록
- [ ] 오늘 보기 화면
- [ ] 휴무 토글
- [ ] 설정 편집
- [ ] `core/app` 서비스 재사용 확인 — 새로 쓴 로직이 있으면 잘못된 것

**완료 기준**

1. `core/` 를 한 줄도 고치지 않고 GUI 가 동작
2. CLI 와 GUI 가 같은 데이터 폴더를 문제없이 공유
