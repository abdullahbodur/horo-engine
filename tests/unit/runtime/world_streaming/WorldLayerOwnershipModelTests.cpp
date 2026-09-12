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
        using TestSupport::StreamingLayerOwner;
        using TestSupport::World;
        using TestSupport::WorldOwner;

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

        WorldLayerOwnershipAdmissionContext RuntimeControlledContext(const std::uint64_t ownerGeneration = 3,
                                                                     const std::uint64_t revision = 4) {
            auto context = Context();
            context.current =
                Descriptor(WorldLayerPlacement::NonSpatial, WorldLayerResidencyPolicy::RuntimeControlled, WorldLayerAudience::Runtime,
                           ExplicitOwner(WorldLayerControlOwnerKind::GameplayScript, 9, ownerGeneration), revision);
            return context;
        }

        WorldLayerControlHandoffReceipt AuthorizeHandoff(WorldLayerOwnershipAdmissionContext &context, const WorldLayerControlOwner &target,
                                                         const std::uint64_t authorizationGeneration = 1) {
            REQUIRE(context.current.has_value());
            const WorldLayerControlHandoffReceipt receipt{
                .id = IdentityFrom<WorldLayerControlHandoffId>(21),
                .generation = IdentityFrom<WorldLayerControlHandoffGeneration>(authorizationGeneration),
                .currentOwner = context.current->owner,
                .targetOwner = target,
                .layer = context.current->layer,
                .expectedRevision = context.current->revision,
            };
            context.handoff = WorldLayerValidatedHandoffContext{.authorization = receipt, .validatedTarget = target};
            return receipt;
        }

        TEST_CASE("Layer classification keeps placement residency audience and owner orthogonal",
                  "[unit][world_streaming][layer_ownership]") {
            const auto persistent = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Persistent,
                                               WorldLayerAudience::Runtime, StreamingLayerOwner());
            const auto spatial = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Streamed, WorldLayerAudience::Runtime,
                                            StreamingLayerOwner());
            const auto nonSpatial = Descriptor(WorldLayerPlacement::NonSpatial, WorldLayerResidencyPolicy::Streamed,
                                               WorldLayerAudience::Runtime, StreamingLayerOwner());
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
                                                                          WorldLayerAudience::Runtime, StreamingLayerOwner())),
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
            auto streaming = StreamingLayerOwner();
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
            auto context = RuntimeControlledContext(1);

            auto replacement = *context.current;
            replacement.revision = IdentityFrom<WorldLayerRevision>(5);
            REQUIRE(ValidateWorldLayerOwnershipAdmission({replacement, IdentityFrom<WorldLayerRevision>(4)}, context).Value() ==
                    WorldLayerOwnershipAdmissionKind::Replace);

            auto handoff = replacement;
            handoff.owner = ExplicitOwner(WorldLayerControlOwnerKind::NetworkReplication, 12, 3);
            const auto authorization = AuthorizeHandoff(context, handoff.owner);
            REQUIRE(ValidateWorldLayerOwnershipAdmission({handoff, IdentityFrom<WorldLayerRevision>(4), authorization}, context).Value() ==
                    WorldLayerOwnershipAdmissionKind::Handoff);

            auto stale = handoff;
            stale.revision = IdentityFrom<WorldLayerRevision>(6);
            RequireError(ValidateWorldLayerOwnershipAdmission({stale, IdentityFrom<WorldLayerRevision>(4), authorization}, context),
                         WorldStreamingErrors::LayerOwnershipRevisionStale);
            RequireError(ValidateWorldLayerOwnershipAdmission({handoff, IdentityFrom<WorldLayerRevision>(3), authorization}, context),
                         WorldStreamingErrors::LayerOwnershipRevisionStale);
        }

        TEST_CASE("Runtime-control handoff requires exact current-owner authorization",
                  "[unit][world_streaming][layer_ownership][handoff]") {
            auto context = RuntimeControlledContext();

            auto crossRole = *context.current;
            crossRole.revision = IdentityFrom<WorldLayerRevision>(5);
            crossRole.owner = ExplicitOwner(WorldLayerControlOwnerKind::NetworkReplication, 12, 1);
            RequireError(ValidateWorldLayerOwnershipAdmission({crossRole, context.current->revision}, context),
                         WorldStreamingErrors::LayerOwnershipOwnerStale);
            auto wrongCurrentAuthorization = AuthorizeHandoff(context, crossRole.owner);
            wrongCurrentAuthorization.currentOwner = ExplicitOwner(WorldLayerControlOwnerKind::GameplayScript, 8, 3);
            RequireError(ValidateWorldLayerOwnershipAdmission({crossRole, context.current->revision, wrongCurrentAuthorization}, context),
                         WorldStreamingErrors::LayerOwnershipOwnerStale);

            auto crossOwner = crossRole;
            crossOwner.owner = ExplicitOwner(WorldLayerControlOwnerKind::GameplayScript, 10, 1);
            const auto crossOwnerAuthorization = AuthorizeHandoff(context, crossOwner.owner);
            REQUIRE(
                ValidateWorldLayerOwnershipAdmission({crossOwner, context.current->revision, crossOwnerAuthorization}, context).Value() ==
                WorldLayerOwnershipAdmissionKind::Handoff);
        }

        TEST_CASE("Same-owner lifetime handoff advances generation and rejects rewind",
                  "[unit][world_streaming][layer_ownership][handoff][generation]") {
            auto context = RuntimeControlledContext();

            auto successor = *context.current;
            successor.revision = IdentityFrom<WorldLayerRevision>(5);
            successor.owner = ExplicitOwner(WorldLayerControlOwnerKind::GameplayScript, 9, 4);
            auto authorization = AuthorizeHandoff(context, successor.owner);
            REQUIRE(ValidateWorldLayerOwnershipAdmission({successor, context.current->revision, authorization}, context).Value() ==
                    WorldLayerOwnershipAdmissionKind::Handoff);

            successor.owner = ExplicitOwner(WorldLayerControlOwnerKind::GameplayScript, 9, 2);
            authorization = AuthorizeHandoff(context, successor.owner, 2);
            RequireError(ValidateWorldLayerOwnershipAdmission({successor, context.current->revision, authorization}, context),
                         WorldStreamingErrors::LayerOwnershipOwnerStale);
        }

        TEST_CASE("Handoff rejects stale authorization and expired target lifetime",
                  "[unit][world_streaming][layer_ownership][handoff][stale]") {
            auto context = RuntimeControlledContext();
            auto candidate = *context.current;
            candidate.revision = IdentityFrom<WorldLayerRevision>(5);
            candidate.owner = ExplicitOwner(WorldLayerControlOwnerKind::NetworkReplication, 12, 1);

            const auto authorization = AuthorizeHandoff(context, candidate.owner, 2);
            auto staleAuthorization = authorization;
            staleAuthorization.generation = IdentityFrom<WorldLayerControlHandoffGeneration>(1);
            RequireError(ValidateWorldLayerOwnershipAdmission({candidate, context.current->revision, staleAuthorization}, context),
                         WorldStreamingErrors::LayerOwnershipOwnerStale);

            auto otherLayerContext = context;
            otherLayerContext.current->layer = Layer(3);
            auto otherLayerCandidate = candidate;
            otherLayerCandidate.layer = Layer(3);
            RequireError(ValidateWorldLayerOwnershipAdmission({otherLayerCandidate, otherLayerContext.current->revision, authorization},
                                                              otherLayerContext),
                         WorldStreamingErrors::LayerOwnershipOwnerStale);

            context.handoff->validatedTarget = ExplicitOwner(WorldLayerControlOwnerKind::NetworkReplication, 12, 2);
            RequireError(ValidateWorldLayerOwnershipAdmission({candidate, context.current->revision, authorization}, context),
                         WorldStreamingErrors::LayerOwnershipOwnerStale);
        }

        TEST_CASE("Stable layer identity and classification cannot change during replacement",
                  "[unit][world_streaming][layer_ownership][identity]") {
            auto context = Context();
            context.current = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Streamed, WorldLayerAudience::Runtime,
                                         StreamingLayerOwner(), 7);
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
            candidate.owner = StreamingLayerOwner();
            candidate.owner.world = WorldOwner(6);
            RequireError(ValidateWorldLayerOwnershipAdmission({candidate, IdentityFrom<WorldLayerRevision>(7)}, context),
                         WorldStreamingErrors::LayerOwnershipOwnerStale);
        }

        TEST_CASE("Layer admission enforces capacity cancellation and shutdown transactionally",
                  "[unit][world_streaming][layer_ownership][lifecycle]") {
            const auto candidate = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Persistent,
                                              WorldLayerAudience::Runtime, StreamingLayerOwner());
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
            context.current = candidate;
            context.layerCount = 0;
            auto replacement = candidate;
            replacement.revision = IdentityFrom<WorldLayerRevision>(2);
            RequireError(ValidateWorldLayerOwnershipAdmission({replacement, candidate.revision}, context),
                         WorldStreamingErrors::LayerOwnershipInvalid);
            context = Context();
            context.state = static_cast<WorldLayerOwnershipAuthorityState>(255);
            RequireError(ValidateWorldLayerOwnershipAdmission({candidate, std::nullopt}, context),
                         WorldStreamingErrors::LayerOwnershipInvalid);
        }

        TEST_CASE("Layer descriptors reject malformed enums owners and revision exhaustion",
                  "[unit][world_streaming][layer_ownership][failure]") {
            auto descriptor = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Persistent, WorldLayerAudience::Runtime,
                                         StreamingLayerOwner());
            descriptor.placement = static_cast<WorldLayerPlacement>(255);
            RequireError(ValidateWorldLayerOwnershipDescriptor(descriptor), WorldStreamingErrors::LayerOwnershipUnsupported);

            descriptor = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Persistent, WorldLayerAudience::Runtime,
                                    StreamingLayerOwner());
            descriptor.revision = {};
            RequireError(ValidateWorldLayerOwnershipDescriptor(descriptor), WorldStreamingErrors::LayerOwnershipInvalid);

            constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
            auto context = Context();
            context.current = Descriptor(WorldLayerPlacement::Spatial, WorldLayerResidencyPolicy::Persistent, WorldLayerAudience::Runtime,
                                         StreamingLayerOwner(), maximum);
            auto successor = *context.current;
            successor.revision = IdentityFrom<WorldLayerRevision>(1);
            RequireError(ValidateWorldLayerOwnershipAdmission({successor, IdentityFrom<WorldLayerRevision>(maximum)}, context),
                         WorldStreamingErrors::GenerationExhausted);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
