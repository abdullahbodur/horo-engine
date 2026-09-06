#include "Horo/Navigation/NavigationCapabilities.h"

#include "Horo/Navigation/NavigationErrors.h"

#include <algorithm>
#include <cmath>

namespace Horo::Navigation {
    namespace {
        /** @brief Checks one closed enum without accepting its Count sentinel. */
        template <typename Enum> bool IsKnown(const Enum value, const Enum count) noexcept {
            return value < count;
        }

        /** @brief Checks support evidence against the composition-level availability fact. */
        bool IsCoherentSupport(const NavigationSupport support, const NavigationProviderAvailability availability) noexcept {
            if (!IsKnown(support, NavigationSupport::Count))
                return false;
            if (availability == NavigationProviderAvailability::Omitted)
                return support == NavigationSupport::Unsupported;
            return availability == NavigationProviderAvailability::Available || support != NavigationSupport::Available;
        }

        /** @brief Reports whether a query limit declaration is exactly the unused zero representation. */
        bool IsZero(const NavigationQueryLimits &limits) noexcept {
            return limits.maximumNodeExpansions == 0 && limits.maximumResultPoints == 0 && limits.maximumSearchDistanceMeters == 0.0F;
        }

        /** @brief Validates positive integral bounds and a positive finite distance without inventing product policy. */
        bool ValidateLimits(const NavigationQueryLimits &limits) noexcept {
            return limits.maximumNodeExpansions > 0 && limits.maximumResultPoints > 0 &&
                   std::isfinite(limits.maximumSearchDistanceMeters) && limits.maximumSearchDistanceMeters > 0.0F;
        }

        /** @brief Combines exact query evidence without hiding available or temporarily unavailable support. */
        NavigationSupport CombineQuerySupport(const NavigationSupport aggregate, const NavigationSupport support) noexcept {
            if (aggregate == NavigationSupport::Available || support == NavigationSupport::Available)
                return NavigationSupport::Available;
            if (aggregate == NavigationSupport::Unavailable || support == NavigationSupport::Unavailable)
                return NavigationSupport::Unavailable;
            if (aggregate == NavigationSupport::Unknown || support == NavigationSupport::Unknown)
                return NavigationSupport::Unknown;
            return NavigationSupport::Unsupported;
        }

        /** @brief Validates all query-quality support and limit pairs and derives their single aggregate support state. */
        bool ValidateQueries(const NavigationProviderCapabilities &capabilities, NavigationSupport &aggregate) noexcept {
            aggregate = NavigationSupport::Unsupported;
            for (std::size_t query = 0; query < capabilities.querySupport.size(); ++query) {
                for (std::size_t quality = 0; quality < capabilities.querySupport[query].size(); ++quality) {
                    const auto support = capabilities.querySupport[query][quality];
                    if (!IsCoherentSupport(support, capabilities.availability))
                        return false;
                    const bool available = support == NavigationSupport::Available;
                    if (available ? !ValidateLimits(capabilities.queryLimits[query][quality])
                                  : !IsZero(capabilities.queryLimits[query][quality]))
                        return false;
                    aggregate = CombineQuerySupport(aggregate, support);
                }
            }
            return true;
        }

        /** @brief Checks whether the requested limits fit the exact supported query-quality declaration. */
        bool Fits(const NavigationQueryLimits &requested, const NavigationQueryLimits &available) noexcept {
            return requested.maximumNodeExpansions <= available.maximumNodeExpansions &&
                   requested.maximumResultPoints <= available.maximumResultPoints &&
                   requested.maximumSearchDistanceMeters <= available.maximumSearchDistanceMeters;
        }
    }  // namespace
}  // namespace Horo::Navigation
