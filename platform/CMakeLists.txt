# 플랫폼 의존 코드. core/ 가 이 타겟에 의존하면 안 된다 — 방향은 항상 core <- platform <- cli.
if(NOT WIN32)
    message(FATAL_ERROR
        "전체 빌드는 Windows 에서만 됩니다. platform/ 에 Windows 구현만 있기 때문입니다.\n"
        "리눅스에서는 코어만 빌드하세요:\n"
        "  cmake --preset debug-core-only && cmake --build --preset debug-core-only")
endif()

add_library(sched_platform STATIC
    win/console_win.cpp
    win/fileswap_win.cpp
    win/paths_win.cpp
)
add_library(sched::platform ALIAS sched_platform)

target_include_directories(sched_platform PUBLIC "${PROJECT_SOURCE_DIR}")
# core 를 링크한다. 방향은 core <- platform 이며 그 반대는 금지다.
target_link_libraries(sched_platform PUBLIC sched::core sched::options)
