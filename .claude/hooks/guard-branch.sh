#!/usr/bin/env bash
# main 브랜치에 직접 커밋하는 것을 막는다 (CLAUDE.md 1장, DESIGN.md 8.1).
# Claude Code 의 PreToolUse 훅으로 등록되어 Bash 도구 호출 직전에 실행된다.
# stdin 으로 훅 페이로드(JSON)를 받고, 종료 코드 2 로 도구 호출을 차단한다.
set -uo pipefail

payload="$(cat)"

if command -v jq >/dev/null 2>&1; then
  cmd="$(printf '%s' "$payload" | jq -r '.tool_input.command // ""')"
else
  # jq 가 없는 환경(윈도우 등)을 위한 최소 추출. 실패하면 통과시킨다.
  cmd="$(printf '%s' "$payload" | tr '\n' ' ' \
    | sed -n 's/.*"command"[[:space:]]*:[[:space:]]*"\(\([^"\\]\|\\.\)*\)".*/\1/p')"
fi

[ -n "$cmd" ] || exit 0

# git commit / git merge 만 검사한다. 조회 명령은 건드리지 않는다.
printf '%s' "$cmd" | grep -Eq '(^|[;&|[:space:]])git([[:space:]]+-[^[:space:]]+)*[[:space:]]+(commit|merge)([[:space:]]|$)' || exit 0

branch="$(git rev-parse --abbrev-ref HEAD 2>/dev/null)" || exit 0

if [ "$branch" = "main" ] && [ "${SCHED_ALLOW_MAIN:-}" != "1" ]; then
  cat >&2 <<'MSG'
차단됨: main 브랜치에는 직접 커밋하지 않습니다.

main 은 릴리스 태그만 받는 브랜치입니다 (CLAUDE.md 1장, DESIGN.md 8.1).
작업 브랜치로 옮긴 뒤 다시 시도하세요.

  git checkout develop
  git checkout -b feature/<짧은-이름>

릴리스 병합처럼 정말 main 에서 작업해야 한다면 SCHED_ALLOW_MAIN=1 을 붙이세요.
MSG
  exit 2
fi

exit 0
