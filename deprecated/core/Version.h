/** @file Version.h
 *  @brief Semantic version type for build artifacts.
 *
 *  Implements semver 2.0.0 parsing, string round-trip, and release
 *  precedence comparison.  Accepts optional leading 'v' (git-tag style),
 *  but normalises to a 'v'-free canonical form via ToString().
 */
#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace Horo {

/** @brief A semantic version (major.minor.patch[-prerelease][+build]).
 *
 *  Build metadata is stored but, per the semver spec, does not affect
 *  precedence ordering.  The canonical string form excludes a leading 'v'.
 */
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string prerelease; ///< Dot-separated prerelease identifiers (empty for releases).
    std::string build;      ///< Dot-separated build metadata (empty if none).
};

/** @brief Parse a semantic version string.
 *
 *  Accepted forms:
 *    - major.minor.patch
 *    - major.minor.patch-prerelease
 *    - major.minor.patch+build
 *    - major.minor.patch-prerelease+build
 *    - v<VERSION> (leading 'v' / 'V' stripped before parsing)
 *
 *  Rejects inputs with leading/trailing whitespace, leading zeros on
 *  numeric components, empty identifiers, and other semver violations.
 *
 *  @param input The version string to parse.
 *  @return A valid Version on success, std::nullopt on parse failure.
 */
std::optional<Version> ParseVersion(std::string_view input);

/** @brief Returns the canonical string form (no leading 'v').
 *
 *  This is the stable round-trip form: `ToString(ParseVersion(s)) == s`
 *  for inputs that do not start with 'v'.  v-prefixed inputs are
 *  normalised (the leading 'v' is dropped).
 *
 *  @param v The version to format.
 *  @return "major.minor.patch[-prerelease][+build]"
 */
std::string ToString(const Version &v);

/** @brief Compare two versions for release precedence (semver 2.0.0 §11).
 *
 *  Build metadata is ignored; only major, minor, patch, and prerelease
 *  contribute to the ordering.
 *
 *  @param a Left operand.
 *  @param b Right operand.
 *  @return std::strong_ordering result.
 */
std::strong_ordering Compare(const Version &a, const Version &b);

/// @name Relational operators (delegate to Compare)
/// @{
bool operator==(const Version &a, const Version &b);
bool operator!=(const Version &a, const Version &b);
bool operator<(const Version &a, const Version &b);
bool operator<=(const Version &a, const Version &b);
bool operator>(const Version &a, const Version &b);
bool operator>=(const Version &a, const Version &b);
/// @}

} // namespace Horo
