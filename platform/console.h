#pragma once

// Windows 콘솔의 한글·색상 처리. 여기 있는 함수는 전부 실패해도 프로그램을 멈추지 않는다 —
// 콘솔이 아닌 곳(파이프, 파일 리다이렉트, GUI 호스트)에서 실행되는 것은 오류가 아니기 때문이다.
//
// core/ 는 이 헤더에 의존하지 않는다. 의존 방향은 core <- platform <- cli 다.

namespace platform {

// 콘솔 입출력 코드 페이지를 UTF-8 로 바꾼다. main() 의 첫 줄에서 부른다.
// 이걸 거치지 않으면 한글이 깨져 나온다 (DESIGN 5장).
// 반환값: 실제로 바뀌었으면 true. 리다이렉트된 출력에서는 false 이며 무시해도 된다.
bool initConsole();

// ANSI escape 시퀀스 처리를 켠다.
// 반환값이 false 면 색을 쓰지 말고 그냥 출력해야 한다.
bool enableAnsi();

}  // namespace platform
