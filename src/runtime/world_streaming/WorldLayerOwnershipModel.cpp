#include "Horo/WorldStreaming/WorldLayerOwnershipModel.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingInternal.h"

#include <algorithm>
#include <array>
#include <limits>

namespace Horo::WorldStreaming {
    namespace {
        template <typename T, std::size_t Size> [[nodiscard]] bool IsOneOf(const T value, const std::array<T, Size> &values) noexcept {
            return std::find(values.begin(), values.end(), value) != values.end();
        }

        template <std::size_t Size> [[nodiscard]] bool All(const std::array<bool, Size> &conditions) noexcept {
            return std::all_of(conditions.begin(), conditions.end(), [](const bool condition) {
                return condition;
            });
        }

        [[nodiscard]] bool IsKnown(const WorldLayerPlacement value) noexcept {
            constexpr std::array values{WorldLayerPlacement::Spatial, WorldLayerPlacement::NonSpatial};
            return IsOneOf(value, values);
        }

        [[nodiscard]] bool IsKnown(const WorldLayerResidencyPolicy value) noexcept {
            constexpr std::array values{WorldLayerResidencyPolicy::Persistent, WorldLayerResidencyPolicy::Streamed,
                                        WorldLayerResidencyPolicy::RuntimeControlled};
            return IsOneOf(value, values);
        }

        [[nodiscard]] bool IsKnown(const WorldLayerAudience value) noexcept {
            constexpr std::array values{WorldLayerAudience::Runtime, WorldLayerAudience::EditorOnly};
            return IsOneOf(value, values);
        }

        [[nodiscard]] bool IsKnown(const WorldLayerControlOwnerKind value) noexcept {
            constexpr std::array values{WorldLayerControlOwnerKind::WorldStreaming, WorldLayerControlOwnerKind::EditorDocument,
                                        WorldLayerControlOwnerKind::GameplayScript, WorldLayerControlOwnerKind::NetworkReplication};
            return IsOneOf(value, values);
        }

        [[nodiscard]] bool IsKnown(const WorldLayerOwnershipAuthorityState value) noexcept {
            constexpr std::array values{WorldLayerOwnershipAuthorityState::Active, WorldLayerOwnershipAuthorityState::Cancelling,
                                        WorldLayerOwnershipAuthorityState::Closed};
            return IsOneOf(value, values);
        }

        [[nodiscard]] bool HasCoherentPolicy(const WorldLayerOwnershipDescriptor &descriptor) noexcept {
            using enum WorldLayerControlOwnerKind;
            if (descriptor.audience == WorldLayerAudience::EditorOnly) {
                return descriptor.residency != WorldLayerResidencyPolicy::RuntimeControlled && descriptor.owner.kind == EditorDocument;
            }
            if (descriptor.residency == WorldLayerResidencyPolicy::RuntimeControlled) {
                return descriptor.owner.kind == GameplayScript || descriptor.owner.kind == NetworkReplication;
            }
            return descriptor.owner.kind == WorldStreaming;
        }

        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Internal::Failure<T>(descriptor);
        }

        [[nodiscard]] Result<void> ValidateContext(const WorldLayerOwnershipAdmissionContext &context) {
            if (!All(std::array{context.expectedWorld.IsValid(), IsKnown(context.state), context.layerCapacity > 0,
                                context.layerCount <= context.layerCapacity})) {
                return Failure<void>(WorldStreamingErrors::LayerOwnershipInvalid);
            }
            if (!context.current.has_value()) {
                if (context.handoff.has_value())
                    return Failure<void>(WorldStreamingErrors::LayerOwnershipInvalid);
                return Result<void>::Success();
            }
            if (context.layerCount == 0)
                return Failure<void>(WorldStreamingErrors::LayerOwnershipInvalid);
            if (const auto valid = ValidateWorldLayerOwnershipDescriptor(*context.current); valid.HasError())
                return valid;
            if (context.current->owner.world != context.expectedWorld)
                return Failure<void>(WorldStreamingErrors::LayerOwnershipOwnerStale);
            if (!context.handoff.has_value())
                return Result<void>::Success();
            if (!context.handoff->IsValid())
                return Failure<void>(WorldStreamingErrors::LayerOwnershipInvalid);
            if (!All(std::array{context.handoff->authorization.currentOwner == context.current->owner,
                                context.handoff->authorization.layer == context.current->layer,
                                context.handoff->authorization.expectedRevision == context.current->revision,
                                context.handoff->validatedTarget.world == context.expectedWorld})) {
                return Failure<void>(WorldStreamingErrors::LayerOwnershipOwnerStale);
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<WorldLayerOwnershipAdmissionKind> ValidateInsert(const WorldLayerOwnershipRequest &request,
                                                                              const WorldLayerOwnershipAdmissionContext &context) {
            if (request.expectedRevision.has_value() || request.handoff.has_value())
                return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipRevisionStale);
            if (context.layerCount == context.layerCapacity)
                return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipCapacityExceeded);
            return Result<WorldLayerOwnershipAdmissionKind>::Success(WorldLayerOwnershipAdmissionKind::Insert);
        }

        [[nodiscard]] bool SameClassification(const WorldLayerOwnershipDescriptor &left,
                                              const WorldLayerOwnershipDescriptor &right) noexcept {
            return left.placement == right.placement && left.residency == right.residency && left.audience == right.audience;
        }

        /** @brief Identify two lifetimes of the same typed non-streaming authority. */
        [[nodiscard]] bool SameOwnerLineage(const WorldLayerControlOwner &left, const WorldLayerControlOwner &right) noexcept {
            return left.kind == right.kind && left.authority == right.authority;
        }

        /** @brief Validate exact source authorization and independently fresh target-lifetime evidence. */
        [[nodiscard]] Result<void> ValidateHandoff(const WorldLayerOwnershipRequest &request,
                                                   const WorldLayerOwnershipAdmissionContext &context,
                                                   const WorldLayerOwnershipDescriptor &current) {
            if (!request.handoff.has_value() || !context.handoff.has_value()) {
                return Failure<void>(WorldStreamingErrors::LayerOwnershipOwnerStale);
            }
            const auto &receipt = *request.handoff;
            if (!receipt.IsValid())
                return Failure<void>(WorldStreamingErrors::LayerOwnershipInvalid);
            if (!All(std::array{receipt == context.handoff->authorization, receipt.currentOwner == current.owner,
                                receipt.targetOwner == request.candidate.owner, receipt.layer == current.layer,
                                receipt.layer == request.candidate.layer, receipt.expectedRevision == current.revision,
                                receipt.targetOwner == context.handoff->validatedTarget})) {
                return Failure<void>(WorldStreamingErrors::LayerOwnershipOwnerStale);
            }
            if (SameOwnerLineage(current.owner, request.candidate.owner) &&
                request.candidate.owner.generation <= current.owner.generation) {
                return Failure<void>(WorldStreamingErrors::LayerOwnershipOwnerStale);
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<WorldLayerOwnershipAdmissionKind> ValidateReplacement(const WorldLayerOwnershipRequest &request,
                                                                                   const WorldLayerOwnershipAdmissionContext &context,
                                                                                   const WorldLayerOwnershipDescriptor &current) {
            if (request.candidate.layer != current.layer)
                return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipIdentityConflict);
            if (!request.expectedRevision.has_value() || *request.expectedRevision != current.revision)
                return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipRevisionStale);
            const auto next = NextWorldLayerRevision(current.revision);
            if (next.HasError())
                return Result<WorldLayerOwnershipAdmissionKind>::Failure(next.ErrorValue());
            if (request.candidate.revision != next.Value())
                return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipRevisionStale);
            if (!SameClassification(current, request.candidate))
                return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipUnsupported);

            const bool ownerChanged = current.owner != request.candidate.owner;
            if (ownerChanged && current.residency != WorldLayerResidencyPolicy::RuntimeControlled)
                return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipUnsupported);
            if (!ownerChanged && request.handoff.has_value())
                return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipInvalid);
            if (ownerChanged) {
                if (const auto handoff = ValidateHandoff(request, context, current); handoff.HasError())
                    return Result<WorldLayerOwnershipAdmissionKind>::Failure(handoff.ErrorValue());
            }
            return Result<WorldLayerOwnershipAdmissionKind>::Success(ownerChanged ? WorldLayerOwnershipAdmissionKind::Handoff
                                                                                  : WorldLayerOwnershipAdmissionKind::Replace);
        }
    }  // namespace

    /** @copydoc WorldLayerControlOwner::IsValid */
    bool WorldLayerControlOwner::IsValid() const noexcept {
        if (!world.IsValid() || !IsKnown(kind))
            return false;
        if (kind == WorldLayerControlOwnerKind::WorldStreaming)
            return !authority.IsValid() && !generation.IsValid();
        return authority.IsValid() && generation.IsValid();
    }

    /** @copydoc WorldLayerControlHandoffReceipt::IsValid */
    bool WorldLayerControlHandoffReceipt::IsValid() const noexcept {
        return All(std::array{id.IsValid(), generation.IsValid(), currentOwner.IsValid(), targetOwner.IsValid(), layer.IsValid(),
                              expectedRevision.IsValid(), currentOwner.world == targetOwner.world, currentOwner != targetOwner});
    }

    /** @copydoc WorldLayerValidatedHandoffContext::IsValid */
    bool WorldLayerValidatedHandoffContext::IsValid() const noexcept {
        return All(std::array{authorization.IsValid(), validatedTarget.IsValid(), authorization.targetOwner == validatedTarget});
    }

    /** @copydoc WorldLayerOwnershipDescriptor::IsValid */
    bool WorldLayerOwnershipDescriptor::IsValid() const noexcept {
        return All(std::array{layer.IsValid(), revision.IsValid(), IsKnown(placement), IsKnown(residency), IsKnown(audience),
                              owner.IsValid(), HasCoherentPolicy(*this)});
    }

    /** @copydoc ValidateWorldLayerOwnershipDescriptor */
    Result<void> ValidateWorldLayerOwnershipDescriptor(const WorldLayerOwnershipDescriptor &descriptor) {
        if (!All(std::array{IsKnown(descriptor.placement), IsKnown(descriptor.residency), IsKnown(descriptor.audience),
                            IsKnown(descriptor.owner.kind)})) {
            return Failure<void>(WorldStreamingErrors::LayerOwnershipUnsupported);
        }
        if (!descriptor.layer.IsValid() || !descriptor.revision.IsValid() || !descriptor.owner.IsValid())
            return Failure<void>(WorldStreamingErrors::LayerOwnershipInvalid);
        if (!HasCoherentPolicy(descriptor))
            return Failure<void>(WorldStreamingErrors::LayerOwnershipUnsupported);
        return Result<void>::Success();
    }

    /** @copydoc ValidateWorldLayerOwnershipAdmission */
    Result<WorldLayerOwnershipAdmissionKind> ValidateWorldLayerOwnershipAdmission(const WorldLayerOwnershipRequest &request,
                                                                                  const WorldLayerOwnershipAdmissionContext &context) {
        if (const auto valid = ValidateWorldLayerOwnershipDescriptor(request.candidate); valid.HasError())
            return Result<WorldLayerOwnershipAdmissionKind>::Failure(valid.ErrorValue());
        if (request.expectedRevision.has_value() && !request.expectedRevision->IsValid())
            return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipInvalid);
        if (request.handoff.has_value() && !request.handoff->IsValid())
            return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipInvalid);
        if (const auto valid = ValidateContext(context); valid.HasError())
            return Result<WorldLayerOwnershipAdmissionKind>::Failure(valid.ErrorValue());
        if (request.candidate.owner.world != context.expectedWorld)
            return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipOwnerStale);
        if (context.state != WorldLayerOwnershipAuthorityState::Active)
            return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipLifecycleUnavailable);
        if (!context.current.has_value())
            return ValidateInsert(request, context);
        return ValidateReplacement(request, context, *context.current);
    }

    /** @copydoc NextWorldLayerRevision */
    Result<WorldLayerRevision> NextWorldLayerRevision(const WorldLayerRevision current) {
        if (!current.IsValid())
            return Failure<WorldLayerRevision>(WorldStreamingErrors::IdentityInvalid);
        if (current.Value() == std::numeric_limits<std::uint64_t>::max())
            return Failure<WorldLayerRevision>(WorldStreamingErrors::GenerationExhausted);
        return WorldLayerRevision::Create(current.Value() + 1);
    }
}  // namespace Horo::WorldStreaming
