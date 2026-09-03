#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>
#include <unordered_set>

namespace Horo::Gameplay::Detail {
    [[nodiscard]] constexpr bool IsAsciiLower(const char character) noexcept {
        return character >= 'a' && character <= 'z';
    }

    [[nodiscard]] constexpr bool IsAsciiDigit(const char character) noexcept {
        return character >= '0' && character <= '9';
    }

    [[nodiscard]] constexpr bool IsIdentifierCharacter(const char character) noexcept {
        return character == '_' || IsAsciiLower(character) || IsAsciiDigit(character);
    }

    [[nodiscard]] inline bool IsLowercaseIdentifier(const std::string_view value, const std::size_t maximumBytes) noexcept {
        return !value.empty() && value.size() <= maximumBytes && std::ranges::all_of(value, IsIdentifierCharacter);
    }

    [[nodiscard]] constexpr bool HasValidNamespaceSegments(const std::string_view value) noexcept {
        bool previousDot = false;
        std::size_t dotCount = 0;
        for (const char character : value) {
            const bool dot = character == '.';
            if ((dot && previousDot) || (!dot && !IsIdentifierCharacter(character)))
                return false;
            dotCount += dot ? 1U : 0U;
            previousDot = dot;
        }
        return dotCount >= 2 && !previousDot;
    }

    [[nodiscard]] inline bool IsNamespacedId(const std::string_view value, const std::size_t maximumBytes) noexcept {
        return value.size() >= 5 && value.size() <= maximumBytes && HasValidNamespaceSegments(value);
    }

    [[nodiscard]] inline bool IsNamespacedGameplayId(const std::string_view value, const std::size_t maximumBytes) noexcept {
        return value.starts_with("game.") && IsNamespacedId(value, maximumBytes);
    }

    template <typename Id> [[nodiscard]] bool ContainsInvalidOrDuplicateIds(const std::span<const Id> ids) {
        std::unordered_set<std::string_view> unique;
        unique.reserve(ids.size());
        return std::ranges::any_of(ids, [&unique](const Id &id) {
            return !id.IsValid() || !unique.emplace(id.Value()).second;
        });
    }
}  // namespace Horo::Gameplay::Detail
