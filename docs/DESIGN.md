# 설계 문서

- **플랫폼**: Windows / C++20 / CMake / VSCode
- **1단계**: CLI 완성 → **2단계**: 동일 코어 위에 GUI 이식
- **배포**: GitHub + gitflow, 1인 개발, 자동 업데이트 내장

확정된 설계 판단과 그 근거는 `DECISIONS.md` 에 있습니다. 이 문서와 충돌하면 `DECISIONS.md` 가 우선입니다.

---

## 1. 도메인 모델

### 1.1 개념 계층

```
근무 달력(WorkCalendar)
 └ 시간대(TimeSlot)          예: 오전 09:00–12:00 (화~금)
    └ 작업 분류(Category)     예: "평일 오전"      ← 가시성 단위
       └ 작업 집합(TaskSet)   예: 청소, 배치구역    ← 배정 단위
          └ 작업(Task)        예: 청소기(2명)
작업자(Worker) ─ 휴무(Absence)
```

두 가지를 먼저 못 박아 둡니다.

- **작업 집합 = 배정이 일어나는 단위.** 라운드 로빈 커서와 상호 배타 검사가 여기서 작동합니다.
- **작업 분류 = 화면에 묶어 보여주는 단위.** 배정 로직에 관여하지 않고 시간대에 붙습니다.

### 1.2 소속 방향

작업집합 소속은 **작업이 갖습니다**(D-001). `TaskSet` 은 `id` 와 `displayName` 만 갖습니다.

```
청소기 (2명, 밀대와 배타)
  └ 소속: cleaning_am, cleaning_pm, cleaning_weekend
소독 (2명)
  └ 소속: cleaning_am
쓰레기 배출 (1명)
  └ 소속: cleaning_pm
```

`cleaning_am` 과 `cleaning_pm` 은 표시명이 둘 다 "청소" 이지만 별개의 집합입니다. 이 구조 덕분에 "오전과 오후의 청소가 일부 공통, 일부 다름" 이 중복 정의 없이 표현됩니다. **ID 와 표시명은 항상 분리**합니다.

### 1.3 엔티티

| 엔티티 | 필드 |
|---|---|
| `Worker` | `id`, `name`, `active`, `weeklyOff[]`, `note` |
| `Absence` | `workerId`, `from`, `to`, `reason`, `createdAt` |
| `Task` | `id`, `name`, `taskSetIds[]`, `requiredCount`, `weekdays[]`, `conflictsWith[]`, `priority` |
| `TaskSet` | `id`, `displayName` |
| `Category` | `id`, `displayName`, `taskSetIds[]` |
| `TimeSlot` | `id`, `displayName`, `start`, `end`, `weekdays[]`, `categoryIds[]` |
| `WorkCalendar` | `workWeekdays[]`, `holidays[]` |

`Task.weekdays` 는 선택입니다. 비어 있으면 근무일 전체를 뜻합니다.
`Task.priority` 는 로드·저장만 되고 엔진은 무시합니다(D-010).

정확한 JSON 형태는 `DATA-SCHEMA.md` 를 보세요.

### 1.4 상호 배타(conflictsWith)

`청소기.conflictsWith = ["밀대"]` 는 **한 작업자가 같은 시간대에 두 작업을 동시에 맡을 수 없다**는 뜻입니다.

- 관계는 대칭입니다. 로드할 때 양방향으로 정규화합니다.
- 모든 작업은 **암묵적으로 자기 자신과 배타**입니다. `requiredCount: 2` 인 작업은 반드시 서로 다른 두 사람에게 갑니다.
- 검사 범위는 기본이 작업집합 내부이고, `assignment.crossSetConflicts` 로 시간대 전체까지 넓힐 수 있습니다(D-002).

---

## 2. 저장 구조

기본 경로는 실행 파일 옆의 `./data` 입니다(D-011). 모든 파일은 UTF-8 JSON 이고, 사람이 직접 열어 고칠 수 있어야 하므로 바이너리는 쓰지 않습니다.

```
data/
├── config.json          근무 요일, 시간대, 분류, 배정 옵션
├── workers.json         작업자 + 정기 휴무
├── tasks.json           작업 집합과 작업
├── absences.json        단발성 휴무
└── state/
    ├── rr-cursor.json           작업집합별 라운드 로빈 커서
    ├── assign-2026-09-04.json   그날의 배정 스냅샷
    ├── .lock                    쓰기 락 (Phase 5)
    └── archive/2026-06.jsonl    90일 지난 스냅샷
```

쓰기는 전부 `storage::atomicWrite()` 를 통합니다(D-008). 임시 파일에 쓰고 교체하며, 교체 직전 `.bak` 을 남깁니다.

---

## 3. 배정 엔진

### 3.1 결정론과 스냅샷

같은 날 여러 번 조회했을 때 배정이 달라지면 안 됩니다.

1. 그날 최초 조회 시 계산하고 `state/assign-YYYY-MM-DD.json` 에 저장
2. 이후 조회는 스냅샷을 읽기만 함
3. 재계산은 `assign --force` 로 명시적으로만

### 3.2 알고리즘

작업집합 단위로 실행합니다.

```
입력: 날짜 D, 시간대 S, 작업집합 T, 작업자 명단 W
출력: 배정 목록 + 미배정 목록

1. 대상 작업 필터
   taskSetIds 에 T 를 포함하고, weekdays 에 D 의 요일이 포함된 작업만.

2. 가용 작업자
   available = W 중 active && D가 근무일 && D에 휴무 아님
   (순서는 workers.json 의 정의 순서. 임의 정렬 금지 — 결정론이 깨진다)

3. 배타 그룹 분해
   conflictsWith 를 간선으로 보는 그래프의 연결 요소를 구한다.
   필요 인원 합이 큰 순서로 정렬한다.
   합이 같으면 작업집합 내 첫 작업의 정의 순서로 정렬한다.
   → 제약이 강한 그룹부터 배정해야 실패가 줄어든다.

4. 그룹별 배정
   for each 그룹 g (정렬된 순서):
     for each 작업 t in g (정의 순서):
       for i in 1..t.requiredCount:
         커서 위치부터 available 을 한 바퀴 순회하며 첫 적격자 선택
           적격 조건:
             - 이미 t 에 배정되지 않았을 것
             - t 와 배타 관계인 작업에 배정되지 않았을 것
               (검사 범위는 crossSetConflicts 설정에 따름)
           동점 후보 중에서는 현재 시간대 배정 건수가 적은 사람 우선
         한 바퀴 돌아 후보가 없으면 → 미배정 슬롯 기록, 예외를 던지지 않음
         커서 += 1

5. 커서를 state/rr-cursor.json 에 저장
```

가용 인원이 슬롯 수보다 많으면 남는 사람은 그냥 남깁니다(D-004). 커서는 계속 전진하므로 오늘 빠진 사람이 내일 먼저 배정됩니다.

### 3.3 기준 시나리오

청소 집합, 작업자 4명(A B C D), 청소기(2)·밀대(2)·소독(2)·정리(2), 청소기↔밀대 배타.

슬롯 합계 8, 인원 4 → 1인당 2건.

| 순서 | 작업 | 배정 | 이유 |
|---|---|---|---|
| 1 | 청소기 | A, B | 배타 그룹(합 4)을 먼저, 커서 0에서 시작 |
| 2 | 밀대 | C, D | A·B 는 청소기 배타로 제외 |
| 3 | 소독 | A, B | 커서가 A 로 돌아옴, 배타 없음 |
| 4 | 정리 | C, D | |

인원이 3명(A B C)이면: 청소기 A·B / 밀대 C·**미배정 1** / 소독 A·B / 정리 C·A.

이 시나리오는 테스트로 고정되며 변경 금지입니다.

### 3.4 공정성 (설계만, 구현 보류)

```cpp
class IAssignmentPolicy {
public:
  virtual int score(const Worker&, const Task&, const Context&) const = 0;
  virtual ~IAssignmentPolicy() = default;
};
```

지금은 `RoundRobinPolicy` 만 구현합니다. 나중에 누적 배정 횟수, 최근 N일 기피 작업 이력, 숙련도 가중치를 `score` 에 넣으면 엔진 본체를 건드리지 않고 확장됩니다. 이를 위해 **누적 통계는 지금부터 `state/` 에 쌓아 둡니다.**

---

## 4. 휴무 처리

| 상황 | 동작 |
|---|---|
| 사전 등록 휴무 (배정 계산 전) | 가용 인원에서 제외하고 배정 |
| 당일 급휴 (스냅샷 이미 존재) | 기존 배정 **유지**, `absentAssignee: true` 플래그 부여 |

당일 급휴에서 자동 재배정하지 않는 이유는 `DECISIONS.md` D-009 에 있습니다. CLI 는 이렇게 안내합니다.

```
⚠ 김철수 휴무 처리됨. 3건이 휴무자에게 배정된 상태입니다.
  재배정하려면: sched assign --today --force
```

표시 규칙:
- 휴무자 배정 → 회색 + `(휴무)` 태그
- 미배정 슬롯 → 빨강 + `─ 미배정 ─`

---

## 5. Windows 콘솔 한글 처리

Phase 0 에서 처리합니다. 나중에 고치면 출력 코드 전체를 훑어야 합니다.

- 시작 시 `SetConsoleOutputCP(CP_UTF8)` + `SetConsoleCP(CP_UTF8)` — `platform::initConsole()`
- 소스는 BOM 없는 UTF-8, MSVC 에 `/utf-8` 플래그
- **표 정렬은 문자 개수가 아니라 표시 폭으로 계산.** `core/util/display_width.h` 의 `displayWidth()` 를 쓰고 한글·전각은 폭 2 로 셉니다. `std::setw` 를 그대로 쓰면 표가 어긋납니다.
- 색상은 ANSI escape + `ENABLE_VIRTUAL_TERMINAL_PROCESSING`, 실패 시 색 없이 출력
- `--no-color` 옵션 제공, `NO_COLOR` 환경 변수 존중

`displayWidth()` 는 `core/` 에 두되 Windows API 를 쓰지 않습니다. East Asian Width 판정은 유니코드 범위 테이블로 직접 구현합니다.

---

## 6. 아키텍처

GUI 이식과 클라우드 빌드를 동시에 가능하게 하는 가장 중요한 구조적 결정은 **코어를 플랫폼 독립 정적 라이브러리로 분리**하는 것입니다.

```
scheduler/
├── core/                  static lib. windows.h 금지
│   ├── domain/            엔티티, 값 객체, 불변식
│   ├── storage/           JSON 직렬화, 원자적 파일 IO
│   ├── scheduling/        배정 엔진, 정책 인터페이스
│   ├── app/               유스케이스 서비스
│   └── util/              display_width, date, result
├── platform/              인터페이스 + Windows 구현
│   ├── console.h          initConsole, enableAnsi
│   ├── fileswap.h         ReplaceFileW 래퍼
│   └── http.h             WinHTTP 래퍼
├── cli/                   core 링크. 파싱 + 렌더링만
├── gui/                   2단계. core 링크
├── updater/               별도 exe
└── tests/                 Catch2
```

의존 방향은 `domain ← storage ← scheduling ← app ← cli` 입니다. 역방향 include 는 금지입니다.

`core/app` 의 서비스 시그니처를 CLI 와 GUI 가 그대로 공유합니다.

```cpp
DayView   SchedulerService::getDayView(Date d);
SlotView  SchedulerService::getCurrentSlot(DateTime now);
Result<>  SchedulerService::markAbsent(WorkerId w, DateRange r, std::string reason);
Result<>  SchedulerService::clearAbsent(WorkerId w, Date d);
Result<>  SchedulerService::reassign(Date d, bool force);
```

CLI 에서 이 함수들만 호출하는 규율을 지키면 GUI 이식은 렌더링 코드만 새로 쓰면 됩니다.

### 6.1 의존성

| 용도 | 선택 | 이유 |
|---|---|---|
| 빌드 | CMake + vcpkg | VSCode/CI 모두 호환 |
| JSON | nlohmann/json | 헤더 온리, 사용이 단순 |
| 날짜/시간 | `std::chrono` (C++20) | `year_month_day`, `weekday` 내장 |
| CLI 파싱 | CLI11 | 헤더 온리 |
| 테스트 | Catch2 v3 | |
| HTTP | **WinHTTP** (OS 내장) | TLS 포함, 의존성 0, 배포 크기 작음 |

`core/` 는 nlohmann/json 과 표준 라이브러리만 씁니다.

---

## 7. 자동 업데이트

### 7.1 흐름

```
1. sched update check (또는 시작 시 하루 1회 자동)
   GET https://api.github.com/repos/{owner}/{repo}/releases/latest
2. tag_name(v1.2.3) 을 현재 버전과 semver 비교
3. 새 버전이면 asset(sched-win-x64.zip) 다운로드
4. SHA256 검증
5. updater.exe 실행 → 본체 종료
6. updater 가 본체 프로세스 종료 대기 → 파일 교체 → 본체 재실행
```

### 7.2 주의점

- **Windows 는 실행 중인 exe 를 덮어쓸 수 없습니다.** 반드시 별도 `updater.exe` 가 교체를 수행해야 합니다. 이걸 모르고 설계하면 Phase 6 에서 막힙니다.
- 시작 시 자동 확인은 비동기 + 실패 시 조용히 무시. 네트워크가 없다고 프로그램이 멈추면 안 됩니다.
- GitHub API 비인증 한도는 시간당 60회입니다. 마지막 확인 시각을 `state/` 에 기록해 하루 1회로 제한합니다.
- `--no-auto-update` 옵션과 `config.json` 스위치 제공.
- `data/` 는 절대 건드리지 않습니다. 교체 대상은 실행 파일과 리소스뿐입니다.
- 업데이트 후 첫 실행에서 스키마 `version` 을 확인하고 필요하면 마이그레이션합니다.

---

## 8. Git 전략과 CI/CD

### 8.1 브랜치

| 브랜치 | 용도 |
|---|---|
| `main` | 릴리스만. 태그 `v1.2.3` 이 붙는 곳 |
| `develop` | 통합 브랜치. 기본 작업 대상 |
| `feature/*` | `develop` 에서 분기 → `develop` 병합 |
| `release/*` | 버전 확정, 문서 갱신 → `main` + `develop` |
| `hotfix/*` | `main` 에서 분기 → `main` + `develop` |

혼자 개발해도 **`main` 에 직접 커밋하지 않는다** 는 규칙 하나만 지키면 gitflow 의 실익은 대부분 확보됩니다. `.claude/hooks/guard-branch.sh` 가 이를 강제합니다.

커밋 메시지는 Conventional Commits (`feat:`, `fix:`, `docs:`, `test:`, `refactor:`, `chore:`).

### 8.2 워크플로

- `ci.yml` — `develop`·`feature/*` push 와 PR 에서 실행. windows-latest 전체 빌드 + ubuntu-latest 코어 전용 빌드.
- `release.yml` — `v*` 태그 push 에서 실행. Windows 빌드 → zip → SHA256 → GitHub Release 발행.

ubuntu 잡의 목적은 "core 에 플랫폼 의존이 새어들지 않았는지" 를 자동으로 잡는 것입니다. 클라우드 세션에서 개발할 수 있게 해주는 안전망이기도 합니다.

---

## 9. 예상 일정

| Phase | 내용 | 기간 | 누적 |
|---|---|---|---|
| 0 | 프로젝트 골격, UTF-8, displayWidth | 1일 | 1일 |
| 1 | 도메인 모델, JSON 저장, validate | 3일 | 4일 |
| 2 | 날짜·시간대 해석 | 2일 | 6일 |
| 3 | 배정 엔진 ★ | 4일 | 10일 |
| 4 | 휴무 처리 | 2일 | 12일 |
| 5 | CLI 완성, 락, prune | 3일 | 15일 |
| 6 | 자동 업데이트, 릴리스 파이프라인 | 3일 | 18일 |
| 7 | 사용설명서 | 1일 | 19일 |

**CLI v1.0 까지 약 3~4주** (하루 몇 시간 기준). GUI 는 프레임워크 선택에 따라 추가 2~4주.

가장 위험한 구간은 **Phase 3(배정 엔진)** 과 **Phase 6(exe 자가 교체)** 입니다. Phase 3 은 테스트를 먼저 쓰는 방식으로, Phase 6 은 별도 프로세스 구조를 처음부터 잡는 방식으로 위험을 줄입니다.

단계별 작업 목록과 완료 기준은 `ROADMAP.md` 에 있습니다.

---

## 10. GUI 이식 (Phase 8)

| 후보 | 장점 | 단점 |
|---|---|---|
| Dear ImGui | 가볍고 단일 exe, C++ 친화적 | 폼·표 위젯을 직접 만들어야 함 |
| Qt Widgets | 표·폼이 강력, 한글 처리 성숙 | 배포가 무겁고 라이선스 고려 필요 |
| WinUI 3 | 네이티브 외관 | 학습 곡선, C++ 지원이 번거로움 |

이 프로젝트는 표 중심이라 Qt 가 기능적으로 가장 맞지만, 단일 exe 배포와 자동 업데이트 구조를 생각하면 Dear ImGui 가 어울립니다. Phase 8 시작 시점에 다시 판단합니다.

화면 우선순위: 오늘 보기 → 휴무 토글 → 설정 편집.
