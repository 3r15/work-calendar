#include "core/util/semver.h"

#include <catch2/catch_test_macros.hpp>

using util::Version;

TEST_CASE("parses plain and v prefixed versions", "[semver]") {
    // GitHub 태그는 v 접두사가 관례다. 둘 다 받아야 한다.
    REQUIRE(Version::parse("1.2.3").has_value());
    REQUIRE(Version::parse("v1.2.3").has_value());
    REQUIRE(*Version::parse("v1.2.3") == Version{1, 2, 3});
    REQUIRE(Version::parse("v0.1.0")->toString() == "0.1.0");
}

TEST_CASE("rejects things that are not versions", "[semver]") {
    // 조용히 잘못 읽는 것보다 거절하는 편이 낫다. 업데이트 판단이 여기 걸려 있다.
    REQUIRE_FALSE(Version::parse("").has_value());
    REQUIRE_FALSE(Version::parse("1.2").has_value());
    REQUIRE_FALSE(Version::parse("1.2.3.4").has_value());
    REQUIRE_FALSE(Version::parse("v").has_value());
    REQUIRE_FALSE(Version::parse("1.2.x").has_value());
    REQUIRE_FALSE(Version::parse("최신").has_value());
    REQUIRE_FALSE(Version::parse("1.2.3 ").has_value());
    REQUIRE_FALSE(Version::parse("1.2.3-").has_value());
}

TEST_CASE("orders versions by number", "[semver]") {
    REQUIRE(*Version::parse("0.1.0") < *Version::parse("0.1.1"));
    REQUIRE(*Version::parse("0.1.9") < *Version::parse("0.2.0"));
    REQUIRE(*Version::parse("0.9.9") < *Version::parse("1.0.0"));
    REQUIRE(*Version::parse("1.0.0") == *Version::parse("v1.0.0"));
    REQUIRE(*Version::parse("2.0.0") > *Version::parse("1.99.99"));
    // 자릿수가 아니라 수로 비교해야 한다.
    REQUIRE(*Version::parse("0.10.0") > *Version::parse("0.9.0"));
}

TEST_CASE("a prerelease is older than the release with the same number", "[semver]") {
    REQUIRE(*Version::parse("1.2.3-rc1") < *Version::parse("1.2.3"));
    REQUIRE(*Version::parse("1.2.3-rc1") < *Version::parse("1.2.3-rc2"));
    REQUIRE(*Version::parse("1.2.3-rc9") < *Version::parse("1.2.4"));
    REQUIRE(Version::parse("1.2.3-rc1")->preRelease() == "rc1");
    REQUIRE(Version::parse("1.2.3-rc1")->toString() == "1.2.3-rc1");
}
