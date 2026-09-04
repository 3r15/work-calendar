cmake_minimum_required(VERSION 3.21)

project(sched
    VERSION 0.1.0
    DESCRIPTION "작업 스케줄러"
    LANGUAGES CXX)

option(SCHED_BUILD_TESTS "테스트 타겟을 만든다" ON)
option(SCHED_CORE_ONLY   "플랫폼 독립 코어만 빌드한다 (리눅스·클라우드 세션)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# 모든 타겟이 공유하는 컴파일 옵션.
# /utf-8 이 빠지면 MSVC 가 소스의 한글을 현재 코드 페이지로 읽어 문자열이 깨진다 (DESIGN 5장).
add_library(sched_options INTERFACE)
add_library(sched::options ALIAS sched_options)

if(MSVC)
    target_compile_options(sched_options INTERFACE
        /utf-8
        /W4
        /permissive-
        /Zc:__cplusplus)
else()
    target_compile_options(sched_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

target_compile_definitions(sched_options INTERFACE
    SCHED_VERSION="${PROJECT_VERSION}")

add_subdirectory(core)

if(NOT SCHED_CORE_ONLY)
    add_subdirectory(platform)
    add_subdirectory(cli)
endif()

if(SCHED_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "sched ${PROJECT_VERSION} — 코어 전용: ${SCHED_CORE_ONLY}, 테스트: ${SCHED_BUILD_TESTS}")
