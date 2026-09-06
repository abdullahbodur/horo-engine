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
    }  // namespace
}  // namespace Horo::Navigation
