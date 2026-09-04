# 데이터 스키마

모든 파일은 **BOM 없는 UTF-8 JSON**, 최상위에 `version` 정수 필드를 갖습니다. 현재 스키마 버전은 전부 `1` 입니다.

공통 규칙:

- ID 는 소문자 영숫자와 `_` 만 사용합니다. 한글 ID 는 금지입니다. 화면에 보이는 이름은 별도 필드입니다.
- 요일 코드는 `"SUN" | "MON" | "TUE" | "WED" | "THU" | "FRI" | "SAT"` 입니다.
- 날짜는 `"YYYY-MM-DD"`, 시각은 `"HH:MM"` (24시간제) 입니다.
- 선택 필드가 없거나 `null` 이면 기본값이 적용됩니다. 빈 배열과 없는 필드는 같게 취급합니다.

---

## config.json

```json
{
  "version": 1,
  "workCalendar": {
    "workWeekdays": ["TUE", "WED", "THU", "FRI", "SAT"],
    "holidays": ["2026-09-28", "2026-10-03"]
  },
  "timeSlots": [
    {
      "id": "am_weekday",
      "displayName": "평일 오전",
      "start": "09:00",
      "end": "12:00",
      "weekdays": ["TUE", "WED", "THU", "FRI"],
      "categoryIds": ["weekday_am"]
    }
  ],
  "categories": [
    {
      "id": "weekday_am",
      "displayName": "평일 오전",
      "taskSetIds": ["cleaning_am", "zone"]
    }
  ],
  "assignment": {
    "crossSetConflicts": false
  },
  "update": {
    "enabled": true,
    "repo": "owner/scheduler",
    "checkIntervalHours": 24
  }
}
```

| 필드 | 필수 | 설명 |
|---|---|---|
| `workCalendar.workWeekdays` | ✅ | 조직 전체의 근무 요일. 비어 있으면 검증 실패 |
| `workCalendar.holidays` | | 근무일이라도 쉬는 날 |
| `timeSlots[].start` / `end` | ✅ | `start < end`. 같은 요일에서 시간대끼리 겹치면 검증 실패 |
| `timeSlots[].weekdays` | | 비어 있으면 근무일 전체 |
| `timeSlots[].categoryIds` | ✅ | 최소 1개 |
| `categories[].taskSetIds` | ✅ | 최소 1개 |
| `assignment.crossSetConflicts` | | 기본 `false`. `true` 면 배타 검사가 시간대 전체로 확장 (D-002) |
| `update.repo` | | `owner/name` 형식. 없으면 자동 업데이트 비활성 |

---

## workers.json

```json
{
  "version": 1,
  "workers": [
    {
      "id": "w_kim",
      "name": "김철수",
      "active": true,
      "weeklyOff": ["SUN", "MON"],
      "note": ""
    }
  ]
}
```

| 필드 | 필수 | 설명 |
|---|---|---|
| `id` | ✅ | 고유. 한 번 정하면 바꾸지 않음 (스냅샷이 참조하므로) |
| `name` | ✅ | 화면 표시용. 중복 허용하되 검증 시 경고 |
| `active` | | 기본 `true`. `false` 면 배정 대상에서 완전히 제외 |
| `weeklyOff` | | 매주 반복되는 휴무 요일. 근무일이 아닌 요일을 넣어도 무해 |
| `note` | | 자유 메모 |

**배열 순서가 라운드 로빈의 기준 순서입니다.** 순서를 바꾸면 배정 결과가 달라집니다. 정렬하지 마세요.

---

## tasks.json

```json
{
  "version": 1,
  "taskSets": [
    { "id": "cleaning_am", "displayName": "청소" },
    { "id": "cleaning_pm", "displayName": "청소" },
    { "id": "zone",        "displayName": "배치구역" }
  ],
  "tasks": [
    {
      "id": "vacuum",
      "name": "청소기",
      "taskSetIds": ["cleaning_am", "cleaning_pm"],
      "requiredCount": 2,
      "conflictsWith": ["mop"],
      "weekdays": [],
      "priority": null
    },
    {
      "id": "sanitize",
      "name": "소독",
      "taskSetIds": ["cleaning_am"],
      "requiredCount": 2,
      "conflictsWith": [],
      "weekdays": [],
      "priority": null
    }
  ]
}
```

| 필드 | 필수 | 설명 |
|---|---|---|
| `taskSets[].id` | ✅ | 고유 |
| `taskSets[].displayName` | ✅ | 중복 허용. `cleaning_am` 과 `cleaning_pm` 은 둘 다 "청소" |
| `tasks[].taskSetIds` | ✅ | 최소 1개. 소속의 유일한 출처 (D-001) |
| `tasks[].requiredCount` | ✅ | 1 이상 정수 |
| `tasks[].conflictsWith` | | 작업 ID 배열. 로드 시 양방향 대칭으로 정규화 |
| `tasks[].weekdays` | | 비어 있으면 근무일 전체 |
| `tasks[].priority` | | 로드·저장만 되고 엔진은 무시 (D-010) |

**작업집합 안의 작업 순서는 `tasks` 배열의 정의 순서**입니다. 배정 시 이 순서를 따릅니다.

---

## absences.json

```json
{
  "version": 1,
  "absences": [
    {
      "workerId": "w_kim",
      "from": "2026-09-04",
      "to": "2026-09-04",
      "reason": "당일 휴무",
      "createdAt": "2026-09-04T08:12:33+09:00"
    },
    {
      "workerId": "w_lee",
      "from": "2026-09-10",
      "to": "2026-09-14",
      "reason": "연차",
      "createdAt": "2026-09-01T14:20:05+09:00"
    }
  ]
}
```

| 필드 | 필수 | 설명 |
|---|---|---|
| `workerId` | ✅ | `workers.json` 에 존재해야 함 |
| `from`, `to` | ✅ | `from <= to`. 하루면 같은 값 |
| `reason` | | 자유 텍스트 |
| `createdAt` | ✅ | ISO 8601, 오프셋 포함. 당일 급휴 판정에 사용 |

같은 작업자의 기간이 겹쳐도 오류가 아닙니다. 합집합으로 처리합니다.

**당일 급휴 판정**: `createdAt` 의 날짜가 `from` 과 같고, 그날의 배정 스냅샷이 이미 존재하면 급휴입니다(D-009).

---

## state/rr-cursor.json

```json
{
  "version": 1,
  "cursors": {
    "cleaning_am": 2,
    "cleaning_pm": 0,
    "zone": 3
  }
}
```

값은 `workers.json` 배열의 인덱스입니다. 작업자를 추가·삭제하면 인덱스가 어긋날 수 있으므로, 로드 시 `cursor % workerCount` 로 정규화합니다.

---

## state/assign-YYYY-MM-DD.json

배정 스냅샷입니다. 한 번 쓰면 `assign --force` 없이는 다시 쓰지 않습니다.

```json
{
  "version": 1,
  "date": "2026-09-04",
  "weekday": "FRI",
  "generatedAt": "2026-09-04T09:01:00+09:00",
  "slots": [
    {
      "timeSlotId": "am_weekday",
      "categoryId": "weekday_am",
      "taskSets": [
        {
          "taskSetId": "cleaning_am",
          "assignments": [
            {
              "taskId": "vacuum",
              "workerId": "w_kim",
              "absentAssignee": false,
              "done": false,
              "doneAt": null
            },
            {
              "taskId": "vacuum",
              "workerId": "w_lee",
              "absentAssignee": true,
              "done": false,
              "doneAt": null
            }
          ],
          "unassigned": [
            { "taskId": "tidy", "count": 1 }
          ]
        }
      ]
    }
  ]
}
```

| 필드 | 설명 |
|---|---|
| `assignments[]` | 슬롯 하나당 항목 하나. `requiredCount: 2` 면 같은 `taskId` 로 두 항목 |
| `absentAssignee` | 당일 급휴로 이 사람이 쉬게 되었음. 표시만 다르게 하고 배정은 유지 (D-009) |
| `done`, `doneAt` | 예약 필드. 현재 CLI 는 읽지도 쓰지도 않음 (D-006) |
| `unassigned[]` | 인원 부족으로 채우지 못한 슬롯. `count` 는 남은 개수 |

`generatedAt` 의 초는 항상 `00` 입니다. 이 프로그램은 시간대 경계를 분 단위로 다루므로
초를 저장하지 않습니다.

`absentAssignee` 는 스냅샷 생성 시점이 아니라 **조회 시점에 계산**해서 갱신합니다. 휴무가 등록되면 다음 조회에서 `true` 가 되고, 휴무가 취소되면 `false` 로 돌아갑니다.

---

## state/archive/YYYY-MM.jsonl

90일 지난 스냅샷을 한 달치씩 모은 파일입니다(D-005). 한 줄에 스냅샷 하나가 통째로 들어갑니다. 줄 단위이므로 나중에 통계를 낼 때 스트리밍으로 읽을 수 있습니다.

---

## 검증 규칙 (`sched validate`)

Phase 1 에서 구현합니다. 아래를 모두 검사하고, 발견한 문제를 전부 모아서 한 번에 보고합니다. 첫 오류에서 멈추지 마세요.

**오류 (실행 불가)**

- 존재하지 않는 ID 참조: `categories[].taskSetIds`, `tasks[].taskSetIds`, `tasks[].conflictsWith`, `absences[].workerId`, `timeSlots[].categoryIds`
- ID 중복
- `requiredCount <= 0`
- `start >= end`
- 같은 요일에 시간대 구간이 겹침
- `from > to`
- 알 수 없는 요일 코드
- `version` 이 지원 범위를 벗어남

**경고 (실행은 가능)**

- 어떤 분류에도 속하지 않은 작업집합
- 어떤 작업도 속하지 않은 작업집합
- 어떤 시간대에도 연결되지 않은 분류
- 작업자 이름 중복
- `active: false` 인 작업자만 남은 경우
- 배타 그룹의 필요 인원 합이 현재 활성 작업자 수를 초과 (매일 미배정이 발생함)
- 근무일이 아닌 요일만 지정된 작업 (절대 실행되지 않음)
