#include "Horo/WorldStreaming/WorldLayerState.h"
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

        WorldLayerControlOwner GameplayOwner(const std::uint64_t identity = 9, const std::uint64_t generation = 1) {
            return {.world = WorldOwner(),
                    .kind = WorldLayerControlOwnerKind::GameplayScript,
                    .authority = IdentityFrom<WorldLayerControlOwnerId>(identity),
                    .generation = IdentityFrom<WorldLayerControlOwnerGeneration>(generation)};
        }

        WorldLayerOwnershipDescriptor Ownership(const std::uint64_t revision = 1, const bool runtimeControlled = false) {
            return {.layer = Layer(),
                    .revision = IdentityFrom<WorldLayerRevision>(revision),
                    .placement = WorldLayerPlacement::Spatial,
                    .residency = runtimeControlled ? WorldLayerResidencyPolicy::RuntimeControlled : WorldLayerResidencyPolicy::Streamed,
                    .audience = WorldLayerAudience::Runtime,
                    .owner = runtimeControlled ? GameplayOwner() : StreamingOwner()};
        }

        WorldLayerStateAdmissionContext Context() {
            return {.expectedWorld = WorldOwner(),
                    .layerCount = 2,
                    .layerCapacity = 4,
                    .authorityState = WorldLayerStateAuthorityState::Active};
        }

        WorldLayerStateRecord Record(const std::uint64_t ownershipRevision = 1, const bool runtimeControlled = false) {
            const auto result = CreateWorldLayerStateRecord(Ownership(ownershipRevision, runtimeControlled), Context());
            REQUIRE(result.HasValue());
            return result.Value();
        }

        void Advance(WorldLayerStateRecord &record, const WorldLayerStateTransition transition,
                     const WorldLayerStateAuthorityState authorityState = WorldLayerStateAuthorityState::Active) {
            const WorldLayerStateTransitionRequest request{.expected = record.Fence(), .transition = transition};
            const auto result = AdvanceWorldLayerState(record, request, authorityState);
            REQUIRE(result.HasValue());
            record = result.Value();
        }

        TEST_CASE("Layer state follows load activation deactivation and unload independently from cells",
                  "[unit][world_streaming][layer_state]") {
            auto record = Record();
            REQUIRE(record.state == WorldLayerState::Unloaded);
            REQUIRE(record.Fence().layer == Layer());

            Advance(record, WorldLayerStateTransition::BeginLoad);
            REQUIRE(record.state == WorldLayerState::Loading);
            Advance(record, WorldLayerStateTransition::CompleteLoad);
            REQUIRE(record.state == WorldLayerState::Loaded);
            Advance(record, WorldLayerStateTransition::BeginActivation);
            REQUIRE(record.state == WorldLayerState::Activating);
            Advance(record, WorldLayerStateTransition::CompleteActivation);
            REQUIRE(record.state == WorldLayerState::Activated);
            Advance(record, WorldLayerStateTransition::BeginDeactivation);
            REQUIRE(record.state == WorldLayerState::Deactivating);
            Advance(record, WorldLayerStateTransition::CompleteDeactivation);
            REQUIRE(record.state == WorldLayerState::Loaded);
            Advance(record, WorldLayerStateTransition::BeginUnload);
            REQUIRE(record.state == WorldLayerState::Unloading);
            Advance(record, WorldLayerStateTransition::CompleteUnload);
            REQUIRE(record.state == WorldLayerState::Unloaded);
            REQUIRE(record.revision.Value() == 9);
            static_assert(std::is_trivially_copyable_v<WorldLayerStateRecord>);
        }

        TEST_CASE("Layer transition failure leaves the immutable source record unchanged",
                  "[unit][world_streaming][layer_state][transition]") {
            const auto record = Record();
            const WorldLayerStateTransitionRequest illegal{.expected = record.Fence(),
                                                           .transition = WorldLayerStateTransition::BeginActivation};
            RequireError(AdvanceWorldLayerState(record, illegal, WorldLayerStateAuthorityState::Active),
                         WorldStreamingErrors::LayerStateTransitionInvalid);
            REQUIRE(record.state == WorldLayerState::Unloaded);
            REQUIRE(record.revision.Value() == 1);

            auto unsupported = illegal;
            unsupported.transition = static_cast<WorldLayerStateTransition>(255);
            RequireError(AdvanceWorldLayerState(record, unsupported, WorldLayerStateAuthorityState::Active),
                         WorldStreamingErrors::LayerStateUnsupported);
        }

        TEST_CASE("Cancellation rolls in-flight work back to the last completed layer state",
                  "[unit][world_streaming][layer_state][cancellation]") {
            auto loading = Record();
            Advance(loading, WorldLayerStateTransition::BeginLoad);
            Advance(loading, WorldLayerStateTransition::Cancel, WorldLayerStateAuthorityState::Cancelling);
            REQUIRE(loading.state == WorldLayerState::Unloading);
            Advance(loading, WorldLayerStateTransition::CompleteUnload, WorldLayerStateAuthorityState::Cancelling);
            REQUIRE(loading.state == WorldLayerState::Unloaded);

            auto activating = Record();
            Advance(activating, WorldLayerStateTransition::BeginLoad);
            Advance(activating, WorldLayerStateTransition::CompleteLoad);
            Advance(activating, WorldLayerStateTransition::BeginActivation);
            Advance(activating, WorldLayerStateTransition::Cancel, WorldLayerStateAuthorityState::Cancelling);
            REQUIRE(activating.state == WorldLayerState::Deactivating);
            Advance(activating, WorldLayerStateTransition::CompleteDeactivation, WorldLayerStateAuthorityState::Cancelling);
            REQUIRE(activating.state == WorldLayerState::Loaded);

            const WorldLayerStateTransitionRequest forward{.expected = activating.Fence(),
                                                           .transition = WorldLayerStateTransition::BeginActivation};
            RequireError(AdvanceWorldLayerState(activating, forward, WorldLayerStateAuthorityState::Cancelling),
                         WorldStreamingErrors::LayerStateLifecycleUnavailable);
        }

        TEST_CASE("Cancelling authority drains activated layers without fabricating unloaded state",
                  "[unit][world_streaming][layer_state][shutdown]") {
            auto record = Record();
            Advance(record, WorldLayerStateTransition::BeginLoad);
            Advance(record, WorldLayerStateTransition::CompleteLoad);
            Advance(record, WorldLayerStateTransition::BeginActivation);
            Advance(record, WorldLayerStateTransition::CompleteActivation);

            Advance(record, WorldLayerStateTransition::BeginDeactivation, WorldLayerStateAuthorityState::Cancelling);
            REQUIRE(record.state == WorldLayerState::Deactivating);
            Advance(record, WorldLayerStateTransition::CompleteDeactivation, WorldLayerStateAuthorityState::Cancelling);
            REQUIRE(record.state == WorldLayerState::Loaded);
            Advance(record, WorldLayerStateTransition::BeginUnload, WorldLayerStateAuthorityState::Cancelling);
            REQUIRE(record.state == WorldLayerState::Unloading);
            Advance(record, WorldLayerStateTransition::CompleteUnload, WorldLayerStateAuthorityState::Cancelling);
            REQUIRE(record.state == WorldLayerState::Unloaded);

            const WorldLayerStateTransitionRequest closed{.expected = record.Fence(), .transition = WorldLayerStateTransition::BeginLoad};
            RequireError(AdvanceWorldLayerState(record, closed, WorldLayerStateAuthorityState::Closed),
                         WorldStreamingErrors::LayerStateLifecycleUnavailable);
        }

        TEST_CASE("Layer state rejects stale world ownership and state revisions", "[unit][world_streaming][layer_state][fence]") {
            const auto record = Record();
            WorldLayerStateTransitionRequest request{.expected = record.Fence(), .transition = WorldLayerStateTransition::BeginLoad};
            request.expected.stateRevision = IdentityFrom<WorldLayerStateRevision>(2);
            RequireError(AdvanceWorldLayerState(record, request, WorldLayerStateAuthorityState::Active),
                         WorldStreamingErrors::LayerStateStale);

            request.expected = record.Fence();
            request.expected.world = WorldOwner(6);
            RequireError(AdvanceWorldLayerState(record, request, WorldLayerStateAuthorityState::Active),
                         WorldStreamingErrors::LayerStateStale);

            request.expected = record.Fence();
            request.expected.ownershipRevision = IdentityFrom<WorldLayerRevision>(2);
            RequireError(AdvanceWorldLayerState(record, request, WorldLayerStateAuthorityState::Active),
                         WorldStreamingErrors::LayerStateStale);
        }

        TEST_CASE("Ownership replacement requires quiescent state and exact successor publication",
                  "[unit][world_streaming][layer_state][replacement]") {
            auto current = Record(3, true);
            auto replacement = Ownership(4, true);
            replacement.owner = GameplayOwner(10, 2);
            const auto replaced =
                ReplaceWorldLayerStateOwnership(current, replacement, current.Fence(), WorldLayerStateAuthorityState::Active);
            REQUIRE(replaced.HasValue());
            REQUIRE(replaced.Value().ownership.owner == replacement.owner);
            REQUIRE(replaced.Value().state == WorldLayerState::Unloaded);
            REQUIRE(replaced.Value().revision.Value() == 2);

            Advance(current, WorldLayerStateTransition::BeginLoad);
            RequireError(ReplaceWorldLayerStateOwnership(current, replacement, current.Fence(), WorldLayerStateAuthorityState::Active),
                         WorldStreamingErrors::LayerStateTransitionInvalid);

            current = Record(3, true);
            replacement.revision = IdentityFrom<WorldLayerRevision>(5);
            RequireError(ReplaceWorldLayerStateOwnership(current, replacement, current.Fence(), WorldLayerStateAuthorityState::Active),
                         WorldStreamingErrors::LayerOwnershipRevisionStale);
        }

        TEST_CASE("Initial layer-state admission is bounded and lifecycle gated", "[unit][world_streaming][layer_state][admission]") {
            auto context = Context();
            context.layerCount = context.layerCapacity;
            RequireError(CreateWorldLayerStateRecord(Ownership(), context), WorldStreamingErrors::LayerStateCapacityExceeded);

            context = Context();
            context.authorityState = WorldLayerStateAuthorityState::Cancelling;
            RequireError(CreateWorldLayerStateRecord(Ownership(), context), WorldStreamingErrors::LayerStateLifecycleUnavailable);
            context.authorityState = WorldLayerStateAuthorityState::Closed;
            RequireError(CreateWorldLayerStateRecord(Ownership(), context), WorldStreamingErrors::LayerStateLifecycleUnavailable);

            context = Context();
            context.expectedWorld = WorldOwner(6);
            RequireError(CreateWorldLayerStateRecord(Ownership(), context), WorldStreamingErrors::LayerStateStale);

            context = Context();
            context.layerCapacity = 0;
            RequireError(CreateWorldLayerStateRecord(Ownership(), context), WorldStreamingErrors::LayerStateInvalid);
        }

        TEST_CASE("Layer-state failure and revision exhaustion remain typed", "[unit][world_streaming][layer_state][failure]") {
            auto record = Record();
            Advance(record, WorldLayerStateTransition::BeginLoad);
            Advance(record, WorldLayerStateTransition::Fail);
            REQUIRE(record.state == WorldLayerState::Failed);

            record.revision = IdentityFrom<WorldLayerStateRevision>(std::numeric_limits<std::uint64_t>::max());
            const WorldLayerStateTransitionRequest exhausted{.expected = record.Fence(),
                                                             .transition = WorldLayerStateTransition::BeginLoad};
            RequireError(AdvanceWorldLayerState(record, exhausted, WorldLayerStateAuthorityState::Active),
                         WorldStreamingErrors::GenerationExhausted);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
