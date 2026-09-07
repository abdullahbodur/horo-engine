#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Runtime/Save/SaveErrors.h"
#include "Horo/Runtime/Save/SaveTestCompositions.h"
#include "SaveTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace {
    using namespace Horo;
    using namespace Horo::Runtime;

    SaveCompositionAddress Address(const std::uint8_t slotSuffix) {
        const SaveNamespaceId namespaceId{
            .product = Test::Id<ProductStorageId>(1),
            .environment = Test::Id<EnvironmentStorageId>(2),
            .owner = UserProfileOwner{.user = Test::Id<LocalUserStorageId>(3), .profile = Test::Id<GameProfileId>(4)},
        };
        return {.nameSpace = EncodeSaveNamespaceKey(namespaceId).Value(), .slot = Test::Id<SaveGameSlotId>(slotSuffix)};
    }

    std::vector<std::byte> Bytes(const std::initializer_list<std::uint8_t> values) {
        std::vector<std::byte> result;
        result.reserve(values.size());
        for (const auto value : values)
            result.push_back(static_cast<std::byte>(value));
        return result;
    }

    std::unique_ptr<DeterministicMockSaveComposition> Composition(const std::size_t operationCapacity = 16,
                                                                  const std::size_t objectCapacity = 4,
                                                                  const std::size_t byteCapacity = 32) {
        auto result = CreateDeterministicMockSaveComposition({operationCapacity, objectCapacity, byteCapacity});
        REQUIRE(result.HasValue());
        return std::move(result).Value();
    }

    SaveCompositionOperationSnapshot Snapshot(const DeterministicMockSaveComposition &composition,
                                              const SaveCompositionOperationId operation) {
        const auto snapshot = composition.Snapshot(operation);
        REQUIRE(snapshot.has_value());
        return *snapshot;
    }

    void RequireError(const SaveCompositionOperationSnapshot &snapshot, const ErrorCodeDescriptor &descriptor) {
        REQUIRE(snapshot.error.has_value());
        CHECK(snapshot.error->code.Value() == descriptor.code.Value());
        CHECK(snapshot.commit == SaveCompositionCommitOutcome::NotCommitted);
    }
}  // namespace

TEST_CASE("Null save composition reports unsupported without retaining state", "[unit][runtime][save]") {
    NullSaveComposition composition;
    CancellationToken cancellation;
    SaveCompositionRequest request{
        .kind = SaveCompositionOperationKind::Store,
        .address = Address(5),
        .bytes = Bytes({1, 2, 3}),
        .cancellation = cancellation,
    };

    CHECK(composition.Capability() == SaveCompositionCapability::Unsupported);
    const auto submitted = composition.Submit(request);
    REQUIRE(submitted.HasError());
    CHECK(submitted.ErrorValue().code.Value() == SaveErrors::CompositionUnsupported.code.Value());
    CHECK_FALSE(composition.AdvanceOne());
    CHECK_FALSE(composition.Snapshot({1}).has_value());
    CHECK(composition.StoredObjectCount() == 0);
}

TEST_CASE("Deterministic save composition validates finite admission bounds and requests", "[unit][runtime][save]") {
    for (const auto limits : {DeterministicSaveCompositionLimits{0, 1, 1}, DeterministicSaveCompositionLimits{1, 0, 1},
                              DeterministicSaveCompositionLimits{1, 1, 0}}) {
        const auto invalid = CreateDeterministicMockSaveComposition(limits);
        REQUIRE(invalid.HasError());
        CHECK(invalid.ErrorValue().code.Value() == SaveErrors::CompositionInvalid.code.Value());
    }

    auto composition = Composition(2, 1, 2);
    CHECK(composition->Capability() == SaveCompositionCapability::DeterministicMemory);
    auto oversized = composition->Submit({.kind = SaveCompositionOperationKind::Store, .address = Address(5), .bytes = Bytes({1, 2, 3})});
    REQUIRE(oversized.HasError());
    CHECK(oversized.ErrorValue().code.Value() == SaveErrors::CompositionCapacityExceeded.code.Value());

    auto malformed = composition->Submit({.kind = SaveCompositionOperationKind::Load, .address = Address(5), .bytes = Bytes({1})});
    REQUIRE(malformed.HasError());
    CHECK(malformed.ErrorValue().code.Value() == SaveErrors::CompositionInvalid.code.Value());
    CHECK(composition->StoredObjectCount() == 0);
}

TEST_CASE("Deterministic scheduler preserves FIFO order across explicit delays", "[unit][runtime][save]") {
    auto composition = Composition();
    const auto first =
        composition->Submit({.kind = SaveCompositionOperationKind::Store, .address = Address(5), .bytes = Bytes({1}), .delaySteps = 2});
    const auto second = composition->Submit({.kind = SaveCompositionOperationKind::Store, .address = Address(6), .bytes = Bytes({2})});
    REQUIRE(first.HasValue());
    REQUIRE(second.HasValue());

    CHECK(composition->AdvanceOne());
    CHECK(Snapshot(*composition, first.Value()).state == SaveCompositionOperationState::Waiting);
    CHECK(Snapshot(*composition, second.Value()).state == SaveCompositionOperationState::Queued);
    CHECK(composition->AdvanceOne());
    CHECK(Snapshot(*composition, second.Value()).state == SaveCompositionOperationState::Queued);
    CHECK(composition->AdvanceOne());
    CHECK(Snapshot(*composition, first.Value()).commit == SaveCompositionCommitOutcome::Committed);
    CHECK_FALSE(composition->ObjectSnapshot(Address(6)).has_value());
    CHECK(composition->AdvanceOne());
    CHECK(Snapshot(*composition, second.Value()).commit == SaveCompositionCommitOutcome::Committed);
    CHECK_FALSE(composition->AdvanceOne());
}

TEST_CASE("Injected failure and cancellation cannot publish or replace bytes", "[unit][runtime][save]") {
    auto composition = Composition();
    const auto address = Address(5);
    const auto initial = composition->Submit({.kind = SaveCompositionOperationKind::Store, .address = address, .bytes = Bytes({1, 2})});
    REQUIRE(initial.HasValue());
    REQUIRE(composition->AdvanceOne());

    const auto failed = composition->Submit({.kind = SaveCompositionOperationKind::Store,
                                             .address = address,
                                             .bytes = Bytes({9}),
                                             .fault = SaveCompositionFault::InjectedFailure});
    REQUIRE(failed.HasValue());
    REQUIRE(composition->AdvanceOne());
    const auto failedSnapshot = Snapshot(*composition, failed.Value());
    CHECK(failedSnapshot.state == SaveCompositionOperationState::Failed);
    RequireError(failedSnapshot, SaveErrors::CompositionInjectedFailure);
    CHECK(composition->ObjectSnapshot(address) == Bytes({1, 2}));

    CancellationSource cancellation;
    const auto cancelled = composition->Submit(
        {.kind = SaveCompositionOperationKind::Store, .address = address, .bytes = Bytes({8}), .cancellation = cancellation.Token()});
    REQUIRE(cancelled.HasValue());
    cancellation.RequestCancellation();
    REQUIRE(composition->AdvanceOne());
    const auto cancelledSnapshot = Snapshot(*composition, cancelled.Value());
    CHECK(cancelledSnapshot.state == SaveCompositionOperationState::Cancelled);
    RequireError(cancelledSnapshot, SaveErrors::CompositionCancelled);
    CHECK(composition->ObjectSnapshot(address) == Bytes({1, 2}));
}

TEST_CASE("Cancellation after atomic memory publication cannot relabel completion", "[unit][runtime][save]") {
    auto composition = Composition();
    CancellationSource cancellation;
    const auto stored = composition->Submit(
        {.kind = SaveCompositionOperationKind::Store, .address = Address(5), .bytes = Bytes({4}), .cancellation = cancellation.Token()});
    REQUIRE(stored.HasValue());
    REQUIRE(composition->AdvanceOne());
    cancellation.RequestCancellation();

    const auto terminal = Snapshot(*composition, stored.Value());
    CHECK(terminal.state == SaveCompositionOperationState::Completed);
    CHECK(terminal.commit == SaveCompositionCommitOutcome::Committed);
    CHECK_FALSE(terminal.error.has_value());
    CHECK_FALSE(composition->AdvanceOne());
}

TEST_CASE("Load snapshots own their bytes and remove is deterministic", "[unit][runtime][save]") {
    auto composition = Composition();
    const auto address = Address(5);
    REQUIRE(composition->Submit({.kind = SaveCompositionOperationKind::Store, .address = address, .bytes = Bytes({1, 2, 3})}).HasValue());
    REQUIRE(composition->AdvanceOne());

    const auto loaded = composition->Submit({.kind = SaveCompositionOperationKind::Load, .address = address});
    REQUIRE(loaded.HasValue());
    REQUIRE(composition->AdvanceOne());
    auto copied = Snapshot(*composition, loaded.Value());
    REQUIRE(copied.bytes == Bytes({1, 2, 3}));
    copied.bytes.front() = std::byte{9};
    CHECK(Snapshot(*composition, loaded.Value()).bytes == Bytes({1, 2, 3}));

    const auto removed = composition->Submit({.kind = SaveCompositionOperationKind::Remove, .address = address});
    REQUIRE(removed.HasValue());
    REQUIRE(composition->AdvanceOne());
    CHECK(Snapshot(*composition, removed.Value()).commit == SaveCompositionCommitOutcome::Committed);
    CHECK_FALSE(composition->ObjectSnapshot(address).has_value());

    const auto missing = composition->Submit({.kind = SaveCompositionOperationKind::Load, .address = address});
    REQUIRE(missing.HasValue());
    REQUIRE(composition->AdvanceOne());
    RequireError(Snapshot(*composition, missing.Value()), SaveErrors::CompositionObjectMissing);
}

TEST_CASE("Object and operation capacity failures are explicit and replacements preserve capacity", "[unit][runtime][save]") {
    auto composition = Composition(3, 1, 4);
    const auto firstAddress = Address(5);
    const auto secondAddress = Address(6);
    REQUIRE(composition->Submit({.kind = SaveCompositionOperationKind::Store, .address = firstAddress, .bytes = Bytes({1})}).HasValue());
    REQUIRE(composition->AdvanceOne());

    const auto replacement =
        composition->Submit({.kind = SaveCompositionOperationKind::Store, .address = firstAddress, .bytes = Bytes({2})});
    REQUIRE(replacement.HasValue());
    REQUIRE(composition->AdvanceOne());
    CHECK(composition->ObjectSnapshot(firstAddress) == Bytes({2}));
    CHECK(composition->StoredObjectCount() == 1);

    const auto excessObject =
        composition->Submit({.kind = SaveCompositionOperationKind::Store, .address = secondAddress, .bytes = Bytes({3})});
    REQUIRE(excessObject.HasValue());
    REQUIRE(composition->AdvanceOne());
    RequireError(Snapshot(*composition, excessObject.Value()), SaveErrors::CompositionCapacityExceeded);

    const auto excessOperation = composition->Submit({.kind = SaveCompositionOperationKind::Load, .address = firstAddress});
    REQUIRE(excessOperation.HasError());
    CHECK(excessOperation.ErrorValue().code.Value() == SaveErrors::CompositionCapacityExceeded.code.Value());
}
