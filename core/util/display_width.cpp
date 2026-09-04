#include "core/util/display_width.h"

#include <array>
#include <cstdint>

namespace util {
namespace {

struct Range {
    char32_t first;
    char32_t last;
};

// 폭 0. 결합 문자(앞 글자에 얹히는 것)와 폭이 없는 서식 문자.
constexpr std::array kZeroWidth{
    Range{0x0300, 0x036F},    // 결합 발음 기호
    Range{0x0483, 0x0489},    //
    Range{0x0591, 0x05BD},    // 히브리 악센트
    Range{0x0610, 0x061A},    //
    Range{0x064B, 0x065F},    // 아랍어 모음 기호
    Range{0x0670, 0x0670},    //
    Range{0x06D6, 0x06DC},    //
    Range{0x0E31, 0x0E31},    // 태국어 결합 모음
    Range{0x0E34, 0x0E3A},    //
    Range{0x0E47, 0x0E4E},    //
    Range{0x1AB0, 0x1AFF},    // 결합 기호 확장
    Range{0x1DC0, 0x1DFF},    // 결합 기호 보충
    Range{0x200B, 0x200F},    // ZWSP, ZWNJ, ZWJ, 방향 표시
    Range{0x202A, 0x202E},    // 방향 서식
    Range{0x2060, 0x2064},    // 폭 없는 결합자
    Range{0x20D0, 0x20F0},    // 기호용 결합 기호
    Range{0xFE00, 0xFE0F},    // 변이 선택자
    Range{0xFE20, 0xFE2F},    // 반쪽 결합 기호
    Range{0xFEFF, 0xFEFF},    // BOM
    Range{0x1F3FB, 0x1F3FF},  // 이모지 피부색 수정자
    Range{0xE0100, 0xE01EF},  // 변이 선택자 보충
};

// 폭 2. East Asian Wide(W) 와 Fullwidth(F).
constexpr std::array kWide{
    Range{0x1100, 0x115F},    // 한글 자모 초성
    Range{0x231A, 0x231B},    // ⌚⌛
    Range{0x2329, 0x232A},    // 〈〉
    Range{0x23E9, 0x23EC},    //
    Range{0x25FD, 0x25FE},    //
    Range{0x2614, 0x2615},    //
    Range{0x2648, 0x2653},    // 별자리 기호
    Range{0x267F, 0x267F},    //
    Range{0x2693, 0x2693},    //
    Range{0x26AA, 0x26AB},    //
    Range{0x26BD, 0x26BE},    //
    Range{0x26C4, 0x26C5},    //
    Range{0x2705, 0x2705},    //
    Range{0x270A, 0x270B},    //
    Range{0x2728, 0x2728},    //
    Range{0x274C, 0x274C},    //
    Range{0x2753, 0x2755},    //
    Range{0x2795, 0x2797},    //
    Range{0x27B0, 0x27B0},    //
    Range{0x2B1B, 0x2B1C},    //
    Range{0x2B50, 0x2B50},    // ⭐
    Range{0x2B55, 0x2B55},    //
    Range{0x2E80, 0x2E99},    // CJK 부수 보충
    Range{0x2E9B, 0x2EF3},    //
    Range{0x2F00, 0x2FD5},    // 강희 부수
    Range{0x2FF0, 0x2FFB},    // 한자 구성 기술 문자
    Range{0x3000, 0x303E},    // CJK 기호와 문장부호 (전각 공백 포함)
    Range{0x3041, 0x3096},    // 히라가나
    Range{0x3099, 0x30FF},    // 가타카나
    Range{0x3105, 0x312F},    // 주음부호
    Range{0x3131, 0x318E},    // 한글 호환 자모
    Range{0x3190, 0x31E3},    //
    Range{0x31F0, 0x321E},    //
    Range{0x3220, 0x3247},    //
    Range{0x3250, 0x4DBF},    // CJK 확장 A
    Range{0x4E00, 0xA48C},    // CJK 통합 한자, 이 음절
    Range{0xA490, 0xA4C6},    //
    Range{0xA960, 0xA97C},    // 한글 자모 확장 A
    Range{0xAC00, 0xD7A3},    // 한글 음절 (가–힣)
    Range{0xF900, 0xFAFF},    // CJK 호환 한자
    Range{0xFE10, 0xFE19},    // 세로쓰기 형태
    Range{0xFE30, 0xFE52},    // CJK 호환 형태
    Range{0xFE54, 0xFE66},    //
    Range{0xFE68, 0xFE6B},    //
    Range{0xFF01, 0xFF60},    // 전각 영숫자와 기호
    Range{0xFFE0, 0xFFE6},    // 전각 통화 기호
    Range{0x16FE0, 0x16FE4},  //
    Range{0x17000, 0x187F7},  // 서하 문자
    Range{0x18800, 0x18CD5},  //
    Range{0x1B000, 0x1B152},  // 가나 보충
    Range{0x1F004, 0x1F004},  // 🀄
    Range{0x1F0CF, 0x1F0CF},  //
    Range{0x1F18E, 0x1F18E},  //
    Range{0x1F191, 0x1F19A},  //
    Range{0x1F200, 0x1F320},  //
    Range{0x1F32D, 0x1F335},  //
    Range{0x1F337, 0x1F37C},  //
    Range{0x1F37E, 0x1F393},  //
    Range{0x1F3A0, 0x1F3CA},  //
    Range{0x1F3CF, 0x1F3D3},  //
    Range{0x1F3E0, 0x1F3F0},  //
    Range{0x1F3F4, 0x1F3F4},  //
    Range{0x1F3F8, 0x1F43E},  //
    Range{0x1F440, 0x1F440},  //
    Range{0x1F442, 0x1F4FC},  //
    Range{0x1F4FF, 0x1F53D},  //
    Range{0x1F54B, 0x1F54E},  //
    Range{0x1F550, 0x1F567},  //
    Range{0x1F57A, 0x1F57A},  //
    Range{0x1F595, 0x1F596},  //
    Range{0x1F5A4, 0x1F5A4},  //
    Range{0x1F5FB, 0x1F64F},  // 이모티콘
    Range{0x1F680, 0x1F6C5},  //
    Range{0x1F6CC, 0x1F6CC},  //
    Range{0x1F6D0, 0x1F6D2},  //
    Range{0x1F6EB, 0x1F6EC},  //
    Range{0x1F6F4, 0x1F6FC},  //
    Range{0x1F7E0, 0x1F7EB},  //
    Range{0x1F90C, 0x1F93A},  //
    Range{0x1F93C, 0x1F945},  //
    Range{0x1F947, 0x1F9FF},  //
    Range{0x1FA70, 0x1FAFF},  //
    Range{0x20000, 0x2FFFD},  // CJK 확장 B 이후
    Range{0x30000, 0x3FFFD},  //
};

template <std::size_t N>
constexpr bool inRanges(const std::array<Range, N>& ranges, char32_t cp) {
    // 범위는 정렬되어 있지만 개수가 적어 선형 탐색으로 충분하다.
    for (const Range& r : ranges) {
        if (cp < r.first) {
            return false;  // 정렬되어 있으므로 더 볼 필요가 없다
        }
        if (cp <= r.last) {
            return true;
        }
    }
    return false;
}

// UTF-8 한 글자를 읽는다. 잘못된 바이트열이면 codepoint 를 U+FFFD 로 두고 1바이트만 소비한다.
std::size_t decodeUtf8(std::string_view text, std::size_t pos, char32_t& codepoint) {
    const auto byte = [&](std::size_t i) { return static_cast<std::uint8_t>(text[i]); };
    const std::uint8_t lead = byte(pos);
    const std::size_t remaining = text.size() - pos;

    std::size_t length = 0;
    char32_t value = 0;
    if (lead < 0x80) {
        codepoint = lead;
        return 1;
    } else if ((lead & 0xE0) == 0xC0) {
        length = 2;
        value = lead & 0x1FU;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        value = lead & 0x0FU;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        value = lead & 0x07U;
    } else {
        codepoint = 0xFFFD;
        return 1;
    }

    if (remaining < length) {
        codepoint = 0xFFFD;
        return 1;
    }
    for (std::size_t i = 1; i < length; ++i) {
        const std::uint8_t continuation = byte(pos + i);
        if ((continuation & 0xC0) != 0x80) {
            codepoint = 0xFFFD;
            return 1;
        }
        value = (value << 6) | (continuation & 0x3FU);
    }

    codepoint = value;
    return length;
}

}  // namespace

std::size_t displayWidth(char32_t codepoint) {
    if (codepoint == 0) {
        return 0;
    }
    // 제어 문자는 칸을 차지하지 않는 것으로 센다. 표에 섞여 들어오면 어차피 정렬이 의미 없다.
    if (codepoint < 0x20 || (codepoint >= 0x7F && codepoint < 0xA0)) {
        return 0;
    }
    if (inRanges(kZeroWidth, codepoint)) {
        return 0;
    }
    if (inRanges(kWide, codepoint)) {
        return 2;
    }
    return 1;
}

std::size_t displayWidth(std::string_view text) {
    std::size_t width = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        char32_t codepoint = 0;
        pos += decodeUtf8(text, pos, codepoint);
        width += displayWidth(codepoint);
    }
    return width;
}

}  // namespace util
