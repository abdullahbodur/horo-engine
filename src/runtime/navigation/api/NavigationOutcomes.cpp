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

        /** @brief Validates bounded exact coverage inputs shared by complete and partial factories. */
        bool ValidateExactCoverage(const std::span<const NavigationCoverageDependency> covered,
                                   const std::span<const NavigationCoverageDependency> missing) noexcept {
            if (covered.size() > MaximumNavigationOutcomeCoverageDependencies ||
                missing.size() > MaximumNavigationOutcomeCoverageDependencies - covered.size())
                return false;
            if (!std::ranges::all_of(covered, IsValidDependency) || !std::ranges::all_of(missing, IsValidDependency))
                return false;
            return HasUniqueRegions(covered, missing);
        }

        /** @brief Copies a bounded dependency view into fixed outcome-owned storage. */
        void CopyDependencies(const std::span<const NavigationCoverageDependency> source,
                              std::array<NavigationCoverageDependency, MaximumNavigationOutcomeCoverageDependencies> &destination) {
            std::ranges::copy(source, destination.begin());
        }
    }  // namespace

    /** @copydoc ValidateNavigationOutcomeProvenance */
    bool ValidateNavigationOutcomeProvenance(const NavigationOutcomeProvenance &provenance) noexcept {
        return provenance.snapshot.IsValid() && provenance.world.IsValid() && provenance.topology.IsValid() &&
               provenance.obstacleRevision != 0 && provenance.filterRevision != 0 && provenance.profileRevision != 0 &&
               provenance.originRevision != 0;
    }
}  // namespace Horo::Navigation
