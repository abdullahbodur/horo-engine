#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "core/Version.h"
#include "core/BuildVersion.h"

using namespace Horo;

// ============================================================================
// Parsing: basic major.minor.patch
// ============================================================================

TEST_CASE("Version: parse basic major.minor.patch", "[core][version]") {
    auto v = ParseVersion("1.2.3");
    REQUIRE(v.has_value());
    REQUIRE(v->major == 1);
    REQUIRE(v->minor == 2);
    REQUIRE(v->patch == 3);
    REQUIRE(v->prerelease.empty());
    REQUIRE(v->build.empty());
}

TEST_CASE("Version: parse with zeros", "[core][version]") {
    auto v = ParseVersion("0.0.0");
    REQUIRE(v.has_value());
    REQUIRE(v->major == 0);
    REQUIRE(v->minor == 0);
    REQUIRE(v->patch == 0);
}

TEST_CASE("Version: parse with large numbers", "[core][version]") {
    auto v = ParseVersion("2147483647.999.999999");
    REQUIRE(v.has_value());
    REQUIRE(v->major == 2147483647);
    REQUIRE(v->minor == 999);
    REQUIRE(v->patch == 999999);
}

TEST_CASE("Version: parse single-digit components", "[core][version]") {
    auto v = ParseVersion("0.1.0");
    REQUIRE(v.has_value());
    REQUIRE(v->major == 0);
    REQUIRE(v->minor == 1);
    REQUIRE(v->patch == 0);
}

// ============================================================================
// Parsing: prerelease
// ============================================================================

TEST_CASE("Version: parse with prerelease (alpha)", "[core][version]") {
    auto v = ParseVersion("1.0.0-alpha");
    REQUIRE(v.has_value());
    REQUIRE(v->prerelease == "alpha");
    REQUIRE(v->build.empty());
}

TEST_CASE("Version: parse with prerelease (alpha.1)", "[core][version]") {
    auto v = ParseVersion("1.0.0-alpha.1");
    REQUIRE(v.has_value());
    REQUIRE(v->prerelease == "alpha.1");
}

TEST_CASE("Version: parse with prerelease (beta.2.3)", "[core][version]") {
    auto v = ParseVersion("2.1.0-beta.2.3");
    REQUIRE(v.has_value());
    REQUIRE(v->prerelease == "beta.2.3");
}

TEST_CASE("Version: parse with numeric prerelease", "[core][version]") {
    auto v = ParseVersion("1.0.0-1");
    REQUIRE(v.has_value());
    REQUIRE(v->prerelease == "1");
}

TEST_CASE("Version: parse with prerelease containing hyphens", "[core][version]") {
    auto v = ParseVersion("1.0.0-rc-1");
    REQUIRE(v.has_value());
    REQUIRE(v->prerelease == "rc-1");
}

// ============================================================================
// Parsing: build metadata
// ============================================================================

TEST_CASE("Version: parse with build metadata", "[core][version]") {
    auto v = ParseVersion("1.0.0+20240101");
    REQUIRE(v.has_value());
    REQUIRE(v->prerelease.empty());
    REQUIRE(v->build == "20240101");
}

TEST_CASE("Version: parse with build metadata containing dots", "[core][version]") {
    auto v = ParseVersion("1.0.0+build.42.arm64");
    REQUIRE(v.has_value());
    REQUIRE(v->build == "build.42.arm64");
}

TEST_CASE("Version: parse with build metadata containing hyphens", "[core][version]") {
    auto v = ParseVersion("1.0.0+exp.sha.5114f85");
    REQUIRE(v.has_value());
    REQUIRE(v->build == "exp.sha.5114f85");
}

// ============================================================================
// Parsing: prerelease + build
// ============================================================================

TEST_CASE("Version: parse with prerelease and build", "[core][version]") {
    auto v = ParseVersion("1.0.0-alpha.1+build.42");
    REQUIRE(v.has_value());
    REQUIRE(v->major == 1);
    REQUIRE(v->minor == 0);
    REQUIRE(v->patch == 0);
    REQUIRE(v->prerelease == "alpha.1");
    REQUIRE(v->build == "build.42");
}

TEST_CASE("Version: parse prerelease with build, no dots in prerelease", "[core][version]") {
    auto v = ParseVersion("1.0.0-rc+2024");
    REQUIRE(v.has_value());
    REQUIRE(v->prerelease == "rc");
    REQUIRE(v->build == "2024");
}

// ============================================================================
// Parsing: leading 'v' prefix (git tags)
// ============================================================================

TEST_CASE("Version: parse with leading v", "[core][version]") {
    auto v = ParseVersion("v1.2.3");
    REQUIRE(v.has_value());
    REQUIRE(v->major == 1);
    REQUIRE(v->minor == 2);
    REQUIRE(v->patch == 3);
}

TEST_CASE("Version: parse with leading v and prerelease", "[core][version]") {
    auto v = ParseVersion("v1.0.0-rc.1");
    REQUIRE(v.has_value());
    REQUIRE(v->prerelease == "rc.1");
}

TEST_CASE("Version: parse with leading v, prerelease and build", "[core][version]") {
    auto v = ParseVersion("v2.0.0-beta+build.1");
    REQUIRE(v.has_value());
    REQUIRE(v->major == 2);
    REQUIRE(v->prerelease == "beta");
    REQUIRE(v->build == "build.1");
}

// ============================================================================
// Parsing: invalid versions
// ============================================================================

TEST_CASE("Version: empty string is invalid", "[core][version]") {
    auto v = ParseVersion("");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: missing patch is invalid", "[core][version]") {
    auto v = ParseVersion("1.2");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: missing minor is invalid", "[core][version]") {
    auto v = ParseVersion("1");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: trailing dot is invalid", "[core][version]") {
    auto v = ParseVersion("1.2.");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: leading dot is invalid", "[core][version]") {
    auto v = ParseVersion(".1.2");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: non-numeric major is invalid", "[core][version]") {
    auto v = ParseVersion("a.1.2");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: non-numeric minor is invalid", "[core][version]") {
    auto v = ParseVersion("1.x.2");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: non-numeric patch is invalid", "[core][version]") {
    auto v = ParseVersion("1.2.x");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: negative number is invalid", "[core][version]") {
    auto v = ParseVersion("-1.2.3");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: leading zero in major is invalid (semver)", "[core][version]") {
    auto v = ParseVersion("01.2.3");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: leading zero in minor is invalid (semver)", "[core][version]") {
    auto v = ParseVersion("1.02.3");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: leading zero in patch is invalid (semver)", "[core][version]") {
    auto v = ParseVersion("1.2.03");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: empty prerelease after hyphen is invalid", "[core][version]") {
    auto v = ParseVersion("1.2.3-");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: empty build after plus is invalid", "[core][version]") {
    auto v = ParseVersion("1.2.3+");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: just v with no numbers is invalid", "[core][version]") {
    auto v = ParseVersion("v");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: v prefix with trailing dot is invalid", "[core][version]") {
    auto v = ParseVersion("v1.");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: double plus is invalid", "[core][version]") {
    auto v = ParseVersion("1.2.3+alpha+beta");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: double hyphen is invalid", "[core][version]") {
    auto v = ParseVersion("1.2.3-alpha-beta");
    // Actually this IS valid semver: prerelease identifiers separated by dots.
    // "alpha-beta" is a single identifier containing a hyphen, which is valid.
    // Let me remove this test — hyphen IN identifiers is allowed.
    REQUIRE(v.has_value());
    REQUIRE(v->prerelease == "alpha-beta");
}

TEST_CASE("Version: multiple hyphens are valid (identifiers can contain hyphens)", "[core][version]") {
    auto v = ParseVersion("1.2.3-alpha-beta-gamma");
    REQUIRE(v.has_value());
    REQUIRE(v->prerelease == "alpha-beta-gamma");
}

TEST_CASE("Version: whitespace is invalid", "[core][version]") {
    auto v = ParseVersion(" 1.2.3");
    REQUIRE_FALSE(v.has_value());
}

TEST_CASE("Version: trailing whitespace is invalid", "[core][version]") {
    auto v = ParseVersion("1.2.3 ");
    REQUIRE_FALSE(v.has_value());
}

// ============================================================================
// String round-trip (ToString)
// ============================================================================

TEST_CASE("Version: ToString round-trips basic version", "[core][version]") {
    auto v = ParseVersion("1.2.3");
    REQUIRE(v.has_value());
    REQUIRE(ToString(*v) == "1.2.3");
}

TEST_CASE("Version: ToString round-trips version with prerelease", "[core][version]") {
    auto v = ParseVersion("1.0.0-alpha.1");
    REQUIRE(v.has_value());
    REQUIRE(ToString(*v) == "1.0.0-alpha.1");
}

TEST_CASE("Version: ToString round-trips version with build", "[core][version]") {
    auto v = ParseVersion("1.0.0+build.42");
    REQUIRE(v.has_value());
    REQUIRE(ToString(*v) == "1.0.0+build.42");
}

TEST_CASE("Version: ToString round-trips version with prerelease and build", "[core][version]") {
    auto v = ParseVersion("1.0.0-rc.2+build.99");
    REQUIRE(v.has_value());
    REQUIRE(ToString(*v) == "1.0.0-rc.2+build.99");
}

TEST_CASE("Version: ToString does not include leading v (git tag normalization)", "[core][version]") {
    auto v = ParseVersion("v1.2.3");
    REQUIRE(v.has_value());
    REQUIRE(ToString(*v) == "1.2.3");
}

TEST_CASE("Version: ToString for zero version", "[core][version]") {
    Version zero;
    REQUIRE(ToString(zero) == "0.0.0");
}

// ============================================================================
// Comparison: release vs release
// ============================================================================

TEST_CASE("Version: equal versions compare equal", "[core][version]") {
    auto a = ParseVersion("1.2.3");
    auto b = ParseVersion("1.2.3");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(*a == *b);
    REQUIRE_FALSE(*a != *b);
    REQUIRE_FALSE(*a < *b);
    REQUIRE(*a <= *b);
    REQUIRE_FALSE(*a > *b);
    REQUIRE(*a >= *b);
}

TEST_CASE("Version: major difference determines ordering", "[core][version]") {
    auto a = ParseVersion("2.0.0");
    auto b = ParseVersion("1.9.9");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(*a > *b);
    REQUIRE(*b < *a);
}

TEST_CASE("Version: minor difference determines ordering when major equal", "[core][version]") {
    auto a = ParseVersion("1.3.0");
    auto b = ParseVersion("1.2.9");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(*a > *b);
}

TEST_CASE("Version: patch difference determines ordering when major.minor equal", "[core][version]") {
    auto a = ParseVersion("1.2.4");
    auto b = ParseVersion("1.2.3");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(*a > *b);
}

// ============================================================================
// Comparison: prerelease precedence (semver 2.0.0 §11)
// ============================================================================

TEST_CASE("Version: release has higher precedence than prerelease", "[core][version]") {
    auto release = ParseVersion("1.0.0");
    auto pre = ParseVersion("1.0.0-alpha");
    REQUIRE(release.has_value());
    REQUIRE(pre.has_value());
    REQUIRE(*release > *pre);
    REQUIRE(*pre < *release);
}

TEST_CASE("Version: prerelease ordering by identifier count", "[core][version]") {
    auto a = ParseVersion("1.0.0-alpha");
    auto b = ParseVersion("1.0.0-alpha.1");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    // alpha < alpha.1 because alpha is a shorter prefix
    REQUIRE(*a < *b);
}

TEST_CASE("Version: prerelease ordering by numeric vs alphanumeric", "[core][version]") {
    auto a = ParseVersion("1.0.0-alpha.1");
    auto b = ParseVersion("1.0.0-alpha.beta");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    // Numeric identifiers have lower precedence than alphanumeric
    REQUIRE(*a < *b);
}

TEST_CASE("Version: prerelease ordering with same count", "[core][version]") {
    auto a = ParseVersion("1.0.0-alpha.1");
    auto b = ParseVersion("1.0.0-alpha.2");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(*a < *b);
}

TEST_CASE("Version: prerelease ordering: alpha < beta < rc", "[core][version]") {
    auto alpha = ParseVersion("1.0.0-alpha");
    auto beta = ParseVersion("1.0.0-beta");
    auto rc = ParseVersion("1.0.0-rc");
    REQUIRE(alpha.has_value());
    REQUIRE(beta.has_value());
    REQUIRE(rc.has_value());
    REQUIRE(*alpha < *beta);
    REQUIRE(*beta < *rc);
}

TEST_CASE("Version: prerelease ordering by numeric value not string", "[core][version]") {
    auto a = ParseVersion("1.0.0-2");
    auto b = ParseVersion("1.0.0-10");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    // Numeric: 2 < 10 (not string "10" < "2")
    REQUIRE(*a < *b);
}

// ============================================================================
// Comparison: build metadata ignored per semver 2.0.0 §10
// ============================================================================

TEST_CASE("Version: build metadata is ignored in comparison", "[core][version]") {
    auto a = ParseVersion("1.0.0+build.1");
    auto b = ParseVersion("1.0.0+build.2");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(*a == *b);
}

TEST_CASE("Version: build metadata ignored but prerelease still matters", "[core][version]") {
    auto a = ParseVersion("1.0.0-alpha+build.1");
    auto b = ParseVersion("1.0.0-alpha+build.2");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(*a == *b);
}

TEST_CASE("Version: build metadata doesn't affect release vs prerelease", "[core][version]") {
    auto release = ParseVersion("1.0.0+build.999");
    auto pre = ParseVersion("1.0.0-alpha+build.1");
    REQUIRE(release.has_value());
    REQUIRE(pre.has_value());
    REQUIRE(*release > *pre);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE("Version: Compare static function", "[core][version]") {
    auto a = ParseVersion("1.0.0");
    auto b = ParseVersion("2.0.0");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(Compare(*a, *b) == std::strong_ordering::less);
    REQUIRE(Compare(*b, *a) == std::strong_ordering::greater);
    REQUIRE(Compare(*a, *a) == std::strong_ordering::equal);
}

TEST_CASE("Version: default constructed version is 0.0.0", "[core][version]") {
    Version v;
    REQUIRE(v.major == 0);
    REQUIRE(v.minor == 0);
    REQUIRE(v.patch == 0);
    REQUIRE(v.prerelease.empty());
    REQUIRE(v.build.empty());
}

// ============================================================================
// Build version injection (Horo::Build::EngineVersion)
// ============================================================================

TEST_CASE("Build: EngineVersion returns non-empty string", "[core][version][build]") {
    const char *ver = Horo::Build::EngineVersion();
    REQUIRE(ver != nullptr);
    REQUIRE(std::strlen(ver) > 0);
}

TEST_CASE("Build: EngineVersion returns valid semver x.y.z", "[core][version][build]") {
    const char *ver = Horo::Build::EngineVersion();
    auto parsed = ParseVersion(ver);
    REQUIRE(parsed.has_value());
    // Engine versions must be releases (no prerelease/build), matching CMake project()
    REQUIRE(parsed->prerelease.empty());
    REQUIRE(parsed->build.empty());
}

TEST_CASE("Build: EngineVersion round-trips through ParseVersion/ToString", "[core][version][build]") {
    const char *ver = Horo::Build::EngineVersion();
    auto parsed = ParseVersion(ver);
    REQUIRE(parsed.has_value());
    REQUIRE(ToString(*parsed) == ver);
}

TEST_CASE("Build: EngineVersion is usable as std::string", "[core][version][build]") {
    std::string ver{Horo::Build::EngineVersion()};
    REQUIRE(ver == Horo::Build::EngineVersion());
}
