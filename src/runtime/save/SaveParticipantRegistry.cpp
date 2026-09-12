#include "Horo/Runtime/Save/SaveParticipantRegistry.h"

#include "Horo/Runtime/Save/SaveErrors.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Horo::Runtime {
    struct SaveParticipantRegistryDetail::SnapshotStorage final {
        std::vector<SaveParticipantBinding> bindings;
        std::vector<SaveParticipantBinding> captureBindings;
        std::vector<SaveParticipantBinding> restoreBindings;
    };

    namespace {
        using ParticipantIndices = std::unordered_map<SaveParticipantId, std::size_t, SaveParticipantIdHash>;

        struct PhasePlanGraph final {
            std::vector<std::vector<std::size_t>> dependents;
            std::vector<std::size_t> dependencyCounts;
            std::size_t participantCount{0};
        };

        /** @brief Reports whether the descriptor declares one supported semantic scope. */
        [[nodiscard]] bool IsValidScope(const SaveParticipantScope scope) noexcept {
            using enum SaveParticipantScope;
            switch (scope) {
                case RuntimeScene:
                case SlotPlayer:
                case PersistentWorld:
                    return true;
            }
            return false;
        }

        /** @brief Reports whether role flags are non-empty and contain only supported bits. */
        [[nodiscard]] bool HasValidRoles(const SaveParticipantRole roles) noexcept {
            constexpr auto kSupportedRoles = std::byte{static_cast<std::uint8_t>(SaveParticipantRole::Capture)} |
                                             std::byte{static_cast<std::uint8_t>(SaveParticipantRole::Restore)};
            const auto encodedRoles = std::byte{static_cast<std::uint8_t>(roles)};
            return encodedRoles != std::byte{} && (encodedRoles & ~kSupportedRoles) == std::byte{};
        }

        /** @brief Reports whether all local limits are finite and cover declared record ownership. */
        [[nodiscard]] bool HasValidLimits(const CanonicalStateParticipantDescriptor &descriptor) noexcept {
            return descriptor.limits.maximumPayloadBytes != 0 && descriptor.limits.maximumRecordCount != 0 &&
                   descriptor.limits.maximumNestingDepth != 0 && descriptor.ownedRecords.size() <= descriptor.limits.maximumRecordCount;
        }

        /** @brief Validates required scalar and owned-record descriptor fields. */
        [[nodiscard]] bool HasValidRequiredFields(const CanonicalStateParticipantDescriptor &descriptor) {
            return descriptor.participant.IsValid() && descriptor.schemaVersion.IsValid() && IsValidScope(descriptor.scope) &&
                   HasValidRoles(descriptor.roles) && HasValidLimits(descriptor) && !descriptor.ownedRecords.empty();
        }

        /** @brief Reports whether a dependency requirement is a supported closed value. */
        [[nodiscard]] bool IsKnown(const SaveParticipantDependencyRequirement requirement) noexcept {
            switch (requirement) {
                case SaveParticipantDependencyRequirement::Required:
                case SaveParticipantDependencyRequirement::Optional:
                    return true;
            }
            return false;
        }

        /** @brief Reports whether a dependency phase is a supported closed value. */
        [[nodiscard]] bool IsKnown(const SaveParticipantDependencyPhase phase) noexcept {
            switch (phase) {
                case SaveParticipantDependencyPhase::Capture:
                case SaveParticipantDependencyPhase::Restore:
                case SaveParticipantDependencyPhase::CaptureAndRestore:
                    return true;
            }
            return false;
        }

        /** @brief Reports whether an edge applies to one operation role. */
        [[nodiscard]] bool AppliesTo(const SaveParticipantDependencyPhase phase, const SaveParticipantRole role) noexcept {
            if (phase == SaveParticipantDependencyPhase::CaptureAndRestore)
                return true;
            if (role == SaveParticipantRole::Capture)
                return phase == SaveParticipantDependencyPhase::Capture;
            return phase == SaveParticipantDependencyPhase::Restore;
        }

        /** @brief Formats one dependency phase for actionable diagnostics. */
        [[nodiscard]] std::string_view PhaseName(const SaveParticipantRole role) noexcept {
            return role == SaveParticipantRole::Capture ? "capture" : "restore";
        }

        /** @brief Validates dependency identity, self-reference, and uniqueness rules. */
        [[nodiscard]] Result<void> ValidateDependencyMetadata(const CanonicalStateParticipantDescriptor &descriptor) {
            if (descriptor.dependencies.size() > MaximumSaveParticipantCount)
                return Result<void>::Failure(MakeError(SaveErrors::ParticipantDescriptorInvalid));
            std::unordered_set<SaveParticipantId, SaveParticipantIdHash> uniqueDependencies;
            uniqueDependencies.reserve(descriptor.dependencies.size());
            for (const SaveParticipantDependency &dependency : descriptor.dependencies) {
                if (!dependency.participant.IsValid() || dependency.participant == descriptor.participant ||
                    !IsKnown(dependency.requirement) || !IsKnown(dependency.phase) ||
                    !uniqueDependencies.insert(dependency.participant).second)
                    return Result<void>::Failure(MakeError(SaveErrors::ParticipantDescriptorInvalid));
                const bool captureCompatible = !AppliesTo(dependency.phase, SaveParticipantRole::Capture) ||
                                               HasSaveParticipantRole(descriptor.roles, SaveParticipantRole::Capture);
                const bool restoreCompatible = !AppliesTo(dependency.phase, SaveParticipantRole::Restore) ||
                                               HasSaveParticipantRole(descriptor.roles, SaveParticipantRole::Restore);
                if (!captureCompatible || !restoreCompatible) {
                    return Result<void>::Failure(MakeError(SaveErrors::ParticipantDependencyPhaseIncompatible,
                                                           "Participant '" + descriptor.participant.Value() + "' declares dependency '" +
                                                               dependency.participant.Value() + "' for a phase it does not support."));
                }
            }
            return Result<void>::Success();
        }

        /** @brief Validates one inert descriptor before registry mutation. */
        [[nodiscard]] Result<void> ValidateDescriptor(const CanonicalStateParticipantDescriptor &descriptor) {
            if (!HasValidRequiredFields(descriptor))
                return Result<void>::Failure(MakeError(SaveErrors::ParticipantDescriptorInvalid));
            if (const Result<void> dependencies = ValidateDependencyMetadata(descriptor); dependencies.HasError())
                return dependencies;
            if (const Result<void> records = ValidateUniqueSaveIdentities<SaveRecordIdentityTag>(descriptor.ownedRecords);
                records.HasError())
                return Result<void>::Failure(MakeError(SaveErrors::ParticipantDescriptorInvalid));
            return Result<void>::Success();
        }

        /** @brief Builds the immutable participant-to-binding index used for graph validation. */
        [[nodiscard]] ParticipantIndices BuildParticipantIndices(const std::vector<SaveParticipantBinding> &bindings) {
            ParticipantIndices indices;
            indices.reserve(bindings.size());
            for (std::size_t index = 0; index < bindings.size(); ++index)
                indices.try_emplace(bindings[index].Descriptor().participant, index);
            return indices;
        }

        /** @brief Builds and validates the dependency graph for one operation phase. */
        [[nodiscard]] Result<PhasePlanGraph> BuildPhasePlanGraph(const std::vector<SaveParticipantBinding> &bindings,
                                                                 const SaveParticipantRole role) {
            const ParticipantIndices indices = BuildParticipantIndices(bindings);
            PhasePlanGraph graph{std::vector<std::vector<std::size_t>>(bindings.size()), std::vector<std::size_t>(bindings.size()), 0};
            for (std::size_t index = 0; index < bindings.size(); ++index) {
                const CanonicalStateParticipantDescriptor &descriptor = bindings[index].Descriptor();
                if (!HasSaveParticipantRole(descriptor.roles, role))
                    continue;
                ++graph.participantCount;
                for (const SaveParticipantDependency &dependency : descriptor.dependencies) {
                    if (!AppliesTo(dependency.phase, role))
                        continue;
                    const auto found = indices.find(dependency.participant);
                    if (found == indices.end()) {
                        if (dependency.requirement == SaveParticipantDependencyRequirement::Optional)
                            continue;
                        return Result<PhasePlanGraph>::Failure(MakeError(SaveErrors::ParticipantDependencyMissing,
                                                                         "Participant '" + descriptor.participant.Value() +
                                                                             "' requires missing " + std::string{PhaseName(role)} +
                                                                             " dependency '" + dependency.participant.Value() + "'."));
                    }
                    const CanonicalStateParticipantDescriptor &provider = bindings[found->second].Descriptor();
                    if (!HasSaveParticipantRole(provider.roles, role)) {
                        return Result<PhasePlanGraph>::Failure(MakeError(SaveErrors::ParticipantDependencyPhaseIncompatible,
                                                                         "Participant '" + descriptor.participant.Value() +
                                                                             "' depends on '" + provider.participant.Value() + "' during " +
                                                                             std::string{PhaseName(role)} +
                                                                             ", but the dependency does not support that phase."));
                    }
                    graph.dependents[found->second].push_back(index);
                    ++graph.dependencyCounts[index];
                }
            }
            return Result<PhasePlanGraph>::Success(std::move(graph));
        }

        /** @brief Creates an actionable diagnostic for participants remaining in a dependency cycle. */
        [[nodiscard]] Error MakeDependencyCycleError(const std::vector<SaveParticipantBinding> &bindings, const std::vector<bool> &emitted,
                                                     const SaveParticipantRole role) {
            std::string message = std::string{PhaseName(role)} + " participant dependency cycle involves";
            for (std::size_t index = 0; index < bindings.size(); ++index) {
                if (!emitted[index] && HasSaveParticipantRole(bindings[index].Descriptor().roles, role))
                    message += " '" + bindings[index].Descriptor().participant.Value() + "'";
            }
            message += ".";
            return MakeError(SaveErrors::ParticipantDependencyCycle, std::move(message));
        }

        /** @brief Builds one stable topological phase plan with actionable dependency diagnostics. */
        [[nodiscard]] Result<std::vector<SaveParticipantBinding>> BuildPhasePlan(const std::vector<SaveParticipantBinding> &bindings,
                                                                                 const SaveParticipantRole role) {
            auto graphResult = BuildPhasePlanGraph(bindings, role);
            if (graphResult.HasError())
                return Result<std::vector<SaveParticipantBinding>>::Failure(graphResult.ErrorValue());
            PhasePlanGraph graph = std::move(graphResult).Value();

            std::vector<bool> emitted(bindings.size());
            std::vector<SaveParticipantBinding> plan;
            plan.reserve(graph.participantCount);
            while (plan.size() < graph.participantCount) {
                std::size_t selected = bindings.size();
                for (std::size_t index = 0; index < bindings.size(); ++index) {
                    if (!emitted[index] && graph.dependencyCounts[index] == 0 &&
                        HasSaveParticipantRole(bindings[index].Descriptor().roles, role)) {
                        selected = index;
                        break;
                    }
                }
                if (selected == bindings.size())
                    return Result<std::vector<SaveParticipantBinding>>::Failure(MakeDependencyCycleError(bindings, emitted, role));
                emitted[selected] = true;
                plan.push_back(bindings[selected]);
                for (const std::size_t dependent : graph.dependents[selected])
                    --graph.dependencyCounts[dependent];
            }
            return Result<std::vector<SaveParticipantBinding>>::Success(std::move(plan));
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

    SaveParticipantRegistrySnapshot::SaveParticipantRegistrySnapshot(
        const std::uint64_t generation, std::shared_ptr<const SaveParticipantRegistryDetail::SnapshotStorage> storage)
        : generation_(generation), storage_(std::move(storage)) {}

    /** @copydoc SaveParticipantRegistrySnapshot::IsValid */
    bool SaveParticipantRegistrySnapshot::IsValid() const noexcept {
        return generation_ != 0 && storage_ != nullptr;
    }

    /** @copydoc SaveParticipantRegistrySnapshot::Generation */
    std::uint64_t SaveParticipantRegistrySnapshot::Generation() const noexcept {
        return generation_;
    }

    /** @copydoc SaveParticipantRegistrySnapshot::Bindings */
    std::span<const SaveParticipantBinding> SaveParticipantRegistrySnapshot::Bindings() const noexcept {
        return storage_ == nullptr ? std::span<const SaveParticipantBinding>{}
                                   : std::span<const SaveParticipantBinding>{storage_->bindings};
    }

    /** @copydoc SaveParticipantRegistrySnapshot::CaptureBindings */
    std::span<const SaveParticipantBinding> SaveParticipantRegistrySnapshot::CaptureBindings() const noexcept {
        return storage_ == nullptr ? std::span<const SaveParticipantBinding>{}
                                   : std::span<const SaveParticipantBinding>{storage_->captureBindings};
    }

    /** @copydoc SaveParticipantRegistrySnapshot::RestoreBindings */
    std::span<const SaveParticipantBinding> SaveParticipantRegistrySnapshot::RestoreBindings() const noexcept {
        return storage_ == nullptr ? std::span<const SaveParticipantBinding>{}
                                   : std::span<const SaveParticipantBinding>{storage_->restoreBindings};
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
        std::ranges::sort(descriptor.dependencies);
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
        auto storage = std::make_shared<SaveParticipantRegistryDetail::SnapshotStorage>();
        storage->bindings = bindings_;
        std::ranges::sort(storage->bindings, {}, [](const SaveParticipantBinding &binding) {
            return binding.Descriptor().participant.Value();
        });
        auto capturePlan = BuildPhasePlan(storage->bindings, SaveParticipantRole::Capture);
        if (capturePlan.HasError())
            return Result<SaveParticipantRegistrySnapshot>::Failure(capturePlan.ErrorValue());
        auto restorePlan = BuildPhasePlan(storage->bindings, SaveParticipantRole::Restore);
        if (restorePlan.HasError())
            return Result<SaveParticipantRegistrySnapshot>::Failure(restorePlan.ErrorValue());
        storage->captureBindings = std::move(capturePlan).Value();
        storage->restoreBindings = std::move(restorePlan).Value();
        return Result<SaveParticipantRegistrySnapshot>::Success({generation_, std::move(storage)});
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
