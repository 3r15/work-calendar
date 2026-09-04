# 별도 exe. Windows 가 실행 중인 exe 를 덮어쓸 수 없어서 존재한다 (DESIGN 7.2).
#
# core 를 링크하지 않는다. 하는 일이 "기다렸다 파일을 옮기고 다시 띄운다" 뿐이라 도메인이
# 필요 없고, 의존이 없어야 이 프로그램 자체가 교체 대상에서 빠져도 안전하다.
add_executable(updater
    main.cpp
)
set_target_properties(updater PROPERTIES OUTPUT_NAME "updater")
target_link_libraries(updater PRIVATE sched::options)
