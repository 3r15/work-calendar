# 플랫폼 독립 정적 라이브러리. <windows.h> 를 포함하는 코드가 여기 들어오면 안 된다 (CLAUDE.md 1장).
# 하위 폴더는 Phase 가 진행되며 늘어난다: domain / storage / scheduling / app
add_library(sched_core STATIC
    util/display_width.cpp
)
add_library(sched::core ALIAS sched_core)

# include 는 저장소 루트 기준으로 쓴다: #include "core/util/display_width.h"
# 경로만 봐도 의존 방향(domain <- storage <- scheduling <- app <- cli)이 드러난다.
target_include_directories(sched_core PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(sched_core PUBLIC sched::options)
