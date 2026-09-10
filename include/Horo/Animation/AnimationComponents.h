#pragma once

/**
 * @file AnimationComponents.h
 * @brief Inert authoring and runtime animation component value contracts.
 */

#include "Horo/Animation/AnimationIdentity.h"

#include <compare>
#include <cstdint>

namespace Horo::Animation {
    /** @brief Version of the public animation component value contract. */
    struct AnimationComponentContractVersion final {
        std::uint16_t major{};
        std::uint16_t minor{};
        std::uint16_t patch{};

        [[nodiscard]] constexpr auto operator<=>(const AnimationComponentContractVersion &) const noexcept = default;
    };

    /** @brief Exact contract version implemented by this API slice. */
    inline constexpr AnimationComponentContractVersion CurrentAnimationComponentContractVersion{1, 0, 0};

    /** @brief Declares which immutable authored asset supplies an animation component's root evaluation. */
    enum class AnimationSourceKind : std::uint8_t {
        Clip,
        Graph,
        Count
    };

    /** @brief Declares how a component treats extracted authoritative root motion. */
    enum class RootMotionPolicy : std::uint8_t {
        Ignore,
        RequestCharacterMovement,
        Count
    };

    /** @brief Explicit evaluation domain; presentation and preview never advance authoritative state. */
    enum class AnimationEvaluationDomain : std::uint8_t {
        AuthoritativeSimulation,
        Presentation,
        EditorPreview,
        Count
    };

    /** @brief Published lifecycle state of one live runtime animation component. */
    enum class AnimationPlaybackState : std::uint8_t {
        Ready,
        Playing,
        Paused,
        Stopped,
        Failed,
        Count
    };

    /** @brief Typed union-like authored source reference with exactly one active asset identity. */
    struct AnimationSourceReference final {
        AnimationSourceKind kind{AnimationSourceKind::Count}; /**< Selects the one required identity. */
        AnimationClipId clip{};                               /**< Required only when kind is Clip. */
        AnimationGraphId graph{};                             /**< Required only when kind is Graph. */

        [[nodiscard]] auto operator<=>(const AnimationSourceReference &) const noexcept = default;
    };

    /**
     * @brief Persistent, inert scene authoring contract for one skeletal animation occurrence.
     *
     * This value owns no runtime instance, pose storage, callback, job, asset lease, native object,
     * service locator, or backend choice. The scene/runtime composition owner resolves it explicitly.
     */
    struct AnimationAuthoringComponent final {
        AnimationComponentContractVersion contractVersion{CurrentAnimationComponentContractVersion};
        AnimationComponentId component{};                      /**< Stable authored occurrence identity. */
        SkeletonId skeleton{};                                 /**< Required immutable skeleton asset. */
        AnimationSourceReference source{};                     /**< Required clip or graph asset. */
        RetargetProfileId retarget{};                          /**< Optional profile; invalid means no retargeting. */
        RootMotionPolicy rootMotion{RootMotionPolicy::Ignore}; /**< Explicit owner-request policy. */
        bool startsPlaying{true};                              /**< Initial runtime intent, not current mutable state. */

        [[nodiscard]] auto operator<=>(const AnimationAuthoringComponent &) const noexcept = default;
    };

    /**
     * @brief Immutable published runtime projection for one live animation component.
     *
     * The animation runtime owns mutable instance and pose storage on its declared owner thread.
     * This projection is non-owning. Shutdown closes admission, joins bounded work, revokes leases,
     * and retires the instance generation before storage or immutable assets are released.
     */
    struct RuntimeAnimationComponent final {
        AnimationComponentContractVersion contractVersion{CurrentAnimationComponentContractVersion};
        AnimationInstanceHandle instance{};                                 /**< Exact live runtime incarnation. */
        SkeletonId skeleton{};                                              /**< Resolved immutable skeleton identity. */
        AnimationSourceReference source{};                                  /**< Resolved authored clip/graph identity. */
        AnimationEvaluationDomain domain{AnimationEvaluationDomain::Count}; /**< Mutation/publication domain. */
        AnimationPlaybackState state{AnimationPlaybackState::Count};        /**< Explicit published lifecycle state. */
        PoseHandle previousPose{};                                          /**< Previous committed pose retained for presentation. */
        PoseHandle currentPose{}; /**< Current committed pose; never mutable through this value. */

        [[nodiscard]] auto operator<=>(const RuntimeAnimationComponent &) const noexcept = default;
    };

    /**
     * @brief Validates an authoring component without resolving assets or mutating ambient state.
     * @param component Candidate persistent component value.
     * @return Success, ContractVersionUnsupported, IdentityInvalid, or ComponentInvalid.
     * @pre Tooling/load-time boundary; bounded and side-effect free.
     */
    [[nodiscard]] Result<void> ValidateAnimationAuthoringComponent(const AnimationAuthoringComponent &component);

    /**
     * @brief Validates a published runtime component against its immutable authoring contract.
     * @param runtime Candidate non-owning runtime projection.
     * @param authoring Previously validated authored component that owns the occurrence identity.
     * @return Success or a stable version, component, binding, malformed-handle, or stale-handle failure.
     * @pre Owner/control-thread publication boundary; bounded, allocation-free on success, and side-effect free.
     * @post Success does not prove registry residency or extend any pose lifetime.
     */
    [[nodiscard]] Result<void> ValidateRuntimeAnimationComponent(const RuntimeAnimationComponent &runtime,
                                                                 const AnimationAuthoringComponent &authoring);
}  // namespace Horo::Animation
