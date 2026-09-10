#pragma once

/**
 * @file AnimationIdentity.h
 * @brief Persistent animation asset identities and generation-safe process-local handles.
 */

#include "Horo/Animation/AnimationErrors.h"
#include "Horo/Assets/AssetId.h"
#include "Horo/Foundation/Handles.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/StrongId.h"

#include <compare>
#include <cstdint>
#include <limits>

namespace Horo::Animation {
    /** @brief Maximum live or recyclable slots admitted by one typed animation registry. */
    inline constexpr std::uint32_t MaximumAnimationHandleSlots = 1'048'576;

    /** @brief Strong persistent animation asset identity backed by the canonical Assets identity. */
    template <typename Tag> class AnimationAssetIdentity final {
    public:
        /** @brief Constructs the reserved invalid asset identity. */
        AnimationAssetIdentity() = default;

        /**
         * @brief Creates a typed animation identity from a persistent asset identity.
         * @param asset Canonical non-zero Assets identity.
         * @return Typed identity or AnimationErrors::IdentityInvalid.
         */
        [[nodiscard]] static Result<AnimationAssetIdentity> Create(const Assets::AssetId &asset) {
            if (!asset.IsValid())
                return Result<AnimationAssetIdentity>::Failure(MakeError(AnimationErrors::IdentityInvalid));
            return Result<AnimationAssetIdentity>::Success(AnimationAssetIdentity{asset});
        }

        /** @brief Returns the persistent identity. @return Borrowed canonical Assets identity. */
        [[nodiscard]] const Assets::AssetId &Asset() const noexcept {
            return asset_;
        }

        /** @brief Checks representation. @return True when the asset identity is non-zero. */
        [[nodiscard]] bool IsValid() const noexcept {
            return Asset().IsValid();
        }

        /**
         * @brief Compares identities by canonical asset value.
         * @param left First typed identity.
         * @param right Second typed identity.
         * @return True when both identities contain the same canonical asset value.
         */
        [[nodiscard]] friend bool operator==(const AnimationAssetIdentity &left, const AnimationAssetIdentity &right) noexcept {
            return left.Asset() == right.Asset();
        }

        /**
         * @brief Orders identities by canonical asset value.
         * @param left First typed identity.
         * @param right Second typed identity.
         * @return Canonical asset-value ordering.
         */
        [[nodiscard]] friend auto operator<=>(const AnimationAssetIdentity &left, const AnimationAssetIdentity &right) noexcept {
            return left.Asset() <=> right.Asset();
        }

    private:
        explicit AnimationAssetIdentity(const Assets::AssetId &asset) : asset_(asset) {}

        Assets::AssetId asset_;
    };

    struct SkeletonIdentityTag;
    struct AnimationClipIdentityTag;
    struct AnimationGraphIdentityTag;
    struct RetargetProfileIdentityTag;
    struct SkeletalMeshIdentityTag;

    /** @brief Persistent skeleton asset identity, independent of path, name, and load instance. */
    using SkeletonId = AnimationAssetIdentity<SkeletonIdentityTag>;
    /** @brief Persistent animation-clip asset identity, independent of a runtime player. */
    using AnimationClipId = AnimationAssetIdentity<AnimationClipIdentityTag>;
    /** @brief Persistent compiled animation-graph asset identity. */
    using AnimationGraphId = AnimationAssetIdentity<AnimationGraphIdentityTag>;
    /** @brief Persistent retarget-profile asset identity; the invalid value explicitly means no retargeting. */
    using RetargetProfileId = AnimationAssetIdentity<RetargetProfileIdentityTag>;
    /** @brief Persistent skeletal-mesh asset identity, distinct from skeleton and resident renderer resources. */
    using SkeletalMeshId = AnimationAssetIdentity<SkeletalMeshIdentityTag>;

    /** @brief Strong non-zero animation identity in one tag-defined authored or runtime domain. */
    template <typename Tag> using AnimationStableIdentity = Foundation::Detail::NonZeroId64<Tag, AnimationErrors::IdentityInvalid>;

    struct AnimationComponentIdentityTag;
    struct JointIdentityTag;
    struct SkeletonSocketIdentityTag;
    struct SkinningJointIdentityTag;
    struct SkeletalMeshSectionIdentityTag;
    struct SkeletonAssetGenerationIdentityTag;
    struct AnimationClipGenerationIdentityTag;
    struct AnimationReferencePoseIdentityTag;
    struct AnimationReferencePoseGenerationIdentityTag;
    struct AnimationRuntimeIdentityTag;
    struct PoseGenerationIdentityTag;
    struct AnimationTickIdentityTag;
    struct RootMotionGenerationIdentityTag;
    struct AnimationFrameIdentityTag;

    /** @brief Stable authored scene-component identity, independent of entity slot or display name. */
    using AnimationComponentId = AnimationStableIdentity<AnimationComponentIdentityTag>;
    /** @brief Stable skeleton-local joint identity, independent of hierarchy array position or name. */
    using JointId = AnimationStableIdentity<JointIdentityTag>;
    /** @brief Stable skeleton-local socket identity, independent of joint array position or display name. */
    using SkeletonSocketId = AnimationStableIdentity<SkeletonSocketIdentityTag>;
    /** @brief Stable mesh-local skinning-joint identity resolved through an explicit skeleton remap. */
    using SkinningJointId = AnimationStableIdentity<SkinningJointIdentityTag>;
    /** @brief Stable section identity within one skeletal-mesh asset. */
    using SkeletalMeshSectionId = AnimationStableIdentity<SkeletalMeshSectionIdentityTag>;
    /** @brief Non-reusable immutable skeleton publication generation used to reject stale bindings. */
    using SkeletonAssetGeneration = AnimationStableIdentity<SkeletonAssetGenerationIdentityTag>;
    /** @brief Non-reusable immutable animation-clip publication generation. */
    using AnimationClipGeneration = AnimationStableIdentity<AnimationClipGenerationIdentityTag>;
    /** @brief Stable identity of an additive clip's authored reference pose. */
    using AnimationReferencePoseId = AnimationStableIdentity<AnimationReferencePoseIdentityTag>;
    /** @brief Non-reusable immutable additive-reference-pose publication generation. */
    using AnimationReferencePoseGeneration = AnimationStableIdentity<AnimationReferencePoseGenerationIdentityTag>;
    /** @brief Process-local owner identity of one animation-runtime incarnation. */
    using AnimationRuntimeId = AnimationStableIdentity<AnimationRuntimeIdentityTag>;
    /** @brief Monotonic committed-pose generation within one runtime instance. */
    using PoseGeneration = AnimationStableIdentity<PoseGenerationIdentityTag>;
    /** @brief Exact authoritative fixed-tick identity used by animation handoffs. */
    using AnimationTickId = AnimationStableIdentity<AnimationTickIdentityTag>;
    /** @brief Non-reusable root-motion request generation within one animation instance. */
    using RootMotionGeneration = AnimationStableIdentity<RootMotionGenerationIdentityTag>;
    /** @brief Process-local presentation-frame identity; never an authoritative clock or persistent value. */
    using AnimationFrameId = AnimationStableIdentity<AnimationFrameIdentityTag>;

    struct AnimationInstanceSlotTag;
    struct AnimationPoseSlotTag;
    struct PresentationPoseSlotTag;

    /**
     * @brief Non-owning handle to one exact runtime incarnation of an authored animation component.
     *
     * The animation runtime owns the slot and its storage. Copying this value does not retain the
     * instance. Handles are process-local and must never be serialized, persisted, or replicated.
     */
    struct AnimationInstanceHandle final {
        AnimationRuntimeId owner{};                    /**< Exact runtime incarnation that issued the handle. */
        AnimationComponentId component{};              /**< Stable authored occurrence represented by the instance. */
        Horo::Handle<AnimationInstanceSlotTag> slot{}; /**< Registry slot plus non-zero reuse generation. */

        /** @brief Checks representation, not current registry occupancy. @return True when every dimension is usable. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return owner.IsValid() && component.IsValid() && slot.IsValid() && slot.index < MaximumAnimationHandleSlots &&
                   slot.generation != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const AnimationInstanceHandle &) const noexcept = default;
    };

    /**
     * @brief Non-owning immutable pose identity scoped to an exact animation instance.
     *
     * The slot identifies recyclable storage while generation identifies the committed semantic pose.
     * External consumers require a separate owner-issued lease to extend storage lifetime.
     */
    struct PoseHandle final {
        AnimationInstanceHandle instance{};        /**< Exact instance that owns the pose. */
        Horo::Handle<AnimationPoseSlotTag> slot{}; /**< Recyclable pose-storage slot. */
        PoseGeneration generation{};               /**< Exact committed semantic pose generation. */

        /** @brief Checks representation, not storage residency or lease validity. @return True when all dimensions are usable. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return instance.IsValid() && slot.IsValid() && slot.index < MaximumAnimationHandleSlots && slot.generation != 0 &&
                   generation.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const PoseHandle &) const noexcept = default;
    };

    /** @brief Stable identity of one exact root-motion request staged for an authoritative tick. */
    struct RootMotionRequestId final {
        AnimationInstanceHandle instance{}; /**< Exact source instance incarnation. */
        AnimationTickId tick{};             /**< Attempted fixed tick that staged the request. */
        RootMotionGeneration generation{};  /**< Non-reusable request generation. */

        /** @brief Checks representation only. @return True when instance, tick, and generation are usable. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return instance.IsValid() && tick.IsValid() && generation.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const RootMotionRequestId &) const noexcept = default;
    };

    /**
     * @brief Frame-local immutable presentation-pose view produced from one committed source pose.
     *
     * This non-owning process-local value cannot advance animation, emit events, produce root motion,
     * or feed a later simulation tick. Render extraction must retire it with the exact frame lease.
     */
    struct PresentationPoseHandle final {
        PoseHandle source{};                          /**< Exact committed pose projected for presentation. */
        AnimationFrameId frame{};                     /**< Frame generation that owns this view. */
        Horo::Handle<PresentationPoseSlotTag> slot{}; /**< Frame-pool slot plus reuse generation. */

        /** @brief Checks representation, not frame-pool residency. @return True when all dimensions are usable. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return source.IsValid() && frame.IsValid() && slot.IsValid() && slot.index < MaximumAnimationHandleSlots &&
                   slot.generation != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const PresentationPoseHandle &) const noexcept = default;
    };

    /**
     * @brief Advances one valid non-wrapping animation generation.
     * @tparam Generation One AnimationStableIdentity generation type.
     * @param current Current non-zero generation.
     * @return Next generation, IdentityInvalid, or GenerationExhausted.
     */
    template <typename Generation> [[nodiscard]] Result<Generation> AdvanceAnimationGeneration(const Generation current) {
        if (!current.IsValid())
            return Result<Generation>::Failure(MakeError(AnimationErrors::IdentityInvalid));
        if (current.Value() == std::numeric_limits<std::uint64_t>::max())
            return Result<Generation>::Failure(MakeError(AnimationErrors::GenerationExhausted));
        return Generation::Create(current.Value() + 1U);
    }

    /**
     * @brief Validates a submitted instance against the exact currently published incarnation.
     * @param submitted Consumer-supplied non-owning handle.
     * @param current Current handle resolved by the animation owner.
     * @return Success, HandleMalformed, HandleOwnerMismatch, or HandleStale.
     * @post Success proves identity association only; the owner must still prove residency and lease state.
     */
    [[nodiscard]] Result<void> ValidateAnimationInstanceAccess(const AnimationInstanceHandle &submitted,
                                                               const AnimationInstanceHandle &current);

    /**
     * @brief Validates an immutable pose identity against the exact current instance and pose generation.
     * @param submitted Consumer-supplied pose handle.
     * @param currentInstance Current owning instance incarnation.
     * @param currentGeneration Current committed pose generation.
     * @return Success or a stable malformed, owner-mismatch, or stale-handle failure.
     * @post Success does not retain pose storage; callers still require an owner-issued immutable lease.
     */
    [[nodiscard]] Result<void> ValidatePoseAccess(const PoseHandle &submitted, const AnimationInstanceHandle &currentInstance,
                                                  PoseGeneration currentGeneration);

    /**
     * @brief Validates one root-motion request for the exact instance, tick, and current generation.
     * @param submitted Candidate request identity.
     * @param currentInstance Current animation instance incarnation.
     * @param expectedTick Exact attempted fixed tick being resolved.
     * @param currentGeneration Current request generation for that tick.
     * @return Success or a stable malformed, owner-mismatch, or stale-handle failure.
     * @post Success does not mark the request consumed; tick commit owns durable deduplication.
     */
    [[nodiscard]] Result<void> ValidateRootMotionRequestAccess(const RootMotionRequestId &submitted,
                                                               const AnimationInstanceHandle &currentInstance, AnimationTickId expectedTick,
                                                               RootMotionGeneration currentGeneration);

    /**
     * @brief Validates a frame-local presentation view against its exact source pose and frame.
     * @param submitted Candidate non-owning presentation view.
     * @param currentSource Exact committed pose projected for the current frame.
     * @param currentFrame Current presentation-frame generation.
     * @return Success or a stable malformed, owner-mismatch, or stale-handle failure.
     * @post Success neither retains the frame lease nor authorizes simulation feedback.
     */
    [[nodiscard]] Result<void> ValidatePresentationPoseAccess(const PresentationPoseHandle &submitted, const PoseHandle &currentSource,
                                                              AnimationFrameId currentFrame);
}  // namespace Horo::Animation
