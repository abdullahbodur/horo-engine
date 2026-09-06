#pragma once

/**
 * @file NavigationAreas.h
 * @brief Stable navigation-area identities, query-filter policies, and deterministic descriptor resolution.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Navigation/NavigationIdentity.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Horo::Navigation {
    struct NavigationAreaIdentityTag;
    struct NavigationFilterIdentityTag;
    struct NavigationDescriptorSourceIdentityTag;

    /** @brief Stable authored area identity, independent of ordering and localized presentation. */
    using NavigationAreaId = NavigationIdentity<NavigationAreaIdentityTag>;
    /** @brief Stable reusable query-filter identity, independent of ordering and localized presentation. */
    using NavigationFilterId = NavigationIdentity<NavigationFilterIdentityTag>;
    /** @brief Stable identity of a project or package contributing navigation descriptors. */
    using NavigationDescriptorSourceId = NavigationIdentity<NavigationDescriptorSourceIdentityTag>;

    /** @brief Closed ownership classes for project-authoritative and package-contributed descriptors. */
    enum class NavigationDescriptorSourceKind : std::uint8_t {
        Project,
        Package,
        Count
    };

    /** @brief Typed provenance retained when deterministic registry input is assembled. */
    struct NavigationDescriptorSource final {
        NavigationDescriptorSourceKind kind{NavigationDescriptorSourceKind::Project};
        NavigationDescriptorSourceId id;

        constexpr auto operator<=>(const NavigationDescriptorSource &) const noexcept = default;
    };

    /** @brief Project-defined traversal groups used by reusable include and exclude policies. */
    struct NavigationAreaFlags final {
        std::uint64_t bits{};

        /** @brief Reports whether this set shares at least one flag with another set. */
        [[nodiscard]] constexpr bool Intersects(const NavigationAreaFlags other) const noexcept {
            return (bits & other.bits) != 0;
        }

        /** @brief Reports whether no traversal group is selected. */
        [[nodiscard]] constexpr bool Empty() const noexcept {
            return bits == 0;
        }

        constexpr auto operator<=>(const NavigationAreaFlags &) const noexcept = default;
    };

    /** @brief One stable area and its validated base traversal policy. */
    struct NavigationAreaDescriptor final {
        NavigationAreaId id;
        NavigationDescriptorSource source;
        float traversalCost{1.0F};
        NavigationAreaFlags flags;
    };

    /** @brief Filter-local traversal cost replacing the referenced area's base cost. */
    struct NavigationAreaCostOverride final {
        NavigationAreaId area;
        float traversalCost{1.0F};
    };

    /** @brief Stable reusable filter with exclusion-wins flag policy and optional exact area costs. */
    struct NavigationQueryFilterDescriptor final {
        NavigationFilterId id;
        NavigationDescriptorSource source;
        NavigationAreaFlags includedFlags; /**< Empty includes every area unless excluded. */
        NavigationAreaFlags excludedFlags; /**< Matching exclusion always wins over inclusion. */
        std::vector<NavigationAreaCostOverride> costOverrides;
    };

    /** @brief Fully resolved traversal decision for one exact filter and area identity. */
    struct NavigationTraversalPolicy final {
        bool traversable{};
        float traversalCost{};
    };

    /**
     * @brief Immutable identity-sorted navigation area and query-filter registry.
     *
     * Project and package contributions share one collision domain. Duplicate identities are rejected rather than
     * resolved by input order or implicit precedence. Filter cost overrides must reference a registered area.
     */
    class NavigationAreaRegistry final {
    public:
        /** @brief Registry storage has unique ownership and cannot be copied. */
        NavigationAreaRegistry(const NavigationAreaRegistry &) = delete;
        /** @brief Registry storage has unique ownership and cannot be copy-assigned. */
        NavigationAreaRegistry &operator=(const NavigationAreaRegistry &) = delete;

        /**
         * @brief Transfers owned descriptor storage to a new registry.
         * @param other Registry whose storage and borrowed-pointer lifetime responsibility transfer to this instance.
         */
        NavigationAreaRegistry(NavigationAreaRegistry &&) noexcept = default;
        /** @brief Existing registry storage cannot be replaced while borrowed descriptors may exist. */
        NavigationAreaRegistry &operator=(NavigationAreaRegistry &&) = delete;

        /**
         * @brief Validates, owns, and deterministically orders complete descriptor contributions.
         * @param areas Project and package area contributions in arbitrary order.
         * @param filters Project and package filter contributions in arbitrary order.
         * @return Immutable registry or a typed invalid, conflict, missing-area, or capacity error.
         */
        [[nodiscard]] static Result<NavigationAreaRegistry> Create(std::span<const NavigationAreaDescriptor> areas,
                                                                   std::span<const NavigationQueryFilterDescriptor> filters);

        /** @brief Returns identity-sorted area descriptors. @return Registry-owned immutable descriptors. */
        [[nodiscard]] std::span<const NavigationAreaDescriptor> Areas() const noexcept;

        /** @brief Returns identity-sorted filter descriptors and their identity-sorted overrides. */
        [[nodiscard]] std::span<const NavigationQueryFilterDescriptor> Filters() const noexcept;

        /**
         * @brief Resolves one exact area without a default fallback.
         * @param id Stable area identity.
         * @return A copy of the registered descriptor or NavigationErrors::AreaUnknown.
         */
        [[nodiscard]] Result<NavigationAreaDescriptor> ResolveArea(NavigationAreaId id) const;

        /**
         * @brief Resolves one exact query filter without a default fallback.
         * @param id Stable filter identity.
         * @return A registry-owned immutable descriptor pointer or NavigationErrors::FilterUnknown. The pointer remains
         * valid until the registry is destroyed and must not outlive it.
         */
        [[nodiscard]] Result<const NavigationQueryFilterDescriptor *> ResolveFilter(NavigationFilterId id) const;

        /**
         * @brief Applies exclusion-wins flags and an exact optional cost override.
         * @param filter Stable filter identity; an unknown identity is never treated as a default filter.
         * @param area Stable area identity; an unknown identity is never treated as a default area.
         * @return Resolved traversability and finite non-negative cost, or a typed unknown-identity error.
         */
        [[nodiscard]] Result<NavigationTraversalPolicy> ResolveTraversal(NavigationFilterId filter, NavigationAreaId area) const;

    private:
        NavigationAreaRegistry(std::vector<NavigationAreaDescriptor> areas, std::vector<NavigationQueryFilterDescriptor> filters);

        std::vector<NavigationAreaDescriptor> areas_;
        std::vector<NavigationQueryFilterDescriptor> filters_;
    };
}  // namespace Horo::Navigation
