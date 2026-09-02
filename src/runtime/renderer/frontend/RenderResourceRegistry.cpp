#include "RenderResourceRegistry.h"

#include "RenderFrontendErrors.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <limits>
#include <string>

namespace Horo::Render::Detail {
    namespace {
        std::atomic<std::uint64_t> nextOwnerId{1};

        [[nodiscard]] Error RegistryError(const ErrorCodeDescriptor &descriptor, std::string message) {
            return MakeError(descriptor, std::move(message));
        }
    }  // namespace

    bool RenderResourceRegistryLimits::IsValid() const noexcept {
        return maximumSlots > 0 && maximumPendingRequests > 0 && retirementDrainBudget > 0 &&
               maximumOperationResults >= maximumPendingRequests;
    }

    RenderResourceRegistry::RenderResourceRegistry(const RenderResourceOwnerId owner, const RenderResourceRegistryLimits limits,
                                                   void *const releaseContext, const BackendResourceRelease releaseBackendResource)
        : owner_(owner), limits_(limits), releaseContext_(releaseContext), releaseBackendResource_(releaseBackendResource) {
        assert(owner_.IsValid());
        assert(limits_.IsValid());
    }

    Result<ResourceReservation> RenderResourceRegistry::Reserve(const RenderResourceClass resourceClass,
                                                                const std::span<const RenderResourceIdentity> dependencies) {
        if (const Result<void> admitted = ValidateReservationAdmission(); admitted.HasError()) {
            return Result<ResourceReservation>::Failure(admitted.ErrorValue());
        }
        if (const Result<void> validDependencies = ValidateDependencies(dependencies); validDependencies.HasError()) {
            return Result<ResourceReservation>::Failure(validDependencies.ErrorValue());
        }
        if (const Result<void> resultCapacity = EnsureOperationResultCapacity(); resultCapacity.HasError()) {
            return Result<ResourceReservation>::Failure(resultCapacity.ErrorValue());
        }
        auto acquiredSlot = AcquireSlot();
        if (acquiredSlot.HasError()) {
            return Result<ResourceReservation>::Failure(acquiredSlot.ErrorValue());
        }
        const std::size_t slot = acquiredSlot.Value();
        Entry &entry = entries_[slot];
        entry.resourceClass = resourceClass;
        entry.state = RenderResourceState::Pending;
        entry.dependentPins = 0;
        entry.submissionPins = 0;
        entry.backendInstance = 0;
        entry.operation = ResourceOperationId{nextOperation_++};
        entry.dependencies.assign(dependencies.begin(), dependencies.end());
        for (const RenderResourceIdentity dependency : dependencies) {
            ++FindExact(dependency)->dependentPins;
        }
        operations_.push_back(OperationRecord{.id = entry.operation});
        ++pendingRequests_;
        return Result<ResourceReservation>::Success(
            ResourceReservation{.identity = {owner_, static_cast<std::uint32_t>(slot), entry.generation}, .operation = entry.operation});
    }

    Result<void> RenderResourceRegistry::ValidateReservationAdmission() const {
        if (!acceptingRequests_) {
            return Result<void>::Failure(
                RegistryError(FrontendErrors::ResourceRegistryStopped, "Resource creation is disabled during frontend shutdown."));
        }
        if (pendingRequests_ >= limits_.maximumPendingRequests) {
            return Result<void>::Failure(
                RegistryError(FrontendErrors::ResourceQueueFull, "The bounded renderer resource request queue is full."));
        }
        if (nextOperation_ == 0) {
            return Result<void>::Failure(
                RegistryError(FrontendErrors::ResourceCapacityExhausted, "The renderer resource operation identity space is exhausted."));
        }
        return Result<void>::Success();
    }

    Result<void> RenderResourceRegistry::ValidateDependencies(const std::span<const RenderResourceIdentity> dependencies) const {
        for (std::size_t dependencyIndex = 0; dependencyIndex < dependencies.size(); ++dependencyIndex) {
            const RenderResourceIdentity dependency = dependencies[dependencyIndex];
            const auto earlierDependencies = dependencies.first(dependencyIndex);
            if (std::ranges::find(earlierDependencies, dependency) != earlierDependencies.end()) {
                return Result<void>::Failure(
                    RegistryError(FrontendErrors::ResourceHandleMalformed, "A resource dependency generation is listed more than once."));
            }
            const Entry *entry = FindExact(dependency);
            if (entry == nullptr || entry->state != RenderResourceState::Ready) {
                return Result<void>::Failure(
                    RegistryError(FrontendErrors::ResourceDependencyNotReady, "A resource dependency is foreign, stale, or not ready."));
            }
            if (entry->dependentPins == std::numeric_limits<std::uint32_t>::max()) {
                return Result<void>::Failure(
                    RegistryError(FrontendErrors::ResourceCapacityExhausted, "A renderer resource dependency pin count is exhausted."));
            }
        }
        return Result<void>::Success();
    }

    Result<void> RenderResourceRegistry::EnsureOperationResultCapacity() {
        while (operations_.size() >= limits_.maximumOperationResults) {
            const auto completed = std::ranges::find_if(operations_, &OperationRecord::complete);
            if (completed == operations_.end()) {
                return Result<void>::Failure(
                    RegistryError(FrontendErrors::ResourceQueueFull, "The bounded renderer resource result store is full."));
            }
            operations_.erase(completed);
        }
        return Result<void>::Success();
    }

    Result<std::size_t> RenderResourceRegistry::AcquireSlot() {
        for (std::size_t slot = 1; slot < entries_.size(); ++slot) {
            const Entry &entry = entries_[slot];
            if (entry.state == RenderResourceState::Retired && !entry.generationExhausted) {
                return Result<std::size_t>::Success(slot);
            }
        }
        if (entries_.size() - 1 >= limits_.maximumSlots) {
            return Result<std::size_t>::Failure(
                RegistryError(FrontendErrors::ResourceCapacityExhausted, "The renderer resource slot pool is exhausted."));
        }
        entries_.push_back({});
        return Result<std::size_t>::Success(entries_.size() - 1);
    }

    Result<void> RenderResourceRegistry::Publish(const RenderResourceClass resourceClass, const RenderResourceIdentity identity,
                                                 const std::uint64_t backendInstance) {
        auto validated = Validate(resourceClass, identity);
        if (validated.HasError()) {
            return Result<void>::Failure(validated.ErrorValue());
        }
        Entry &entry = entries_[validated.Value()];
        if (entry.state != RenderResourceState::Pending) {
            return Result<void>::Failure(
                RegistryError(FrontendErrors::ResourceNotPending, "Only a pending resource generation can be published."));
        }
        if (backendInstance == 0) {
            return Result<void>::Failure(
                RegistryError(FrontendErrors::ResourceBackendInstanceInvalid, "A ready resource requires a backend instance identity."));
        }
        entry.backendInstance = backendInstance;
        entry.state = RenderResourceState::Ready;
        --pendingRequests_;
        for (OperationRecord &operation : operations_) {
            if (operation.id == entry.operation) {
                operation.complete = true;
                break;
            }
        }
        return Result<void>::Success();
    }

    Result<void> RenderResourceRegistry::Fail(const RenderResourceClass resourceClass, const RenderResourceIdentity identity, Error error) {
        auto validated = Validate(resourceClass, identity);
        if (validated.HasError()) {
            return Result<void>::Failure(validated.ErrorValue());
        }
        Entry &entry = entries_[validated.Value()];
        if (entry.state != RenderResourceState::Pending) {
            return Result<void>::Failure(
                RegistryError(FrontendErrors::ResourceNotPending, "Only a pending resource generation can fail creation."));
        }
        entry.state = RenderResourceState::Failed;
        --pendingRequests_;
        for (OperationRecord &operation : operations_) {
            if (operation.id == entry.operation) {
                operation.complete = true;
                operation.error = std::move(error);
                break;
            }
        }
        return Result<void>::Success();
    }

    Result<void> RenderResourceRegistry::Release(const RenderResourceClass resourceClass, const RenderResourceIdentity identity) {
        auto validated = Validate(resourceClass, identity);
        if (validated.HasError()) {
            return Result<void>::Failure(validated.ErrorValue());
        }
        Entry &entry = entries_[validated.Value()];
        if (entry.state == RenderResourceState::Retiring) {
            return Result<void>::Failure(
                RegistryError(FrontendErrors::ResourceAlreadyRetiring, "The resource generation is already retiring."));
        }
        if (entry.state != RenderResourceState::Ready) {
            return Result<void>::Failure(
                RegistryError(FrontendErrors::ResourceNotReady, "Only a ready resource generation can be released."));
        }
        entry.state = RenderResourceState::Retiring;
        return Result<void>::Success();
    }

    Result<RenderResourceState> RenderResourceRegistry::State(const RenderResourceClass resourceClass,
                                                              const RenderResourceIdentity identity) const {
        auto validated = Validate(resourceClass, identity);
        if (validated.HasError()) {
            return Result<RenderResourceState>::Failure(validated.ErrorValue());
        }
        return Result<RenderResourceState>::Success(entries_[validated.Value()].state);
    }

    Result<void> RenderResourceRegistry::OperationResult(const ResourceOperationId operation) const {
        if (!operation.IsValid()) {
            return Result<void>::Failure(
                RegistryError(FrontendErrors::ResourceOperationUnknown, "The resource operation identity is invalid."));
        }
        for (const OperationRecord &record : operations_) {
            if (record.id != operation) {
                continue;
            }
            if (!record.complete) {
                return Result<void>::Failure(
                    RegistryError(FrontendErrors::ResourceOperationPending, "The renderer resource operation has not completed."));
            }
            if (record.error.has_value()) {
                return Result<void>::Failure(*record.error);
            }
            return Result<void>::Success();
        }
        return Result<void>::Failure(
            RegistryError(FrontendErrors::ResourceOperationUnknown, "The resource operation does not belong to this registry."));
    }

    Result<std::uint64_t> RenderResourceRegistry::BackendInstance(const RenderResourceClass resourceClass,
                                                                  const RenderResourceIdentity identity) const {
        const auto validated = Validate(resourceClass, identity);
        if (validated.HasError()) {
            return Result<std::uint64_t>::Failure(validated.ErrorValue());
        }
        const Entry &entry = entries_[validated.Value()];
        if (entry.state != RenderResourceState::Ready) {
            return Result<std::uint64_t>::Failure(
                RegistryError(FrontendErrors::ResourceNotReady, "The renderer resource has no usable backend instance."));
        }
        return Result<std::uint64_t>::Success(entry.backendInstance);
    }

    Result<void> RenderResourceRegistry::AddSubmissionPin(const RenderResourceClass resourceClass, const RenderResourceIdentity identity) {
        const auto validated = Validate(resourceClass, identity);
        if (validated.HasError()) {
            return Result<void>::Failure(validated.ErrorValue());
        }
        Entry &entry = entries_[validated.Value()];
        if (entry.state != RenderResourceState::Ready) {
            return Result<void>::Failure(
                RegistryError(FrontendErrors::ResourceNotReady, "Only a ready resource may enter a new submission."));
        }
        if (entry.submissionPins == std::numeric_limits<std::uint32_t>::max()) {
            return Result<void>::Failure(
                RegistryError(FrontendErrors::ResourceCapacityExhausted, "The renderer resource submission pin count is exhausted."));
        }
        ++entry.submissionPins;
        return Result<void>::Success();
    }

    Result<void> RenderResourceRegistry::ReleaseSubmissionPin(const RenderResourceClass resourceClass,
                                                              const RenderResourceIdentity identity) {
        const auto validated = Validate(resourceClass, identity);
        if (validated.HasError()) {
            return Result<void>::Failure(validated.ErrorValue());
        }
        Entry &entry = entries_[validated.Value()];
        if (entry.submissionPins == 0) {
            return Result<void>::Failure(
                RegistryError(FrontendErrors::ResourceHandleMalformed, "The renderer resource has no submission pin to release."));
        }
        --entry.submissionPins;
        return Result<void>::Success();
    }

    std::size_t RenderResourceRegistry::DrainRetirements() {
        std::size_t retired = 0;
        for (std::size_t slot = 1; slot < entries_.size() && retired < limits_.retirementDrainBudget; ++slot) {
            const Entry &entry = entries_[slot];
            if ((entry.state == RenderResourceState::Retiring || entry.state == RenderResourceState::Failed) && entry.dependentPins == 0 &&
                entry.submissionPins == 0) {
                Retire(slot);
                ++retired;
            }
        }
        return retired;
    }

    void RenderResourceRegistry::Shutdown() noexcept {
        if (!acceptingRequests_) {
            return;
        }
        acceptingRequests_ = false;
        pendingRequests_ = 0;
        for (Entry &entry : entries_) {
            if (entry.state == RenderResourceState::Pending) {
                for (OperationRecord &operation : operations_) {
                    if (operation.id == entry.operation) {
                        operation.complete = true;
                        operation.error = RegistryError(FrontendErrors::ResourceRegistryStopped,
                                                        "The pending resource operation was cancelled by frontend shutdown.");
                        break;
                    }
                }
                entry.state = RenderResourceState::Retiring;
            } else if (entry.state == RenderResourceState::Ready) {
                entry.state = RenderResourceState::Retiring;
            }
            entry.submissionPins = 0;
        }
        while (DrainRetirements() != 0) {
        }
        for (std::size_t slot = 1; slot < entries_.size(); ++slot) {
            if (entries_[slot].state != RenderResourceState::Retired) {
                Retire(slot);
            }
        }
    }

    RenderResourceOwnerId RenderResourceRegistry::Owner() const noexcept {
        return owner_;
    }

    Result<std::size_t> RenderResourceRegistry::Validate(const RenderResourceClass resourceClass,
                                                         const RenderResourceIdentity identity) const {
        if (!identity.owner.IsValid() || identity.slot == 0 || identity.generation == 0) {
            return Result<std::size_t>::Failure(
                RegistryError(FrontendErrors::ResourceHandleMalformed, "The renderer resource handle contains a zero identity field."));
        }
        if (identity.owner != owner_) {
            return Result<std::size_t>::Failure(
                RegistryError(FrontendErrors::ResourceWrongOwner, "The renderer resource handle belongs to another frontend."));
        }
        if (identity.slot >= entries_.size()) {
            return Result<std::size_t>::Failure(
                RegistryError(FrontendErrors::ResourceSlotOutOfRange, "The renderer resource slot is outside the registry."));
        }
        const Entry &entry = entries_[identity.slot];
        if (entry.generation != identity.generation || entry.state == RenderResourceState::Retired) {
            return Result<std::size_t>::Failure(
                RegistryError(FrontendErrors::ResourceStale, "The renderer resource generation is stale or retired."));
        }
        if (entry.resourceClass != resourceClass) {
            return Result<std::size_t>::Failure(
                RegistryError(FrontendErrors::ResourceWrongType, "The renderer resource handle type does not match the registry entry."));
        }
        return Result<std::size_t>::Success(identity.slot);
    }

    RenderResourceRegistry::Entry *RenderResourceRegistry::FindExact(const RenderResourceIdentity identity) noexcept {
        if (identity.owner != owner_ || identity.slot == 0 || identity.slot >= entries_.size()) {
            return nullptr;
        }
        Entry &entry = entries_[identity.slot];
        return entry.generation == identity.generation && entry.state != RenderResourceState::Retired ? &entry : nullptr;
    }

    const RenderResourceRegistry::Entry *RenderResourceRegistry::FindExact(const RenderResourceIdentity identity) const noexcept {
        if (identity.owner != owner_ || identity.slot == 0 || identity.slot >= entries_.size()) {
            return nullptr;
        }
        const Entry &entry = entries_[identity.slot];
        return entry.generation == identity.generation && entry.state != RenderResourceState::Retired ? &entry : nullptr;
    }

    void RenderResourceRegistry::Retire(const std::size_t slot) noexcept {
        Entry &entry = entries_[slot];
        for (const RenderResourceIdentity dependency : entry.dependencies) {
            if (Entry *dependencyEntry = FindExact(dependency); dependencyEntry != nullptr && dependencyEntry->dependentPins > 0) {
                --dependencyEntry->dependentPins;
            }
        }
        entry.dependencies.clear();
        if (entry.backendInstance != 0 && releaseBackendResource_ != nullptr) {
            releaseBackendResource_(releaseContext_, entry.resourceClass, entry.backendInstance);
        }
        entry.backendInstance = 0;
        entry.operation = {};
        entry.state = RenderResourceState::Retired;
        if (entry.generation == std::numeric_limits<std::uint32_t>::max()) {
            entry.generationExhausted = true;
        } else {
            ++entry.generation;
        }
    }

    Result<RenderResourceOwnerId> AcquireRenderResourceOwnerId() {
        std::uint64_t current = nextOwnerId.load(std::memory_order_relaxed);
        while (current != 0) {
            if (nextOwnerId.compare_exchange_weak(current, current + 1, std::memory_order_relaxed)) {
                return Result<RenderResourceOwnerId>::Success(RenderResourceOwnerId{current});
            }
        }
        return Result<RenderResourceOwnerId>::Failure(
            RegistryError(FrontendErrors::ResourceOwnerExhausted, "The process-wide renderer resource owner identity space is exhausted."));
    }
}  // namespace Horo::Render::Detail
