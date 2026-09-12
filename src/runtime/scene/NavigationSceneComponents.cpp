#include "Horo/Runtime/Scene/NavigationSceneComponents.h"

#include "Horo/Navigation/NavigationErrors.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>

namespace Horo::Runtime {
    namespace {
        [[nodiscard]] Result<void> Failure(const ErrorCodeDescriptor &descriptor, std::string message) {
            return Result<void>::Failure(MakeError(descriptor, std::move(message)));
        }

        [[nodiscard]] bool IsValid(const NavigationLocalBounds &bounds) noexcept {
            return Math::IsFinite(bounds.center) && Math::IsFinite(bounds.halfExtents) && bounds.halfExtents.x > 0.0F &&
                   bounds.halfExtents.y > 0.0F && bounds.halfExtents.z > 0.0F;
        }
    }  // namespace

    /** @copydoc ValidateNavigationSurfaceComponent */
    Result<void> ValidateNavigationSurfaceComponent(const NavigationSurfaceComponent &component) {
        if (!component.id.IsValid() || !component.definition.IsValid() || component.schemaVersion != 1 || component.generation == 0 ||
            component.bakeScope >= NavigationBakeScope::Count || component.profiles.empty() ||
            component.profiles.size() > MaximumNavigationSurfaceProfiles) {
            return Failure(Navigation::NavigationErrors::SceneComponentInvalid,
                           "Navigation surfaces require valid identities, schema, generation, definition, scope, and bounded profiles.");
        }
        if ((component.bakeScope == NavigationBakeScope::LocalBounds) != component.localBounds.has_value() ||
            (component.localBounds.has_value() && !IsValid(*component.localBounds))) {
            return Failure(Navigation::NavigationErrors::SceneComponentInvalid,
                           "Navigation surface bounds must be finite, positive, and present only for a local-bounds scope.");
        }
        std::vector<std::uint64_t> profileIds;
        profileIds.reserve(component.profiles.size());
        for (const Navigation::NavigationAgentProfileId profile : component.profiles) {
            if (!profile.IsValid() || std::ranges::find(profileIds, profile.Value()) != profileIds.end()) {
                return Failure(Navigation::NavigationErrors::SceneComponentInvalid,
                               "Navigation surface profile identities must be non-zero and unique.");
            }
            profileIds.push_back(profile.Value());
        }
        return Result<void>::Success();
    }

    /** @copydoc ValidateNavigationRegionComponent */
    Result<void> ValidateNavigationRegionComponent(const NavigationRegionComponent &component) {
        if (!component.id.IsValid() || !component.surface.IsValid() || component.schemaVersion != 1 || component.generation == 0 ||
            component.sourceSelection >= NavigationRegionSourceSelection::Count || component.mode >= NavigationRegionMode::Count ||
            !IsValid(component.localBounds)) {
            return Failure(Navigation::NavigationErrors::SceneComponentInvalid,
                           "Navigation regions require valid identities, schema, generation, bounds, source selection, and mode.");
        }
        return Result<void>::Success();
    }

    /** @copydoc ValidateNavigationSceneComponents */
    Result<void> ValidateNavigationSceneComponents(const std::span<const NavigationSurfaceComponent> surfaces,
                                                   const std::span<const NavigationRegionComponent> regions) {
        std::unordered_set<std::uint64_t> surfaceIds;
        surfaceIds.reserve(surfaces.size());
        for (const NavigationSurfaceComponent &surface : surfaces) {
            if (Result<void> valid = ValidateNavigationSurfaceComponent(surface); valid.HasError())
                return valid;
            if (!surfaceIds.insert(surface.id.Value()).second) {
                return Failure(Navigation::NavigationErrors::SceneComponentConflict,
                               "Navigation surface identities must be unique within one committed Scene snapshot.");
            }
        }

        std::unordered_set<std::uint64_t> regionIds;
        regionIds.reserve(regions.size());
        for (const NavigationRegionComponent &region : regions) {
            if (Result<void> valid = ValidateNavigationRegionComponent(region); valid.HasError())
                return valid;
            if (!regionIds.insert(region.id.Value()).second) {
                return Failure(Navigation::NavigationErrors::SceneComponentConflict,
                               "Navigation region identities must be unique within one committed Scene snapshot.");
            }
            if (!surfaceIds.contains(region.surface.Value())) {
                return Failure(Navigation::NavigationErrors::SceneSurfaceMissing,
                               "Navigation regions must reference an exact surface in the same committed Scene snapshot.");
            }
        }
        return Result<void>::Success();
    }
}  // namespace Horo::Runtime
