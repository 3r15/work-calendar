#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace util {

// SHA-256. 내려받은 파일이 온전한지 확인하는 데 쓴다 (DESIGN 7.1).
//
// 왜 직접 구현하는가: Windows 의 CryptoAPI 를 쓰면 core/ 가 플랫폼에 묶인다. 검증 자체는
// 순수 계산이라 표준 라이브러리만으로 되고, 그래야 리눅스에서 테스트할 수 있다.
std::string sha256Hex(std::string_view data);

// 파일을 조각내어 읽는다. 릴리스 zip 이 수십 MB 일 수 있어 통째로 올리지 않는다.
std::optional<std::string> sha256HexOfFile(const std::filesystem::path& path);

// 대소문자와 앞뒤 공백을 무시하고 비교한다. 체크섬 파일마다 표기가 다르다.
bool checksumMatches(std::string_view expected, std::string_view actual);

}  // namespace util
