#include <compare>
#pragma once

#include <limits>

namespace Horo
{
    /** @brief Generation-checked, process-local identity owned by a registry. Never serialize it. */
    template <typename Tag>
    struct Handle
    {
        static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t index = InvalidIndex;
        std::uint32_t generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return index != InvalidIndex; }
        [[nodiscard]] constexpr auto operator<=>(const Handle &) const noexcept = default;
    };
} // namespace Horo
