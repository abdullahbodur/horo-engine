#include "Horo/WorldStreaming/WorldLayerOwnershipModel.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <type_traits>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;
        using TestSupport::Layer;
        using TestSupport::RequireError;
        using TestSupport::World;

        StreamingRuntimeOwnerToken WorldOwner(const std::uint64_t owner = 5, const std::uint64_t epoch = 1) {
            return {.partition = World(),
                    .epoch = IdentityFrom<PartitionEpoch>(epoch),
                    .owner = IdentityFrom<StreamingRuntimeOwnerId>(owner)};
        }

        WorldLayerControlOwner StreamingOwner() {
            return {.world = WorldOwner(), .kind = WorldLayerControlOwnerKind::WorldStreaming};
        }

        WorldLayerControlOwner ExplicitOwner(const WorldLayerControlOwnerKind kind, const std::uint64_t identity = 9,
                                             const std::uint64_t generation = 1) {
            return {.world = WorldOwner(),
                    .kind = kind,
                    .authority = IdentityFrom<WorldLayerControlOwnerId>(identity),
                    .generation = IdentityFrom<WorldLayerControlOwnerGeneration>(generation)};
        }

        WorldLayerOwnershipDescriptor Descriptor(const WorldLayerPlacement placement, const WorldLayerResidencyPolicy residency,
                                                 const WorldLayerAudience audience, const WorldLayerControlOwner &owner,
                                                 const std::uint64_t revision = 1, const std::uint16_t layer = 2) {
            return {.layer = Layer(layer),
                    .revision = IdentityFrom<WorldLayerRevision>(revision),
                    .placement = placement,
                    .residency = residency,
                    .audience = audience,
                    .owner = owner};
        }

        WorldLayerOwnershipAdmissionContext Context() {
            return {.expectedWorld = WorldOwner(),
                    .current = std::nullopt,
                    .layerCount = 2,
                    .layerCapacity = 4,
                    .state = WorldLayerOwnershipAuthorityState::Active};
        }

        TEST_CASE("Layer classification keeps placement residency audience and owner orthogonal",
                  "[unit][world_streaming][layer_ownership]") {
            const auto persistent = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Persistent,
                                               WorldLayerAudience::Runtime, StreamingOwner());
            const auto spatial = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Streamed, WorldLayerAudience::Runtime,
                                            StreamingOwner());
            const auto nonSpatial = Descriptor(WorldLayerPlacement::NonSpatial, WorldLayerResidencyPolicy::Streamed,
                                               WorldLayerAudience::Runtime, StreamingOwner());
            const auto editorOnly = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Streamed,
                                               WorldLayerAudience::EditorOnly, ExplicitOwner(WorldLayerControlOwnerKind::EditorDocument));
            const auto runtimeControlled =
                Descriptor(WorldLayerPlacement::NonSpatial, WorldLayerResidencyPolicy::RuntimeControlled, WorldLayerAudience::Runtime,
                           ExplicitOwner(WorldLayerControlOwnerKind::GameplayScript));

            for (const auto &descriptor : {persistent, spatial, nonSpatial, editorOnly, runtimeControlled})
                REQUIRE(ValidateWorldLayerOwnershipDescriptor(descriptor).HasValue());
            REQUIRE(ValidateWorldLayerOwnershipAdmission({persistent, std::nullopt}, Context()).Value() ==
                    WorldLayerOwnershipAdmissionKind::Insert);
            static_assert(std::is_trivially_copyable_v<WorldLayerOwnershipDescriptor>);
        }

        TEST_CASE("Layer owner policy rejects contradictory authority combinations", "[unit][world_streaming][layer_ownership][policy]") {
            RequireError(ValidateWorldLayerOwnershipDescriptor(
                             Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Persistent, WorldLayerAudience::Runtime,
                                        ExplicitOwner(WorldLayerControlOwnerKind::GameplayScript))),
                         WorldStreamingErrors::LayerOwnershipUnsupported);
            RequireError(ValidateWorldLayerOwnershipDescriptor(Descriptor(WorldLayerPlacement::Spatial,
                                                                          WorldLayerResidencyPolicy::RuntimeControlled,
                                                                          WorldLayerAudience::Runtime, StreamingOwner())),
                         WorldStreamingErrors::LayerOwnershipUnsupported);
            RequireError(ValidateWorldLayerOwnershipDescriptor(
                             Descriptor(WorldLayerPlacement::NonSpatial, WorldLayerResidencyPolicy::Streamed,
                                        WorldLayerAudience::EditorOnly, ExplicitOwner(WorldLayerControlOwnerKind::NetworkReplication))),
                         WorldStreamingErrors::LayerOwnershipUnsupported);
            RequireError(ValidateWorldLayerOwnershipDescriptor(
                             Descriptor(WorldLayerPlacement::NonSpatial, WorldLayerResidencyPolicy::RuntimeControlled,
                                        WorldLayerAudience::EditorOnly, ExplicitOwner(WorldLayerControlOwnerKind::EditorDocument))),
                         WorldStreamingErrors::LayerOwnershipUnsupported);
        }

        TEST_CASE("Layer owner bindings carry exactly one applicable authority representation",
                  "[unit][world_streaming][layer_ownership][owner]") {
            auto streaming = StreamingOwner();
            streaming.authority = IdentityFrom<WorldLayerControlOwnerId>(9);
            CHECK_FALSE(streaming.IsValid());

            auto editor = ExplicitOwner(WorldLayerControlOwnerKind::EditorDocument);
            editor.generation = {};
            CHECK_FALSE(editor.IsValid());

            auto unknown = ExplicitOwner(WorldLayerControlOwnerKind::GameplayScript);
            unknown.kind = static_cast<WorldLayerControlOwnerKind>(255);
            CHECK_FALSE(unknown.IsValid());
        }

        TEST_CASE("Layer replacement and runtime-control handoff are exact revision fenced",
                  "[unit][world_streaming][layer_ownership][replacement]") {
            auto context = Context();
            context.current = Descriptor(WorldLayerPlacement::NonSpatial, WorldLayerResidencyPolicy::RuntimeControlled,
                                         WorldLayerAudience::Runtime, ExplicitOwner(WorldLayerControlOwnerKind::GameplayScript), 4);

            auto replacement = *context.current;
            replacement.revision = IdentityFrom<WorldLayerRevision>(5);
            REQUIRE(ValidateWorldLayerOwnershipAdmission({replacement, IdentityFrom<WorldLayerRevision>(4)}, context).Value() ==
                    WorldLayerOwnershipAdmissionKind::Replace);

            auto handoff = replacement;
            handoff.owner = ExplicitOwner(WorldLayerControlOwnerKind::NetworkReplication, 12, 3);
            REQUIRE(ValidateWorldLayerOwnershipAdmission({handoff, IdentityFrom<WorldLayerRevision>(4)}, context).Value() ==
                    WorldLayerOwnershipAdmissionKind::Handoff);

            auto stale = handoff;
            stale.revision = IdentityFrom<WorldLayerRevision>(6);
            RequireError(ValidateWorldLayerOwnershipAdmission({stale, IdentityFrom<WorldLayerRevision>(4)}, context),
                         WorldStreamingErrors::LayerOwnershipRevisionStale);
            RequireError(ValidateWorldLayerOwnershipAdmission({handoff, IdentityFrom<WorldLayerRevision>(3)}, context),
                         WorldStreamingErrors::LayerOwnershipRevisionStale);
        }

        TEST_CASE("Stable layer identity and classification cannot change during replacement",
                  "[unit][world_streaming][layer_ownership][identity]") {
            auto context = Context();
            context.current = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Streamed, WorldLayerAudience::Runtime,
                                         StreamingOwner(), 7);
            auto candidate = *context.current;
            candidate.revision = IdentityFrom<WorldLayerRevision>(8);

            candidate.layer = Layer(3);
            RequireError(ValidateWorldLayerOwnershipAdmission({candidate, IdentityFrom<WorldLayerRevision>(7)}, context),
                         WorldStreamingErrors::LayerOwnershipIdentityConflict);

            candidate = *context.current;
            candidate.revision = IdentityFrom<WorldLayerRevision>(8);
            candidate.placement = WorldLayerPlacement::NonSpatial;
            RequireError(ValidateWorldLayerOwnershipAdmission({candidate, IdentityFrom<WorldLayerRevision>(7)}, context),
                         WorldStreamingErrors::LayerOwnershipUnsupported);

            candidate = *context.current;
            candidate.revision = IdentityFrom<WorldLayerRevision>(8);
            candidate.owner = StreamingOwner();
            candidate.owner.world = WorldOwner(6);
            RequireError(ValidateWorldLayerOwnershipAdmission({candidate, IdentityFrom<WorldLayerRevision>(7)}, context),
                         WorldStreamingErrors::LayerOwnershipOwnerStale);
        }

        TEST_CASE("Layer admission enforces capacity cancellation and shutdown transactionally",
                  "[unit][world_streaming][layer_ownership][lifecycle]") {
            const auto candidate = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Persistent,
                                              WorldLayerAudience::Runtime, StreamingOwner());
            auto context = Context();
            context.layerCount = context.layerCapacity;
            RequireError(ValidateWorldLayerOwnershipAdmission({candidate, std::nullopt}, context),
                         WorldStreamingErrors::LayerOwnershipCapacityExceeded);

            for (const auto state : {WorldLayerOwnershipAuthorityState::Cancelling, WorldLayerOwnershipAuthorityState::Closed}) {
                context = Context();
                context.state = state;
                RequireError(ValidateWorldLayerOwnershipAdmission({candidate, std::nullopt}, context),
                             WorldStreamingErrors::LayerOwnershipLifecycleUnavailable);
                CHECK_FALSE(context.current.has_value());
            }

            context = Context();
            context.layerCount = context.layerCapacity + 1;
            RequireError(ValidateWorldLayerOwnershipAdmission({candidate, std::nullopt}, context),
                         WorldStreamingErrors::LayerOwnershipInvalid);
            context = Context();
            context.state = static_cast<WorldLayerOwnershipAuthorityState>(255);
            RequireError(ValidateWorldLayerOwnershipAdmission({candidate, std::nullopt}, context),
                         WorldStreamingErrors::LayerOwnershipInvalid);
        }

        TEST_CASE("Layer descriptors reject malformed enums owners and revision exhaustion",
                  "[unit][world_streaming][layer_ownership][failure]") {
            auto descriptor = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Persistent, WorldLayerAudience::Runtime,
                                         StreamingOwner());
            descriptor.placement = static_cast<WorldLayerPlacement>(255);
            RequireError(ValidateWorldLayerOwnershipDescriptor(descriptor), WorldStreamingErrors::LayerOwnershipUnsupported);

            descriptor = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Persistent, WorldLayerAudience::Runtime,
                                    StreamingOwner());
            descriptor.revision = {};
            RequireError(ValidateWorldLayerOwnershipDescriptor(descriptor), WorldStreamingErrors::LayerOwnershipInvalid);

            constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
            auto context = Context();
            context.current = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Persistent, WorldLayerAudience::Runtime,
                                         StreamingOwner(), maximum);
            auto successor = *context.current;
            successor.revision = IdentityFrom<WorldLayerRevision>(1);
            RequireError(ValidateWorldLayerOwnershipAdmission({successor, IdentityFrom<WorldLayerRevision>(maximum)}, context),
                         WorldStreamingErrors::GenerationExhausted);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
