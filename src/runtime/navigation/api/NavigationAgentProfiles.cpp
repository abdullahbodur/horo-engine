#include "Horo/Navigation/NavigationAgentProfiles.h"

#include "Horo/Navigation/NavigationErrors.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <new>
#include <utility>

namespace Horo::Navigation {
    namespace {
        /** @brief Reports whether a positive geometry measure is finite. */
        bool IsPositiveFinite(const float value) noexcept {
            return std::isfinite(value) && value > 0.0F;
        }

        /** @brief Reports whether a non-negative geometry measure is finite. */
        bool IsNonNegativeFinite(const float value) noexcept {
            return std::isfinite(value) && value >= 0.0F;
        }

        /** @brief Keeps presentation validation locale-independent while rejecting absent names. */
        bool HasDisplayName(const std::string &displayName) noexcept {
            return std::ranges::any_of(displayName, [](const unsigned char character) {
                return !std::isspace(character);
            });
        }

        /** @brief Validates scalar domains and grounded-agent relationships independently of runtime movement settings. */
        bool IsValid(const NavigationAgentBuildGeometry &geometry) noexcept {
            const std::array positive{geometry.radiusMeters, geometry.heightMeters, geometry.cellSizeMeters, geometry.cellHeightMeters};
            const std::array nonNegative{geometry.maxSlopeDegrees, geometry.stepHeightMeters, geometry.minimumRegionSizeMeters};
            return std::ranges::all_of(positive, IsPositiveFinite) && std::ranges::all_of(nonNegative, IsNonNegativeFinite) &&
                   geometry.maxSlopeDegrees < 90.0F && geometry.stepHeightMeters < geometry.heightMeters;
        }
    }  // namespace

    /** @copydoc ValidateNavigationAgentProfile */
    Result<void> ValidateNavigationAgentProfile(const NavigationAgentProfileDescriptor &profile) {
        if (!profile.id.IsValid() || !HasDisplayName(profile.displayName) || !IsValid(profile.buildGeometry))
            return Result<void>::Failure(MakeError(NavigationErrors::AgentProfileInvalid));
        return Result<void>::Success();
    }

    /** @copydoc RenameNavigationAgentProfile */
    Result<NavigationAgentProfileDescriptor> RenameNavigationAgentProfile(const NavigationAgentProfileDescriptor &profile,
                                                                          std::string displayName) {
        if (const auto valid = ValidateNavigationAgentProfile(profile); valid.HasError())
            return Result<NavigationAgentProfileDescriptor>::Failure(valid.ErrorValue());
        NavigationAgentProfileDescriptor renamed = profile;
        renamed.displayName = std::move(displayName);
        if (const auto valid = ValidateNavigationAgentProfile(renamed); valid.HasError())
            return Result<NavigationAgentProfileDescriptor>::Failure(valid.ErrorValue());
        return Result<NavigationAgentProfileDescriptor>::Success(std::move(renamed));
    }

    /** @copydoc DuplicateNavigationAgentProfile */
    Result<NavigationAgentProfileDescriptor> DuplicateNavigationAgentProfile(const NavigationAgentProfileDescriptor &profile,
                                                                             const NavigationAgentProfileId duplicateId,
                                                                             std::string displayName) {
        if (const auto valid = ValidateNavigationAgentProfile(profile); valid.HasError())
            return Result<NavigationAgentProfileDescriptor>::Failure(valid.ErrorValue());
        if (!duplicateId.IsValid() || duplicateId == profile.id)
            return Result<NavigationAgentProfileDescriptor>::Failure(MakeError(NavigationErrors::AgentProfileInvalid));
        NavigationAgentProfileDescriptor duplicate{.id = duplicateId,
                                                   .displayName = std::move(displayName),
                                                   .buildGeometry = profile.buildGeometry};
        if (const auto valid = ValidateNavigationAgentProfile(duplicate); valid.HasError())
            return Result<NavigationAgentProfileDescriptor>::Failure(valid.ErrorValue());
        return Result<NavigationAgentProfileDescriptor>::Success(std::move(duplicate));
    }

    /** @copydoc RepairNavigationAgentProfileReferences */
    Result<NavigationProfileReferenceRepair> RepairNavigationAgentProfileReferences(
        const std::span<const NavigationAgentProfileId> references, const NavigationAgentProfileId removed,
        const std::optional<NavigationAgentProfileId> replacement) {
        if (!removed.IsValid() || (replacement.has_value() && (!replacement->IsValid() || *replacement == removed)))
            return Result<NavigationProfileReferenceRepair>::Failure(MakeError(NavigationErrors::AgentProfileInvalid));
        const auto referenced = static_cast<std::size_t>(std::ranges::count(references, removed));
        if (referenced != 0 && !replacement.has_value())
            return Result<NavigationProfileReferenceRepair>::Failure(MakeError(NavigationErrors::AgentProfileReferenced));
        try {
            NavigationProfileReferenceRepair repair{.references = {references.begin(), references.end()}, .repairedCount = referenced};
            if (replacement.has_value())
                std::ranges::replace(repair.references, removed, *replacement);
            return Result<NavigationProfileReferenceRepair>::Success(std::move(repair));
        } catch (const std::bad_alloc &) {
            return Result<NavigationProfileReferenceRepair>::Failure(MakeError(NavigationErrors::CapacityExceeded));
        }
    }
}  // namespace Horo::Navigation
