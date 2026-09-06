#pragma once

/**
 * @file WorldCoordinate64.h
 * @brief Exact millimeter global coordinates independent of floating-origin frames.
 */

#include "Horo/Foundation/Result.h"

#include <array>
#include <compare>
#include <cstdint>

namespace Horo::Math {
    /** @brief Stable precision errors produced while admitting canonical world coordinates. */
    namespace WorldCoordinateErrors {
        /** @brief A hierarchical coordinate component is not normalized or exceeds signed 64-bit millimeters. */
        extern const ErrorCodeDescriptor CoordinateOutOfRange;
    }  // namespace WorldCoordinateErrors

    /** @brief One normalized axis of the ADR-026 hierarchical global coordinate. */
    struct WorldCoordinateAxis64 final {
        std::int64_t cellIndex{};         /**< Canonical 1024-meter cell index. */
        std::int32_t offsetMillimeters{}; /**< Millimeter offset in [0, CanonicalCellSizeMillimeters). */

        [[nodiscard]] constexpr auto operator<=>(const WorldCoordinateAxis64 &) const noexcept = default;
    };

    /** @brief Exact ADR-026 global coordinate with millimeter precision and normalized hierarchical axes. */
    class WorldCoordinate64 final {
    public:
        /** @brief Canonical hierarchy cell size selected by ADR-026. */
        static constexpr std::int64_t CanonicalCellSizeMillimeters = 1'024'000;

        /** @brief Constructs the global origin. */
        WorldCoordinate64() = default;

        /**
         * @brief Normalizes exact signed millimeter coordinates.
         * @param xMillimeters Absolute global X millimeters.
         * @param yMillimeters Absolute global Y millimeters.
         * @param zMillimeters Absolute global Z millimeters.
         * @return Canonical hierarchical coordinate with an exact round trip.
         */
        [[nodiscard]] static WorldCoordinate64 FromMillimeters(std::int64_t xMillimeters, std::int64_t yMillimeters,
                                                               std::int64_t zMillimeters) noexcept;

        /**
         * @brief Validates externally decoded hierarchical components.
         * @param x Normalized X component.
         * @param y Normalized Y component.
         * @param z Normalized Z component.
         * @return Exact coordinate or WorldCoordinateErrors::CoordinateOutOfRange.
         */
        [[nodiscard]] static Result<WorldCoordinate64> Create(WorldCoordinateAxis64 x, WorldCoordinateAxis64 y, WorldCoordinateAxis64 z);

        /** @brief Returns normalized X. @return Canonical hierarchical component. */
        [[nodiscard]] constexpr WorldCoordinateAxis64 X() const noexcept {
            return axes_[0];
        }

        /** @brief Returns normalized Y. @return Canonical hierarchical component. */
        [[nodiscard]] constexpr WorldCoordinateAxis64 Y() const noexcept {
            return axes_[1];
        }

        /** @brief Returns normalized Z. @return Canonical hierarchical component. */
        [[nodiscard]] constexpr WorldCoordinateAxis64 Z() const noexcept {
            return axes_[2];
        }

        /** @brief Returns exact signed millimeters. @return X, Y, and Z in canonical millimeters. */
        [[nodiscard]] std::array<std::int64_t, 3> Millimeters() const noexcept;

        [[nodiscard]] constexpr auto operator<=>(const WorldCoordinate64 &) const noexcept = default;

    private:
        explicit constexpr WorldCoordinate64(const std::array<WorldCoordinateAxis64, 3> &axes) noexcept : axes_(axes) {}

        std::array<WorldCoordinateAxis64, 3> axes_{};
    };
}  // namespace Horo::Math
