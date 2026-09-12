#include "Horo/Navigation/NavigationBakeInput.h"
#include "Horo/Navigation/NavigationErrors.h"
#include "navigation/NavigationTestAssertions.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::Navigation {
    static_assert(!std::is_copy_constructible_v<NavigationBakeInputSnapshot>);
    static_assert(!std::is_copy_assignable_v<NavigationBakeInputSnapshot>);
    static_assert(std::is_nothrow_move_constructible_v<NavigationBakeInputSnapshot>);
    static_assert(!std::is_move_assignable_v<NavigationBakeInputSnapshot>);

    namespace {
        using TestSupport::Digest;
        using TestSupport::Id;
        using TestSupport::RequireError;

        [[nodiscard]] NavigationBakeInputRevisions Revisions() {
            return {.requestGeneration = Id<NavigationBakeRequestGeneration>(1),
                    .definition = Id<NavigationDefinitionRevision>(2),
                    .scene = Id<NavigationSceneDocumentRevision>(3),
                    .areaRegistry = Id<NavigationAreaRegistryRevision>(4),
                    .projectProfile = Id<NavigationProjectProfileRevision>(5),
                    .coordinates = Id<NavigationCoordinatePolicyRevision>(6),
                    .geometry = Id<NavigationSourceSnapshotRevision>(7)};
        }

        struct OwnedContribution final : TestSupport::OwnedTriangleContribution {
            [[nodiscard]] NavigationSourceContributionInput View() const {
                return {.producer = producer,
                        .contribution = contribution,
                        .revision = revision,
                        .contentDigest = digest,
                        .vertices = vertices,
                        .triangles = triangles};
            }

            [[nodiscard]] NavigationSourceObservation Observation() const {
                return {.producer = producer, .contribution = contribution, .revision = revision, .contentDigest = digest};
            }
        };

        struct Fixture final {
            OwnedContribution low;
            OwnedContribution high;
            std::array<NavigationAgentProfileDescriptor, 2> profiles{NavigationAgentProfileDescriptor{.id = Id<NavigationAgentProfileId>(2),
                                                                                                      .displayName = "Large"},
                                                                     NavigationAgentProfileDescriptor{.id = Id<NavigationAgentProfileId>(1),
                                                                                                      .displayName = "Human"}};
            std::array<NavigationBakeSurfaceInput, 2> surfaces{};
            std::array<NavigationBakeModifierInput, 2> modifiers{};

            Fixture() {
                high.producer = Id<NavigationSourceProducerId>(2);
                high.contribution = Id<NavigationSourceContributionId>(2);
                high.revision = Id<NavigationSourceRevision>(2);
                high.digest = Digest(2);
                high.vertices[1].x = 2.0F;
                high.triangles.push_back({.vertexIndices = {0, 1, 2}, .area = Id<NavigationAreaId>(2), .materialSlot = {.value = 8}});
                surfaces = {NavigationBakeSurfaceInput{.surface = Id<SurfaceId>(20),
                                                       .profile = Id<NavigationAgentProfileId>(2),
                                                       .filter = Id<NavigationFilterId>(1),
                                                       .producer = high.producer,
                                                       .contribution = high.contribution},
                            NavigationBakeSurfaceInput{.surface = Id<SurfaceId>(10),
                                                       .profile = Id<NavigationAgentProfileId>(1),
                                                       .filter = Id<NavigationFilterId>(1),
                                                       .producer = low.producer,
                                                       .contribution = low.contribution}};
                modifiers = {NavigationBakeModifierInput{.id = Id<NavigationModifierId>(2),
                                                         .surface = Id<SurfaceId>(20),
                                                         .profile = Id<NavigationAgentProfileId>(2),
                                                         .area = Id<NavigationAreaId>(2),
                                                         .localBounds = {.minimum = {0.0F, 0.0F, 0.0F}, .maximum = {1.0F, 2.0F, 1.0F}},
                                                         .localToCanonicalMeters = {.translation = {2.0F, 0.0F, 0.0F}}},
                             NavigationBakeModifierInput{.id = Id<NavigationModifierId>(1),
                                                         .surface = Id<SurfaceId>(10),
                                                         .profile = Id<NavigationAgentProfileId>(1),
                                                         .mode = NavigationBakeModifierMode::Exclude,
                                                         .area = Id<NavigationAreaId>(2),
                                                         .localBounds = {.minimum = {-1.0F, 0.0F, -1.0F}, .maximum = {1.0F, 1.0F, 1.0F}}}};
            }

            [[nodiscard]] NavigationAreaRegistry Registry() const {
                const std::array areas{NavigationAreaDescriptor{.id = Id<NavigationAreaId>(2),
                                                                .source = {.id = Id<NavigationDescriptorSourceId>(1)},
                                                                .traversalCost = 3.0F,
                                                                .flags = {.bits = 2}},
                                       NavigationAreaDescriptor{.id = Id<NavigationAreaId>(1),
                                                                .source = {.id = Id<NavigationDescriptorSourceId>(1)},
                                                                .traversalCost = 1.5F,
                                                                .flags = {.bits = 1}}};
                const std::array filters{NavigationQueryFilterDescriptor{.id = Id<NavigationFilterId>(1),
                                                                         .source = {.id = Id<NavigationDescriptorSourceId>(1)},
                                                                         .includedFlags = {.bits = 1}}};
                return std::move(NavigationAreaRegistry::Create(areas, filters)).Value();
            }

            [[nodiscard]] NavigationSourceGeometrySnapshot Geometry(const bool reverse = false) const {
                const std::array inputs = reverse ? std::array{high.View(), low.View()} : std::array{low.View(), high.View()};
                return std::move(NavigationSourceGeometrySnapshot::Create(Revisions().geometry, inputs)).Value();
            }

            [[nodiscard]] std::array<NavigationSourceObservation, 2> Observations() const {
                return {high.Observation(), low.Observation()};
            }
        };

        [[nodiscard]] Result<NavigationBakeInputSnapshot> Build(const Fixture &fixture, const bool reverse = false,
                                                                const NavigationBakeInputLimits &limits = {}) {
            auto registry = fixture.Registry();
            auto geometry = fixture.Geometry(reverse);
            if (!reverse)
                return NavigationBakeInputSnapshot::Create(Revisions(), registry, fixture.profiles, fixture.surfaces, fixture.modifiers,
                                                           std::move(geometry), limits);
            const std::array profiles{fixture.profiles[1], fixture.profiles[0]};
            const std::array surfaces{fixture.surfaces[1], fixture.surfaces[0]};
            const std::array modifiers{fixture.modifiers[1], fixture.modifiers[0]};
            return NavigationBakeInputSnapshot::Create(Revisions(), registry, profiles, surfaces, modifiers, std::move(geometry), limits);
        }
    }  // namespace

    TEST_CASE("Bake input resolves canonical profiles areas surfaces modifiers and provenance", "[unit][navigation][bake_input]") {
        const Fixture fixture;
        const auto input = std::move(Build(fixture)).Value();

        REQUIRE(input.Revisions() == Revisions());
        REQUIRE(input.Profiles().size() == 2);
        REQUIRE(input.Profiles().front().id == Id<NavigationAgentProfileId>(1));
        REQUIRE(input.Areas().size() == 2);
        REQUIRE(input.Areas().front().id == Id<NavigationAreaId>(1));
        REQUIRE(input.Partitions().size() == 2);
        REQUIRE(input.Partitions().front().surface == Id<SurfaceId>(10));
        REQUIRE(input.Partitions().front().firstTriangle == 0);
        REQUIRE(input.Partitions().front().triangleCount == 1);
        REQUIRE(input.Partitions().front().firstModifier == 0);
        REQUIRE(input.Partitions().front().modifierCount == 1);
        REQUIRE(input.Triangles().size() == 2);
        REQUIRE(input.Triangles().front().traversalCost == 1.5F);
        REQUIRE(input.Triangles().front().provenance.producer == fixture.low.producer);
        REQUIRE(input.Modifiers().size() == 2);
        REQUIRE(input.Modifiers().front().id == Id<NavigationModifierId>(1));
        REQUIRE(input.Modifiers().back().canonicalBounds.minimum == Math::Vec3{2.0F, 0.0F, 0.0F});
        REQUIRE(input.Modifiers().back().canonicalBounds.maximum == Math::Vec3{3.0F, 2.0F, 1.0F});
    }

    TEST_CASE("Bake input fingerprint and canonical ranges are independent of every caller order",
              "[unit][navigation][bake_input][determinism]") {
        const Fixture fixture;
        const auto forward = std::move(Build(fixture)).Value();
        const auto reverse = std::move(Build(fixture, true)).Value();
        auto replacementRevisions = Revisions();
        replacementRevisions.requestGeneration = Id<NavigationBakeRequestGeneration>(2);
        auto registry = fixture.Registry();
        auto replacementGeometry = fixture.Geometry();
        const auto replacement =
            std::move(NavigationBakeInputSnapshot::Create(replacementRevisions, registry, fixture.profiles, fixture.surfaces,
                                                          fixture.modifiers, std::move(replacementGeometry)))
                .Value();

        REQUIRE(forward.Fingerprint() == reverse.Fingerprint());
        REQUIRE(forward.Fingerprint() == replacement.Fingerprint());
        REQUIRE(forward.Partitions().size() == reverse.Partitions().size());
        REQUIRE(forward.Triangles().size() == reverse.Triangles().size());
        REQUIRE(forward.Modifiers().size() == reverse.Modifiers().size());
        for (std::size_t index = 0; index < forward.Partitions().size(); ++index) {
            REQUIRE(forward.Partitions()[index].surface == reverse.Partitions()[index].surface);
            REQUIRE(forward.Partitions()[index].profile == reverse.Partitions()[index].profile);
            REQUIRE(forward.Partitions()[index].firstTriangle == reverse.Partitions()[index].firstTriangle);
            REQUIRE(forward.Partitions()[index].triangleCount == reverse.Partitions()[index].triangleCount);
        }
        for (std::size_t index = 0; index < forward.Triangles().size(); ++index) {
            REQUIRE(forward.Triangles()[index].vertices == reverse.Triangles()[index].vertices);
            REQUIRE(forward.Triangles()[index].provenance == reverse.Triangles()[index].provenance);
        }
    }

    TEST_CASE("Bake input rejects missing references conflicting surfaces and degenerate modifiers with context",
              "[unit][navigation][bake_input][validation]") {
        Fixture fixture;
        auto registry = fixture.Registry();

        fixture.surfaces.front().profile = Id<NavigationAgentProfileId>(99);
        auto geometry = fixture.Geometry();
        auto missingProfile = NavigationBakeInputSnapshot::Create(Revisions(), registry, fixture.profiles, fixture.surfaces,
                                                                  fixture.modifiers, std::move(geometry));
        RequireError(missingProfile, NavigationErrors::BakeInputReferenceMissing);
        REQUIRE(missingProfile.ErrorValue().message.find("producer 2") != std::string::npos);

        fixture = Fixture{};
        fixture.surfaces.front().filter = Id<NavigationFilterId>(99);
        auto geometryWithMissingFilter = fixture.Geometry();
        RequireError(NavigationBakeInputSnapshot::Create(Revisions(), registry, fixture.profiles, fixture.surfaces, fixture.modifiers,
                                                         std::move(geometryWithMissingFilter)),
                     NavigationErrors::BakeInputReferenceMissing);

        fixture = Fixture{};
        fixture.modifiers.front().localBounds.maximum.x = fixture.modifiers.front().localBounds.minimum.x;
        auto geometryWithInvalidModifier = fixture.Geometry();
        auto invalidModifier = NavigationBakeInputSnapshot::Create(Revisions(), registry, fixture.profiles, fixture.surfaces,
                                                                   fixture.modifiers, std::move(geometryWithInvalidModifier));
        RequireError(invalidModifier, NavigationErrors::BakeInputInvalid);
        REQUIRE(invalidModifier.ErrorValue().message.find("modifier 2") != std::string::npos);

        fixture = Fixture{};
        fixture.surfaces[1] = fixture.surfaces[0];
        auto geometryWithDuplicateSurface = fixture.Geometry();
        RequireError(NavigationBakeInputSnapshot::Create(Revisions(), registry, fixture.profiles, fixture.surfaces, fixture.modifiers,
                                                         std::move(geometryWithDuplicateSurface)),
                     NavigationErrors::DescriptorConflict);
    }

    TEST_CASE("Bake input enforces count work and owned-byte limits without truncation", "[unit][navigation][bake_input][capacity]") {
        const Fixture fixture;
        NavigationBakeInputLimits limits{};
        limits.maxProfiles = 1;
        RequireError(Build(fixture, false, limits), NavigationErrors::BakeInputCapacityExceeded);
        limits = {};
        limits.maxAreas = 1;
        RequireError(Build(fixture, false, limits), NavigationErrors::BakeInputCapacityExceeded);
        limits = {};
        limits.maxSurfaceBindings = 1;
        RequireError(Build(fixture, false, limits), NavigationErrors::BakeInputCapacityExceeded);
        limits = {};
        limits.maxModifiers = 1;
        RequireError(Build(fixture, false, limits), NavigationErrors::BakeInputCapacityExceeded);
        limits = {};
        limits.maxTileTriangles = 1;
        RequireError(Build(fixture, false, limits), NavigationErrors::BakeInputCapacityExceeded);
        limits = {};
        limits.maxWorkUnits = 1;
        RequireError(Build(fixture, false, limits), NavigationErrors::BakeInputCapacityExceeded);
        limits = {};
        limits.maxOwnedBytes = 1;
        RequireError(Build(fixture, false, limits), NavigationErrors::BakeInputCapacityExceeded);
    }

    TEST_CASE("Bake publication rejects stale replacement cancellation failure and shutdown",
              "[unit][navigation][bake_input][lifecycle][stale]") {
        const Fixture fixture;
        const auto input = std::move(Build(fixture)).Value();
        auto observations = fixture.Observations();

        REQUIRE(input.ValidatePublication(Revisions().requestGeneration, Revisions(), observations).HasValue());
        auto current = Revisions();
        current.scene = Id<NavigationSceneDocumentRevision>(30);
        RequireError(input.ValidatePublication(Revisions().requestGeneration, current, observations), NavigationErrors::BakeInputStale);
        RequireError(input.ValidatePublication(Id<NavigationBakeRequestGeneration>(2), Revisions(), observations),
                     NavigationErrors::BakeInputStale);
        observations.front().revision = Id<NavigationSourceRevision>(30);
        RequireError(input.ValidatePublication(Revisions().requestGeneration, Revisions(), observations), NavigationErrors::BakeInputStale);
        observations = fixture.Observations();
        RequireError(input.ValidatePublication(Revisions().requestGeneration, Revisions(), observations,
                                               NavigationBakePublicationState::Cancelled),
                     NavigationErrors::BakeInputCancelled);
        RequireError(input.ValidatePublication(Revisions().requestGeneration, Revisions(), observations,
                                               NavigationBakePublicationState::Failed),
                     NavigationErrors::BakeInputFailed);
        RequireError(input.ValidatePublication(Revisions().requestGeneration, Revisions(), observations,
                                               NavigationBakePublicationState::Superseded),
                     NavigationErrors::BakeInputStale);
        RequireError(input.ValidatePublication(Revisions().requestGeneration, Revisions(), observations,
                                               NavigationBakePublicationState::ShuttingDown),
                     NavigationErrors::BakeInputShuttingDown);
    }

    TEST_CASE("Source geometry normalizes explicit units axes and left-handed winding", "[unit][navigation][source_geometry]") {
        OwnedContribution source;
        source.vertices = {{0.0F, 0.0F, 0.0F}, {100.0F, 0.0F, 0.0F}, {0.0F, 100.0F, 0.0F}};
        auto input = source.View();
        input.coordinates = {.system = NavigationSourceCoordinateSystem::RightHandedZUp, .metersPerUnit = 0.01F};
        auto snapshot =
            std::move(NavigationSourceGeometrySnapshot::Create(Id<NavigationSourceSnapshotRevision>(1), std::span{&input, 1})).Value();
        REQUIRE(snapshot.Vertices()[1] == Math::Vec3{1.0F, 0.0F, 0.0F});
        REQUIRE(snapshot.Vertices()[2] == Math::Vec3{0.0F, 0.0F, -1.0F});

        input.coordinates = {.system = NavigationSourceCoordinateSystem::LeftHandedYUp, .metersPerUnit = 0.01F};
        const auto leftHandedSnapshot =
            std::move(NavigationSourceGeometrySnapshot::Create(Id<NavigationSourceSnapshotRevision>(1), std::span{&input, 1})).Value();
        REQUIRE(leftHandedSnapshot.Triangles().front().vertexIndices == std::array<std::uint32_t, 3>{0, 2, 1});

        input.coordinates.metersPerUnit = std::numeric_limits<float>::infinity();
        auto invalid = NavigationSourceGeometrySnapshot::Create(Id<NavigationSourceSnapshotRevision>(1), std::span{&input, 1});
        RequireError(invalid, NavigationErrors::SourceGeometryInvalid);
        REQUIRE(invalid.ErrorValue().message.find("producer 1") != std::string::npos);

        input.coordinates.metersPerUnit = 1.0F;
        source.vertices.front().x = std::numeric_limits<float>::quiet_NaN();
        invalid = NavigationSourceGeometrySnapshot::Create(Id<NavigationSourceSnapshotRevision>(1), std::span{&input, 1});
        RequireError(invalid, NavigationErrors::SourceGeometryInvalid);
        REQUIRE(invalid.ErrorValue().message.find("contribution 1") != std::string::npos);
    }
}  // namespace Horo::Navigation
