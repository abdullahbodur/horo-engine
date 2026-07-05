/** @file Version.cpp
 *  @brief Implements semver 2.0.0 parsing, formatting, and comparison.
 *  See Version.h. */
#include "core/Version.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <system_error>
#include <vector>

namespace Horo {

// ============================================================================
// Internal helpers
// ============================================================================

namespace {

/** @brief True if every char in `s` is ASCII digit. */
bool IsAllDigits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(),
                                     [](unsigned char c) { return std::isdigit(c); });
}

/** @brief True if `s` is a valid semver identifier (alphanumeric + hyphens,
 *  non-empty, no leading zero on numeric ids). */
bool IsValidIdentifier(std::string_view s) {
    if (s.empty()) return false;

    // Must be alphanumeric + hyphens
    for (unsigned char c : s) {
        if (!std::isalnum(c) && c != '-') return false;
    }

    // Numeric identifiers must not have leading zeros
    if (IsAllDigits(s) && s.size() > 1 && s[0] == '0') return false;

    return true;
}

/** @brief Split `s` by '.' and validate each segment. */
std::vector<std::string_view> SplitDots(std::string_view s) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start <= s.size()) {
        auto dot = s.find('.', start);
        auto part = s.substr(start, dot - start);
        if (!IsValidIdentifier(part)) return {}; // invalid → bail
        if (part.empty() && dot != std::string_view::npos) return {}; // empty segment
        parts.push_back(part);
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    return parts;
}

/** @brief Trim whitespace from both ends. */
bool HasSurroundingWhitespace(std::string_view s) {
    return !s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) ||
                          std::isspace(static_cast<unsigned char>(s.back())));
}

/** @brief Parses a non-negative semver numeric core component. */
bool ParseCoreInt(std::string_view s, int &out) {
    if (s.empty()) return false;
    if (s.size() > 1 && s[0] == '0') return false;
    if (!IsAllDigits(s)) return false;

    const auto result = std::from_chars(s.data(), s.data() + s.size(), out);
    return result.ec == std::errc{} && out >= 0;
}

/** @brief Returns true when major.minor.patch separators are well-ordered. */
bool HasValidSemverStructure(std::string_view s, size_t firstDot,
                             size_t secondDot, size_t hyphen, size_t plus) {
    if (firstDot == std::string_view::npos || secondDot == std::string_view::npos)
        return false;
    if (secondDot <= firstDot)
        return false;
    if (hyphen != std::string_view::npos && plus != std::string_view::npos &&
        hyphen > plus)
        return false;
    return !s.empty();
}

/** @brief Finds the end of the patch component before prerelease/build metadata. */
size_t FindPatchEnd(std::string_view s, size_t secondDot, size_t hyphen,
                    size_t plus) {
    if (hyphen != std::string_view::npos && hyphen > secondDot)
        return hyphen;
    if (plus != std::string_view::npos && plus > secondDot)
        return plus;
    return s.size();
}

/** @brief Parses a dot-separated prerelease/build identifier span. */
std::optional<std::string> ParseIdentifierSpan(std::string_view s) {
    if (s.empty())
        return std::nullopt;

    const auto parts = SplitDots(s);
    if (parts.empty() && s.find('.') != std::string_view::npos)
        return std::nullopt;
    if (parts.empty() && !IsValidIdentifier(s))
        return std::nullopt;
    return std::string(s);
}

/** @brief Parses prerelease metadata after '-' when present. */
std::optional<std::string> ParsePrerelease(std::string_view s, size_t hyphen,
                                           size_t plus, size_t secondDot) {
    if (hyphen == std::string_view::npos || hyphen <= secondDot)
        return std::string{};

    const size_t preStart = hyphen + 1;
    const size_t preEnd = (plus != std::string_view::npos && plus > hyphen)
                              ? plus
                              : s.size();
    return ParseIdentifierSpan(s.substr(preStart, preEnd - preStart));
}

/** @brief Parses build metadata after '+' when present. */
std::optional<std::string> ParseBuildMetadata(std::string_view s, size_t plus,
                                              size_t secondDot) {
    if (plus == std::string_view::npos || plus <= secondDot)
        return std::string{};

    return ParseIdentifierSpan(s.substr(plus + 1));
}

} // namespace

// ============================================================================
// ParseVersion
// ============================================================================

std::optional<Version> ParseVersion(std::string_view input) {
    if (input.empty()) return std::nullopt;
    if (HasSurroundingWhitespace(input)) return std::nullopt;

    std::string_view trimmed = input;
    if ((trimmed[0] == 'v' || trimmed[0] == 'V') && trimmed.size() > 1)
        trimmed = trimmed.substr(1);
    if (trimmed.empty()) return std::nullopt;

    const auto firstDot = trimmed.find('.');
    const auto secondDot = (firstDot == std::string_view::npos)
                               ? std::string_view::npos
                               : trimmed.find('.', firstDot + 1);
    const auto hyphen = trimmed.find('-');
    const auto plus = trimmed.find('+');
    if (!HasValidSemverStructure(trimmed, firstDot, secondDot, hyphen, plus))
        return std::nullopt;

    const size_t patchEnd = FindPatchEnd(trimmed, secondDot, hyphen, plus);
    const auto majorStr = trimmed.substr(0, firstDot);
    const auto minorStr = trimmed.substr(firstDot + 1, secondDot - firstDot - 1);
    const auto patchStr = trimmed.substr(secondDot + 1, patchEnd - secondDot - 1);

    int major = 0, minor = 0, patch = 0;
    if (!ParseCoreInt(majorStr, major)) return std::nullopt;
    if (!ParseCoreInt(minorStr, minor)) return std::nullopt;
    if (!ParseCoreInt(patchStr, patch)) return std::nullopt;

    auto prerelease = ParsePrerelease(trimmed, hyphen, plus, secondDot);
    if (!prerelease) return std::nullopt;

    auto build = ParseBuildMetadata(trimmed, plus, secondDot);
    if (!build) return std::nullopt;

    return Version{major, minor, patch, std::move(*prerelease), std::move(*build)};
}

// ============================================================================
// ToString
// ============================================================================

std::string ToString(const Version &v) {
    std::string result =
        std::format("{}.{}.{}", v.major, v.minor, v.patch);
    if (!v.prerelease.empty()) {
        result += '-';
        result += v.prerelease;
    }
    if (!v.build.empty()) {
        result += '+';
        result += v.build;
    }
    return result;
}

// ============================================================================
// Compare
// ============================================================================

std::strong_ordering Compare(const Version &a, const Version &b) {
    // Compare numeric components
    if (auto cmp = a.major <=> b.major; cmp != 0) return cmp;
    if (auto cmp = a.minor <=> b.minor; cmp != 0) return cmp;
    if (auto cmp = a.patch <=> b.patch; cmp != 0) return cmp;

    // Prerelease precedence (semver §11)
    const bool aHasPre = !a.prerelease.empty();
    const bool bHasPre = !b.prerelease.empty();

    if (!aHasPre && !bHasPre) return std::strong_ordering::equal;
    if (!aHasPre && bHasPre) return std::strong_ordering::greater;
    if (aHasPre && !bHasPre) return std::strong_ordering::less;

    // Both have prerelease — compare identifiers
    auto aIds = SplitDots(a.prerelease);
    auto bIds = SplitDots(b.prerelease);

    size_t n = std::min(aIds.size(), bIds.size());
    for (size_t i = 0; i < n; ++i) {
        bool aNum = IsAllDigits(aIds[i]);
        bool bNum = IsAllDigits(bIds[i]);

        if (aNum && bNum) {
            // Both numeric — compare numerically
            int aVal = 0, bVal = 0;
            std::from_chars(aIds[i].data(), aIds[i].data() + aIds[i].size(), aVal);
            std::from_chars(bIds[i].data(), bIds[i].data() + bIds[i].size(), bVal);
            if (auto cmp = aVal <=> bVal; cmp != 0) return cmp;
        } else if (aNum && !bNum) {
            // Numeric identifiers have lower precedence
            return std::strong_ordering::less;
        } else if (!aNum && bNum) {
            return std::strong_ordering::greater;
        } else {
            // Both alphanumeric — compare lexically
            if (auto cmp = aIds[i] <=> bIds[i]; cmp != 0) return cmp;
        }
    }

    // Equal prefix — longer set has higher precedence
    return aIds.size() <=> bIds.size();
}

// ============================================================================
// Relational operators (delegate to Compare)
// ============================================================================

bool operator==(const Version &a, const Version &b) {
    return Compare(a, b) == std::strong_ordering::equal;
}

bool operator!=(const Version &a, const Version &b) {
    return Compare(a, b) != std::strong_ordering::equal;
}

bool operator<(const Version &a, const Version &b) {
    return Compare(a, b) == std::strong_ordering::less;
}

bool operator<=(const Version &a, const Version &b) {
    return Compare(a, b) != std::strong_ordering::greater;
}

bool operator>(const Version &a, const Version &b) {
    return Compare(a, b) == std::strong_ordering::greater;
}

bool operator>=(const Version &a, const Version &b) {
    return Compare(a, b) != std::strong_ordering::less;
}

} // namespace Horo
