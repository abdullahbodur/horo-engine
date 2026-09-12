#include "Horo/WorldStreaming/WorldLayerOwnershipModel.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingInternal.h"

#include <limits>

namespace Horo::WorldStreaming {
    namespace {
        [[nodiscard]] bool IsKnown(const WorldLayerPlacement value) noexcept {
            using enum WorldLayerPlacement;
            switch (value) {
                case Spatial:
                case NonSpatial:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool IsKnown(const WorldLayerResidencyPolicy value) noexcept {
            using enum WorldLayerResidencyPolicy;
            switch (value) {
                case Persistent:
                case Streamed:
                case RuntimeControlled:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool IsKnown(const WorldLayerAudience value) noexcept {
            using enum WorldLayerAudience;
            switch (value) {
                case Runtime:
                case EditorOnly:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool IsKnown(const WorldLayerControlOwnerKind value) noexcept {
            using enum WorldLayerControlOwnerKind;
            switch (value) {
                case WorldStreaming:
                case EditorDocument:
                case GameplayScript:
                case NetworkReplication:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool IsKnown(const WorldLayerOwnershipAuthorityState value) noexcept {
            using enum WorldLayerOwnershipAuthorityState;
            switch (value) {
                case Active:
                case Cancelling:
                case Closed:
                    return true;
            }
            return false;
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
            if (!context.expectedWorld.IsValid() || !IsKnown(context.state) || context.layerCapacity == 0 ||
                context.layerCount > context.layerCapacity) {
                return Failure<void>(WorldStreamingErrors::LayerOwnershipInvalid);
            }
            if (!context.current.has_value())
                return Result<void>::Success();
            if (const auto valid = ValidateWorldLayerOwnershipDescriptor(*context.current); valid.HasError())
                return valid;
            if (context.current->owner.world != context.expectedWorld)
                return Failure<void>(WorldStreamingErrors::LayerOwnershipOwnerStale);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<WorldLayerOwnershipAdmissionKind> ValidateInsert(const WorldLayerOwnershipRequest &request,
                                                                              const WorldLayerOwnershipAdmissionContext &context) {
            if (request.expectedRevision.has_value())
                return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipRevisionStale);
            if (context.layerCount == context.layerCapacity)
                return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipCapacityExceeded);
            return Result<WorldLayerOwnershipAdmissionKind>::Success(WorldLayerOwnershipAdmissionKind::Insert);
        }

        [[nodiscard]] bool SameClassification(const WorldLayerOwnershipDescriptor &left,
                                              const WorldLayerOwnershipDescriptor &right) noexcept {
            return left.placement == right.placement && left.residency == right.residency && left.audience == right.audience;
        }

        [[nodiscard]] Result<WorldLayerOwnershipAdmissionKind> ValidateReplacement(const WorldLayerOwnershipRequest &request,
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

    /** @copydoc WorldLayerOwnershipDescriptor::IsValid */
    bool WorldLayerOwnershipDescriptor::IsValid() const noexcept {
        return layer.IsValid() && revision.IsValid() && IsKnown(placement) && IsKnown(residency) && IsKnown(audience) && owner.IsValid() &&
               HasCoherentPolicy(*this);
    }

    /** @copydoc ValidateWorldLayerOwnershipDescriptor */
    Result<void> ValidateWorldLayerOwnershipDescriptor(const WorldLayerOwnershipDescriptor &descriptor) {
        if (!IsKnown(descriptor.placement) || !IsKnown(descriptor.residency) || !IsKnown(descriptor.audience) ||
            !IsKnown(descriptor.owner.kind)) {
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
        if (const auto valid = ValidateContext(context); valid.HasError())
            return Result<WorldLayerOwnershipAdmissionKind>::Failure(valid.ErrorValue());
        if (request.candidate.owner.world != context.expectedWorld)
            return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipOwnerStale);
        if (context.state != WorldLayerOwnershipAuthorityState::Active)
            return Failure<WorldLayerOwnershipAdmissionKind>(WorldStreamingErrors::LayerOwnershipLifecycleUnavailable);
        if (!context.current.has_value())
            return ValidateInsert(request, context);
        return ValidateReplacement(request, *context.current);
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
