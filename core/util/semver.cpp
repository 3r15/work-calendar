#include "core/util/semver.h"

#include <cctype>

namespace util {
namespace {

// 숫자 하나를 읽는다. 자릿수가 없으면 실패.
bool readNumber(std::string_view text, std::size_t& pos, int& out) {
    const std::size_t start = pos;
    long value = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        value = value * 10 + (text[pos] - '0');
        if (value > 1000000) {
            return false;  // 버전 번호가 이 정도면 잘못 읽은 것이다
        }
        ++pos;
    }
    if (pos == start) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

}  // namespace

std::optional<Version> Version::parse(std::string_view text) {
    std::size_t pos = 0;
    // GitHub 태그는 "v1.2.3" 형태가 관례다. 접두사를 벗겨 준다.
    if (pos < text.size() && (text[pos] == 'v' || text[pos] == 'V')) {
        ++pos;
    }

    Version out;
    if (!readNumber(text, pos, out.major_)) {
        return std::nullopt;
    }
    if (pos >= text.size() || text[pos] != '.') {
        return std::nullopt;
    }
    ++pos;
    if (!readNumber(text, pos, out.minor_)) {
        return std::nullopt;
    }
    if (pos >= text.size() || text[pos] != '.') {
        return std::nullopt;
    }
    ++pos;
    if (!readNumber(text, pos, out.patch_)) {
        return std::nullopt;
    }

    if (pos < text.size() && text[pos] == '-') {
        out.preRelease_ = std::string{text.substr(pos + 1)};
        if (out.preRelease_.empty()) {
            return std::nullopt;
        }
        return out;
    }
    // 남은 글자가 있으면 우리가 아는 형식이 아니다. 조용히 잘못 읽는 것보다 거절이 낫다.
    if (pos != text.size()) {
        return std::nullopt;
    }
    return out;
}

std::string Version::toString() const {
    std::string out = std::to_string(major_) + "." + std::to_string(minor_) + "." +
                      std::to_string(patch_);
    if (!preRelease_.empty()) {
        out += "-" + preRelease_;
    }
    return out;
}

std::strong_ordering operator<=>(const Version& a, const Version& b) {
    if (const auto cmp = a.major_ <=> b.major_; cmp != std::strong_ordering::equal) {
        return cmp;
    }
    if (const auto cmp = a.minor_ <=> b.minor_; cmp != std::strong_ordering::equal) {
        return cmp;
    }
    if (const auto cmp = a.patch_ <=> b.patch_; cmp != std::strong_ordering::equal) {
        return cmp;
    }
    // 프리릴리스는 같은 번호의 정식 릴리스보다 낮다. 1.2.3-rc1 < 1.2.3
    if (a.preRelease_.empty() != b.preRelease_.empty()) {
        return a.preRelease_.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
    }
    return a.preRelease_ <=> b.preRelease_;
}

bool operator==(const Version& a, const Version& b) {
    return (a <=> b) == std::strong_ordering::equal;
}

}  // namespace util
