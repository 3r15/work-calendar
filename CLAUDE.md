# CLAUDE.md

작업 스케줄러. Windows / C++20 / CMake. CLI 먼저, 그다음 GUI 이식.

## 문서 위치

| 필요할 때 | 읽을 것 |
|---|---|
| 왜 이렇게 설계했는지 | `docs/DECISIONS.md` |
| 아키텍처, 배정 알고리즘 | `docs/DESIGN.md` |
| JSON 필드 정의, 검증 규칙 | `docs/DATA-SCHEMA.md` |
| 명령어, 출력 형식, 종료 코드 | `docs/CLI-SPEC.md` |
| 지금 뭘 해야 하는지 | `docs/ROADMAP.md` |

도메인 개념과 배정 규칙 요약은 `scheduler-domain` 스킬에 있습니다. `core/scheduling`, `core/domain`, `core/storage` 를 건드리기 전에 부르세요.

---

## 1. 절대 규칙

**`core/` 에서 `<windows.h>` 를 포함하지 않습니다.**
플랫폼 의존 코드는 전부 `platform/` 뒤에 인터페이스로 숨깁니다. GUI 이식 시 코어를 재사용하고, 클라우드(리눅스) 세션에서 코어를 빌드·테스트할 수 있어야 하기 때문입니다. CI 의 ubuntu 잡이 이 규칙을 자동으로 검사합니다.

**`main` 브랜치에 직접 커밋하지 않습니다.**
작업은 `develop` 또는 `feature/*` 에서. `main` 은 릴리스 태그만 받습니다. 훅이 차단합니다.

**한글 처리를 임의로 바꾸지 않습니다.**
소스는 BOM 없는 UTF-8, 빌드는 `/utf-8`, 콘솔은 `platform::initConsole()` 을 거칩니다. 표 정렬은 `core/util/display_width.h` 의 `displayWidth()` 를 쓰고 `std::setw` 를 문자 수 기준으로 쓰지 않습니다. 한글은 폭 2 입니다.

**데이터 파일 쓰기는 항상 `storage::atomicWrite()`.**
`ofstream` 으로 데이터 파일을 직접 덮어쓰지 마세요.

**설계 결정을 말없이 바꾸지 않습니다.**
`docs/DECISIONS.md` 와 다르게 하는 게 낫다고 판단되면 먼저 이유를 말하고 확인을 받으세요. 승인되면 `DECISIONS.md` 에 새 항목으로 기록합니다.

---

## 2. 디렉터리

```
core/          플랫폼 독립. domain / storage / scheduling / app / util
platform/      Windows 전용 (콘솔, 파일 교체, WinHTTP). 인터페이스 + win 구현
cli/           명령 파싱과 렌더링만. 비즈니스 로직 금지
updater/       별도 exe. 실행 중 exe 교체용
tests/         Catch2
docs/          설계 문서
data-sample/   샘플 데이터. 테스트 픽스처로도 사용
```

의존 방향은 `domain ← storage ← scheduling ← app ← cli` 입니다. 역방향 include 금지.

`cli/` 에 로직이 생기면 GUI 이식 때 다시 써야 합니다. 계산은 `core/app` 의 서비스 함수로 올리세요.

---

## 3. 빌드와 테스트

```bash
cmake --preset debug && cmake --build --preset debug
ctest --preset debug --output-on-failure
```

리눅스(클라우드 세션 포함)에서는 코어만:

```bash
cmake --preset debug-core-only && cmake --build --preset debug-core-only
ctest --preset debug-core-only --output-on-failure
```

코드를 고쳤으면 커밋 전에 반드시 테스트를 돌립니다. 통과하지 않은 상태로 커밋하지 마세요.

---

## 4. 도메인 요점

작업집합 소속은 **작업이 갖습니다**. `Task.taskSetIds[]` 가 유일한 출처이고, `TaskSet` 은 `id` 와 `displayName` 만 갖습니다. 이 방향을 뒤집지 마세요 (D-001).

`conflictsWith` 는 "한 작업자가 같은 시간대에 두 작업을 동시에 맡을 수 없다" 는 뜻입니다. 대칭이며, 모든 작업은 암묵적으로 자기 자신과 배타입니다.

`priority` 는 스키마에만 존재합니다. 엔진은 읽지 않습니다 (D-010).

---

## 5. 배정 엔진 작업 규칙

`core/scheduling/` 을 건드릴 때는 **`test-first` 에이전트로 테스트를 먼저 씁니다.** 규칙이 미묘해서 나중에 테스트를 붙이면 잘못된 동작을 그대로 고정시킵니다.

기준 시나리오(`tests/scheduling/test_roundrobin.cpp`)는 변경 금지입니다.
작업자 4명, 청소기(2)·밀대(2)·소독(2)·정리(2), 청소기↔밀대 배타
→ 청소기 A·B / 밀대 C·D / 소독 A·B / 정리 C·D

이 결과가 바뀌는 변경은 설계 변경이므로 먼저 물어보세요.

엔진은 파일 IO 를 하지 않습니다. 입력은 전부 인자로 받습니다.

---

## 6. 커밋과 브랜치

- Conventional Commits: `feat:` `fix:` `docs:` `test:` `refactor:` `chore:`
- `feature/<짧은-이름>` → `develop`
- `release/vX.Y.Z` → `main` + 태그 → `develop` 역병합
- `hotfix/*` → `main` + `develop`
- 커밋은 한 가지 일만. 포맷팅과 로직 변경을 섞지 마세요.

---

## 7. 코드 스타일

- C++20. 예외보다 `Result<T>` 반환을 선호합니다.
- 소유권은 값과 `unique_ptr`. `new`/`delete` 직접 사용 금지.
- 도메인 ID 는 강타입 래퍼(`WorkerId`, `TaskId`)를 씁니다. 인자 순서를 바꿔도 컴파일이 막아줍니다.
- 헤더는 `#pragma once`. 헤더에 `using namespace` 금지.
- 주석은 "무엇"이 아니라 "왜".
- 사용자에게 보이는 문자열은 한국어. 식별자와 주석은 영어여도 됩니다.
- Catch2 `TEST_CASE` 이름은 ASCII 로 씁니다. ctest 가 이름을 필터 인자로 넘기는데 Windows 에서
  한글이 `?` 로 깨져 테스트가 하나도 실행되지 않습니다. 설명은 바로 위 주석과 `SECTION` 에
  한국어로 씁니다.

---

## 8. 하지 말 것

- 요구사항에 없는 기능 추가
- 데이터 스키마를 바꾸면서 `version` 을 올리지 않기
- 배정 결과를 조회할 때마다 새로 계산하기 (그날 스냅샷을 읽어야 함)
- 당일 급휴 시 자동 재배정 (D-009)
- `workers.json` 배열 순서를 정렬하기 (라운드 로빈 기준 순서가 깨짐)
- 배정 실패 시 예외 던지기 (미배정 슬롯으로 기록해야 함)

---

## 9. 진행 상황

현재 Phase: **4 (휴무)**

Phase 를 마치면 `docs/ROADMAP.md` 의 체크박스를 채우고 이 줄을 갱신하세요.
