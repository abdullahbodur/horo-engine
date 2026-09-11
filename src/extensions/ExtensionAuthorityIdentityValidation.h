#pragma once

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <string_view>

namespace Horo::Extensions::Detail {
    inline constexpr std::size_t MaximumExtensionAuthorityIdentityBytes = 256;

    /** @brief Validates the canonical lowercase dot-separated identity shared by extension authority contracts. */
    [[nodiscard]] inline bool IsCanonicalExtensionAuthorityId(const std::string_view value) noexcept {
        if (value.empty() || value.size() > MaximumExtensionAuthorityIdentityBytes || value.front() == '.' || value.back() == '.')
            return false;
        std::size_t start = 0;
        while (start < value.size()) {
            const std::size_t separator = value.find('.', start);
            const std::size_t end = separator == std::string_view::npos ? value.size() : separator;
            const std::string_view segment = value.substr(start, end - start);
            if (segment.empty() || segment.front() < 'a' || segment.front() > 'z' || segment.back() == '-' ||
                !std::ranges::all_of(segment, [](const char character) {
                return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-';
            })) {
                return false;
            }
            start = end + 1;
        }
        return true;
    }
}  // namespace Horo::Extensions::Detail
