#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace util {

// semver 의 최소 부분집합. 우리가 다루는 것은 우리가 붙인 태그뿐이라 전부를 구현하지 않는다.
//
// "v1.2.3" 과 "1.2.3" 을 모두 받는다. GitHub 태그는 v 접두사가 붙는 관례라서다.
// 프리릴리스("1.2.3-rc1")는 버전 비교에서 정식 릴리스보다 낮게 본다.
class Version {
public:
    Version() = default;
    Version(int major, int minor, int patch) : major_(major), minor_(minor), patch_(patch) {}

    static std::optional<Version> parse(std::string_view text);

    int major() const { return major_; }
    int minor() const { return minor_; }
    int patch() const { return patch_; }
    const std::string& preRelease() const { return preRelease_; }

    std::string toString() const;

    friend std::strong_ordering operator<=>(const Version& a, const Version& b);
    friend bool operator==(const Version& a, const Version& b);

private:
    int major_{0};
    int minor_{0};
    int patch_{0};
    std::string preRelease_;
};

}  // namespace util
