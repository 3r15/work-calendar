# CLI 사양

실행 파일 이름은 `sched.exe` 입니다.

## 전역 옵션

| 옵션 | 설명 |
|---|---|
| `--data-dir <경로>` | 데이터 폴더 지정. 기본값은 실행 파일 옆 `./data` |
| `--no-color` | ANSI 색상 끄기. `NO_COLOR` 환경 변수도 존중 |
| `--json` | 사람이 읽는 표 대신 JSON 출력. GUI·스크립트 연동용 |
| `--quiet` | 경고와 안내를 숨기고 결과만 |
| `--version` | 버전 출력 후 종료 |
| `--help` | 도움말 |

환경 변수 `SCHED_DATA_DIR` 로도 데이터 폴더를 지정할 수 있습니다. 우선순위는 `--data-dir` > 환경 변수 > 기본값입니다.

## 종료 코드

| 코드 | 의미 |
|---|---|
| 0 | 성공 |
| 1 | 일반 오류 |
| 2 | 잘못된 사용법 (인자 오류) |
| 3 | 데이터 파일 오류 (파싱 실패, 검증 실패) |
| 4 | 성공했으나 미배정 슬롯이 존재 (`--strict` 를 준 경우에만) |

---

## 조회

```
sched now                          지금 시간대의 작업
sched today                        오늘 전체 시간대
sched show [--date YYYY-MM-DD] [--slot <시간대ID>]
```

- `now` 는 시간대에 속하지 않는 시각이면 다음 시간대를 안내와 함께 표시합니다 (D-003).
- 조회 시 그날 스냅샷이 없으면 계산해서 저장합니다. 있으면 읽기만 합니다.
- `--strict` 를 주면 미배정 슬롯이 있을 때 종료 코드 4 를 반환합니다.

## 휴무

```
sched off add --worker <이름|ID> --today
sched off add --worker <이름|ID> --date YYYY-MM-DD [--reason "연차"]
sched off add --worker <이름|ID> --from YYYY-MM-DD --to YYYY-MM-DD
sched off rm  --worker <이름|ID> [--today | --date YYYY-MM-DD]
sched off list [--date YYYY-MM-DD] [--worker <이름|ID>]
```

`--worker` 는 ID 와 이름을 모두 받습니다. 이름이 여러 명과 일치하면 후보를 보여주고 종료 코드 2 로 끝냅니다.

당일 휴무를 등록하면 그날 스냅샷의 해당 배정이 `(휴무)` 로 표시되고, 재배정 안내가 출력됩니다.

정기 휴무(매주 반복)는 `workers.json` 의 `weeklyOff` 이며 별도 명령으로 다룹니다.

```
sched worker off --worker <이름|ID> --weekly MON,TUE
```

## 배정

```
sched assign [--date YYYY-MM-DD | --today] [--force]
```

`--force` 없이 실행하면 스냅샷이 없을 때만 계산합니다. `--force` 는 기존 스냅샷을 버리고 다시 계산합니다. 실행 전 확인을 묻고, `--yes` 로 건너뛸 수 있습니다.

## 관리

```
sched worker add --name "김철수" [--id w_kim] [--weekly-off SUN,MON]
sched worker list [--all]
sched worker rm  --worker <이름|ID>
sched worker edit --worker <이름|ID> [--name ...] [--active true|false]

sched task add --id vacuum --name "청소기" --set cleaning_am,cleaning_pm --count 2 [--conflicts mop] [--weekdays TUE,THU]
sched task list [--set <집합ID>]
sched task rm  --id <작업ID>
sched task edit --id <작업ID> [--count N] [--conflicts ...] [--weekdays ...]

sched set  add|list|rm      작업집합
sched cat  add|list|rm      작업분류
sched slot add|list|rm      시간대

sched validate              설정 정합성 검사
sched prune [--keep-days 90]  오래된 스냅샷 아카이브
```

`worker rm` 은 실제 삭제 대신 `active: false` 를 제안합니다. 과거 스냅샷이 ID 를 참조하기 때문입니다. `--hard` 를 주면 정말 지우되 경고합니다.

## 업데이트

```
sched update check     새 버전 확인만
sched update apply     다운로드 후 교체 및 재시작
```

`config.json` 의 `update.enabled` 가 `false` 면 두 명령 모두 안내 후 종료합니다.

---

## 출력 형식

### `sched now` 기본 출력

```
2026-09-04 (금) 14:20  ·  평일 오후

[청소]
  청소기   김철수  이영희
  밀대     박민수  최지우(휴무)
  소독     김철수  이영희
  정리     ─ 미배정 ─  박민수

[배치구역]
  A구역    최지우(휴무)
  B구역    김철수

⚠ 최지우 휴무. 2건이 휴무자에게 배정되어 있습니다.
  재배정: sched assign --today --force
```

규칙:

- 첫 줄은 날짜·요일·시각·시간대 표시명
- 분류가 여러 개면 분류마다 구분해서 출력하고, 분류 안에 작업집합을 `[표시명]` 으로 묶음
- 작업 이름 열은 표시 폭 기준으로 정렬. `displayWidth()` 사용
- 휴무자는 이름 뒤에 `(휴무)`, 색은 회색
- 미배정은 `─ 미배정 ─`, 색은 빨강
- 경고는 본문 뒤에 빈 줄 하나 두고 출력

### `sched today`

시간대마다 위 블록을 반복하고 사이에 빈 줄을 넣습니다. 이미 지난 시간대는 흐리게, 현재 시간대는 `◀ 지금` 표시를 붙입니다.

### `--json`

스냅샷 스키마(`DATA-SCHEMA.md` 의 `assign-YYYY-MM-DD.json`)를 그대로 내보내되, `workerId` 옆에 `workerName`, `taskId` 옆에 `taskName` 을 붙여 소비 측이 이름을 다시 조회하지 않아도 되게 합니다.

### 오류 메시지

기술 용어 대신 사람 말로 씁니다. 무엇이 잘못됐고 어떻게 고치는지를 한 줄씩 씁니다.

나쁜 예:
```
Error: parse failure at tasks.json:14:3 (unexpected token)
```

좋은 예:
```
tasks.json 14번째 줄에서 형식 오류가 발생했습니다.
쉼표나 괄호가 빠졌는지 확인해 주세요.

문제가 계속되면 data/tasks.json.bak 으로 되돌릴 수 있습니다.
```

`sched validate` 는 발견한 모든 문제를 오류와 경고로 나눠 한 번에 보고합니다.

```
오류 2건, 경고 1건

[오류] tasks.json — 작업 "밀대"가 존재하지 않는 작업 "vacuum2"와 배타 관계로 지정됨
[오류] config.json — 분류 "평일 오전"이 존재하지 않는 작업집합 "cleaning"을 참조함
[경고] 작업집합 "주말청소"에 속한 작업이 없습니다
```
