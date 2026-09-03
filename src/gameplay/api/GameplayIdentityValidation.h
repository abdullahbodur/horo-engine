#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

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

    [[nodiscard]] inline bool IsNamespacedGameplayId(const std::string_view value, const std::size_t maximumBytes) noexcept {
        return value.size() >= 7 && value.size() <= maximumBytes && value.starts_with("game.") && HasValidNamespaceSegments(value);
    }
}  // namespace Horo::Gameplay::Detail
