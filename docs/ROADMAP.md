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

**완료** — 1~4번은 CI 에서 확인했습니다 (windows-latest 전체 빌드·테스트 14건, ubuntu 코어 전용 빌드).
5번은 CI 가 `sched --version` 의 출력 바이트를 검사해 UTF-8 이 온전한 것까지 확인합니다.

```
0000000   s   c   h   e   d       0   .   1   .   0     342 200 224
0000020 354 236 221 354 227 205     354 212 244 354 274 200 354 244 204
0000040 353 237 254  \n
```

파이프로 받은 것이므로 **실제 콘솔 렌더링은 Windows PC 에서 한 번 눈으로 확인**해 주세요.
폰트가 한글 글리프를 갖고 있지 않으면 프로그램이 옳아도 네모로 보입니다.

Phase 0 에서 배운 것: `TEST_CASE` 이름에 한글을 쓰면 안 됩니다. ctest 가 이름을 필터 인자로
넘기는데 Windows 가 `argv` 를 ANSI 코드 페이지로 변환하면서 `?` 로 날려버립니다. 같은 문제가
Phase 4~5 에서 `--worker 김철수` 같은 한글 인자로 다시 옵니다.

---

## Phase 1 — 도메인 모델과 저장

- [x] `core/domain/` 엔티티 (`Worker`, `Task`, `TaskSet`, `Category`, `TimeSlot`, `Absence`, `WorkCalendar`)
- [x] 강타입 ID 래퍼 (`WorkerId`, `TaskId`, `TaskSetId`, `CategoryId`, `TimeSlotId`)
- [x] `core/storage/json_io.h` — 5개 파일의 로드·저장
- [x] `storage::atomicWrite()` + `.bak` 백업 (플랫폼 호출은 `platform/fileswap.h` 뒤로)
- [x] `conflictsWith` 양방향 정규화
- [x] `core/domain/validator.h` — `DATA-SCHEMA.md` 의 오류·경고 규칙 전부
- [x] `sched validate` 명령
- [x] `data-sample/` 을 픽스처로 쓰는 테스트

**완료 기준**

1. `data-sample/` 을 읽고 다시 써도 의미가 동일 (round-trip 테스트 통과)
2. 일부러 망가뜨린 데이터 8종에 대해 `validate` 가 정확한 오류를 냄
3. 쓰기 도중 중단을 시뮬레이션해도 원본이 손상되지 않음
4. `cpp-reviewer` 에이전트 검사에서 심각 항목 없음

**Phase 1 에서 더한 것**

- `core/util/korean.h` — 받침에 따라 조사를 고릅니다. 오류 메시지에 작업 이름이 그대로 들어가는데
  `작업 "밀대"이` 처럼 어긋나면 프로그램이 한국어를 모르는 것처럼 보입니다. 이름은 사용자 데이터라
  실행 중에 골라야 합니다.
- 날짜·시각 문자열의 모양 검사 — `from > to` 나 `start >= end` 비교가 뜻을 가지려면 형식이 먼저
  맞아야 합니다. `DATA-SCHEMA.md` 의 규칙 목록에는 없지만 그 규칙들이 작동하기 위한 전제입니다.
- 파싱 단계 발견(알 수 없는 요일 코드, `version` 범위)을 `storage` 가 `Report` 로 들고 나와
  의미 검사 결과와 합칩니다. 첫 오류에서 멈추지 않는다는 요구를 지키려면 두 단계의 발견이 같은
  보고서에 있어야 합니다.

---

## Phase 2 — 날짜와 시간대

- [x] `core/util/date.h` — `std::chrono` 래퍼, 요일 판정, 파싱·포맷
- [x] 시스템 로컬 시각 조회 (플랫폼 독립. `std::chrono::current_zone()`)
- [x] "지금이 어느 시간대인가" 판정
- [x] 시간대에 속하지 않는 시각 → 다음 시간대 반환 (D-003)
- [x] 근무일·공휴일 판정
- [x] `sched today` — 배정 없이 구조만 출력
- [x] 시각을 주입할 수 있는 인터페이스 (테스트용 `IClock`)

**완료 기준**

1. 임의 시각 20개를 주입해 올바른 시간대를 반환하는 테스트 통과
2. 경계값(시간대 시작 정각, 종료 정각, 자정) 동작이 문서와 일치
3. 점심시간에 `today` 를 실행하면 다음 시간대 안내가 나옴

**Phase 2 에서 정한 것**

- 시간대 구간은 반열림 `[start, end)` 입니다. 문서에 정해져 있지 않던 것이라 `DECISIONS.md`
  D-012 로 기록했습니다. 12:00 은 오전이 아니라 "다음은 평일 오후" 입니다.
- `SystemClock` 은 시간대 데이터베이스가 없는 환경에서 UTC 로 떨어집니다. 시각이 어긋나는 것이
  아예 실행되지 않는 것보다 낫다고 봤습니다.

---

## Phase 3 — 배정 엔진 ★ 가장 위험

**반드시 `test-first` 에이전트로 테스트를 먼저 작성합니다.**

- [x] `IAssignmentPolicy` 인터페이스 + `RoundRobinPolicy`
- [x] 배타 그래프 연결 요소 분해, 필요 인원 합 내림차순 정렬
- [x] 커서 순회 + 부하 균형 tiebreak
- [x] 미배정 슬롯 기록 (예외를 던지지 않음)
- [x] `crossSetConflicts` 옵션 지원 (D-002)
- [x] 커서 영속화 (`rr-cursor.json`)
- [x] 스냅샷 저장·로드
- [x] 남는 인원 처리 (D-004)

**테스트 목록**

- [x] 기준 시나리오 4명 → 청소기 A·B / 밀대 C·D / 소독 A·B / 정리 C·D
- [x] 인원 부족 3명 → 미배정 정확히 1건
- [x] 인원 초과 8명 → 중복 배정 없음, 남는 사람 존재 (10명으로 검증)
- [x] 요일 필터로 작업이 제외됨
- [x] 배타 그룹 2개 이상 공존
- [x] 배타가 한쪽에만 적혀도 대칭 동작
- [x] `crossSetConflicts: true` 일 때 집합 간 배타 적용
- [x] 커서 이월 — 이틀 연속 실행 시 시작 인원이 달라짐
- [x] 결정론 — 같은 입력 두 번 계산 시 동일 결과
- [x] 가용 인원 0명 → 전부 미배정, 크래시 없음

**완료 기준**

1. 위 테스트 전부 통과
2. 기준 시나리오 결과가 `DESIGN.md` 3.3 표와 정확히 일치
3. 엔진이 파일 IO 를 직접 하지 않음 (입력은 전부 인자로 받음)

**Phase 3 에서 정한 것**

- **커서는 배정에 성공했을 때만 전진합니다.** DESIGN 3.2 의 "커서 += 1" 은 위치가 애매한데,
  실패했을 때도 전진시키면 `SKILL.md` 의 3명 예시(밀대 C·미배정 → 소독이 A 부터 시작)가 맞지
  않습니다. 한 바퀴 순회는 정확히 N칸이라 제자리로 돌아온 것과 같다고 해석했습니다.
- **부하 균형은 이번 호출 안에서만 셉니다.** `crossSetConflicts` 가 꺼져 있으면 작업집합끼리
  독립이므로(D-002) 앞선 집합의 배정을 부하로 세지 않습니다. 세면 한쪽 집합의 선택이 다른
  집합의 선택을 좌우해 "독립" 이 아니게 됩니다.
- `sched assign` 과 `sched today` 의 배정 표시를 앞당겨 넣었습니다. ROADMAP 은 명령 전체를
  Phase 5 로 두지만, 엔진이 실제로 맞는지 손으로 확인할 방법이 필요했습니다.

---

## Phase 4 — 휴무

- [x] 단발·기간·정기 휴무 등록과 해제
- [x] 사전 휴무 = 가용 인원에서 제외
- [x] 당일 급휴 = 배정 유지 + `absentAssignee` 플래그 (D-009)
- [x] 급휴 판정 로직 (`createdAt` 과 스냅샷 존재 여부)
- [x] `assign --force` 재배정 경로
- [x] `sched off` 명령군

**완료 기준**

1. 당일 휴무 등록 직후 `sched now` 에 `(휴무)` 가 즉시 반영
2. 휴무 취소 시 `(휴무)` 가 사라짐
3. 사전 휴무자는 애초에 배정되지 않음
4. `--force` 재배정 시 휴무자가 빠지고 미배정이 줄어듦

**Phase 4 에서 정한 것**

- **4번은 "실제로 사람이 없는 자리" 기준으로 읽습니다.** 급휴 직후 스냅샷에는 미배정이 0건이지만
  휴무자가 4자리를 붙들고 있습니다. `--force` 로 다시 계산하면 미배정이 2건이 되는데, 숫자만 보면
  늘어난 것 같아도 실제로 비는 자리는 4 → 2 로 줄었습니다. 테스트는 이 기준으로 씁니다.
- **급휴 판정에 배정 건수를 함께 봅니다.** `createdAt` 의 날짜가 `from` 과 같고 그날 스냅샷이
  있어도, 그 사람에게 배정된 것이 없으면 경고할 이유가 없습니다.
- **기간 휴무는 통째로 지웁니다.** 가운데 하루만 뚫는 요구는 아직 없습니다. 필요해지면 그때
  기간을 쪼개는 방식을 정하면 됩니다.
- **`--worker` 는 ID 를 이름보다 먼저 봅니다.** 누군가의 이름이 다른 사람의 ID 와 같아도
  ID 쪽이 이깁니다. 이름이 여러 명과 겹치면 후보를 보여주고 멈춥니다 — 조용히 한 명을 고르면
  엉뚱한 사람이 쉬게 됩니다.

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
