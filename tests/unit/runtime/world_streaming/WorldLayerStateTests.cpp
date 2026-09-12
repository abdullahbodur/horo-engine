#include "Horo/WorldStreaming/WorldLayerState.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <array>
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
                    .owner = runtimeControlled ? GameplayOwner() : StreamingLayerOwner()};
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

        void RequireIdempotentCancellation(WorldLayerStateRecord &record, const WorldLayerState rollbackState) {
            Advance(record, WorldLayerStateTransition::Cancel, WorldLayerStateAuthorityState::Cancelling);
            REQUIRE(record.state == rollbackState);
            const auto rollback = record;
            Advance(record, WorldLayerStateTransition::Cancel, WorldLayerStateAuthorityState::Cancelling);
            REQUIRE(record == rollback);

            auto exhausted = record;
            exhausted.revision = IdentityFrom<WorldLayerStateRevision>(std::numeric_limits<std::uint64_t>::max());
            const WorldLayerStateTransitionRequest request{.expected = exhausted.Fence(), .transition = WorldLayerStateTransition::Cancel};
            const auto result = AdvanceWorldLayerState(exhausted, request, WorldLayerStateAuthorityState::Cancelling);
            REQUIRE(result.HasValue());
            REQUIRE(result.Value() == exhausted);
        }

        enum class ExpectedResult : std::uint8_t {
            Success,
            InvalidTransition,
            LifecycleUnavailable
        };

        struct ExpectedTransition final {
            ExpectedResult result{ExpectedResult::InvalidTransition};
            WorldLayerState state{};
            WorldLayerStateRollbackDisposition disposition{};
            bool advancesRevision{};
        };

        struct StateCase final {
            WorldLayerState state{};
            WorldLayerStateRollbackDisposition disposition{};
        };

        constexpr std::array kStateCases{
            StateCase{WorldLayerState::Unloaded, WorldLayerStateRollbackDisposition::None},
            StateCase{WorldLayerState::Loading, WorldLayerStateRollbackDisposition::None},
            StateCase{WorldLayerState::Loaded, WorldLayerStateRollbackDisposition::None},
            StateCase{WorldLayerState::Activating, WorldLayerStateRollbackDisposition::None},
            StateCase{WorldLayerState::Activated, WorldLayerStateRollbackDisposition::None},
            StateCase{WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::None},
            StateCase{WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::CancellationPending},
            StateCase{WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::FailurePending},
            StateCase{WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::None},
            StateCase{WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::CancellationPending},
            StateCase{WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::FailurePending},
            StateCase{WorldLayerState::Failed, WorldLayerStateRollbackDisposition::None},
        };
        constexpr std::array kTransitions{
            WorldLayerStateTransition::BeginLoad,
            WorldLayerStateTransition::CompleteLoad,
            WorldLayerStateTransition::BeginActivation,
            WorldLayerStateTransition::CompleteActivation,
            WorldLayerStateTransition::BeginDeactivation,
            WorldLayerStateTransition::CompleteDeactivation,
            WorldLayerStateTransition::BeginUnload,
            WorldLayerStateTransition::CompleteUnload,
            WorldLayerStateTransition::Cancel,
            WorldLayerStateTransition::Fail,
        };
        constexpr std::array kAuthorityStates{WorldLayerStateAuthorityState::Active, WorldLayerStateAuthorityState::Cancelling,
                                              WorldLayerStateAuthorityState::Closed};

        [[nodiscard]] constexpr bool IsDrainCommand(const WorldLayerStateTransition transition) {
            return transition == WorldLayerStateTransition::BeginDeactivation ||
                   transition == WorldLayerStateTransition::CompleteDeactivation || transition == WorldLayerStateTransition::BeginUnload ||
                   transition == WorldLayerStateTransition::CompleteUnload || transition == WorldLayerStateTransition::Cancel ||
                   transition == WorldLayerStateTransition::Fail;
        }

        [[nodiscard]] constexpr ExpectedTransition Success(const WorldLayerState state,
                                                           const WorldLayerStateRollbackDisposition disposition,
                                                           const bool advancesRevision = true) {
            return {ExpectedResult::Success, state, disposition, advancesRevision};
        }

        [[nodiscard]] ExpectedTransition ExpectedOrderedTransition(const WorldLayerStateRecord &current,
                                                                   const WorldLayerStateTransition transition) {
            using enum WorldLayerState;
            using enum WorldLayerStateRollbackDisposition;
            using enum WorldLayerStateTransition;
            switch (transition) {
                case BeginLoad:
                    if (current.state == Unloaded || current.state == Failed)
                        return Success(Loading, None);
                    break;
                case CompleteLoad:
                    if (current.state == Loading)
                        return Success(Loaded, None);
                    break;
                case BeginActivation:
                    if (current.state == Loaded)
                        return Success(Activating, None);
                    break;
                case CompleteActivation:
                    if (current.state == Activating)
                        return Success(Activated, None);
                    break;
                case BeginDeactivation:
                    if (current.state == Activated)
                        return Success(Deactivating, None);
                    break;
                case CompleteDeactivation:
                    if (current.state == Deactivating && current.rollbackDisposition == FailurePending)
                        return Success(Unloading, FailurePending);
                    if (current.state == Deactivating)
                        return Success(Loaded, None);
                    break;
                case BeginUnload:
                    if (current.state == Loaded)
                        return Success(Unloading, None);
                    break;
                case CompleteUnload:
                    if (current.state == Unloading && current.rollbackDisposition == FailurePending)
                        return Success(Failed, None);
                    if (current.state == Unloading)
                        return Success(Unloaded, None);
                    break;
                case Cancel:
                case Fail:
                    break;
            }
            return {};
        }

        [[nodiscard]] ExpectedTransition ExpectedSignalTransition(const WorldLayerStateRecord &current,
                                                                  const WorldLayerStateTransition transition) {
            using enum WorldLayerState;
            using enum WorldLayerStateRollbackDisposition;
            if (transition == WorldLayerStateTransition::Cancel) {
                if (current.state == Loading)
                    return Success(Unloading, CancellationPending);
                if (current.state == Activating)
                    return Success(Deactivating, CancellationPending);
                if (current.state == Deactivating || current.state == Unloading)
                    return Success(current.state, current.rollbackDisposition, false);
                return {};
            }
            if (current.state == Loading)
                return Success(Unloading, FailurePending);
            if (current.state == Activating)
                return Success(Deactivating, FailurePending);
            if (current.state == Deactivating || current.state == Unloading)
                return Success(current.state, FailurePending, current.rollbackDisposition != FailurePending);
            return {};
        }

        [[nodiscard]] ExpectedTransition ExpectedFor(const WorldLayerStateRecord &current, const WorldLayerStateTransition transition,
                                                     const WorldLayerStateAuthorityState authorityState) {
            if (authorityState == WorldLayerStateAuthorityState::Closed ||
                (authorityState == WorldLayerStateAuthorityState::Cancelling && !IsDrainCommand(transition))) {
                return {.result = ExpectedResult::LifecycleUnavailable};
            }
            if (transition == WorldLayerStateTransition::Cancel || transition == WorldLayerStateTransition::Fail)
                return ExpectedSignalTransition(current, transition);
            return ExpectedOrderedTransition(current, transition);
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
            for (const auto authority : {WorldLayerStateAuthorityState::Active, WorldLayerStateAuthorityState::Cancelling,
                                         WorldLayerStateAuthorityState::Closed}) {
                RequireError(AdvanceWorldLayerState(record, unsupported, authority), WorldStreamingErrors::LayerStateUnsupported);
            }
        }

        TEST_CASE("Every valid layer state command and lifecycle combination has an explicit outcome",
                  "[unit][world_streaming][layer_state][matrix]") {
            for (const auto stateCase : kStateCases) {
                for (const auto transition : kTransitions) {
                    for (const auto authorityState : kAuthorityStates) {
                        auto current = Record();
                        current.state = stateCase.state;
                        current.rollbackDisposition = stateCase.disposition;
                        const auto expected = ExpectedFor(current, transition, authorityState);
                        const WorldLayerStateTransitionRequest request{.expected = current.Fence(), .transition = transition};
                        const auto result = AdvanceWorldLayerState(current, request, authorityState);
                        CAPTURE(stateCase.state, stateCase.disposition, transition, authorityState);

                        if (expected.result == ExpectedResult::LifecycleUnavailable) {
                            RequireError(result, WorldStreamingErrors::LayerStateLifecycleUnavailable);
                        } else if (expected.result == ExpectedResult::InvalidTransition) {
                            RequireError(result, WorldStreamingErrors::LayerStateTransitionInvalid);
                        } else {
                            REQUIRE(result.HasValue());
                            REQUIRE(result.Value().state == expected.state);
                            REQUIRE(result.Value().rollbackDisposition == expected.disposition);
                            if (expected.advancesRevision)
                                REQUIRE(result.Value().revision.Value() == current.revision.Value() + 1);
                            else
                                REQUIRE(result.Value() == current);
                        }
                    }
                }
            }
        }

        TEST_CASE("Cancellation rolls in-flight work back to the last completed layer state",
                  "[unit][world_streaming][layer_state][cancellation]") {
            auto loading = Record();
            Advance(loading, WorldLayerStateTransition::BeginLoad);
            RequireIdempotentCancellation(loading, WorldLayerState::Unloading);
            REQUIRE(loading.rollbackDisposition == WorldLayerStateRollbackDisposition::CancellationPending);
            Advance(loading, WorldLayerStateTransition::CompleteUnload, WorldLayerStateAuthorityState::Cancelling);
            REQUIRE(loading.state == WorldLayerState::Unloaded);
            REQUIRE(loading.rollbackDisposition == WorldLayerStateRollbackDisposition::None);

            auto activating = Record();
            Advance(activating, WorldLayerStateTransition::BeginLoad);
            Advance(activating, WorldLayerStateTransition::CompleteLoad);
            Advance(activating, WorldLayerStateTransition::BeginActivation);
            RequireIdempotentCancellation(activating, WorldLayerState::Deactivating);
            REQUIRE(activating.rollbackDisposition == WorldLayerStateRollbackDisposition::CancellationPending);
            Advance(activating, WorldLayerStateTransition::CompleteDeactivation, WorldLayerStateAuthorityState::Cancelling);
            REQUIRE(activating.state == WorldLayerState::Loaded);
            REQUIRE(activating.rollbackDisposition == WorldLayerStateRollbackDisposition::None);

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
            REQUIRE(record.state == WorldLayerState::Unloading);
            REQUIRE(record.rollbackDisposition == WorldLayerStateRollbackDisposition::FailurePending);
            Advance(record, WorldLayerStateTransition::CompleteUnload);
            REQUIRE(record.state == WorldLayerState::Failed);
            REQUIRE(record.rollbackDisposition == WorldLayerStateRollbackDisposition::None);

            auto activationFailure = Record();
            Advance(activationFailure, WorldLayerStateTransition::BeginLoad);
            Advance(activationFailure, WorldLayerStateTransition::CompleteLoad);
            Advance(activationFailure, WorldLayerStateTransition::BeginActivation);
            Advance(activationFailure, WorldLayerStateTransition::Fail);
            REQUIRE(activationFailure.state == WorldLayerState::Deactivating);
            REQUIRE(activationFailure.rollbackDisposition == WorldLayerStateRollbackDisposition::FailurePending);
            const auto replacement = Ownership(2);
            RequireError(ReplaceWorldLayerStateOwnership(activationFailure, replacement, activationFailure.Fence(),
                                                         WorldLayerStateAuthorityState::Active),
                         WorldStreamingErrors::LayerStateTransitionInvalid);
            Advance(activationFailure, WorldLayerStateTransition::CompleteDeactivation);
            REQUIRE(activationFailure.state == WorldLayerState::Unloading);
            REQUIRE(activationFailure.rollbackDisposition == WorldLayerStateRollbackDisposition::FailurePending);
            RequireError(ReplaceWorldLayerStateOwnership(activationFailure, replacement, activationFailure.Fence(),
                                                         WorldLayerStateAuthorityState::Active),
                         WorldStreamingErrors::LayerStateTransitionInvalid);
            Advance(activationFailure, WorldLayerStateTransition::CompleteUnload);
            REQUIRE(activationFailure.state == WorldLayerState::Failed);
            const auto recovered = ReplaceWorldLayerStateOwnership(activationFailure, replacement, activationFailure.Fence(),
                                                                   WorldLayerStateAuthorityState::Active);
            REQUIRE(recovered.HasValue());
            REQUIRE(recovered.Value().state == WorldLayerState::Unloaded);

            record.revision = IdentityFrom<WorldLayerStateRevision>(std::numeric_limits<std::uint64_t>::max());
            const WorldLayerStateTransitionRequest exhausted{.expected = record.Fence(),
                                                             .transition = WorldLayerStateTransition::BeginLoad};
            RequireError(AdvanceWorldLayerState(record, exhausted, WorldLayerStateAuthorityState::Active),
                         WorldStreamingErrors::GenerationExhausted);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
