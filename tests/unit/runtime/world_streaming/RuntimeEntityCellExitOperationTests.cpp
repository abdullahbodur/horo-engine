#include "Horo/WorldStreaming/RuntimeEntityCellExitOperation.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::Asset;
        using TestSupport::IdentityFrom;
        using TestSupport::Layer;
        using TestSupport::RequireError;
        using TestSupport::World;

        constexpr auto InterruptionCases = std::to_array<std::pair<RuntimeEntityCellExitTransition, RuntimeEntityCellExitOutcome>>({
            {RuntimeEntityCellExitTransition::Cancel, RuntimeEntityCellExitOutcome::Cancelled},
            {RuntimeEntityCellExitTransition::Fail, RuntimeEntityCellExitOutcome::Failed},
            {RuntimeEntityCellExitTransition::Replace, RuntimeEntityCellExitOutcome::Replaced},
            {RuntimeEntityCellExitTransition::Shutdown, RuntimeEntityCellExitOutcome::Shutdown},
        });

        [[nodiscard]] StreamingRuntimeOwnerToken WorldOwner(const std::uint64_t epoch = 3) {
            return {
                .partition = World(),
                .epoch = IdentityFrom<PartitionEpoch>(epoch),
                .owner = IdentityFrom<StreamingRuntimeOwnerId>(9),
            };
        }

        [[nodiscard]] StreamingFence SourceCell(const std::uint64_t generation = 4, const std::uint64_t epoch = 3) {
            return {
                .partition = World(),
                .epoch = IdentityFrom<PartitionEpoch>(epoch),
                .cell = {1, -2, 3, 0, Layer()},
                .generation = IdentityFrom<StreamingGeneration>(generation),
            };
        }

        [[nodiscard]] WorldObjectOwnerBinding CellOwner(const std::uint64_t generation = 4) {
            return {.world = WorldOwner(), .kind = WorldObjectOwnerKind::Cell, .cell = SourceCell(generation)};
        }

        [[nodiscard]] WorldObjectOwnerBinding RuntimeOwner(const std::uint64_t generation = 7) {
            return {
                .world = WorldOwner(),
                .kind = WorldObjectOwnerKind::Runtime,
                .runtimeOwner = IdentityFrom<WorldObjectRuntimeOwnerId>(12),
                .runtimeGeneration = IdentityFrom<WorldObjectRuntimeOwnerGeneration>(generation),
            };
        }

        [[nodiscard]] WorldObjectOwnerBinding PersistentWorldOwner() {
            return {.world = WorldOwner(), .kind = WorldObjectOwnerKind::World};
        }

        [[nodiscard]] WorldObjectOwnershipDescriptor Ownership(const WorldObjectCellExitPolicy policy, const std::uint64_t revision = 1) {
            return {
                .objectClass = WorldObjectOwnershipClass::RuntimeSpawned,
                .runtimeSpawned = IdentityFrom<RuntimeSpawnedObjectId>(22),
                .revision = IdentityFrom<WorldObjectOwnershipRevision>(revision),
                .owner = CellOwner(),
                .cellExitPolicy = policy,
            };
        }

        [[nodiscard]] WorldObjectOwnershipDescriptor Destination(const std::uint64_t revision = 2) {
            return {
                .objectClass = WorldObjectOwnershipClass::RuntimeSpawned,
                .runtimeSpawned = IdentityFrom<RuntimeSpawnedObjectId>(22),
                .revision = IdentityFrom<WorldObjectOwnershipRevision>(revision),
                .owner = RuntimeOwner(),
                .cellExitPolicy = WorldObjectCellExitPolicy::NotApplicable,
            };
        }

        [[nodiscard]] RuntimeEntityCellExitHandle Handle(const std::uint64_t operation = 5, const std::uint64_t cellGeneration = 4) {
            return {
                .operation = IdentityFrom<RuntimeEntityCellExitOperationId>(operation),
                .entity = IdentityFrom<RuntimeSpawnedObjectId>(22),
                .source = SourceCell(cellGeneration),
            };
        }

        [[nodiscard]] RuntimeEntityCellExitAdmissionContext Context(const WorldObjectOwnershipDescriptor &current) {
            return {
                .expectedWorld = WorldOwner(),
                .retiringCell = SourceCell(),
                .currentOwnership = current,
                .inFlightOperations = 0,
                .operationCapacity = 2,
                .state = WorldObjectOwnershipOwnerState::Active,
            };
        }

        [[nodiscard]] RuntimeEntityCellExitRequest RetireRequest() {
            return {.handle = Handle(), .sourceOwnership = Ownership(WorldObjectCellExitPolicy::Retire)};
        }

        [[nodiscard]] RuntimeEntityCellExitRequest HandoffRequest() {
            return {
                .handle = Handle(),
                .sourceOwnership = Ownership(WorldObjectCellExitPolicy::RequireHandoff),
                .destination = Destination(),
            };
        }

        [[nodiscard]] RuntimeEntityCellExitOperation Advance(RuntimeEntityCellExitOperation operation,
                                                             const RuntimeEntityCellExitTransition transition) {
            return operation.Advance(operation.Handle(), transition).Value();
        }

        [[nodiscard]] RuntimeEntityCellExitOperation AdmittedHandoff() {
            const auto request = HandoffRequest();
            auto operation = RuntimeEntityCellExitOperation::Create(request, Context(request.sourceOwnership)).Value();
            return Advance(std::move(operation), RuntimeEntityCellExitTransition::Admit);
        }

        [[nodiscard]] RuntimeEntityCellExitOperation PreparedHandoff() {
            return Advance(AdmittedHandoff(), RuntimeEntityCellExitTransition::BeginDestinationPreparation);
        }

        [[nodiscard]] RuntimeEntityCellExitOperation AcceptedHandoff() {
            return Advance(PreparedHandoff(), RuntimeEntityCellExitTransition::AcceptDestination);
        }

        TEST_CASE("Runtime entity retires only through the exact source-cell transaction",
                  "[unit][world_streaming][runtime_entity_cell_exit][retire]") {
            const auto request = RetireRequest();
            auto operation = RuntimeEntityCellExitOperation::Create(request, Context(request.sourceOwnership)).Value();
            REQUIRE(operation.Disposition() == RuntimeEntityCellExitDisposition::Retire);
            REQUIRE(operation.State() == RuntimeEntityCellExitState::Queued);
            REQUIRE_FALSE(operation.DestinationOwnership().has_value());
            REQUIRE(operation.SourceOwnership() == request.sourceOwnership);

            operation = Advance(std::move(operation), RuntimeEntityCellExitTransition::Admit);
            operation = Advance(std::move(operation), RuntimeEntityCellExitTransition::BeginSourceRetirement);
            REQUIRE(operation.State() == RuntimeEntityCellExitState::RetiringSource);
            REQUIRE(operation.Outcome() == RuntimeEntityCellExitOutcome::Retired);
            REQUIRE(operation.IsCommitted());
            operation = Advance(std::move(operation), RuntimeEntityCellExitTransition::AcknowledgeSourceRetirement);
            REQUIRE(operation.IsTerminal());
            REQUIRE(operation.IsCommitted());
            REQUIRE(operation.Outcome() == RuntimeEntityCellExitOutcome::Retired);
        }

        TEST_CASE("Required handoff accepts an exact ownership successor before source retirement",
                  "[unit][world_streaming][runtime_entity_cell_exit][handoff]") {
            auto operation = AcceptedHandoff();
            REQUIRE(operation.Disposition() == RuntimeEntityCellExitDisposition::Handoff);
            REQUIRE(operation.DestinationOwnership() == HandoffRequest().destination);
            REQUIRE(operation.State() == RuntimeEntityCellExitState::DestinationAccepted);
            REQUIRE_FALSE(operation.IsCommitted());
            operation = Advance(std::move(operation), RuntimeEntityCellExitTransition::BeginSourceRetirement);
            REQUIRE(operation.State() == RuntimeEntityCellExitState::RetiringSource);
            REQUIRE(operation.Outcome() == RuntimeEntityCellExitOutcome::HandedOff);
            REQUIRE(operation.IsCommitted());
            operation = Advance(std::move(operation), RuntimeEntityCellExitTransition::AcknowledgeSourceRetirement);
            REQUIRE(operation.IsTerminal());
            REQUIRE(operation.Outcome() == RuntimeEntityCellExitOutcome::HandedOff);
        }

        TEST_CASE("Runtime entity may survive cell unload under an explicit world owner",
                  "[unit][world_streaming][runtime_entity_cell_exit][survive]") {
            auto request = HandoffRequest();
            request.destination->owner = PersistentWorldOwner();
            const auto operation = RuntimeEntityCellExitOperation::Create(request, Context(request.sourceOwnership)).Value();
            REQUIRE(operation.Disposition() == RuntimeEntityCellExitDisposition::Handoff);
            REQUIRE(operation.DestinationOwnership()->owner.kind == WorldObjectOwnerKind::World);
        }

        TEST_CASE("Cell-exit creation rejects stale entity revision and source-cell generations",
                  "[unit][world_streaming][runtime_entity_cell_exit][stale]") {
            auto request = RetireRequest();
            auto context = Context(request.sourceOwnership);
            context.currentOwnership.revision = IdentityFrom<WorldObjectOwnershipRevision>(2);
            RequireError(RuntimeEntityCellExitOperation::Create(request, context), WorldStreamingErrors::RuntimeEntityCellExitStale);

            context = Context(request.sourceOwnership);
            context.retiringCell = SourceCell(5);
            RequireError(RuntimeEntityCellExitOperation::Create(request, context), WorldStreamingErrors::RuntimeEntityCellExitStale);

            context = Context(request.sourceOwnership);
            request.handle.entity = IdentityFrom<RuntimeSpawnedObjectId>(23);
            RequireError(RuntimeEntityCellExitOperation::Create(request, context), WorldStreamingErrors::RuntimeEntityCellExitStale);

            request = HandoffRequest();
            context = Context(*request.destination);
            RequireError(RuntimeEntityCellExitOperation::Create(request, context), WorldStreamingErrors::RuntimeEntityCellExitStale);
        }

        TEST_CASE("Cell-exit creation rejects malformed and non-runtime source authority",
                  "[unit][world_streaming][runtime_entity_cell_exit][validation]") {
            auto request = RetireRequest();
            auto context = Context(request.sourceOwnership);
            request.sourceOwnership.revision = {};
            RequireError(RuntimeEntityCellExitOperation::Create(request, context), WorldStreamingErrors::RuntimeEntityCellExitInvalid);

            request = RetireRequest();
            context = Context(request.sourceOwnership);
            context.currentOwnership.owner = {};
            RequireError(RuntimeEntityCellExitOperation::Create(request, context), WorldStreamingErrors::RuntimeEntityCellExitInvalid);

            request = RetireRequest();
            request.sourceOwnership.objectClass = WorldObjectOwnershipClass::AuthoredSpatial;
            request.sourceOwnership.authored = {
                .page = Asset(1),
                .object = 3,
            };
            request.sourceOwnership.runtimeSpawned = {};
            context = Context(request.sourceOwnership);
            RequireError(RuntimeEntityCellExitOperation::Create(request, context), WorldStreamingErrors::RuntimeEntityCellExitUnsupported);
        }

        TEST_CASE("Cell-exit admission enforces structural lifecycle and in-flight capacity bounds",
                  "[unit][world_streaming][runtime_entity_cell_exit][admission]") {
            const auto request = RetireRequest();
            auto context = Context(request.sourceOwnership);
            context.operationCapacity = 0;
            RequireError(RuntimeEntityCellExitOperation::Create(request, context), WorldStreamingErrors::RuntimeEntityCellExitInvalid);

            context = Context(request.sourceOwnership);
            context.inFlightOperations = context.operationCapacity;
            RequireError(RuntimeEntityCellExitOperation::Create(request, context),
                         WorldStreamingErrors::RuntimeEntityCellExitCapacityExceeded);

            for (const auto state : {WorldObjectOwnershipOwnerState::Cancelling, WorldObjectOwnershipOwnerState::Closed}) {
                context = Context(request.sourceOwnership);
                context.state = state;
                RequireError(RuntimeEntityCellExitOperation::Create(request, context),
                             WorldStreamingErrors::RuntimeEntityCellExitLifecycleUnavailable);
            }
        }

        TEST_CASE("Retire and handoff policies cannot be silently exchanged", "[unit][world_streaming][runtime_entity_cell_exit][policy]") {
            auto retire = RetireRequest();
            retire.destination = Destination();
            RequireError(RuntimeEntityCellExitOperation::Create(retire, Context(retire.sourceOwnership)),
                         WorldStreamingErrors::RuntimeEntityCellExitUnsupported);

            auto handoff = HandoffRequest();
            handoff.destination.reset();
            RequireError(RuntimeEntityCellExitOperation::Create(handoff, Context(handoff.sourceOwnership)),
                         WorldStreamingErrors::RuntimeEntityCellExitUnsupported);

            handoff = HandoffRequest();
            handoff.destination->revision = IdentityFrom<WorldObjectOwnershipRevision>(3);
            RequireError(RuntimeEntityCellExitOperation::Create(handoff, Context(handoff.sourceOwnership)),
                         WorldStreamingErrors::ObjectOwnershipRevisionStale);

            handoff = HandoffRequest();
            handoff.destination->owner = CellOwner();
            handoff.destination->cellExitPolicy = WorldObjectCellExitPolicy::RequireHandoff;
            RequireError(RuntimeEntityCellExitOperation::Create(handoff, Context(handoff.sourceOwnership)),
                         WorldStreamingErrors::RuntimeEntityCellExitUnsupported);
        }

        TEST_CASE("Pre-commit interruption preserves the source and rolls back only prepared destination work",
                  "[unit][world_streaming][runtime_entity_cell_exit][rollback]") {
            for (const auto [transition, outcome] : InterruptionCases) {
                auto operation = PreparedHandoff();
                const auto uninterrupted = operation;
                operation = Advance(std::move(operation), transition);
                REQUIRE(operation.State() == RuntimeEntityCellExitState::RollingBackDestination);
                REQUIRE(operation.Outcome() == outcome);
                REQUIRE_FALSE(operation.IsCommitted());
                REQUIRE(uninterrupted.State() == RuntimeEntityCellExitState::PreparingDestination);
                operation = Advance(std::move(operation), RuntimeEntityCellExitTransition::AcknowledgeDestinationRollback);
                REQUIRE(operation.IsTerminal());
                REQUIRE(operation.Outcome() == outcome);
            }
        }

        TEST_CASE("Interruption before destination resources exist terminates without a false rollback barrier",
                  "[unit][world_streaming][runtime_entity_cell_exit][cancellation]") {
            const auto request = HandoffRequest();
            for (const auto [transition, outcome] : InterruptionCases) {
                auto queued = RuntimeEntityCellExitOperation::Create(request, Context(request.sourceOwnership)).Value();
                queued = Advance(std::move(queued), transition);
                REQUIRE(queued.IsTerminal());
                REQUIRE(queued.Outcome() == outcome);

                auto admitted = AdmittedHandoff();
                admitted = Advance(std::move(admitted), transition);
                REQUIRE(admitted.IsTerminal());
                REQUIRE(admitted.Outcome() == outcome);
            }
        }

        TEST_CASE("Accepted destination still rolls back on replacement before commit",
                  "[unit][world_streaming][runtime_entity_cell_exit][replacement]") {
            auto operation = AcceptedHandoff();
            operation = Advance(std::move(operation), RuntimeEntityCellExitTransition::Replace);
            REQUIRE(operation.State() == RuntimeEntityCellExitState::RollingBackDestination);
            REQUIRE(operation.Outcome() == RuntimeEntityCellExitOutcome::Replaced);
            REQUIRE_FALSE(operation.IsCommitted());
        }

        TEST_CASE("Shutdown is idempotent while an interrupted destination rolls back",
                  "[unit][world_streaming][runtime_entity_cell_exit][rollback][shutdown]") {
            auto operation = PreparedHandoff();
            operation = Advance(std::move(operation), RuntimeEntityCellExitTransition::Fail);
            operation = Advance(std::move(operation), RuntimeEntityCellExitTransition::Shutdown);
            REQUIRE(operation.State() == RuntimeEntityCellExitState::RollingBackDestination);
            REQUIRE(operation.Outcome() == RuntimeEntityCellExitOutcome::Failed);
            operation = Advance(std::move(operation), RuntimeEntityCellExitTransition::AcknowledgeDestinationRollback);
            REQUIRE(operation.IsTerminal());
            REQUIRE(operation.Outcome() == RuntimeEntityCellExitOutcome::Failed);
        }

        TEST_CASE("Shutdown drains a committed handoff without changing its canonical outcome",
                  "[unit][world_streaming][runtime_entity_cell_exit][shutdown]") {
            auto operation = AcceptedHandoff();
            operation = Advance(std::move(operation), RuntimeEntityCellExitTransition::BeginSourceRetirement);
            operation = Advance(std::move(operation), RuntimeEntityCellExitTransition::Shutdown);
            REQUIRE(operation.State() == RuntimeEntityCellExitState::RetiringSource);
            REQUIRE(operation.Outcome() == RuntimeEntityCellExitOutcome::HandedOff);
            RequireError(operation.Advance(operation.Handle(), RuntimeEntityCellExitTransition::Cancel),
                         WorldStreamingErrors::RuntimeEntityCellExitTransitionInvalid);
            operation = Advance(std::move(operation), RuntimeEntityCellExitTransition::AcknowledgeSourceRetirement);
            REQUIRE(operation.IsTerminal());
            REQUIRE(operation.Outcome() == RuntimeEntityCellExitOutcome::HandedOff);
        }

        TEST_CASE("Cell-exit commands reject malformed stale unsupported and skipped transitions without mutation",
                  "[unit][world_streaming][runtime_entity_cell_exit]") {
            const auto request = HandoffRequest();
            const auto operation = RuntimeEntityCellExitOperation::Create(request, Context(request.sourceOwnership)).Value();
            RequireError(RuntimeEntityCellExitOperation::Create({}, {}), WorldStreamingErrors::RuntimeEntityCellExitInvalid);
            RequireError(operation.Advance({}, RuntimeEntityCellExitTransition::Admit), WorldStreamingErrors::RuntimeEntityCellExitInvalid);
            RequireError(operation.Advance(Handle(6), RuntimeEntityCellExitTransition::Admit),
                         WorldStreamingErrors::RuntimeEntityCellExitStale);
            RequireError(operation.Advance(operation.Handle(), static_cast<RuntimeEntityCellExitTransition>(255)),
                         WorldStreamingErrors::RuntimeEntityCellExitUnsupported);
            RequireError(operation.Advance(operation.Handle(), RuntimeEntityCellExitTransition::AcceptDestination),
                         WorldStreamingErrors::RuntimeEntityCellExitTransitionInvalid);
            const auto retireRequest = RetireRequest();
            auto retirement = RuntimeEntityCellExitOperation::Create(retireRequest, Context(retireRequest.sourceOwnership)).Value();
            retirement = Advance(std::move(retirement), RuntimeEntityCellExitTransition::Admit);
            RequireError(retirement.Advance(retirement.Handle(), RuntimeEntityCellExitTransition::BeginDestinationPreparation),
                         WorldStreamingErrors::RuntimeEntityCellExitTransitionInvalid);
            REQUIRE(operation.State() == RuntimeEntityCellExitState::Queued);
            REQUIRE(operation.Outcome() == RuntimeEntityCellExitOutcome::None);
            REQUIRE_FALSE(operation.IsCommitted());
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
