#pragma once

#include "Horo/Navigation/Backends/RecastDetourProvider.h"

#include <DetourNavMesh.h>
#include <cmath>
#include <cstddef>
#include <limits>

namespace Horo::Navigation::Detail {
    [[nodiscard]] inline bool IsPositiveFinite(const float value) noexcept {
        return std::isfinite(value) && value > 0.0F;
    }

    [[nodiscard]] inline bool IsNonNegativeFinite(const float value) noexcept {
        return std::isfinite(value) && value >= 0.0F;
    }

    [[nodiscard]] inline bool CheckedAdd(std::size_t &total, const std::size_t value) noexcept {
        const std::size_t remaining = std::numeric_limits<std::size_t>::max() - total;
        if (value > remaining)
            return false;
        total = total + value;
        return true;
    }

    [[nodiscard]] inline bool CheckedProduct(const std::size_t first, const std::size_t second, std::size_t &result) noexcept {
        if (first == 0) {
            result = 0;
            return true;
        }
        if (second > std::numeric_limits<std::size_t>::max() / first)
            return false;
        result = first * second;
        return true;
    }

    [[nodiscard]] inline bool FitsOwnedBudget(const RecastDetourProviderCreateInfo &info) noexcept {
        std::size_t bytes{};
        std::size_t value{};
        if (!CheckedProduct(info.vertices.size(), 64U, value) || !CheckedAdd(bytes, value) ||
            !CheckedProduct(info.polygons.size(), 256U, value) || !CheckedAdd(bytes, value) ||
            !CheckedProduct(info.maximumConcurrentQueries,
                            (static_cast<std::size_t>(info.maximumQueryNodes) * sizeof(dtPolyRef)) +
                                (static_cast<std::size_t>(info.maximumResultPoints) *
                                 ((sizeof(float) * 3U) + sizeof(unsigned char) + sizeof(dtPolyRef))),
                            value) ||
            !CheckedAdd(bytes, value))
            return false;
        return bytes <= info.maximumOwnedBytes;
    }
}  // namespace Horo::Navigation::Detail
