# 인계 문서 — 여기서 시작하세요

작업 스케줄러 프로젝트의 설계가 끝났습니다. 이 폴더의 내용을 새 GitHub 저장소 루트에 그대로 넣고, Claude Code에서 구현을 시작하면 됩니다.

## 1. 저장소 준비

```bash
git init
git add .
git commit -m "chore: 설계 문서와 프로젝트 설정 추가"
git branch -M main
git checkout -b develop
git remote add origin <저장소-URL>
git push -u origin main
git push -u origin develop
chmod +x .claude/hooks/guard-branch.sh
```

GitHub에서 `main` 브랜치에 보호 규칙을 걸어두면 좋습니다. `develop`을 기본 브랜치로 지정하세요.

## 2. 읽는 순서

| 파일 | 내용 |
|---|---|
| `docs/DECISIONS.md` | **먼저 읽으세요.** 확정된 설계 결정과 그 이유 |
| `docs/DESIGN.md` | 전체 아키텍처와 배정 알고리즘 |
| `docs/DATA-SCHEMA.md` | JSON 파일 스키마 정밀 명세 |
| `docs/CLI-SPEC.md` | 명령어, 출력 형식, 종료 코드 |
| `docs/ROADMAP.md` | Phase별 작업 목록과 완료 기준 |
| `docs/README-PLAN.md` | Phase 7에서 쓸 사용설명서 작성 지침 |

`CLAUDE.md`는 Claude Code가 매 세션 자동으로 읽습니다. 직접 읽을 필요는 없지만 규칙이 무엇인지는 알아두세요.

## 3. Claude Code 첫 프롬프트

저장소를 열고 그대로 붙여넣으세요.

```
docs/ROADMAP.md 의 Phase 0 을 구현해줘.

먼저 docs/DECISIONS.md 와 docs/DESIGN.md 6장(아키텍처)을 읽고,
CMakeLists.txt / CMakePresets.json / VSCode 설정 / .clang-format 을
설계와 일치하도록 만들어줘.

특히 다음 두 가지를 Phase 0 에서 반드시 해결해야 해:
1. UTF-8 콘솔 초기화 (platform/console.h + win 구현)
2. displayWidth() 유틸과 그 단위 테스트 (한글 폭 2)

Phase 0 완료 기준은 ROADMAP 에 적혀 있어. 그 기준을 전부 만족시킨 뒤 보고해줘.
```

`CMakePresets.json`은 이 패키지에 이미 들어 있습니다. Claude Code는 이 프리셋 이름(`debug`, `release`, `debug-core-only`)에 맞춰 `CMakeLists.txt`를 만들면 됩니다.

## 4. 알아둘 것

**클라우드 세션(모바일)에서는 `core/`만 빌드됩니다.** 클라우드 VM은 Ubuntu이고 MSVC가 없습니다. `platform/`, `updater/`, 실제 콘솔 한글 출력은 Windows PC 또는 GitHub Actions에서만 검증됩니다. `.github/workflows/ci.yml`이 두 플랫폼을 모두 빌드하므로, 폰에서 작업하고 CI 결과로 Windows 쪽을 확인하는 방식이 가능합니다.

**서브에이전트 사용법**

- `core/scheduling/`에 새 동작을 넣기 전 → `test-first` 에이전트로 테스트부터
- C++ 파일을 고친 뒤 커밋 전 → `cpp-reviewer` 에이전트로 규칙 위반 검사

**설계를 바꿔야 할 것 같으면** 말없이 바꾸지 말고 먼저 `docs/DECISIONS.md`에 근거와 함께 새 결정을 추가하세요. 결정 기록이 남아야 나중에 왜 그렇게 했는지 알 수 있습니다.

## 5. 파일 목록

```
HANDOFF.md                              이 문서
CLAUDE.md                               Claude Code 프로젝트 규칙
README.md                               저장소 소개 (사용설명서는 Phase 7)
.gitignore  .editorconfig  .clang-format
CMakePresets.json

docs/DESIGN.md
docs/DECISIONS.md
docs/DATA-SCHEMA.md
docs/CLI-SPEC.md
docs/ROADMAP.md
docs/README-PLAN.md

data-sample/config.json
data-sample/workers.json
data-sample/tasks.json
data-sample/absences.json

.claude/settings.json                   훅 설정
.claude/skills/scheduler-domain/SKILL.md
.claude/agents/cpp-reviewer.md
.claude/agents/test-first.md
.claude/hooks/guard-branch.sh

.github/workflows/ci.yml
.github/workflows/release.yml
```
