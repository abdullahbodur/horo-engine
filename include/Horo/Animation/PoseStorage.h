#pragma once

/**
 * @file PoseStorage.h
 * @brief Bounded frame pose storage, immutable leases, and deterministic hierarchy evaluation.
 */

#include "Horo/Animation/AnimationIdentity.h"
#include "Horo/Animation/SkeletonAsset.h"
#include "Horo/Math/SceneMath.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace Horo::Animation {
    /** @brief Exact public pose-storage contract version. */
    struct PoseStorageContractVersion final {
        std::uint16_t major{};
        std::uint16_t minor{};
        std::uint16_t patch{};

        [[nodiscard]] constexpr auto operator<=>(const PoseStorageContractVersion &) const noexcept = default;
    };

    /** @brief Pose-storage version implemented by this API slice. */
    inline constexpr PoseStorageContractVersion CurrentPoseStorageContractVersion{1, 0, 0};

    /** @brief Compile-time safety ceilings for one frame arena. */
    struct PoseStorageHardLimits final {
        static constexpr std::uint32_t PosesPerFrame = 4'096;                           /**< Absolute frame pose ceiling. */
        static constexpr std::uint32_t JointsPerPose = SkeletonAssetHardLimits::Joints; /**< Absolute joint ceiling. */
        static constexpr std::uint32_t LeasesPerPose = 64; /**< Absolute concurrent immutable-consumer ceiling. */
    };

    /** @brief Finite storage policy captured once when an arena is created. */
    struct PoseStorageLimits final {
        std::uint32_t maximumPosesPerFrame{256}; /**< Exact number of recyclable slots reserved at creation. */
        std::uint32_t maximumJointsPerPose{PoseStorageHardLimits::JointsPerPose}; /**< Maximum admitted skeleton size. */

        [[nodiscard]] constexpr auto operator<=>(const PoseStorageLimits &) const noexcept = default;
    };

    /** @brief Owner admission snapshot used to fail closed before allocating arena storage. */
    enum class PoseStorageAdmissionState : std::uint8_t {
        Accepting,
        CancellationRequested,
        ShuttingDown,
        Count
    };

    /** @brief Immutable arena construction inputs captured at an animation owner safe point. */
    struct PoseStorageCreateContext final {
        PoseStorageContractVersion contractVersion{CurrentPoseStorageContractVersion}; /**< Exact caller schema. */
        PoseStorageAdmissionState admission{PoseStorageAdmissionState::Accepting};     /**< Captured owner lifecycle. */
        AnimationRuntimeId runtime{};                 /**< Exact runtime incarnation owning all mutation. */
        SkeletonAssetGeneration skeletonGeneration{}; /**< Exact immutable skeleton publication. */
        PoseStorageLimits limits{};                   /**< Finite capacity fixed for the arena lifetime. */
    };

    /** @brief Allocation-free counters for capacity diagnostics. */
    struct PoseStorageStatistics final {
        std::uint32_t capacityPoses{};     /**< Immutable slot capacity. */
        std::uint32_t usedPoses{};         /**< Slots occupied in the current frame. */
        std::uint32_t peakUsedPoses{};     /**< Lifetime high-water mark. */
        std::uint64_t failedAllocations{}; /**< Lifetime exhausted-allocation count. */
        std::uint32_t activeLeases{};      /**< Immutable leases currently pinning slots. */

        [[nodiscard]] constexpr auto operator<=>(const PoseStorageStatistics &) const noexcept = default;
    };

    namespace Detail {
        struct PoseArenaState;
    }

    /**
     * @brief Move-only immutable lease over one exact pose generation.
     *
     * A lease may be moved to and read or destroyed on another thread. It pins its slot and
     * prevents owner-thread mutation, cancellation, reset, and shutdown until release.
     */
    class PoseReadLease final {
    public:
        /** @brief Constructs an inactive lease that owns no storage. */
        PoseReadLease() noexcept = default;
        PoseReadLease(const PoseReadLease &) = delete;
        PoseReadLease &operator=(const PoseReadLease &) = delete;
        /** @brief Transfers one lease without changing its pin count. @param other Source made inactive. */
        PoseReadLease(PoseReadLease &&other) noexcept;
        /** @brief Releases the current pin and transfers another. @param other Source made inactive. @return This lease. */
        PoseReadLease &operator=(PoseReadLease &&other) noexcept;
        /** @brief Releases the exact pinned slot; safe on a consumer thread. */
        ~PoseReadLease();

        /** @brief Checks whether this object pins an exact live slot. @return True for an active lease. */
        [[nodiscard]] bool IsValid() const noexcept;

        /** @brief Returns the exact leased semantic pose identity. @return Non-owning pose handle. */
        [[nodiscard]] PoseHandle Handle() const noexcept;

        /** @brief Returns the frame that owns the storage. @return Exact frame identity. */
        [[nodiscard]] AnimationFrameId Frame() const noexcept;

        /** @brief Returns immutable local transforms in canonical skeleton order. @return Lease-scoped contiguous view. */
        [[nodiscard]] std::span<const Math::Transform> LocalTransforms() const noexcept;

        /**
         * @brief Reads a cached model-space matrix by stable joint identity.
         * @param joint Stable joint in the arena's immutable skeleton.
         * @return Matrix, PoseJointMissing, or PoseNotEvaluated when the requested branch remains dirty.
         */
        [[nodiscard]] Result<Math::Mat4> ModelSpace(JointId joint) const;

    private:
        friend class PoseFrameArena;
        /** @brief Creates one internal pin already counted by the arena. @param state Shared lifetime state. @param slot Pinned slot. */
        PoseReadLease(std::shared_ptr<Detail::PoseArenaState> state, std::uint32_t slot) noexcept;
        /** @brief Releases this lease's pin when active. */
        void Release() noexcept;

        std::shared_ptr<Detail::PoseArenaState> state_{};
        std::uint32_t slot_{Horo::Handle<AnimationPoseSlotTag>::InvalidIndex};
    };

    /**
     * @brief Preallocated frame arena owning local poses and lazily cached model matrices.
     *
     * Creation may allocate. All later frame, allocation, dirty propagation, evaluation, lease,
     * cancellation, and shutdown operations are bounded and allocation-free. Mutable methods must
     * run on the thread that called Create. Immutable leases may be consumed and released on other
     * threads. The arena need not outlive a lease; shared internal state makes late lease
     * destruction safe without extending admission or mutation authority.
     */
    class PoseFrameArena final {
    public:
        PoseFrameArena(const PoseFrameArena &) = delete;
        PoseFrameArena &operator=(const PoseFrameArena &) = delete;
        /** @brief Transfers the sole mutable arena facade without moving preallocated storage. */
        PoseFrameArena(PoseFrameArena &&) noexcept = default;
        /** @brief Transfers the sole mutable facade. @return This arena. */
        PoseFrameArena &operator=(PoseFrameArena &&) noexcept = default;
        /** @brief Releases the facade; active immutable leases safely retain only read lifetime. */
        ~PoseFrameArena() = default;

        /**
         * @brief Preallocates bounded storage for one runtime and skeleton publication.
         * @param skeleton Immutable canonical hierarchy retained by value-independent metadata.
         * @param context Version, admission, runtime, generation, and capacity snapshot.
         * @return Arena or a stable version, lifecycle, identity, or limit failure.
         * @pre Owner/control-thread setup boundary; not frame-hot.
         */
        [[nodiscard]] static Result<PoseFrameArena> Create(const SkeletonAsset &skeleton, const PoseStorageCreateContext &context);

        /**
         * @brief Begins a strictly newer frame and recycles all unleased prior-frame slots.
         * @param frame New non-zero frame identity.
         * @param skeleton Current persistent skeleton identity.
         * @param generation Current immutable skeleton publication generation.
         * @return Success or a stable thread, lifecycle, stale-publication, lease, or generation failure.
         */
        [[nodiscard]] Result<void> BeginFrame(AnimationFrameId frame, SkeletonId skeleton, SkeletonAssetGeneration generation);

        /**
         * @brief Copies one complete local pose into the next preallocated slot.
         * @param instance Exact instance owned by this arena's runtime.
         * @param generation Non-reusable semantic pose generation.
         * @param localTransforms Complete finite local transforms in canonical skeleton order.
         * @return Exact pose handle or a stable lifecycle, malformed, limit, transform, or exhaustion failure.
         */
        [[nodiscard]] Result<PoseHandle> AllocatePose(const AnimationInstanceHandle &instance, PoseGeneration generation,
                                                      std::span<const Math::Transform> localTransforms);

        /**
         * @brief Replaces one local transform and dirties that joint's complete descendant branch.
         * @param pose Exact current-frame pose.
         * @param joint Stable joint identity.
         * @param transform Finite non-singular local transform.
         * @return Success or a stable handle, joint, transform, thread, lifecycle, or lease failure.
         */
        [[nodiscard]] Result<void> SetLocalTransform(const PoseHandle &pose, JointId joint, const Math::Transform &transform);

        /**
         * @brief Evaluates all dirty matrices or only the requested joints and their ancestors.
         * @param pose Exact current-frame pose.
         * @param requestedJoints Empty for the complete hierarchy; otherwise a stable partial-evaluation set.
         * @return Success or a stable handle, joint, thread, lifecycle, transform, or lease failure.
         */
        [[nodiscard]] Result<void> EvaluateModelSpace(const PoseHandle &pose, std::span<const JointId> requestedJoints = {});

        /**
         * @brief Pins one exact current-frame pose for immutable cross-thread consumption.
         * @param pose Exact current-frame pose.
         * @return Move-only lease or a stable handle, thread, lifecycle, or lease-capacity failure.
         */
        [[nodiscard]] Result<PoseReadLease> AcquireReadLease(const PoseHandle &pose);

        /**
         * @brief Atomically abandons the active frame after all leases retire.
         * @return Success, PoseLeaseConflict, PoseThreadViolation, or an idempotent success when already cancelled.
         */
        [[nodiscard]] Result<void> CancelFrame();

        /**
         * @brief Idempotently closes admission and retires storage after all leases release.
         * @return Success, PoseLeaseConflict, or PoseThreadViolation.
         */
        [[nodiscard]] Result<void> Shutdown();

        /** @brief Returns allocation-free arena diagnostics. @return Current capacity, use, peak, failure, and lease counts. */
        [[nodiscard]] PoseStorageStatistics Statistics() const noexcept;

    private:
        explicit PoseFrameArena(std::shared_ptr<Detail::PoseArenaState> state) noexcept : state_(std::move(state)) {}

        std::shared_ptr<Detail::PoseArenaState> state_;
    };
}  // namespace Horo::Animation
