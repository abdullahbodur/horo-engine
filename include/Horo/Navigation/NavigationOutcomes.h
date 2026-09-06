#pragma once

/**
 * @file NavigationOutcomes.h
 * @brief Provider-neutral terminal query outcomes and normalized provider failure details.
 */

#include "Horo/Foundation/ErrorCode.h"
#include "Horo/Navigation/NavigationIdentity.h"

#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

namespace Horo::Navigation {
    /** @brief Stable terminal outcome kinds; Unsupported is a pre-dispatch admission error, not a queued outcome. */
    enum class NavigationOutcomeKind : std::uint8_t {
        Succeeded,
        Partial,
        NoPath,
        Cancelled,
        Stale,
        Unavailable,
        Failed,
        Count
    };

    /** @brief Normalized provider failure categories; native codes remain diagnostic evidence only. */
    enum class NavigationProviderFailureCategory : std::uint8_t {
        InvalidProviderData,
        ResourceExhausted,
        TransientFailure,
        PermanentFailure,
        Count
    };

    /** @brief Typed reasons that admitted work can become unavailable before publication. */
    enum class NavigationUnavailableReason : std::uint8_t {
        NoNavigationData,
        WorldUnavailable,
        ProviderUnavailable,
        Count
    };

    /** @brief Complete successful value produced from the captured navigation generation. */
    template <typename ValueT> struct NavigationSucceeded final {
        ValueT value;
    };

    /** @brief Explicitly permitted partial value; it is never equivalent to a stale result. */
    template <typename ValueT> struct NavigationPartial final {
        ValueT value;
    };

    /** @brief Proven negative result over complete admitted coverage. */
    struct NavigationNoPath final {};

    /** @brief Work cancelled before owner-thread publication. */
    struct NavigationCancelled final {};

    /** @brief Result computed for an old topology generation and therefore not publishable. */
    struct NavigationStale final {
        NavigationGeneration expectedTopology;
        NavigationGeneration observedTopology;
    };

    /** @brief Admitted work could not obtain the required navigation data or provider availability. */
    struct NavigationUnavailable final {
        NavigationUnavailableReason reason{NavigationUnavailableReason::NoNavigationData};
    };

    /** @brief Provider execution failed with a normalized category and Horo-owned diagnostic payload. */
    struct NavigationFailed final {
        NavigationProviderFailureCategory category{NavigationProviderFailureCategory::PermanentFailure};
        Error error;
    };

    /**
     * @brief Closed terminal outcome for one admitted query.
     *
     * The alternative order is stable and matches NavigationOutcomeKind. Provider-native types and error codes are
     * forbidden; adapter detail is retained only as Horo Diagnostic values inside NavigationFailed::error.
     */
    template <typename ValueT>
    using NavigationOutcome = std::variant<NavigationSucceeded<ValueT>, NavigationPartial<ValueT>, NavigationNoPath, NavigationCancelled,
                                           NavigationStale, NavigationUnavailable, NavigationFailed>;

    /**
     * @brief Returns the stable typed kind for a closed navigation outcome.
     * @param outcome Terminal outcome whose alternative remains unchanged.
     * @return Kind corresponding exactly to the active variant alternative.
     */
    template <typename ValueT>
    [[nodiscard]] NavigationOutcomeKind GetNavigationOutcomeKind(const NavigationOutcome<ValueT> &outcome) noexcept {
        static_assert(std::variant_size_v<NavigationOutcome<ValueT>> == static_cast<std::size_t>(NavigationOutcomeKind::Count));
        return static_cast<NavigationOutcomeKind>(outcome.index());
    }

    /**
     * @brief Wraps provider detail behind the stable Navigation provider-failed identity.
     * @param category Typed normalized failure category used for caller branching.
     * @param diagnostics Owned Horo diagnostics translated and redacted by the provider adapter.
     * @return Failed outcome preserving diagnostic order and content without exposing a native code as identity.
     */
    [[nodiscard]] NavigationFailed MakeNavigationProviderFailure(NavigationProviderFailureCategory category,
                                                                 std::vector<Diagnostic> diagnostics = {});
}  // namespace Horo::Navigation
