#pragma once

#include <chrono>
#include <cstdint>

namespace Horo
{
    /** @brief Signed monotonic duration value for scheduling and frame timing. */
    class Duration
    {
    public:
        [[nodiscard]] static constexpr Duration FromMilliseconds(const std::int64_t value) noexcept
        {
            return Duration(std::chrono::milliseconds(value));
        }
        [[nodiscard]] constexpr std::int64_t ToMilliseconds() const noexcept
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(m_value).count();
        }
        [[nodiscard]] friend constexpr Duration operator+(const Duration lhs, const Duration rhs) noexcept
        {
            return Duration(lhs.m_value + rhs.m_value);
        }
    private:
        explicit constexpr Duration(std::chrono::steady_clock::duration value) noexcept : m_value(value) {}
        std::chrono::steady_clock::duration m_value{};
    };
} // namespace Horo
