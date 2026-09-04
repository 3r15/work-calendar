# 명령 파싱과 렌더링만. 비즈니스 로직은 core/app 으로 올린다 (CLAUDE.md 2장).
find_package(CLI11 CONFIG REQUIRED)

add_executable(sched
    main.cpp
)

target_link_libraries(sched PRIVATE
    sched::core
    sched::platform
    CLI11::CLI11
)
