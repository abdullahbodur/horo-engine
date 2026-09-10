#include "Horo/Animation/AnimationIdentity.h"

namespace Horo::Animation {
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
        if (!submitted.IsValid() || !currentGeneration.IsValid())
            return Result<void>::Failure(MakeError(AnimationErrors::HandleMalformed));
        if (auto instance = ValidateAnimationInstanceAccess(submitted.instance, currentInstance); instance.HasError())
            return instance;
        if (submitted.generation != currentGeneration)
            return Result<void>::Failure(MakeError(AnimationErrors::HandleStale));
        return Result<void>::Success();
    }

    /** @copydoc ValidateRootMotionRequestAccess */
    Result<void> ValidateRootMotionRequestAccess(const RootMotionRequestId &submitted, const AnimationInstanceHandle &currentInstance,
                                                 const AnimationTickId expectedTick, const RootMotionGeneration currentGeneration) {
        if (!submitted.IsValid() || !expectedTick.IsValid() || !currentGeneration.IsValid())
            return Result<void>::Failure(MakeError(AnimationErrors::HandleMalformed));
        if (auto instance = ValidateAnimationInstanceAccess(submitted.instance, currentInstance); instance.HasError())
            return instance;
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
