#include "Horo/Runtime/Save/SaveTestCompositions.h"

#include "Horo/Runtime/Save/SaveErrors.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace Horo::Runtime {
    namespace {
        [[nodiscard]] Result<SaveCompositionOperationId> CompositionFailure(const ErrorCodeDescriptor &descriptor) {
            return Result<SaveCompositionOperationId>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool IsKnownKind(const SaveCompositionOperationKind kind) noexcept {
            switch (kind) {
                case SaveCompositionOperationKind::Store:
                case SaveCompositionOperationKind::Load:
                case SaveCompositionOperationKind::Remove:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool IsKnownFault(const SaveCompositionFault fault) noexcept {
            switch (fault) {
                case SaveCompositionFault::None:
                case SaveCompositionFault::InjectedFailure:
                    return true;
            }
            return false;
        }
    }  // namespace

    /** @copydoc NullSaveComposition::Submit */
    Result<SaveCompositionOperationId> NullSaveComposition::Submit(SaveCompositionRequest) const {
        return CompositionFailure(SaveErrors::CompositionUnsupported);
    }

    /** @copydoc NullSaveComposition::Snapshot */
    std::optional<SaveCompositionOperationSnapshot> NullSaveComposition::Snapshot(const SaveCompositionOperationId) const noexcept {
        return std::nullopt;
    }

    struct SaveCompositionDetail::DeterministicMockState final {
        struct StoredObject final {
            SaveCompositionAddress address;
            std::vector<std::byte> bytes;
        };

        struct Operation final {
            SaveCompositionRequest request;
            SaveCompositionOperationSnapshot snapshot;
            std::uint32_t remainingDelay{};
        };

        DeterministicSaveCompositionLimits limits;
        std::vector<StoredObject> objects;
        std::vector<Operation> operations;
        SaveCompositionOperationId nextOperation{1};
    };

    namespace {
        using State = SaveCompositionDetail::DeterministicMockState;

        template <typename StateType> [[nodiscard]] auto FindObject(StateType &state, const SaveCompositionAddress &address) {
            const auto iterator = std::ranges::find(state.objects, address, &State::StoredObject::address);
            using Pointer = decltype(std::addressof(*iterator));
            return iterator == state.objects.end() ? Pointer{} : std::addressof(*iterator);
        }

        [[nodiscard]] bool IsTerminal(const SaveCompositionOperationState state) noexcept {
            switch (state) {
                case SaveCompositionOperationState::Queued:
                case SaveCompositionOperationState::Waiting:
                    return false;
                case SaveCompositionOperationState::Completed:
                case SaveCompositionOperationState::Failed:
                case SaveCompositionOperationState::Cancelled:
                    return true;
            }
            std::terminate();
        }

        void Fail(State::Operation &operation, const ErrorCodeDescriptor &descriptor,
                  const SaveCompositionOperationState state = SaveCompositionOperationState::Failed) {
            operation.snapshot.state = state;
            operation.snapshot.commit = SaveCompositionCommitOutcome::NotCommitted;
            operation.snapshot.error = MakeError(descriptor);
        }

        void CompleteStore(State &state, State::Operation &operation) {
            if (auto *existing = FindObject(state, operation.request.address)) {
                existing->bytes = operation.request.bytes;
            } else {
                if (state.objects.size() >= state.limits.maximumStoredObjects) {
                    Fail(operation, SaveErrors::CompositionCapacityExceeded);
                    return;
                }
                state.objects.push_back(State::StoredObject{operation.request.address, operation.request.bytes});
            }
            operation.snapshot.state = SaveCompositionOperationState::Completed;
            operation.snapshot.commit = SaveCompositionCommitOutcome::Committed;
        }

        void CompleteLoad(const State &state, State::Operation &operation) {
            const auto *object = FindObject(state, operation.request.address);
            if (object == nullptr) {
                Fail(operation, SaveErrors::CompositionObjectMissing);
                return;
            }
            operation.snapshot.bytes = object->bytes;
            operation.snapshot.state = SaveCompositionOperationState::Completed;
        }

        void CompleteRemove(State &state, State::Operation &operation) {
            const auto iterator = std::ranges::find(state.objects, operation.request.address, &State::StoredObject::address);
            if (iterator == state.objects.end()) {
                Fail(operation, SaveErrors::CompositionObjectMissing);
                return;
            }
            state.objects.erase(iterator);
            operation.snapshot.state = SaveCompositionOperationState::Completed;
            operation.snapshot.commit = SaveCompositionCommitOutcome::Committed;
        }
    }  // namespace

    /** @copydoc DeterministicMockSaveComposition::~DeterministicMockSaveComposition */
    DeterministicMockSaveComposition::~DeterministicMockSaveComposition() = default;
    DeterministicMockSaveComposition::DeterministicMockSaveComposition(DeterministicMockSaveComposition &&) noexcept = default;
    DeterministicMockSaveComposition &DeterministicMockSaveComposition::operator=(DeterministicMockSaveComposition &&) noexcept = default;

    DeterministicMockSaveComposition::DeterministicMockSaveComposition(
        std::unique_ptr<SaveCompositionDetail::DeterministicMockState> state) noexcept
        : state_(std::move(state)) {}

    /** @copydoc DeterministicMockSaveComposition::Submit */
    Result<SaveCompositionOperationId> DeterministicMockSaveComposition::Submit(SaveCompositionRequest request) {
        if (!IsKnownKind(request.kind) || !IsKnownFault(request.fault) || !request.address.slot.IsValid())
            return CompositionFailure(SaveErrors::CompositionInvalid);
        if (request.bytes.size() > state_->limits.maximumBytesPerObject)
            return CompositionFailure(SaveErrors::CompositionCapacityExceeded);
        if (request.kind != SaveCompositionOperationKind::Store && !request.bytes.empty())
            return CompositionFailure(SaveErrors::CompositionInvalid);
        if (state_->operations.size() >= state_->limits.maximumOperations ||
            state_->nextOperation.value == std::numeric_limits<std::uint64_t>::max())
            return CompositionFailure(SaveErrors::CompositionCapacityExceeded);

        const SaveCompositionOperationId operationId = state_->nextOperation;
        ++state_->nextOperation.value;
        const auto kind = request.kind;
        const auto delay = request.delaySteps;
        state_->operations.push_back(State::Operation{
            .request = std::move(request),
            .snapshot = SaveCompositionOperationSnapshot{.operation = operationId, .kind = kind},
            .remainingDelay = delay,
        });
        return Result<SaveCompositionOperationId>::Success(operationId);
    }

    /** @copydoc DeterministicMockSaveComposition::AdvanceOne */
    bool DeterministicMockSaveComposition::AdvanceOne() {
        const auto iterator = std::ranges::find_if(state_->operations, [](const State::Operation &operation) {
            return !IsTerminal(operation.snapshot.state);
        });
        if (iterator == state_->operations.end())
            return false;

        State::Operation &operation = *iterator;
        if (operation.remainingDelay > 0) {
            --operation.remainingDelay;
            operation.snapshot.state = SaveCompositionOperationState::Waiting;
            return true;
        }
        if (operation.request.cancellation.IsCancellationRequested()) {
            Fail(operation, SaveErrors::CompositionCancelled, SaveCompositionOperationState::Cancelled);
            return true;
        }
        if (operation.request.fault == SaveCompositionFault::InjectedFailure) {
            Fail(operation, SaveErrors::CompositionInjectedFailure);
            return true;
        }

        switch (operation.request.kind) {
            case SaveCompositionOperationKind::Store:
                CompleteStore(*state_, operation);
                break;
            case SaveCompositionOperationKind::Load:
                CompleteLoad(*state_, operation);
                break;
            case SaveCompositionOperationKind::Remove:
                CompleteRemove(*state_, operation);
                break;
        }
        return true;
    }

    /** @copydoc DeterministicMockSaveComposition::Snapshot */
    std::optional<SaveCompositionOperationSnapshot> DeterministicMockSaveComposition::Snapshot(
        const SaveCompositionOperationId operation) const {
        const auto iterator = std::ranges::find_if(state_->operations, [operation](const State::Operation &candidate) {
            return candidate.snapshot.operation == operation;
        });
        if (iterator == state_->operations.end())
            return std::nullopt;
        return iterator->snapshot;
    }

    /** @copydoc DeterministicMockSaveComposition::ObjectSnapshot */
    std::optional<std::vector<std::byte>> DeterministicMockSaveComposition::ObjectSnapshot(const SaveCompositionAddress &address) const {
        const auto *object = FindObject(*state_, address);
        if (object == nullptr)
            return std::nullopt;
        return object->bytes;
    }

    /** @copydoc DeterministicMockSaveComposition::StoredObjectCount */
    std::size_t DeterministicMockSaveComposition::StoredObjectCount() const noexcept {
        return state_->objects.size();
    }

    /** @copydoc CreateDeterministicMockSaveComposition */
    Result<std::unique_ptr<DeterministicMockSaveComposition>> CreateDeterministicMockSaveComposition(
        const DeterministicSaveCompositionLimits limits) {
        if (limits.maximumOperations == 0 || limits.maximumStoredObjects == 0 || limits.maximumBytesPerObject == 0)
            return Result<std::unique_ptr<DeterministicMockSaveComposition>>::Failure(MakeError(SaveErrors::CompositionInvalid));

        auto state = std::unique_ptr<SaveCompositionDetail::DeterministicMockState>{
            new (std::nothrow) SaveCompositionDetail::DeterministicMockState{.limits = limits}};
        if (!state)
            return Result<std::unique_ptr<DeterministicMockSaveComposition>>::Failure(MakeError(SaveErrors::CompositionCapacityExceeded));
        auto composition =
            std::unique_ptr<DeterministicMockSaveComposition>{new (std::nothrow) DeterministicMockSaveComposition{std::move(state)}};
        if (!composition)
            return Result<std::unique_ptr<DeterministicMockSaveComposition>>::Failure(MakeError(SaveErrors::CompositionCapacityExceeded));
        return Result<std::unique_ptr<DeterministicMockSaveComposition>>::Success(std::move(composition));
    }
}  // namespace Horo::Runtime
