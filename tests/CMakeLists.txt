find_package(Catch2 3 REQUIRED)

add_executable(sched_tests
    util/test_display_width.cpp
    util/test_result.cpp
)

target_link_libraries(sched_tests PRIVATE
    sched::core
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(sched_tests)
