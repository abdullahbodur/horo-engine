#include "Horo/Animation/AnimationComponents.h"

namespace Horo::Animation {
    namespace {
        [[nodiscard]] constexpr bool IsKnown(const AnimationSourceKind value) noexcept {
            return value < AnimationSourceKind::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const RootMotionPolicy value) noexcept {
            return value < RootMotionPolicy::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const AnimationEvaluationDomain value) noexcept {
            return value < AnimationEvaluationDomain::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const AnimationPlaybackState value) noexcept {
            return value < AnimationPlaybackState::Count;
        }

        [[nodiscard]] bool IsValid(const AnimationSourceReference &source) noexcept {
            if (!IsKnown(source.kind))
                return false;
            if (source.kind == AnimationSourceKind::Clip)
                return source.clip.IsValid() && !source.graph.IsValid();
            return source.graph.IsValid() && !source.clip.IsValid();
        }

        [[nodiscard]] constexpr bool HasCompatiblePoseOrder(const RuntimeAnimationComponent &runtime) noexcept {
            return runtime.previousPose.generation.Value() <= runtime.currentPose.generation.Value();
        }

        [[nodiscard]] Result<void> ValidateRuntimeRepresentation(const RuntimeAnimationComponent &runtime) {
            if (runtime.contractVersion != CurrentAnimationComponentContractVersion)
                return Result<void>::Failure(MakeError(AnimationErrors::ContractVersionUnsupported));
            if (!runtime.instance.IsValid() || !runtime.previousPose.IsValid() || !runtime.currentPose.IsValid())
                return Result<void>::Failure(MakeError(AnimationErrors::HandleMalformed));
            if (!IsKnown(runtime.domain) || !IsKnown(runtime.state) || !IsValid(runtime.source))
                return Result<void>::Failure(MakeError(AnimationErrors::ComponentInvalid));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateRuntimeBinding(const RuntimeAnimationComponent &runtime,
                                                          const AnimationAuthoringComponent &authoring) {
            if (runtime.instance.component != authoring.component || runtime.skeleton != authoring.skeleton ||
                runtime.source != authoring.source)
                return Result<void>::Failure(MakeError(AnimationErrors::ComponentBindingMismatch));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateRuntimePoseLineage(const RuntimeAnimationComponent &runtime) {
            if (runtime.previousPose.instance != runtime.instance || runtime.currentPose.instance != runtime.instance)
                return Result<void>::Failure(MakeError(AnimationErrors::HandleOwnerMismatch));
            if (!HasCompatiblePoseOrder(runtime))
                return Result<void>::Failure(MakeError(AnimationErrors::HandleStale));
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc ValidateAnimationAuthoringComponent */
    Result<void> ValidateAnimationAuthoringComponent(const AnimationAuthoringComponent &component) {
        if (component.contractVersion != CurrentAnimationComponentContractVersion)
            return Result<void>::Failure(MakeError(AnimationErrors::ContractVersionUnsupported));
        if (!component.component.IsValid() || !component.skeleton.IsValid())
            return Result<void>::Failure(MakeError(AnimationErrors::IdentityInvalid));
        if (!IsValid(component.source) || !IsKnown(component.rootMotion))
            return Result<void>::Failure(MakeError(AnimationErrors::ComponentInvalid));
        return Result<void>::Success();
    }

    /** @copydoc ValidateRuntimeAnimationComponent */
    Result<void> ValidateRuntimeAnimationComponent(const RuntimeAnimationComponent &runtime, const AnimationAuthoringComponent &authoring) {
        if (auto authored = ValidateAnimationAuthoringComponent(authoring); authored.HasError())
            return authored;
        if (auto representation = ValidateRuntimeRepresentation(runtime); representation.HasError())
            return representation;
        if (auto binding = ValidateRuntimeBinding(runtime, authoring); binding.HasError())
            return binding;
        return ValidateRuntimePoseLineage(runtime);
    }
}  // namespace Horo::Animation
