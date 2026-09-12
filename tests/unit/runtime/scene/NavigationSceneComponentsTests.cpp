#include "Horo/Runtime/Scene/NavigationSceneComponents.h"
#include "Horo/Runtime/Scene/RuntimeSceneDefinition.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace {
    using namespace Horo;

    [[nodiscard]] Assets::AssetId Asset(const std::uint8_t suffix = 1) {
        std::array<std::uint8_t, 16> bytes{};
        bytes.back() = suffix;
        return Assets::AssetId::FromBytes(bytes);
    }

    [[nodiscard]] Runtime::NavigationSurfaceComponent Surface(const std::uint64_t id = 1) {
        return {
            .id = Navigation::SurfaceId::Create(id).Value(),
            .definition = Asset(),
            .profiles = {Navigation::NavigationAgentProfileId::Create(7).Value()},
        };
    }

    [[nodiscard]] Runtime::NavigationRegionComponent Region(const std::uint64_t id = 2, const std::uint64_t surface = 1) {
        return {
            .id = Navigation::NavigationRegionId::Create(id).Value(),
            .surface = Navigation::SurfaceId::Create(surface).Value(),
        };
    }

    TEST_CASE("Navigation Scene components validate bounded typed payloads", "[unit][navigation][scene]") {
        auto surface = Surface();
        REQUIRE(Runtime::ValidateNavigationSurfaceComponent(surface).HasValue());

        surface.profiles.push_back(surface.profiles.front());
        REQUIRE(Runtime::ValidateNavigationSurfaceComponent(surface).HasError());
        surface = Surface();
        surface.bakeScope = Runtime::NavigationBakeScope::LocalBounds;
        REQUIRE(Runtime::ValidateNavigationSurfaceComponent(surface).HasError());
        surface.localBounds = Runtime::NavigationLocalBounds{};
        REQUIRE(Runtime::ValidateNavigationSurfaceComponent(surface).HasValue());
        surface.localBounds->halfExtents.x = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(Runtime::ValidateNavigationSurfaceComponent(surface).HasError());

        auto region = Region();
        REQUIRE(Runtime::ValidateNavigationRegionComponent(region).HasValue());
        region.localBounds.halfExtents.z = 0.0F;
        REQUIRE(Runtime::ValidateNavigationRegionComponent(region).HasError());
    }

    TEST_CASE("Navigation Scene identity validation rejects conflicts and missing surfaces", "[unit][navigation][scene]") {
        const std::array surfaces{Surface()};
        const std::array regions{Region()};
        REQUIRE(Runtime::ValidateNavigationSceneComponents(surfaces, regions).HasValue());

        const std::array duplicateSurfaces{Surface(), Surface()};
        REQUIRE(Runtime::ValidateNavigationSceneComponents(duplicateSurfaces, regions).HasError());
        const std::array duplicateRegions{Region(), Region()};
        REQUIRE(Runtime::ValidateNavigationSceneComponents(surfaces, duplicateRegions).HasError());
        const std::array missingRegions{Region(2, 99)};
        REQUIRE(Runtime::ValidateNavigationSceneComponents(surfaces, missingRegions).HasError());
    }

    TEST_CASE("Runtime definition pins committed navigation generations and rejects missing references", "[unit][navigation][scene]") {
        Runtime::SceneDefinitionBuilder valid{Runtime::SceneDefinitionId{3}, Runtime::SceneDefinitionRevision{11}};
        valid.Add({.object = Runtime::SceneObjectId{1}, .components = {.navigationSurface = Surface()}});
        valid.Add({.object = Runtime::SceneObjectId{2}, .components = {.navigationRegion = Region()}});
        auto definition = std::move(valid).Build();
        REQUIRE(definition.HasValue());
        REQUIRE(definition.Value().Revision().value == 11);
        REQUIRE(definition.Value().Entities()[0].components.navigationSurface->generation == 1);

        Runtime::SceneDefinitionBuilder missing{Runtime::SceneDefinitionId{3}, Runtime::SceneDefinitionRevision{12}};
        missing.Add({.object = Runtime::SceneObjectId{2}, .components = {.navigationRegion = Region()}});
        REQUIRE(std::move(missing).Build().HasError());
    }
}  // namespace
