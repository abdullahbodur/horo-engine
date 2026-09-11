#include "Horo/WorldStreaming/WorldObjectOwnership.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <type_traits>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::Asset;
        using TestSupport::IdentityFrom;
        using TestSupport::Layer;
        using TestSupport::RequireError;
        using TestSupport::World;

        StreamingRuntimeOwnerToken WorldOwner(const std::uint64_t owner = 5, const std::uint64_t epoch = 1) {
            return {.partition = World(),
                    .epoch = IdentityFrom<PartitionEpoch>(epoch),
                    .owner = IdentityFrom<StreamingRuntimeOwnerId>(owner)};
        }

        StreamingFence Cell(const std::uint64_t generation = 1) {
            return {.partition = World(),
                    .epoch = IdentityFrom<PartitionEpoch>(1),
                    .cell = {1, 2, 3, 0, Layer()},
                    .generation = IdentityFrom<StreamingGeneration>(generation)};
        }

        WorldObjectOwnerBinding WorldBinding() {
            return {.world = WorldOwner(), .kind = WorldObjectOwnerKind::World};
        }

        WorldObjectOwnerBinding CellBinding(const std::uint64_t generation = 1) {
            return {.world = WorldOwner(), .kind = WorldObjectOwnerKind::Cell, .cell = Cell(generation)};
        }

        WorldObjectOwnerBinding RuntimeBinding(const std::uint64_t generation = 1) {
            return {.world = WorldOwner(),
                    .kind = WorldObjectOwnerKind::Runtime,
                    .runtimeOwner = IdentityFrom<WorldObjectRuntimeOwnerId>(9),
                    .runtimeGeneration = IdentityFrom<WorldObjectRuntimeOwnerGeneration>(generation)};
        }

        WorldObjectOwnershipDescriptor Authored(const WorldObjectOwnershipClass objectClass, const WorldObjectOwnerBinding &owner,
                                                const WorldObjectCellExitPolicy policy, const std::uint64_t revision = 1) {
            return {.objectClass = objectClass,
                    .authored = {Asset(3), 7},
                    .revision = IdentityFrom<WorldObjectOwnershipRevision>(revision),
                    .owner = owner,
                    .cellExitPolicy = policy};
        }

        WorldObjectOwnershipDescriptor RuntimeObject(const WorldObjectOwnerBinding &owner, const WorldObjectCellExitPolicy policy,
                                                     const std::uint64_t revision = 1) {
            return {.objectClass = WorldObjectOwnershipClass::RuntimeSpawned,
                    .runtimeSpawned = IdentityFrom<RuntimeSpawnedObjectId>(77),
                    .revision = IdentityFrom<WorldObjectOwnershipRevision>(revision),
                    .owner = owner,
                    .cellExitPolicy = policy};
        }

        WorldObjectOwnershipAdmissionContext Context() {
            return {.expectedWorld = WorldOwner(),
                    .current = std::nullopt,
                    .objectCount = 2,
                    .objectCapacity = 4,
                    .state = WorldObjectOwnershipOwnerState::Active};
        }

        TEST_CASE("Object ownership distinguishes authored always-present spatial and runtime-spawned policy",
                  "[unit][world_streaming][object_ownership]") {
            const auto alwaysPresent =
                Authored(WorldObjectOwnershipClass::AuthoredAlwaysPresent, WorldBinding(), WorldObjectCellExitPolicy::NotApplicable);
            const auto spatial = Authored(WorldObjectOwnershipClass::AuthoredSpatial, CellBinding(), WorldObjectCellExitPolicy::Retire);
            const auto dynamic = RuntimeObject(RuntimeBinding(), WorldObjectCellExitPolicy::NotApplicable);

            REQUIRE(ValidateWorldObjectOwnershipDescriptor(alwaysPresent).HasValue());
            REQUIRE(ValidateWorldObjectOwnershipDescriptor(spatial).HasValue());
            REQUIRE(ValidateWorldObjectOwnershipDescriptor(dynamic).HasValue());
            REQUIRE(ValidateWorldObjectOwnershipAdmission({alwaysPresent, std::nullopt}, Context()).Value() ==
                    WorldObjectOwnershipAdmissionKind::Insert);
            static_assert(std::is_trivially_copyable_v<WorldObjectOwnershipDescriptor>);
        }

        TEST_CASE("Runtime-spawned cell ownership requires explicit retirement or handoff",
                  "[unit][world_streaming][object_ownership][policy]") {
            REQUIRE(ValidateWorldObjectOwnershipDescriptor(RuntimeObject(CellBinding(), WorldObjectCellExitPolicy::Retire)).HasValue());
            REQUIRE(
                ValidateWorldObjectOwnershipDescriptor(RuntimeObject(CellBinding(), WorldObjectCellExitPolicy::RequireHandoff)).HasValue());

            auto invalid = RuntimeObject(CellBinding(), WorldObjectCellExitPolicy::NotApplicable);
            RequireError(ValidateWorldObjectOwnershipDescriptor(invalid), WorldStreamingErrors::ObjectOwnershipUnsupported);
            invalid = RuntimeObject(RuntimeBinding(), WorldObjectCellExitPolicy::RequireHandoff);
            RequireError(ValidateWorldObjectOwnershipDescriptor(invalid), WorldStreamingErrors::ObjectOwnershipUnsupported);
        }

        TEST_CASE("Authored ownership cannot silently change its placement authority",
                  "[unit][world_streaming][object_ownership][policy]") {
            RequireError(ValidateWorldObjectOwnershipDescriptor(
                             Authored(WorldObjectOwnershipClass::AuthoredAlwaysPresent, CellBinding(), WorldObjectCellExitPolicy::Retire)),
                         WorldStreamingErrors::ObjectOwnershipUnsupported);
            RequireError(ValidateWorldObjectOwnershipDescriptor(Authored(WorldObjectOwnershipClass::AuthoredSpatial, WorldBinding(),
                                                                         WorldObjectCellExitPolicy::NotApplicable)),
                         WorldStreamingErrors::ObjectOwnershipUnsupported);
            RequireError(ValidateWorldObjectOwnershipDescriptor(Authored(WorldObjectOwnershipClass::AuthoredSpatial, CellBinding(),
                                                                         WorldObjectCellExitPolicy::RequireHandoff)),
                         WorldStreamingErrors::ObjectOwnershipUnsupported);
        }

        TEST_CASE("Runtime-spawned ownership handoff is exact revision fenced", "[unit][world_streaming][object_ownership][handoff]") {
            auto context = Context();
            context.current = RuntimeObject(CellBinding(), WorldObjectCellExitPolicy::RequireHandoff, 4);
            const WorldObjectOwnershipRequest request{RuntimeObject(RuntimeBinding(), WorldObjectCellExitPolicy::NotApplicable, 5),
                                                      IdentityFrom<WorldObjectOwnershipRevision>(4)};

            REQUIRE(ValidateWorldObjectOwnershipAdmission(request, context).Value() == WorldObjectOwnershipAdmissionKind::Handoff);
            CHECK(context.current->owner.kind == WorldObjectOwnerKind::Cell);

            auto stale = request;
            stale.expectedRevision = IdentityFrom<WorldObjectOwnershipRevision>(3);
            RequireError(ValidateWorldObjectOwnershipAdmission(stale, context), WorldStreamingErrors::ObjectOwnershipRevisionStale);
            stale = request;
            stale.candidate.revision = IdentityFrom<WorldObjectOwnershipRevision>(6);
            RequireError(ValidateWorldObjectOwnershipAdmission(stale, context), WorldStreamingErrors::ObjectOwnershipRevisionStale);

            context.current = RuntimeObject(CellBinding(), WorldObjectCellExitPolicy::Retire, 4);
            RequireError(ValidateWorldObjectOwnershipAdmission(request, context), WorldStreamingErrors::ObjectOwnershipUnsupported);
        }

        TEST_CASE("Same-owner update is replacement and authored owner binding remains stable",
                  "[unit][world_streaming][object_ownership][replacement]") {
            auto context = Context();
            context.current = Authored(WorldObjectOwnershipClass::AuthoredSpatial, CellBinding(1), WorldObjectCellExitPolicy::Retire, 8);
            const WorldObjectOwnershipRequest replacement{Authored(WorldObjectOwnershipClass::AuthoredSpatial, CellBinding(1),
                                                                   WorldObjectCellExitPolicy::Retire, 9),
                                                          IdentityFrom<WorldObjectOwnershipRevision>(8)};
            REQUIRE(ValidateWorldObjectOwnershipAdmission(replacement, context).Value() == WorldObjectOwnershipAdmissionKind::Replace);

            auto changedOwner = replacement;
            changedOwner.candidate.owner = CellBinding(2);
            RequireError(ValidateWorldObjectOwnershipAdmission(changedOwner, context), WorldStreamingErrors::ObjectOwnershipUnsupported);

            auto conflict = replacement;
            conflict.candidate.authored.object = 8;
            RequireError(ValidateWorldObjectOwnershipAdmission(conflict, context), WorldStreamingErrors::ObjectOwnershipIdentityConflict);
        }

        TEST_CASE("Stale world and cell generations cannot cross owner lifetimes", "[unit][world_streaming][object_ownership][fence]") {
            auto staleWorld = RuntimeObject(RuntimeBinding(), WorldObjectCellExitPolicy::NotApplicable);
            staleWorld.owner.world = WorldOwner(6);
            RequireError(ValidateWorldObjectOwnershipAdmission({staleWorld, std::nullopt}, Context()),
                         WorldStreamingErrors::ObjectOwnershipOwnerStale);

            auto malformedCell = CellBinding();
            malformedCell.cell.epoch = IdentityFrom<PartitionEpoch>(2);
            RequireError(ValidateWorldObjectOwnershipDescriptor(RuntimeObject(malformedCell, WorldObjectCellExitPolicy::Retire)),
                         WorldStreamingErrors::ObjectOwnershipInvalid);
        }

        TEST_CASE("Ownership admission enforces capacity cancellation and shutdown transactionally",
                  "[unit][world_streaming][object_ownership][lifecycle]") {
            const auto candidate = RuntimeObject(RuntimeBinding(), WorldObjectCellExitPolicy::NotApplicable);
            auto context = Context();
            context.objectCount = context.objectCapacity;
            RequireError(ValidateWorldObjectOwnershipAdmission({candidate, std::nullopt}, context),
                         WorldStreamingErrors::ObjectOwnershipCapacityExceeded);

            context = Context();
            RequireError(ValidateWorldObjectOwnershipAdmission({candidate, IdentityFrom<WorldObjectOwnershipRevision>(1)}, context),
                         WorldStreamingErrors::ObjectOwnershipRevisionStale);

            for (const auto state : {WorldObjectOwnershipOwnerState::Cancelling, WorldObjectOwnershipOwnerState::Closed}) {
                context = Context();
                context.state = state;
                RequireError(ValidateWorldObjectOwnershipAdmission({candidate, std::nullopt}, context),
                             WorldStreamingErrors::ObjectOwnershipLifecycleUnavailable);
                CHECK_FALSE(context.current.has_value());
            }

            context = Context();
            context.state = static_cast<WorldObjectOwnershipOwnerState>(255);
            RequireError(ValidateWorldObjectOwnershipAdmission({candidate, std::nullopt}, context),
                         WorldStreamingErrors::ObjectOwnershipInvalid);

            context = Context();
            context.objectCount = context.objectCapacity + 1;
            RequireError(ValidateWorldObjectOwnershipAdmission({candidate, std::nullopt}, context),
                         WorldStreamingErrors::ObjectOwnershipInvalid);
        }

        TEST_CASE("Ownership descriptors reject ambiguous identities unknown values and revision exhaustion",
                  "[unit][world_streaming][object_ownership][failure]") {
            auto descriptor = RuntimeObject(RuntimeBinding(), WorldObjectCellExitPolicy::NotApplicable);
            descriptor.authored = {Asset(3), 7};
            RequireError(ValidateWorldObjectOwnershipDescriptor(descriptor), WorldStreamingErrors::ObjectOwnershipInvalid);

            descriptor = RuntimeObject(RuntimeBinding(), WorldObjectCellExitPolicy::NotApplicable);
            descriptor.objectClass = static_cast<WorldObjectOwnershipClass>(255);
            RequireError(ValidateWorldObjectOwnershipDescriptor(descriptor), WorldStreamingErrors::ObjectOwnershipUnsupported);

            auto context = Context();
            constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
            context.current = RuntimeObject(RuntimeBinding(), WorldObjectCellExitPolicy::NotApplicable, maximum);
            const WorldObjectOwnershipRequest wrapped{RuntimeObject(RuntimeBinding(2), WorldObjectCellExitPolicy::NotApplicable, 1),
                                                      IdentityFrom<WorldObjectOwnershipRevision>(maximum)};
            RequireError(ValidateWorldObjectOwnershipAdmission(wrapped, context), WorldStreamingErrors::GenerationExhausted);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
