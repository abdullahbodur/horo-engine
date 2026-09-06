#pragma once

/**
 * @file NavigationCapabilities.h
 * @brief Provider-neutral navigation capability, query-quality, limit, and pre-dispatch admission contracts.
 */

#include "Horo/Foundation/Result.h"

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

namespace Horo::Navigation {
    /** @brief Composition-level provider availability, independent of an active navigation world. */
    enum class NavigationProviderAvailability : std::uint8_t {
        Omitted,
        Unavailable,
        Available,
        Count
    };

    /** @brief Unknown evidence, permanent lack of support, temporary unavailability, and usable support are distinct. */
    enum class NavigationSupport : std::uint8_t {
        Unknown,
        Unsupported,
        Unavailable,
        Available,
        Count
    };

    /** @brief Closed provider capabilities; grounded query support is refined by query kind and quality. */
    enum class NavigationCapability : std::uint8_t {
        GroundedQueries,
        TopologyUpdates,
        DynamicTileCarving,
        CrowdAvoidance,
        MeshBuilding,
        RuntimeGroundedTileRebuild,
        Count
    };

    /** @brief Closed grounded spatial-query families; other navigation domains require distinct future capabilities. */
    enum class NavigationQueryKind : std::uint8_t {
        Path,
        NearestPoint,
        Raycast,
        Count
    };

    /** @brief Closed cost/fidelity levels whose exact work remains bounded by the accompanying limits. */
    enum class NavigationQualityLevel : std::uint8_t {
        Low,
        Balanced,
        High,
        Count
    };

    /** @brief Finite hard ceilings for one supported query-quality pair, not a per-request reservation. */
    struct NavigationQueryLimits final {
        std::uint32_t maximumNodeExpansions{}; /**< Maximum provider work units admitted for one query. */
        std::uint32_t maximumResultPoints{};   /**< Maximum provider-neutral points returned by one query. */
        float maximumSearchDistanceMeters{};   /**< Maximum finite world-space search distance in metres. */

        constexpr auto operator<=>(const NavigationQueryLimits &) const noexcept = default;
    };

    /** @brief Typed request bounds checked against one exact capability revision before dispatch. */
    struct NavigationQueryRequirement final {
        NavigationQueryKind query{NavigationQueryKind::Path};
        NavigationQualityLevel quality{NavigationQualityLevel::Balanced};
        NavigationQueryLimits limits{};
    };

    /**
     * @brief Immutable capability evidence published by a composed provider without provider names or native values.
     *
     * Revision is non-zero and changes whenever any support or limit evidence changes. Unsupported and unavailable
     * query-quality pairs carry zero limits, preventing stale limits from becoming a second source of support truth.
     * An omitted provider reports every capability and query-quality pair Unsupported. An unavailable provider cannot
     * report Available support. Quality levels do not imply fallback: callers request one exact level. Integral limits
     * are finite by representation; the later project-profile contract may impose narrower product capacity ceilings.
     */
    struct NavigationProviderCapabilities final {
        std::uint32_t contractVersion{1};
        std::uint64_t revision{};
        NavigationProviderAvailability availability{NavigationProviderAvailability::Unavailable};
        std::array<NavigationSupport, static_cast<std::size_t>(NavigationCapability::Count)> capabilities{};
        std::array<std::array<NavigationSupport, static_cast<std::size_t>(NavigationQualityLevel::Count)>,
                   static_cast<std::size_t>(NavigationQueryKind::Count)>
            querySupport{};
        std::array<std::array<NavigationQueryLimits, static_cast<std::size_t>(NavigationQualityLevel::Count)>,
                   static_cast<std::size_t>(NavigationQueryKind::Count)>
            queryLimits{};
        std::uint32_t maximumConcurrentQueries{};
    };

    /**
     * @brief Validates every field, support state, query-quality limit, and cross-field invariant.
     * @param capabilities Complete immutable provider evidence.
     * @return True only for coherent version-one evidence; not proof of provider qualification or an active world.
     */
    [[nodiscard]] bool ValidateNavigationProviderCapabilities(const NavigationProviderCapabilities &capabilities) noexcept;

    /**
     * @brief Reads one provider capability without strengthening malformed or unknown evidence.
     * @param capabilities Complete immutable provider evidence.
     * @param capability Known version-one capability.
     * @return Reported support, or Unknown for malformed evidence or an unknown capability.
     */
    [[nodiscard]] NavigationSupport QueryNavigationCapability(const NavigationProviderCapabilities &capabilities,
                                                              NavigationCapability capability) noexcept;

    /**
     * @brief Reads support for one exact grounded-query quality without performing fallback.
     * @param capabilities Complete immutable provider evidence.
     * @param query Known version-one query family.
     * @param quality Exact requested quality level.
     * @return Reported support, or Unknown for malformed evidence or an unknown typed value.
     */
    [[nodiscard]] NavigationSupport QueryNavigationSupport(const NavigationProviderCapabilities &capabilities, NavigationQueryKind query,
                                                           NavigationQualityLevel quality) noexcept;

    /**
     * @brief Validates one bounded query against exact revision-scoped provider evidence before any queue mutation.
     * @param capabilities Immutable capability snapshot captured for this admission attempt.
     * @param expectedRevision Exact non-zero revision retained by the caller.
     * @param requirement Exact query, quality, and finite requested ceilings.
     * @return Success or a typed malformed, stale, unsupported, unavailable, or limit failure.
     * @post Success performs no provider call, allocation, job submission, or queue mutation.
     */
    [[nodiscard]] Result<void> AdmitNavigationQuery(const NavigationProviderCapabilities &capabilities, std::uint64_t expectedRevision,
                                                    const NavigationQueryRequirement &requirement);

    /**
     * @brief Runs a caller-owned dispatch only after the exact query requirement passes capability admission.
     * @tparam Dispatch Callable returning Result<void>; it owns any subsequent bounded queue mutation.
     * @param capabilities Immutable capability snapshot captured for this admission attempt.
     * @param expectedRevision Exact non-zero revision retained by the caller.
     * @param requirement Exact query, quality, and finite requested ceilings.
     * @param dispatch Callable invoked exactly once after successful admission and never on admission failure.
     * @return Admission failure without dispatch, otherwise the dispatch result unchanged.
     */
    template <typename Dispatch>
        requires std::same_as<std::remove_cvref_t<std::invoke_result_t<Dispatch>>, Result<void>>
    [[nodiscard]] Result<void> DispatchNavigationQueryIfSupported(const NavigationProviderCapabilities &capabilities,
                                                                  const std::uint64_t expectedRevision,
                                                                  const NavigationQueryRequirement &requirement, Dispatch &&dispatch) {
        const auto admission = AdmitNavigationQuery(capabilities, expectedRevision, requirement);
        if (admission.HasError())
            return Result<void>::Failure(admission.ErrorValue());
        return std::invoke(std::forward<Dispatch>(dispatch));
    }
}  // namespace Horo::Navigation
