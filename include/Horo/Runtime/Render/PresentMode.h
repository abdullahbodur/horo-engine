#pragma once

/**
 * @file PresentMode.h
 * @brief Backend-neutral present-mode intent and deterministic negotiation contracts.
 */

#include "Horo/Foundation/Result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Horo::Render {
    /** @brief Maximum present-mode entries admitted in one request or capability set. */
    inline constexpr std::size_t MaximumPresentModeEntries = 8;

    /** @brief Backend-neutral presentation pacing policy. */
    enum class PresentMode : std::uint8_t {
        Fifo,
        Immediate,
    };

    /** @brief Whether host intent requires one exact mode or permits an ordered choice. */
    enum class PresentModeRequestKind : std::uint8_t {
        Required,
        Auto,
    };

    /** @brief Owned host intent containing one required mode or ordered Auto preferences. */
    struct PresentModeRequest final {
        PresentModeRequestKind kind{PresentModeRequestKind::Required}; /**< Required or ordered Auto policy. */
        std::vector<PresentMode> preferences;                          /**< Exact singleton or unique preference order. */
    };

    /** @brief Canonical backend-supported present modes without native enum values. */
    struct PresentModeCapabilities final {
        std::vector<PresentMode> supportedModes; /**< Strictly enum-sorted unique supported modes. */
    };

    /** @brief Whether negotiation preserved the first requested mode. */
    enum class PresentModeResolution : std::uint8_t {
        Exact,
        DegradedFallback,
    };

    /** @brief Immutable successful negotiation result with explicit fallback evidence. */
    struct ResolvedPresentMode final {
        PresentMode requested{PresentMode::Fifo};                       /**< First host-requested mode. */
        PresentMode resolved{PresentMode::Fifo};                        /**< Supported mode selected for realization. */
        PresentModeResolution resolution{PresentModeResolution::Exact}; /**< Exact or degraded outcome. */
        std::size_t preferenceIndex{};                                  /**< Selected index in the host preference list. */

        [[nodiscard]] bool operator==(const ResolvedPresentMode &) const noexcept = default;
    };

    /**
     * @brief Selects one supported mode using only explicit host policy.
     * @param request Required mode or ordered Auto preference list.
     * @param capabilities Canonically ordered backend-supported modes.
     * @return Exact/degraded selection or a typed invalid, unsupported, or no-match failure.
     *
     * This pure value operation owns no thread, callback, cancellation, surface, or shutdown
     * lifecycle. The host invokes it before the owning surface lifecycle applies the result.
     */
    [[nodiscard]] Result<ResolvedPresentMode> NegotiatePresentMode(const PresentModeRequest &request,
                                                                   const PresentModeCapabilities &capabilities);
}  // namespace Horo::Render
