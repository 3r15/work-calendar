find_package(Catch2 3 REQUIRED)

add_executable(sched_tests
    util/test_display_width.cpp
    util/test_result.cpp
)

target_link_libraries(sched_tests PRIVATE
    sched::core
    Catch2::Catch2WithMain
)

# TEST_CASE 하나가 ctest 테스트 하나가 된다. ctest 는 테스트 이름을 실행 파일에 필터 인자로
# 넘기는데, Windows 는 argv 를 ANSI 코드 페이지로 변환하므로 이름에 한글이 있으면 "?" 로 깨져
# 아무 테스트도 매칭되지 않는다. TEST_CASE 이름은 ASCII 로 쓴다.
include(Catch)
catch_discover_tests(sched_tests)
