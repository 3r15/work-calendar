#pragma once

#include <string>
#include <string_view>

namespace util {

// 앞말의 받침에 따라 조사를 고른다.
//
// 왜 필요한가: 오류 메시지에 작업자 이름이나 작업 이름이 그대로 들어간다. "밀대이" 나
// "김철수가" 처럼 조사가 어긋나면 프로그램이 한국어를 모르는 것처럼 보인다. 이름은 사용자
// 데이터라 미리 정해 둘 수 없으므로 실행 중에 골라야 한다.
//
// 규칙: 한글 음절의 종성이 있으면 withFinal, 없으면 withoutFinal.
//   josa("밀대", "이", "가") -> "가"      (대 = 받침 없음)
//   josa("청소기", "이", "가") -> "가"
//   josa("소독", "이", "가") -> "이"      (독 = 받침 ㄱ)
//
// 한글이 아닌 글자로 끝나면(영문 ID, 숫자) 받침 없는 쪽을 쓴다. 발음까지 따지는 것은
// 과하고, 이 자리에 오는 값은 대부분 ID 라 어느 쪽이든 자연스럽다.
std::string_view josa(std::string_view word, std::string_view withFinal,
                      std::string_view withoutFinal);

// 자주 쓰는 짝.
inline std::string_view josaIGa(std::string_view word) { return josa(word, "이", "가"); }
inline std::string_view josaEunNeun(std::string_view word) { return josa(word, "은", "는"); }
inline std::string_view josaEulReul(std::string_view word) { return josa(word, "을", "를"); }
inline std::string_view josaWaGwa(std::string_view word) { return josa(word, "과", "와"); }

}  // namespace util
