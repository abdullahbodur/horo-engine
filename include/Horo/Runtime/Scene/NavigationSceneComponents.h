#pragma once

/**
 * @file NavigationSceneComponents.h
 * @brief Typed authored navigation surface and bounded region Scene components.
 */

#include "Horo/Assets/AssetId.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Math/SceneMath.h"
#include "Horo/Navigation/NavigationAgentProfiles.h"
#include "Horo/Navigation/NavigationIdentity.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Horo::Runtime {
    /** @brief Maximum grounded profiles selected by one authored navigation surface. */
    inline constexpr std::size_t MaximumNavigationSurfaceProfiles = 64;

    /** @brief Closed authored scope choices for one navigation surface. */
    enum class NavigationBakeScope : std::uint8_t {
        ObjectSubtree,
        LocalBounds,
        Count,
    };

    /** @brief Closed source-selection policies for one bounded navigation region. */
    enum class NavigationRegionSourceSelection : std::uint8_t {
        ExplicitContributors,
        StaticCollisionInBounds,
        Count,
    };

    /** @brief Semantic contribution of one bounded region to its referenced surface. */
    enum class NavigationRegionMode : std::uint8_t {
        Include,
        Exclude,
        Count,
    };

    /** @brief Finite positive object-local axis-aligned bounds. */
    struct NavigationLocalBounds final {
        Math::Vec3 center{};
        Math::Vec3 halfExtents{1.0F, 1.0F, 1.0F};

        [[nodiscard]] constexpr bool operator==(const NavigationLocalBounds &) const noexcept = default;
    };

    /**
     * @brief Stable authored navigation surface intent owned by one committed Scene object.
     * @details The definition and profile references are durable authoring identities. No runtime or provider handle is serialized.
     */
    struct NavigationSurfaceComponent final {
        Navigation::SurfaceId id;
        Assets::AssetId definition;
        std::uint32_t schemaVersion{1};
        std::uint64_t generation{1};
        NavigationBakeScope bakeScope{NavigationBakeScope::ObjectSubtree};
        std::optional<NavigationLocalBounds> localBounds;
        std::vector<Navigation::NavigationAgentProfileId> profiles;
        bool enabled{true};

        [[nodiscard]] bool operator==(const NavigationSurfaceComponent &) const noexcept = default;
    };

    /** @brief Stable authored bounded region that includes or excludes sources for one exact surface. */
    struct NavigationRegionComponent final {
        Navigation::NavigationRegionId id;
        Navigation::SurfaceId surface;
        std::uint32_t schemaVersion{1};
        std::uint64_t generation{1};
        NavigationLocalBounds localBounds;
        NavigationRegionSourceSelection sourceSelection{NavigationRegionSourceSelection::ExplicitContributors};
        NavigationRegionMode mode{NavigationRegionMode::Include};
        bool enabled{true};

        [[nodiscard]] constexpr bool operator==(const NavigationRegionComponent &) const noexcept = default;
    };

    /** @brief Borrowed Scene-object projection used to validate navigation components without copying payload storage. */
    struct NavigationSceneComponentView final {
        const NavigationSurfaceComponent *surface{}; /**< Optional surface owned by the immutable source snapshot. */
        const NavigationRegionComponent *region{};   /**< Optional region owned by the immutable source snapshot. */
    };

    /** @brief Validates one surface payload independently of Scene-wide identity references.
     * @param component Authored component value.
     * @return Success or NavigationErrors::SceneComponentInvalid.
     */
    [[nodiscard]] Result<void> ValidateNavigationSurfaceComponent(const NavigationSurfaceComponent &component);

    /** @brief Validates one region payload independently of its referenced surface's presence.
     * @param component Authored component value.
     * @return Success or NavigationErrors::SceneComponentInvalid.
     */
    [[nodiscard]] Result<void> ValidateNavigationRegionComponent(const NavigationRegionComponent &component);

    /**
     * @brief Validates unique identities and exact region-to-surface references in one committed Scene snapshot.
     * @param surfaces Surface components in arbitrary Scene-object order.
     * @param regions Region components in arbitrary Scene-object order.
     * @return Success, or a typed invalid, conflict, or missing-surface diagnostic.
     */
    [[nodiscard]] Result<void> ValidateNavigationSceneComponents(std::span<const NavigationSurfaceComponent> surfaces,
                                                                 std::span<const NavigationRegionComponent> regions);

    /**
     * @brief Validates unique identities and references through borrowed component projections without payload copies.
     * @param components Views whose pointers remain valid for the duration of this call.
     * @return Success, or a typed invalid, conflict, or missing-surface diagnostic.
     */
    [[nodiscard]] Result<void> ValidateNavigationSceneComponentViews(std::span<const NavigationSceneComponentView> components);
}  // namespace Horo::Runtime
