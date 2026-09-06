#include "Horo/Navigation/NavigationAreas.h"

#include "Horo/Navigation/NavigationErrors.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace Horo::Navigation {
    namespace {
        /** @brief Reports whether descriptor provenance is a known kind with a stable non-zero identity. */
        bool IsValid(const NavigationDescriptorSource &source) noexcept {
            return source.kind < NavigationDescriptorSourceKind::Count && source.id.IsValid();
        }

        /** @brief Traversal costs are finite and non-negative; zero-cost policies remain explicit and valid. */
        bool IsValidCost(const float cost) noexcept {
            return std::isfinite(cost) && cost >= 0.0F;
        }

        /** @brief Sorts an identity-bearing descriptor sequence and rejects every duplicate identity. */
        template <typename Descriptor, typename Identity>
        bool SortUnique(std::vector<Descriptor> &descriptors, Identity Descriptor::*identity) {
            std::ranges::sort(descriptors, [identity](const Descriptor &left, const Descriptor &right) {
                return (left.*identity).Value() < (right.*identity).Value();
            });
            return std::ranges::adjacent_find(descriptors, [identity](const Descriptor &left, const Descriptor &right) {
                return left.*identity == right.*identity;
            }) == descriptors.end();
        }

        /** @brief Finds an identity in a sorted descriptor sequence without fallback. */
        template <typename Descriptor, typename Identity>
        const Descriptor *Find(std::span<const Descriptor> descriptors, const Identity id, Identity Descriptor::*identity) {
            const auto found = std::ranges::lower_bound(descriptors, id.Value(), {}, [identity](const Descriptor &descriptor) {
                return (descriptor.*identity).Value();
            });
            return found != descriptors.end() && (*found).*identity == id ? &*found : nullptr;
        }

        /** @brief Validates filter provenance, identity, costs, and exact registered area references. */
        Result<void> ValidateFilter(NavigationQueryFilterDescriptor &filter, std::span<const NavigationAreaDescriptor> areas) {
            if (!filter.id.IsValid() || !IsValid(filter.source))
                return Result<void>::Failure(MakeError(NavigationErrors::FilterDescriptorInvalid));
            if (!SortUnique(filter.costOverrides, &NavigationAreaCostOverride::area))
                return Result<void>::Failure(MakeError(NavigationErrors::DescriptorConflict));
            for (const NavigationAreaCostOverride &override : filter.costOverrides) {
                if (!override.area.IsValid() || !IsValidCost(override.traversalCost))
                    return Result<void>::Failure(MakeError(NavigationErrors::FilterDescriptorInvalid));
                if (Find(areas, override.area, &NavigationAreaDescriptor::id) == nullptr)
                    return Result<void>::Failure(MakeError(NavigationErrors::AreaUnknown));
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc NavigationAreaRegistry::Create */
    Result<NavigationAreaRegistry> NavigationAreaRegistry::Create(const std::span<const NavigationAreaDescriptor> areas,
                                                                  const std::span<const NavigationQueryFilterDescriptor> filters) {
        try {
            std::vector<NavigationAreaDescriptor> ownedAreas{areas.begin(), areas.end()};
            std::vector<NavigationQueryFilterDescriptor> ownedFilters{filters.begin(), filters.end()};

            for (const NavigationAreaDescriptor &area : ownedAreas) {
                if (!area.id.IsValid() || !IsValid(area.source) || !IsValidCost(area.traversalCost))
                    return Result<NavigationAreaRegistry>::Failure(MakeError(NavigationErrors::AreaDescriptorInvalid));
            }
            if (!SortUnique(ownedAreas, &NavigationAreaDescriptor::id) || !SortUnique(ownedFilters, &NavigationQueryFilterDescriptor::id))
                return Result<NavigationAreaRegistry>::Failure(MakeError(NavigationErrors::DescriptorConflict));

            for (NavigationQueryFilterDescriptor &filter : ownedFilters) {
                if (const auto valid = ValidateFilter(filter, ownedAreas); valid.HasError())
                    return Result<NavigationAreaRegistry>::Failure(valid.ErrorValue());
            }
            return Result<NavigationAreaRegistry>::Success(NavigationAreaRegistry{std::move(ownedAreas), std::move(ownedFilters)});
        } catch (const std::bad_alloc &) {
            return Result<NavigationAreaRegistry>::Failure(MakeError(NavigationErrors::CapacityExceeded));
        }
    }

    /** @copydoc NavigationAreaRegistry::Areas */
    std::span<const NavigationAreaDescriptor> NavigationAreaRegistry::Areas() const noexcept {
        return areas_;
    }

    /** @copydoc NavigationAreaRegistry::Filters */
    std::span<const NavigationQueryFilterDescriptor> NavigationAreaRegistry::Filters() const noexcept {
        return filters_;
    }

    /** @copydoc NavigationAreaRegistry::ResolveArea */
    Result<NavigationAreaDescriptor> NavigationAreaRegistry::ResolveArea(const NavigationAreaId id) const {
        const auto found = Find(std::span{areas_}, id, &NavigationAreaDescriptor::id);
        if (found == nullptr)
            return Result<NavigationAreaDescriptor>::Failure(MakeError(NavigationErrors::AreaUnknown));
        return Result<NavigationAreaDescriptor>::Success(*found);
    }

    /** @copydoc NavigationAreaRegistry::ResolveFilter */
    Result<NavigationQueryFilterDescriptor> NavigationAreaRegistry::ResolveFilter(const NavigationFilterId id) const {
        const auto found = Find(std::span{filters_}, id, &NavigationQueryFilterDescriptor::id);
        if (found == nullptr)
            return Result<NavigationQueryFilterDescriptor>::Failure(MakeError(NavigationErrors::FilterUnknown));
        return Result<NavigationQueryFilterDescriptor>::Success(*found);
    }

    /** @copydoc NavigationAreaRegistry::ResolveTraversal */
    Result<NavigationTraversalPolicy> NavigationAreaRegistry::ResolveTraversal(const NavigationFilterId filter,
                                                                               const NavigationAreaId area) const {
        const auto foundArea = Find(std::span{areas_}, area, &NavigationAreaDescriptor::id);
        if (foundArea == nullptr)
            return Result<NavigationTraversalPolicy>::Failure(MakeError(NavigationErrors::AreaUnknown));
        const auto foundFilter = Find(std::span{filters_}, filter, &NavigationQueryFilterDescriptor::id);
        if (foundFilter == nullptr)
            return Result<NavigationTraversalPolicy>::Failure(MakeError(NavigationErrors::FilterUnknown));

        const bool included = foundFilter->includedFlags.Empty() || foundArea->flags.Intersects(foundFilter->includedFlags);
        const bool excluded = foundArea->flags.Intersects(foundFilter->excludedFlags);
        float traversalCost = foundArea->traversalCost;
        if (const auto override = Find(std::span{foundFilter->costOverrides}, area, &NavigationAreaCostOverride::area); override != nullptr)
            traversalCost = override->traversalCost;
        return Result<NavigationTraversalPolicy>::Success({.traversable = included && !excluded, .traversalCost = traversalCost});
    }

    /** @brief Owns already validated and identity-sorted descriptors. */
    NavigationAreaRegistry::NavigationAreaRegistry(std::vector<NavigationAreaDescriptor> areas,
                                                   std::vector<NavigationQueryFilterDescriptor> filters)
        : areas_(std::move(areas)), filters_(std::move(filters)) {}
}  // namespace Horo::Navigation
