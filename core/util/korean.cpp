#include "core/util/korean.h"

#include <cstdint>

namespace util {
namespace {

// UTF-8 문자열의 마지막 코드포인트. 비어 있거나 형식이 깨졌으면 0.
char32_t lastCodepoint(std::string_view text) {
    if (text.empty()) {
        return 0;
    }
    // 마지막 선두 바이트를 찾는다. continuation 바이트는 10xxxxxx 다.
    std::size_t start = text.size() - 1;
    while (start > 0 && (static_cast<std::uint8_t>(text[start]) & 0xC0) == 0x80) {
        --start;
    }

    const std::uint8_t lead = static_cast<std::uint8_t>(text[start]);
    const std::size_t length = text.size() - start;
    if (lead < 0x80) {
        return lead;
    }
    char32_t value = 0;
    if ((lead & 0xE0) == 0xC0 && length == 2) {
        value = lead & 0x1FU;
    } else if ((lead & 0xF0) == 0xE0 && length == 3) {
        value = lead & 0x0FU;
    } else if ((lead & 0xF8) == 0xF0 && length == 4) {
        value = lead & 0x07U;
    } else {
        return 0;
    }
    for (std::size_t i = start + 1; i < text.size(); ++i) {
        value = (value << 6) | (static_cast<std::uint8_t>(text[i]) & 0x3FU);
    }
    return value;
}

}  // namespace

std::string_view josa(std::string_view word, std::string_view withFinal,
                      std::string_view withoutFinal) {
    const char32_t last = lastCodepoint(word);
    // 한글 음절 영역(가–힣) 밖이면 받침을 따질 수 없다.
    if (last < 0xAC00 || last > 0xD7A3) {
        return withoutFinal;
    }
    // 음절 = (초성 * 21 + 중성) * 28 + 종성. 종성이 0 이면 받침이 없다.
    const char32_t jongseong = (last - 0xAC00) % 28;
    return (jongseong == 0) ? withoutFinal : withFinal;
}

}  // namespace util
