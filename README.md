# 작업 스케줄러

작업자에게 매일의 작업을 자동으로 배정하는 프로그램입니다. Windows 콘솔에서 동작하며, 나중에 GUI 를 추가할 예정입니다.

> **개발 중입니다.** 아직 사용할 수 있는 릴리스가 없습니다.
> 완성된 사용설명서는 Phase 7 에서 이 문서를 대체합니다. 작성 지침은 `docs/README-PLAN.md` 에 있습니다.

## 무엇을 하나요

- 사람과 작업을 등록해두면 오늘 누가 무슨 일을 할지 자동으로 정합니다
- 함께 맡을 수 없는 작업(예: 청소기와 밀대)을 서로 다른 사람에게 나눕니다
- 사람이 모자라면 억지로 채우지 않고 "미배정"으로 따로 표시합니다
- 누가 쉬는지 알려주면 즉시 반영됩니다
- 하루를 시간대로 나눠 오전과 오후에 다른 작업을 배치할 수 있습니다

## 개발자를 위한 안내

| 문서 | 내용 |
|---|---|
| [HANDOFF.md](HANDOFF.md) | 저장소 준비와 첫 작업 시작 |
| [docs/DECISIONS.md](docs/DECISIONS.md) | 확정된 설계 결정과 근거 |
| [docs/DESIGN.md](docs/DESIGN.md) | 아키텍처와 배정 알고리즘 |
| [docs/DATA-SCHEMA.md](docs/DATA-SCHEMA.md) | JSON 스키마 |
| [docs/CLI-SPEC.md](docs/CLI-SPEC.md) | 명령어 사양 |
| [docs/ROADMAP.md](docs/ROADMAP.md) | 단계별 작업 목록 |

### 빌드

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

리눅스에서는 플랫폼 독립 코어만 빌드됩니다.

```bash
cmake --preset debug-core-only
cmake --build --preset debug-core-only
```

## 라이선스

미정.
