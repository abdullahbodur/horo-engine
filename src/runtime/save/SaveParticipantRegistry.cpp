#include "Horo/Runtime/Save/SaveParticipantRegistry.h"

#include "Horo/Runtime/Save/SaveErrors.h"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace Horo::Runtime {
    namespace {
        [[nodiscard]] Result<void> ValidateDescriptor(const CanonicalStateParticipantDescriptor &descriptor) {
            if (!descriptor.participant.IsValid() || !descriptor.schemaVersion.IsValid() || descriptor.roles == SaveParticipantRole::None ||
                descriptor.limits.maximumPayloadBytes == 0 || descriptor.limits.maximumRecordCount == 0 ||
                descriptor.limits.maximumNestingDepth == 0 || descriptor.ownedRecords.empty())
                return Result<void>::Failure(MakeError(SaveErrors::ParticipantDescriptorInvalid));

            for (std::size_t index = 0; index < descriptor.dependencies.size(); ++index) {
                if (!descriptor.dependencies[index].IsValid() || descriptor.dependencies[index] == descriptor.participant)
                    return Result<void>::Failure(MakeError(SaveErrors::ParticipantDescriptorInvalid));
                if (std::ranges::find(descriptor.dependencies.begin(), descriptor.dependencies.begin() + index,
                                      descriptor.dependencies[index]) != descriptor.dependencies.begin() + index)
                    return Result<void>::Failure(MakeError(SaveErrors::ParticipantDescriptorInvalid));
            }
            if (const Result<void> records = ValidateUniqueSaveIdentities<SaveRecordIdentityTag>(descriptor.ownedRecords);
                records.HasError())
                return Result<void>::Failure(MakeError(SaveErrors::ParticipantDescriptorInvalid));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateDependencies(const std::vector<SaveParticipantBinding> &bindings) {
            enum class VisitState : std::uint8_t {
                Unvisited,
                Visiting,
                Visited
            };
            std::unordered_map<SaveParticipantId, std::size_t, SaveParticipantIdHash> indices;
            indices.reserve(bindings.size());
            for (std::size_t index = 0; index < bindings.size(); ++index)
                indices.emplace(bindings[index].Descriptor().participant, index);

            for (const SaveParticipantBinding &binding : bindings) {
                for (const SaveParticipantId &dependency : binding.Descriptor().dependencies) {
                    if (!indices.contains(dependency))
                        return Result<void>::Failure(MakeError(SaveErrors::ParticipantDependencyMissing));
                }
            }

            std::vector<VisitState> states(bindings.size(), VisitState::Unvisited);
            const auto visit = [&](const auto &self, const std::size_t index) -> bool {
                if (states[index] == VisitState::Visiting)
                    return false;
                if (states[index] == VisitState::Visited)
                    return true;
                states[index] = VisitState::Visiting;
                for (const SaveParticipantId &dependency : bindings[index].Descriptor().dependencies) {
                    if (!self(self, indices.at(dependency)))
                        return false;
                }
                states[index] = VisitState::Visited;
                return true;
            };
            for (std::size_t index = 0; index < bindings.size(); ++index) {
                if (!visit(visit, index))
                    return Result<void>::Failure(MakeError(SaveErrors::ParticipantDependencyCycle));
            }
            return Result<void>::Success();
        }
    }  // namespace

    SaveParticipantBinding::SaveParticipantBinding(CanonicalStateParticipantDescriptor descriptor,
                                                   std::shared_ptr<const ICanonicalStateAdapter> adapter)
        : descriptor_(std::move(descriptor)), adapter_(std::move(adapter)) {}

    /** @copydoc SaveParticipantBinding::Descriptor */
    const CanonicalStateParticipantDescriptor &SaveParticipantBinding::Descriptor() const noexcept {
        return descriptor_;
    }

    /** @copydoc SaveParticipantBinding::Adapter */
    const std::shared_ptr<const ICanonicalStateAdapter> &SaveParticipantBinding::Adapter() const noexcept {
        return adapter_;
    }

    SaveParticipantRegistrySnapshot::SaveParticipantRegistrySnapshot(const std::uint64_t generation,
                                                                     std::shared_ptr<const std::vector<SaveParticipantBinding>> bindings)
        : generation_(generation), bindings_(std::move(bindings)) {}

    /** @copydoc SaveParticipantRegistrySnapshot::IsValid */
    bool SaveParticipantRegistrySnapshot::IsValid() const noexcept {
        return generation_ != 0 && bindings_ != nullptr;
    }

    /** @copydoc SaveParticipantRegistrySnapshot::Generation */
    std::uint64_t SaveParticipantRegistrySnapshot::Generation() const noexcept {
        return generation_;
    }

    /** @copydoc SaveParticipantRegistrySnapshot::Bindings */
    std::span<const SaveParticipantBinding> SaveParticipantRegistrySnapshot::Bindings() const noexcept {
        return bindings_ == nullptr ? std::span<const SaveParticipantBinding>{} : std::span<const SaveParticipantBinding>{*bindings_};
    }

    /** @copydoc SaveParticipantRegistrySnapshot::Find */
    const SaveParticipantBinding *SaveParticipantRegistrySnapshot::Find(const SaveParticipantId &participant) const noexcept {
        const auto bindings = Bindings();
        const auto found = std::ranges::lower_bound(bindings, participant, {}, [](const SaveParticipantBinding &binding) {
            return binding.Descriptor().participant;
        });
        if (found == bindings.end() || found->Descriptor().participant != participant)
            return nullptr;
        return std::to_address(found);
    }

    CanonicalStateParticipantRegistry::~CanonicalStateParticipantRegistry() {
        Close();
    }

    /** @copydoc CanonicalStateParticipantRegistry::Register */
    Result<SaveParticipantRegistration> CanonicalStateParticipantRegistry::Register(CanonicalStateParticipantDescriptor descriptor,
                                                                                    std::shared_ptr<const ICanonicalStateAdapter> adapter) {
        if (closed_)
            return Result<SaveParticipantRegistration>::Failure(MakeError(SaveErrors::ParticipantRegistryClosed));
        if (adapter == nullptr)
            return Result<SaveParticipantRegistration>::Failure(MakeError(SaveErrors::ParticipantAdapterMissing));
        if (const Result<void> valid = ValidateDescriptor(descriptor); valid.HasError())
            return Result<SaveParticipantRegistration>::Failure(valid.ErrorValue());
        if (bindings_.size() >= MaximumSaveParticipantCount)
            return Result<SaveParticipantRegistration>::Failure(MakeError(SaveErrors::ParticipantRegistryCapacityExceeded));
        if (std::ranges::find(bindings_, descriptor.participant, [](const SaveParticipantBinding &binding) {
            return binding.Descriptor().participant;
        }) != bindings_.end())
            return Result<SaveParticipantRegistration>::Failure(MakeError(SaveErrors::ParticipantDuplicate));
        for (const SaveParticipantBinding &binding : bindings_) {
            for (const SaveRecordId &record : descriptor.ownedRecords) {
                if (std::ranges::find(binding.Descriptor().ownedRecords, record) != binding.Descriptor().ownedRecords.end())
                    return Result<SaveParticipantRegistration>::Failure(MakeError(SaveErrors::ParticipantRecordOwnershipDuplicate));
            }
        }
        const SaveParticipantId participant = descriptor.participant;
        if (const Result<void> advanced = AdvanceGeneration(); advanced.HasError())
            return Result<SaveParticipantRegistration>::Failure(advanced.ErrorValue());
        bindings_.push_back(SaveParticipantBinding{std::move(descriptor), std::move(adapter)});
        return Result<SaveParticipantRegistration>::Success({participant, generation_});
    }

    /** @copydoc CanonicalStateParticipantRegistry::Unregister */
    Result<bool> CanonicalStateParticipantRegistry::Unregister(const SaveParticipantId &participant) {
        if (closed_)
            return Result<bool>::Failure(MakeError(SaveErrors::ParticipantRegistryClosed));
        const auto found = std::ranges::find(bindings_, participant, [](const SaveParticipantBinding &binding) {
            return binding.Descriptor().participant;
        });
        if (found == bindings_.end())
            return Result<bool>::Success(false);
        if (const Result<void> advanced = AdvanceGeneration(); advanced.HasError())
            return Result<bool>::Failure(advanced.ErrorValue());
        bindings_.erase(found);
        return Result<bool>::Success(true);
    }

    /** @copydoc CanonicalStateParticipantRegistry::Snapshot */
    Result<SaveParticipantRegistrySnapshot> CanonicalStateParticipantRegistry::Snapshot() const {
        if (closed_)
            return Result<SaveParticipantRegistrySnapshot>::Failure(MakeError(SaveErrors::ParticipantRegistryClosed));
        if (const Result<void> dependencies = ValidateDependencies(bindings_); dependencies.HasError())
            return Result<SaveParticipantRegistrySnapshot>::Failure(dependencies.ErrorValue());
        auto snapshotBindings = std::make_shared<std::vector<SaveParticipantBinding>>(bindings_);
        std::ranges::sort(*snapshotBindings, {}, [](const SaveParticipantBinding &binding) {
            return binding.Descriptor().participant.Value();
        });
        return Result<SaveParticipantRegistrySnapshot>::Success({generation_, std::move(snapshotBindings)});
    }

    /** @copydoc CanonicalStateParticipantRegistry::Close */
    void CanonicalStateParticipantRegistry::Close() noexcept {
        if (closed_)
            return;
        closed_ = true;
        bindings_.clear();
    }

    /** @copydoc CanonicalStateParticipantRegistry::IsClosed */
    bool CanonicalStateParticipantRegistry::IsClosed() const noexcept {
        return closed_;
    }

    /** @copydoc CanonicalStateParticipantRegistry::Generation */
    std::uint64_t CanonicalStateParticipantRegistry::Generation() const noexcept {
        return generation_;
    }

    Result<void> CanonicalStateParticipantRegistry::AdvanceGeneration() {
        if (generation_ == std::numeric_limits<std::uint64_t>::max())
            return Result<void>::Failure(MakeError(SaveErrors::ParticipantRegistryGenerationExhausted));
        ++generation_;
        return Result<void>::Success();
    }
}  // namespace Horo::Runtime
