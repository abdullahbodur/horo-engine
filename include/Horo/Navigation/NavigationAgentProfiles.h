#pragma once

/**
 * @file NavigationAgentProfiles.h
 * @brief Stable grounded-agent profiles and their bake-only geometry contract.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Navigation/NavigationIdentity.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Horo::Navigation {
    struct NavigationAgentProfileIdentityTag;

    /** @brief Stable project-owned profile identity, independent of display name and registry order. */
    using NavigationAgentProfileId = NavigationIdentity<NavigationAgentProfileIdentityTag>;

    /** @brief Grounded-agent dimensions and voxel resolution used only to build navigation geometry. */
    struct NavigationAgentBuildGeometry final {
        float radiusMeters{0.5F};
        float heightMeters{1.8F};
        float maxSlopeDegrees{45.0F};
        float stepHeightMeters{0.4F};
        float cellSizeMeters{0.3F};
        float cellHeightMeters{0.2F};
        float minimumRegionSizeMeters{0.0F};
    };

    /** @brief One authored profile whose presentation may change without changing its identity. */
    struct NavigationAgentProfileDescriptor final {
        NavigationAgentProfileId id;
        std::string displayName;
        NavigationAgentBuildGeometry buildGeometry;
    };

    /** @brief Result of an explicit profile-removal reference-repair operation. */
    struct NavigationProfileReferenceRepair final {
        std::vector<NavigationAgentProfileId> references;
        std::size_t repairedCount{};
    };

    /**
     * @brief Validates one grounded-agent profile before bake admission.
     * @param profile Authored profile to validate.
     * @return Success only for a non-zero identity, non-empty display name, and valid finite geometry values.
     */
    [[nodiscard]] Result<void> ValidateNavigationAgentProfile(const NavigationAgentProfileDescriptor &profile);

    /**
     * @brief Produces a renamed profile without changing its stable identity or build geometry.
     * @param profile Existing valid profile.
     * @param displayName New non-empty presentation name.
     * @return Renamed descriptor or NavigationErrors::AgentProfileInvalid.
     */
    [[nodiscard]] Result<NavigationAgentProfileDescriptor> RenameNavigationAgentProfile(const NavigationAgentProfileDescriptor &profile,
                                                                                        std::string displayName);

    /**
     * @brief Duplicates profile settings under a caller-issued distinct identity.
     * @param profile Existing valid profile.
     * @param duplicateId New non-zero identity, distinct from the source profile.
     * @param displayName New non-empty presentation name.
     * @return Independent descriptor or NavigationErrors::AgentProfileInvalid.
     */
    [[nodiscard]] Result<NavigationAgentProfileDescriptor> DuplicateNavigationAgentProfile(const NavigationAgentProfileDescriptor &profile,
                                                                                           NavigationAgentProfileId duplicateId,
                                                                                           std::string displayName);

    /**
     * @brief Repairs every exact reference before a profile is deleted.
     * @param references Stable profile references in their original order.
     * @param removed Exact identity being deleted.
     * @param replacement Explicit surviving identity, or no value to require that the profile is unreferenced.
     * @return Owned repaired references, or NavigationErrors::AgentProfileReferenced when deletion would dangle references.
     */
    [[nodiscard]] Result<NavigationProfileReferenceRepair> RepairNavigationAgentProfileReferences(
        std::span<const NavigationAgentProfileId> references, NavigationAgentProfileId removed,
        std::optional<NavigationAgentProfileId> replacement = std::nullopt);
}  // namespace Horo::Navigation
