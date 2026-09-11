#include "Horo/PCG/PCGGenerationPlan.h"

#include "Horo/PCG/PCGErrors.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Horo::PCG {
    struct PCGGenerationPlan::State final {
        explicit State(PCGGenerationPlanCandidate candidate, const PCGGenerationResourceEstimate &totalResources)
            : version(candidate.version), plan(candidate.plan), execution(candidate.execution), seed(candidate.seed),
              lineage(candidate.lineage), set(candidate.set), setRevision(candidate.setRevision), cell(candidate.cell),
              requiredCapabilities(candidate.requiredCapabilities), target(candidate.validation),
              dependencies(std::move(candidate.dependencies)), outputs(std::move(candidate.outputs)), resources(totalResources) {}

        PCGGenerationPlanVersion version;
        GenerationPlanId plan;
        ExecutionId execution;
        std::uint64_t seed;
        GenerationLineageId lineage;
        GeneratedSetId set;
        std::uint64_t setRevision;
        GenerationCellId cell;
        PCGCapabilitySet requiredCapabilities;
        PCGGenerationTargetReceipt target;
        std::vector<PCGGenerationDependency> dependencies;
        std::vector<PCGOutputDelta> outputs;
        PCGGenerationResourceEstimate resources;
    };

    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &error) {
            return Result<T>::Failure(MakeError(error));
        }

        [[nodiscard]] bool IsZero(const PCGGenerationDigest &digest) noexcept {
            return std::ranges::all_of(digest.bytes, [](const std::uint8_t byte) {
                return byte == 0;
            });
        }

        [[nodiscard]] bool ValidReceipt(const PCGGenerationTargetReceipt &receipt) noexcept {
            return receipt.receipt.IsValid() && receipt.owner.IsValid() && receipt.ownerGeneration != 0 &&
                   receipt.capabilityGeneration != 0;
        }

        [[nodiscard]] bool ValidProvenance(const PCGGeneratedOutputProvenance &provenance) noexcept {
            return provenance.logicalOutput.IsValid() && provenance.sourceOutput.IsValid() && provenance.lineage.IsValid() &&
                   provenance.set.IsValid() && provenance.setRevision != 0 && provenance.ownershipGeneration != 0 &&
                   provenance.targetOwner.IsValid() && provenance.cell.IsValid();
        }

        [[nodiscard]] bool SameOwnershipScope(const PCGGeneratedOutputProvenance &left,
                                              const PCGGeneratedOutputProvenance &right) noexcept {
            return left.logicalOutput == right.logicalOutput && left.lineage == right.lineage && left.set == right.set &&
                   left.ownershipGeneration == right.ownershipGeneration && left.targetOwner == right.targetOwner &&
                   left.cell == right.cell;
        }

        [[nodiscard]] bool CheckedAdd(const std::uint64_t left, const std::uint64_t right, std::uint64_t &result) noexcept {
            if (right > std::numeric_limits<std::uint64_t>::max() - left)
                return false;
            result = left + right;
            return true;
        }

        [[nodiscard]] bool AddResources(PCGGenerationResourceEstimate &total, const PCGGenerationResourceEstimate &value) noexcept {
            return CheckedAdd(total.workUnits, value.workUnits, total.workUnits) &&
                   CheckedAdd(total.residentBytes, value.residentBytes, total.residentBytes) &&
                   CheckedAdd(total.preparationBytes, value.preparationBytes, total.preparationBytes) &&
                   CheckedAdd(total.retirementBytes, value.retirementBytes, total.retirementBytes);
        }

        [[nodiscard]] bool FitsResourceLimits(const PCGGenerationResourceEstimate &resources,
                                              const PCGGenerationPlanLimits &limits) noexcept {
            std::uint64_t charged{};
            return resources.workUnits <= limits.maximumWorkUnits &&
                   CheckedAdd(resources.residentBytes, resources.preparationBytes, charged) &&
                   CheckedAdd(charged, resources.retirementBytes, charged) && charged <= limits.maximumChargedBytes;
        }

        [[nodiscard]] const PCGOwnedGeneratedOutput *FindOwned(const std::vector<PCGOwnedGeneratedOutput> &owned,
                                                               const GenerationLogicalOutputId id) noexcept {
            const auto iterator = std::ranges::lower_bound(owned, id, {}, [](const PCGOwnedGeneratedOutput &entry) {
                return entry.provenance.logicalOutput;
            });
            return iterator != owned.end() && iterator->provenance.logicalOutput == id ? std::to_address(iterator) : nullptr;
        }

        [[nodiscard]] bool ContainsDependency(const std::vector<PCGGenerationDependency> &dependencies,
                                              const GenerationDependencyId id) noexcept {
            const auto iterator = std::ranges::lower_bound(dependencies, id, {}, &PCGGenerationDependency::id);
            return iterator != dependencies.end() && iterator->id == id;
        }

        [[nodiscard]] Result<void> ValidateCandidateIdentity(const PCGGenerationPlanCandidate &candidate) {
            if (candidate.version != CurrentPCGGenerationPlanVersion || !candidate.plan.IsValid() || !candidate.execution.IsValid() ||
                !candidate.lineage.IsValid() || !candidate.set.IsValid() || candidate.setRevision == 0 || !candidate.cell.IsValid() ||
                !candidate.targetOwner.IsValid() || !ValidReceipt(candidate.validation))
                return Failure<void>(PCGErrors::GenerationPlanInvalid);
            if (candidate.validation.owner != candidate.targetOwner)
                return Failure<void>(PCGErrors::GenerationPlanInvalid);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateTarget(const PCGGenerationPlanCandidate &candidate, const PCGGenerationPlanContext &context) {
            if (!ValidReceipt(context.currentTarget))
                return Failure<void>(PCGErrors::GenerationPlanInvalid);
            if (candidate.validation.owner != context.currentTarget.owner)
                return Failure<void>(PCGErrors::GenerationOwnershipMismatch);
            if (candidate.validation.receipt != context.currentTarget.receipt ||
                candidate.validation.ownerGeneration != context.currentTarget.ownerGeneration ||
                candidate.validation.capabilityGeneration != context.currentTarget.capabilityGeneration)
                return Failure<void>(PCGErrors::GenerationPlanStale);
            if (candidate.validation.capabilities != context.currentTarget.capabilities ||
                !context.currentTarget.capabilities.ContainsAll(candidate.requiredCapabilities))
                return Failure<void>(PCGErrors::UnsupportedCapability);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> CanonicalizeDependencies(std::vector<PCGGenerationDependency> &dependencies,
                                                            const PCGGenerationPlanLimits &limits) {
            if (dependencies.size() > limits.maximumDependencies)
                return Failure<void>(PCGErrors::GenerationPlanCapacityExceeded);
            std::ranges::sort(dependencies, {}, &PCGGenerationDependency::id);
            for (std::size_t index = 0; index < dependencies.size(); ++index) {
                const auto &dependency = dependencies[index];
                if (!dependency.id.IsValid() || dependency.revision == 0 || IsZero(dependency.digest))
                    return Failure<void>(PCGErrors::GenerationPlanInvalid);
                if (index != 0 && dependencies[index - 1].id == dependency.id)
                    return Failure<void>(PCGErrors::GenerationPlanInvalid);
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<std::vector<PCGOwnedGeneratedOutput>> CanonicalOwnedOutputs(const PCGGenerationPlanContext &context) {
            if (context.currentOutputs.size() > context.limits.maximumOutputs)
                return Failure<std::vector<PCGOwnedGeneratedOutput>>(PCGErrors::GenerationPlanCapacityExceeded);
            std::vector<PCGOwnedGeneratedOutput> owned(context.currentOutputs.begin(), context.currentOutputs.end());
            std::ranges::sort(owned, {}, [](const PCGOwnedGeneratedOutput &entry) {
                return entry.provenance.logicalOutput;
            });
            for (std::size_t index = 0; index < owned.size(); ++index) {
                if (!ValidProvenance(owned[index].provenance) || IsZero(owned[index].content) ||
                    (index != 0 && owned[index - 1].provenance.logicalOutput == owned[index].provenance.logicalOutput))
                    return Failure<std::vector<PCGOwnedGeneratedOutput>>(PCGErrors::GenerationPlanInvalid);
            }
            return Result<std::vector<PCGOwnedGeneratedOutput>>::Success(std::move(owned));
        }

        [[nodiscard]] Result<void> ValidateDeltaShape(const PCGOutputDelta &delta) {
            using enum PCGOutputDeltaKind;
            switch (delta.kind) {
                case Create:
                    return delta.expectedSetRevision == 0 && IsZero(delta.priorContent) && !IsZero(delta.desiredContent)
                               ? Result<void>::Success()
                               : Failure<void>(PCGErrors::GenerationPlanInvalid);
                case Update:
                    return delta.expectedSetRevision != 0 && !IsZero(delta.priorContent) && !IsZero(delta.desiredContent) &&
                                   delta.priorContent != delta.desiredContent
                               ? Result<void>::Success()
                               : Failure<void>(PCGErrors::GenerationPlanInvalid);
                case Remove:
                    return delta.expectedSetRevision != 0 && !IsZero(delta.priorContent) && IsZero(delta.desiredContent)
                               ? Result<void>::Success()
                               : Failure<void>(PCGErrors::GenerationPlanInvalid);
                case Count:
                    break;
            }
            return Failure<void>(PCGErrors::GenerationPlanInvalid);
        }

        [[nodiscard]] Result<void> ValidateOwnership(const PCGOutputDelta &delta, const std::vector<PCGOwnedGeneratedOutput> &owned) {
            const auto *current = FindOwned(owned, delta.provenance.logicalOutput);
            if (delta.kind == PCGOutputDeltaKind::Create)
                return current == nullptr ? Result<void>::Success() : Failure<void>(PCGErrors::GenerationOwnershipMismatch);
            if (current == nullptr || delta.expectedSetRevision != current->provenance.setRevision ||
                !SameOwnershipScope(delta.provenance, current->provenance) ||
                delta.provenance.setRevision <= current->provenance.setRevision || delta.priorContent != current->content)
                return Failure<void>(PCGErrors::GenerationOwnershipMismatch);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<PCGGenerationResourceEstimate> ValidateAndCanonicalizeOutputs(
            PCGGenerationPlanCandidate &candidate, const PCGGenerationPlanContext &context,
            const std::vector<PCGOwnedGeneratedOutput> &owned) {
            if (candidate.outputs.size() > context.limits.maximumOutputs)
                return Failure<PCGGenerationResourceEstimate>(PCGErrors::GenerationPlanCapacityExceeded);
            std::ranges::sort(candidate.outputs, {}, [](const PCGOutputDelta &delta) {
                return delta.provenance.logicalOutput;
            });
            PCGGenerationResourceEstimate total{};
            for (std::size_t index = 0; index < candidate.outputs.size(); ++index) {
                auto &delta = candidate.outputs[index];
                if (!ValidProvenance(delta.provenance) || delta.provenance.sourceOutput.execution != candidate.execution ||
                    delta.provenance.lineage != candidate.lineage || delta.provenance.set != candidate.set ||
                    delta.provenance.setRevision != candidate.setRevision || delta.provenance.targetOwner != candidate.targetOwner ||
                    delta.provenance.cell != candidate.cell ||
                    (index != 0 && candidate.outputs[index - 1].provenance.logicalOutput == delta.provenance.logicalOutput))
                    return Failure<PCGGenerationResourceEstimate>(PCGErrors::GenerationPlanInvalid);
                if (delta.provenance.ownershipGeneration != candidate.validation.ownerGeneration)
                    return Failure<PCGGenerationResourceEstimate>(PCGErrors::GenerationOwnershipMismatch);
                if (delta.dependencies.size() > context.limits.maximumDependenciesPerOutput)
                    return Failure<PCGGenerationResourceEstimate>(PCGErrors::GenerationPlanCapacityExceeded);
                std::ranges::sort(delta.dependencies);
                if (std::ranges::adjacent_find(delta.dependencies) != delta.dependencies.end())
                    return Failure<PCGGenerationResourceEstimate>(PCGErrors::GenerationPlanInvalid);
                if (!std::ranges::all_of(delta.dependencies, [&](const GenerationDependencyId id) {
                    return id.IsValid() && ContainsDependency(candidate.dependencies, id);
                }))
                    return Failure<PCGGenerationResourceEstimate>(PCGErrors::GenerationPlanInvalid);
                if (auto valid = ValidateDeltaShape(delta); valid.HasError())
                    return Result<PCGGenerationResourceEstimate>::Failure(valid.ErrorValue());
                if (auto valid = ValidateOwnership(delta, owned); valid.HasError())
                    return Result<PCGGenerationResourceEstimate>::Failure(valid.ErrorValue());
                if (!AddResources(total, delta.resources))
                    return Failure<PCGGenerationResourceEstimate>(PCGErrors::GenerationPlanCapacityExceeded);
            }
            if (!FitsResourceLimits(total, context.limits))
                return Failure<PCGGenerationResourceEstimate>(PCGErrors::GenerationPlanCapacityExceeded);
            return Result<PCGGenerationResourceEstimate>::Success(total);
        }
    }  // namespace

    /** @copydoc PCGGenerationPlan::Id */
    GenerationPlanId PCGGenerationPlan::Id() const noexcept {
        return state_->plan;
    }

    /** @copydoc PCGGenerationPlan::Version */
    PCGGenerationPlanVersion PCGGenerationPlan::Version() const noexcept {
        return state_->version;
    }

    /** @copydoc PCGGenerationPlan::Execution */
    ExecutionId PCGGenerationPlan::Execution() const noexcept {
        return state_->execution;
    }

    /** @copydoc PCGGenerationPlan::Seed */
    std::uint64_t PCGGenerationPlan::Seed() const noexcept {
        return state_->seed;
    }

    /** @copydoc PCGGenerationPlan::Lineage */
    GenerationLineageId PCGGenerationPlan::Lineage() const noexcept {
        return state_->lineage;
    }

    /** @copydoc PCGGenerationPlan::Set */
    GeneratedSetId PCGGenerationPlan::Set() const noexcept {
        return state_->set;
    }

    /** @copydoc PCGGenerationPlan::SetRevision */
    std::uint64_t PCGGenerationPlan::SetRevision() const noexcept {
        return state_->setRevision;
    }

    /** @copydoc PCGGenerationPlan::Cell */
    GenerationCellId PCGGenerationPlan::Cell() const noexcept {
        return state_->cell;
    }

    /** @copydoc PCGGenerationPlan::Target */
    const PCGGenerationTargetReceipt &PCGGenerationPlan::Target() const noexcept {
        return state_->target;
    }

    /** @copydoc PCGGenerationPlan::RequiredCapabilities */
    PCGCapabilitySet PCGGenerationPlan::RequiredCapabilities() const noexcept {
        return state_->requiredCapabilities;
    }

    /** @copydoc PCGGenerationPlan::Dependencies */
    std::span<const PCGGenerationDependency> PCGGenerationPlan::Dependencies() const noexcept {
        return state_->dependencies;
    }

    /** @copydoc PCGGenerationPlan::Outputs */
    std::span<const PCGOutputDelta> PCGGenerationPlan::Outputs() const noexcept {
        return state_->outputs;
    }

    /** @copydoc PCGGenerationPlan::Resources */
    const PCGGenerationResourceEstimate &PCGGenerationPlan::Resources() const noexcept {
        return state_->resources;
    }

    /** @copydoc CreatePCGGenerationPlan */
    Result<PCGGenerationPlan> CreatePCGGenerationPlan(PCGGenerationPlanCandidate candidate, const PCGGenerationPlanContext &context) {
        if (context.admission != PCGGenerationPlanAdmission::Accepting)
            return Failure<PCGGenerationPlan>(PCGErrors::GenerationPlanLifecycleUnavailable);
        if (context.limits.maximumOutputs == 0 || context.limits.maximumDependencies == 0 ||
            context.limits.maximumDependenciesPerOutput == 0 || context.limits.maximumWorkUnits == 0 ||
            context.limits.maximumChargedBytes == 0)
            return Failure<PCGGenerationPlan>(PCGErrors::GenerationPlanInvalid);
        if (auto valid = ValidateCandidateIdentity(candidate); valid.HasError())
            return Result<PCGGenerationPlan>::Failure(valid.ErrorValue());
        if (auto valid = ValidateTarget(candidate, context); valid.HasError())
            return Result<PCGGenerationPlan>::Failure(valid.ErrorValue());
        if (auto valid = CanonicalizeDependencies(candidate.dependencies, context.limits); valid.HasError())
            return Result<PCGGenerationPlan>::Failure(valid.ErrorValue());
        auto owned = CanonicalOwnedOutputs(context);
        if (owned.HasError())
            return Result<PCGGenerationPlan>::Failure(owned.ErrorValue());
        auto resources = ValidateAndCanonicalizeOutputs(candidate, context, owned.Value());
        if (resources.HasError())
            return Result<PCGGenerationPlan>::Failure(resources.ErrorValue());
        auto state = std::make_shared<const PCGGenerationPlan::State>(std::move(candidate), resources.Value());
        return Result<PCGGenerationPlan>::Success(PCGGenerationPlan{PCGGenerationPlan::ConstructionKey{}, std::move(state)});
    }

    /** @copydoc ReplacePCGGenerationPlan */
    Result<PCGGenerationPlan> ReplacePCGGenerationPlan(const PCGGenerationPlan &current, PCGGenerationPlanCandidate candidate,
                                                       const PCGGenerationPlanContext &context) {
        if (candidate.plan == current.Id() || candidate.execution == current.Execution() ||
            candidate.execution.generation.graph != current.Execution().generation.graph ||
            candidate.execution.generation.revision < current.Execution().generation.revision || candidate.lineage != current.Lineage() ||
            candidate.set != current.Set() || candidate.setRevision <= current.SetRevision() || candidate.cell != current.Cell() ||
            candidate.targetOwner != current.Target().owner)
            return Failure<PCGGenerationPlan>(PCGErrors::GenerationPlanStale);
        return CreatePCGGenerationPlan(std::move(candidate), context);
    }
}  // namespace Horo::PCG
