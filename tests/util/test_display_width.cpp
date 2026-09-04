#include "core/util/display_width.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// TEST_CASE 이름은 ASCII 로 쓴다. ctest 가 이름을 그대로 필터 인자로 넘기는데, Windows 는
// argv 를 ANSI 코드 페이지로 변환하므로 한글이 "?" 로 바뀌어 어떤 테스트도 매칭되지 않는다.
// 무엇을 검증하는지는 바로 위 주석과 SECTION 이름에 한국어로 적는다.

using util::displayWidth;

// ROADMAP Phase 0 완료 기준 4번.
TEST_CASE("phase0 acceptance criteria", "[display_width]") {
    // 이 세 줄이 Phase 0 완료 기준 4번이다.
    REQUIRE(displayWidth("김철수") == 6);
    REQUIRE(displayWidth("abc") == 3);
    REQUIRE(displayWidth("김a") == 3);
}

// ASCII 는 문자 수와 폭이 같다.
TEST_CASE("ascii width equals character count", "[display_width]") {
    REQUIRE(displayWidth("") == 0);
    REQUIRE(displayWidth("a") == 1);
    REQUIRE(displayWidth("sched --version") == 15);
    REQUIRE(displayWidth("0123456789") == 10);
}

// 한글은 글자당 2칸.
TEST_CASE("hangul is two columns per syllable", "[display_width]") {
    SECTION("음절") {
        REQUIRE(displayWidth("가") == 2);
        REQUIRE(displayWidth("힣") == 2);
        REQUIRE(displayWidth("청소기") == 6);
        REQUIRE(displayWidth("쓰레기 배출") == 11);  // 한글 6자(12) + 공백 1
    }
    SECTION("호환 자모") {
        REQUIRE(displayWidth("ㄱ") == 2);
        REQUIRE(displayWidth("ㅏ") == 2);
    }
}

// 실제 출력에 쓰이는 문자열.
TEST_CASE("strings used in real cli output", "[display_width]") {
    // CLI-SPEC.md 의 출력 예시에 나오는 것들. 표 정렬이 여기서 어긋나면 안 된다.
    REQUIRE(displayWidth("미배정") == 6);
    REQUIRE(displayWidth("(휴무)") == 6);       // 반각 괄호 2 + 한글 2자 4
    REQUIRE(displayWidth("평일 오전") == 9);    // 한글 4자 8 + 공백 1

    // U+2500 은 East Asian Width 가 Ambiguous 라 1칸으로 센다.
    // 폰트에 따라 2칸으로 보이는 콘솔이 있지만, 그건 폰트 문제이지 폭 계산 문제가 아니다.
    REQUIRE(displayWidth("─ 미배정 ─") == 10);
}

// CJK 와 전각 기호.
TEST_CASE("cjk and fullwidth symbols", "[display_width]") {
    REQUIRE(displayWidth("漢字") == 4);
    REQUIRE(displayWidth("ひらがな") == 8);
    REQUIRE(displayWidth("カタカナ") == 8);
    REQUIRE(displayWidth("Ａ") == 2);   // 전각 A
    REQUIRE(displayWidth("　") == 2);   // 전각 공백
    REQUIRE(displayWidth("！") == 2);   // 전각 느낌표
}

// 폭 0인 문자.
TEST_CASE("zero width characters", "[display_width]") {
    SECTION("결합 문자는 앞 글자에 얹히므로 칸을 차지하지 않는다") {
        REQUIRE(displayWidth("é") == 1);  // e + 결합 액센트
    }
    SECTION("제어 문자") {
        REQUIRE(displayWidth(std::string("a\tb")) == 2);
        REQUIRE(displayWidth(std::string("a\nb")) == 2);
    }
    SECTION("BOM 은 화면을 차지하지 않는다") {
        REQUIRE(displayWidth(std::string("\xEF\xBB\xBF김")) == 2);
    }
}

// 잘못된 UTF-8 은 버리지 않고 1칸으로 센다.
TEST_CASE("invalid utf8 counts as one column", "[display_width]") {
    SECTION("단독 continuation 바이트") {
        REQUIRE(displayWidth(std::string("\x80")) == 1);
    }
    SECTION("잘린 3바이트 시퀀스") {
        // "김" 은 EA B9 80 이다. 마지막 바이트를 자른다.
        REQUIRE(displayWidth(std::string("\xEA\xB9")) == 2);
    }
    SECTION("정상 문자 뒤에 붙은 쓰레기 바이트") {
        REQUIRE(displayWidth(std::string("김\xFF")) == 3);
    }
}

// 코드포인트 단위 판정.
TEST_CASE("per codepoint width", "[display_width]") {
    REQUIRE(displayWidth(U'a') == 1);
    REQUIRE(displayWidth(U'김') == 2);
    REQUIRE(displayWidth(U'\u0300') == 0);  // 결합 액센트
    REQUIRE(displayWidth(U'\uAC00') == 2);  // 가 — 한글 음절 시작
    REQUIRE(displayWidth(U'\uD7A3') == 2);  // 힣 — 한글 음절 끝
    REQUIRE(displayWidth(U'\u4E00') == 2);  // 一 — CJK 시작
    REQUIRE(displayWidth(U'\u00E9') == 1);  // é — 조합된 한 글자
}

// 표 정렬에 쓸 수 있다.
TEST_CASE("usable for table alignment", "[display_width]") {
    // std::setw 로는 맞출 수 없는 상황. 이름 열을 10칸으로 맞춘다고 할 때
    // 필요한 공백 수가 문자 수가 아니라 폭에서 나와야 한다.
    const std::string korean = "청소기";
    const std::string ascii = "vacuum";
    REQUIRE(korean.size() == 9);          // 바이트 수
    REQUIRE(displayWidth(korean) == 6);   // 화면 폭
    REQUIRE(displayWidth(ascii) == 6);    // 둘의 화면 폭이 같다
    REQUIRE(korean.size() != ascii.size());
}
