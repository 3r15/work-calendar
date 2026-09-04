#pragma once

#include <cstddef>
#include <string_view>

namespace util {

// 콘솔에 찍었을 때 차지하는 칸 수. 문자 개수가 아니다.
//
// 한글과 전각 문자는 2칸을 차지하므로 std::setw 로 표를 맞추면 어긋난다 (DESIGN 5장).
// 표 정렬은 반드시 이 함수로 계산한다.
//
// East Asian Width 판정은 유니코드 범위 테이블로 직접 구현한다. Windows API 를 쓰지 않으므로
// core/ 에 둘 수 있고 리눅스에서도 테스트된다.
//
// 입력은 UTF-8 로 본다. 잘못된 바이트열은 버리지 않고 한 바이트를 1칸으로 세고 넘어간다 —
// 표가 조금 어긋나는 편이 사용자 데이터를 조용히 삼키는 것보다 낫다.
std::size_t displayWidth(std::string_view text);

// 코드포인트 하나의 폭. 0(결합 문자·제어 문자), 1, 2 중 하나.
std::size_t displayWidth(char32_t codepoint);

}  // namespace util
