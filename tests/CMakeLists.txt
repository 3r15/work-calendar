find_package(Catch2 3 REQUIRED)

add_executable(sched_tests
    app/test_absence_service.cpp
    app/test_admin_service.cpp
    app/test_assign_service.cpp
    app/test_prune_service.cpp
    app/test_day_view.cpp
    domain/test_day_plan.cpp
    domain/test_validator.cpp
    scheduling/test_conflict_groups.cpp
    scheduling/test_cursor.cpp
    scheduling/test_determinism.cpp
    scheduling/test_headcount.cpp
    scheduling/test_roundrobin.cpp
    scheduling/test_weekday_filter.cpp
    storage/test_atomic_write.cpp
    storage/test_json_io.cpp
    storage/test_lock.cpp
    util/test_date.cpp
    util/test_display_width.cpp
    util/test_korean.cpp
    util/test_semver.cpp
    util/test_sha256.cpp
    util/test_result.cpp
)

# data-sample/ 을 픽스처로 쓴다 (CLAUDE.md 2장). 경로를 하드코딩하지 않는다.
target_compile_definitions(sched_tests PRIVATE
    SCHED_DATA_SAMPLE_DIR="${PROJECT_SOURCE_DIR}/data-sample")

target_link_libraries(sched_tests PRIVATE
    sched::core
    Catch2::Catch2WithMain
)

# TEST_CASE 하나가 ctest 테스트 하나가 된다. ctest 는 테스트 이름을 실행 파일에 필터 인자로
# 넘기는데, Windows 는 argv 를 ANSI 코드 페이지로 변환하므로 이름에 한글이 있으면 "?" 로 깨져
# 아무 테스트도 매칭되지 않는다. TEST_CASE 이름은 ASCII 로 쓴다.
include(Catch)
catch_discover_tests(sched_tests)
