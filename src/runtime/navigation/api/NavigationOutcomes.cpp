#include "Horo/Navigation/NavigationOutcomes.h"

#include <algorithm>

namespace Horo::Navigation {
    namespace {
        /** @brief Validates one Horo-owned region-generation dependency. */
        bool IsValidDependency(const NavigationCoverageDependency &dependency) noexcept {
            return dependency.region != 0 && dependency.generation.IsValid();
        }

        /** @brief Rejects duplicate regions within or across exact coverage sets. */
        bool HasUniqueRegions(const std::span<const NavigationCoverageDependency> covered,
                              const std::span<const NavigationCoverageDependency> missing) noexcept {
            for (std::size_t index = 0; index < covered.size(); ++index) {
                if (std::ranges::any_of(covered.subspan(index + 1),
                                        [&covered, index](const auto &candidate) {
                    return candidate.region == covered[index].region;
                }) ||
                    std::ranges::any_of(missing, [&covered, index](const auto &candidate) {
                    return candidate.region == covered[index].region;
                }))
                    return false;
            }
            for (std::size_t index = 0; index < missing.size(); ++index) {
                if (std::ranges::any_of(missing.subspan(index + 1), [&missing, index](const auto &candidate) {
                    return candidate.region == missing[index].region;
                }))
                    return false;
            }
            return true;
        }
    }  // namespace
}  // namespace Horo::Navigation
