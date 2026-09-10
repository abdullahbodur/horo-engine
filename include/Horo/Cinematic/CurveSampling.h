#pragma once

/**
 * @file CurveSampling.h
 * @brief Validated allocation-free scalar curve interpolation and random-access sampling.
 */

#include "Horo/Foundation/Result.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Horo::Cinematic {
    /** @brief Exact timeline tick used by cooked scalar curves. */
    using CurveTime = std::int64_t;

    /** @brief Interpolation selected by the left key of a segment. */
    enum class CurveInterpolation : std::uint8_t {
        Constant,
        Linear,
        CubicBezier,
        HermiteSpline,
        Count
    };

    /** @brief Behavior when a sample falls outside the authored key range. */
    enum class CurveBoundaryMode : std::uint8_t {
        Clamp,
        Repeat,
        PingPong,
        Count
    };

    /** @brief Independent policies for time before the first and after the last key. */
    struct CurveBoundaryPolicy final {
        CurveBoundaryMode beforeFirst{CurveBoundaryMode::Clamp};
        CurveBoundaryMode afterLast{CurveBoundaryMode::Clamp};
        constexpr auto operator<=>(const CurveBoundaryPolicy &) const noexcept = default;
    };

    /** @brief One explicit tangent offset relative to its owning key. */
    struct CurveTangent final {
        CurveTime timeOffset{}; /**< Exact signed timeline offset; incoming tangents point backward. */
        float valueOffset{};    /**< Scalar offset from the owning key value. */
        constexpr auto operator<=>(const CurveTangent &) const noexcept = default;
    };

    /** @brief One immutable scalar control point in a strictly ordered cooked curve. */
    struct ScalarCurveKey final {
        CurveTime time{};
        float value{};
        CurveInterpolation interpolation{CurveInterpolation::Linear};
        CurveTangent tangentIn{};
        CurveTangent tangentOut{};
        constexpr auto operator<=>(const ScalarCurveKey &) const noexcept = default;
    };

    /** @brief Result metadata sufficient to identify the sampled segment and mapped time. */
    struct ScalarCurveSample final {
        float value{};
        CurveTime mappedTime{};
        std::size_t leftKeyIndex{};
        bool boundaryMapped{};
        constexpr auto operator<=>(const ScalarCurveSample &) const noexcept = default;
    };

    /** @brief Compile-time ceiling for keys admitted by one scalar curve view. */
    inline constexpr std::size_t MaximumScalarCurveKeys = 65'536;

    /**
     * @brief Non-owning validated view for history-independent scalar curve sampling.
     * @note The caller retains immutable key storage for the complete view lifetime.
     * @note Successful Sample calls allocate nothing, perform O(log N) lookup, and use at most twelve cubic iterations.
     */
    class ScalarCurveView final {
    public:
        /**
         * @brief Validates ordered finite keys and tangent monotonicity once, outside the frame-hot path.
         * @param keys Immutable key storage retained by the caller.
         * @param boundaries Mapping policies applied outside the authored range.
         * @return Validated borrowed view or a typed malformed, non-finite, or capacity failure.
         */
        [[nodiscard]] static Result<ScalarCurveView> Create(std::span<const ScalarCurveKey> keys, CurveBoundaryPolicy boundaries = {});

        /**
         * @brief Samples the curve directly at an arbitrary timeline time.
         * @param time Exact target time; prior samples and seek direction are irrelevant.
         * @return Sample value and deterministic segment metadata.
         * @note The successful path is allocation-free and thread-safe while the borrowed keys remain immutable.
         */
        [[nodiscard]] Result<ScalarCurveSample> Sample(CurveTime time) const;

        /** @brief Returns the borrowed validated keys. @return Immutable caller-owned key span. */
        [[nodiscard]] std::span<const ScalarCurveKey> Keys() const noexcept;

        /** @brief Returns the immutable boundary policy. @return Policy captured during validation. */
        [[nodiscard]] CurveBoundaryPolicy Boundaries() const noexcept;

    private:
        ScalarCurveView(std::span<const ScalarCurveKey> keys, CurveBoundaryPolicy boundaries) noexcept;

        std::span<const ScalarCurveKey> keys_;
        CurveBoundaryPolicy boundaries_;
    };
}  // namespace Horo::Cinematic
