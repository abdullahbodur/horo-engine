#include "Horo/Animation/AnimationIdentity.h"

namespace Horo::Animation {
    namespace {
        /** @brief Validates a nested value and its exact animation-instance association. */
        [[nodiscard]] Result<void> ValidateInstanceBoundValue(const bool representationValid, const AnimationInstanceHandle &sourceInstance,
                                                              const AnimationInstanceHandle &currentInstance) {
            if (!representationValid)
                return Result<void>::Failure(MakeError(AnimationErrors::HandleMalformed));
            return ValidateAnimationInstanceAccess(sourceInstance, currentInstance);
        }
    }  // namespace

    /** @copydoc ValidateAnimationInstanceAccess */
    Result<void> ValidateAnimationInstanceAccess(const AnimationInstanceHandle &submitted, const AnimationInstanceHandle &current) {
        if (!submitted.IsValid() || !current.IsValid())
            return Result<void>::Failure(MakeError(AnimationErrors::HandleMalformed));
        if (submitted.owner != current.owner || submitted.component != current.component)
            return Result<void>::Failure(MakeError(AnimationErrors::HandleOwnerMismatch));
        if (submitted.slot.index != current.slot.index || submitted.slot.generation != current.slot.generation)
            return Result<void>::Failure(MakeError(AnimationErrors::HandleStale));
        return Result<void>::Success();
    }

    /** @copydoc ValidatePoseAccess */
    Result<void> ValidatePoseAccess(const PoseHandle &submitted, const AnimationInstanceHandle &currentInstance,
                                    const PoseGeneration currentGeneration) {
        if (auto association =
                ValidateInstanceBoundValue(submitted.IsValid() && currentGeneration.IsValid(), submitted.instance, currentInstance);
            association.HasError())
            return association;
        if (submitted.generation != currentGeneration)
            return Result<void>::Failure(MakeError(AnimationErrors::HandleStale));
        return Result<void>::Success();
    }

    /** @copydoc ValidateRootMotionRequestAccess */
    Result<void> ValidateRootMotionRequestAccess(const RootMotionRequestId &submitted, const AnimationInstanceHandle &currentInstance,
                                                 const AnimationTickId expectedTick, const RootMotionGeneration currentGeneration) {
        const bool representationValid = submitted.IsValid() && expectedTick.IsValid() && currentGeneration.IsValid();
        if (auto association = ValidateInstanceBoundValue(representationValid, submitted.instance, currentInstance); association.HasError())
            return association;
        if (submitted.tick != expectedTick || submitted.generation != currentGeneration)
            return Result<void>::Failure(MakeError(AnimationErrors::HandleStale));
        return Result<void>::Success();
    }

    /** @copydoc ValidatePresentationPoseAccess */
    Result<void> ValidatePresentationPoseAccess(const PresentationPoseHandle &submitted, const PoseHandle &currentSource,
                                                const AnimationFrameId currentFrame) {
        if (!submitted.IsValid() || !currentSource.IsValid() || !currentFrame.IsValid())
            return Result<void>::Failure(MakeError(AnimationErrors::HandleMalformed));
        if (submitted.source.instance.owner != currentSource.instance.owner ||
            submitted.source.instance.component != currentSource.instance.component)
            return Result<void>::Failure(MakeError(AnimationErrors::HandleOwnerMismatch));
        if (submitted.source != currentSource || submitted.frame != currentFrame)
            return Result<void>::Failure(MakeError(AnimationErrors::HandleStale));
        return Result<void>::Success();
    }
}  // namespace Horo::Animation
