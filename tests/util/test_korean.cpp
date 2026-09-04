#include "core/util/korean.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using util::josa;
using util::josaEulReul;
using util::josaIGa;
using util::josaWaGwa;

// 받침이 있으면 이/은/을/과, 없으면 가/는/를/와.
TEST_CASE("picks the particle from the final consonant", "[korean]") {
    SECTION("받침 없음") {
        REQUIRE(josaIGa("밀대") == "가");
        REQUIRE(josaIGa("청소기") == "가");
        REQUIRE(josaIGa("최지우") == "가");
        REQUIRE(josaEulReul("정리") == "를");
        REQUIRE(josaWaGwa("밀대") == "와");
    }
    SECTION("받침 있음") {
        REQUIRE(josaIGa("소독") == "이");
        REQUIRE(josaIGa("김철수") == "가");  // 수 = 받침 없음
        REQUIRE(josaIGa("박민수") == "가");
        REQUIRE(josaIGa("평일 오전") == "이");
        REQUIRE(josaEulReul("작업집합") == "을");
        REQUIRE(josaWaGwa("소독") == "과");
    }
}

TEST_CASE("falls back to the no-final form outside hangul", "[korean]") {
    // ID 나 영문으로 끝나면 받침을 따질 수 없다.
    REQUIRE(josaIGa("vacuum2") == "가");
    REQUIRE(josaIGa("cleaning_am") == "가");
    REQUIRE(josaIGa("") == "가");
    REQUIRE(josaIGa("123") == "가");
}

TEST_CASE("handles mixed and multibyte endings", "[korean]") {
    REQUIRE(josaIGa("A구역") == "이");   // 역 = 받침 ㄱ
    REQUIRE(josaIGa("B구역") == "이");
    REQUIRE(josaIGa("쓰레기 배출") == "이");  // 출 = 받침 ㄹ
    REQUIRE(josaIGa("대청소") == "가");
}

TEST_CASE("accepts arbitrary particle pairs", "[korean]") {
    REQUIRE(josa("소독", "으로", "로") == "으로");
    REQUIRE(josa("정리", "으로", "로") == "로");
}
