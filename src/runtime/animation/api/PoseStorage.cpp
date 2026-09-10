#include "Horo/Animation/PoseStorage.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace Horo::Animation {
    namespace Detail {
        enum class PoseArenaLifecycle : std::uint8_t {
            WaitingForFrame,
            Active,
            Cancelled,
            ShutDown
        };

        struct PoseSlot final {
            std::atomic<std::uint32_t> leases{0};
            PoseHandle handle{};
            std::uint32_t storageGeneration{1};
            bool occupied{false};
        };

        struct JointLookup final {
            JointId id{};
            std::uint32_t index{};
        };

        struct PoseArenaState final {
            AnimationRuntimeId runtime{};
            SkeletonId skeleton{};
            SkeletonAssetGeneration skeletonGeneration{};
            AnimationFrameId frame{};
            PoseStorageLimits limits{};
            std::thread::id ownerThread{};
            PoseArenaLifecycle lifecycle{PoseArenaLifecycle::WaitingForFrame};
            std::uint32_t jointCount{};
            std::vector<PoseSlot> slots{};
            std::vector<Math::Transform> localTransforms{};
            std::vector<Math::Mat4> modelMatrices{};
            std::vector<std::uint8_t> dirty{};
            std::vector<std::uint32_t> parentIndices{};
            std::vector<JointLookup> jointLookup{};
            std::vector<std::uint8_t> evaluationScratch{};
            std::atomic<std::uint32_t> usedPoses{0};
            std::atomic<std::uint32_t> peakUsedPoses{0};
            std::atomic<std::uint64_t> failedAllocations{0};
            std::atomic<std::uint32_t> activeLeases{0};
        };
    }  // namespace Detail

    namespace {
        constexpr std::uint32_t NoParent = std::numeric_limits<std::uint32_t>::max();

        Result<void> OwnerThreadResult(const Detail::PoseArenaState &state) {
            if (std::this_thread::get_id() != state.ownerThread)
                return Result<void>::Failure(MakeError(AnimationErrors::PoseThreadViolation));
            return Result<void>::Success();
        }

        Result<void> ActiveResult(const Detail::PoseArenaState &state) {
            if (state.lifecycle == Detail::PoseArenaLifecycle::Cancelled)
                return Result<void>::Failure(MakeError(AnimationErrors::PoseEvaluationCancelled));
            if (state.lifecycle != Detail::PoseArenaLifecycle::Active)
                return Result<void>::Failure(MakeError(AnimationErrors::PoseAdmissionRejected));
            return Result<void>::Success();
        }

        std::uint32_t FindJointIndex(const Detail::PoseArenaState &state, const JointId joint) {
            const auto found = std::ranges::lower_bound(state.jointLookup, joint, {}, &Detail::JointLookup::id);
            return found != state.jointLookup.end() && found->id == joint ? found->index : NoParent;
        }

        Result<std::uint32_t> ResolvePoseSlot(const Detail::PoseArenaState &state, const PoseHandle &pose) {
            if (!pose.IsValid())
                return Result<std::uint32_t>::Failure(MakeError(AnimationErrors::HandleMalformed));
            if (pose.instance.owner != state.runtime)
                return Result<std::uint32_t>::Failure(MakeError(AnimationErrors::HandleOwnerMismatch));
            if (pose.slot.index >= state.limits.maximumPosesPerFrame)
                return Result<std::uint32_t>::Failure(MakeError(AnimationErrors::HandleMalformed));
            if (const Detail::PoseSlot &slot = state.slots[pose.slot.index]; !slot.occupied || slot.handle != pose)
                return Result<std::uint32_t>::Failure(MakeError(AnimationErrors::HandleStale));
            return Result<std::uint32_t>::Success(pose.slot.index);
        }

        bool HasLeases(const Detail::PoseArenaState &state) noexcept {
            return state.activeLeases.load() != 0;
        }

        Result<void> RetireSlots(Detail::PoseArenaState &state) {
            if (HasLeases(state))
                return Result<void>::Failure(MakeError(AnimationErrors::PoseLeaseConflict));
            const std::uint32_t used = state.usedPoses.load();
            for (std::uint32_t index = 0; index < used; ++index) {
                if (state.slots[index].storageGeneration == std::numeric_limits<std::uint32_t>::max())
                    return Result<void>::Failure(MakeError(AnimationErrors::GenerationExhausted));
            }
            for (std::uint32_t index = 0; index < used; ++index) {
                Detail::PoseSlot &slot = state.slots[index];
                ++slot.storageGeneration;
                slot.handle = {};
                slot.occupied = false;
            }
            state.usedPoses.store(0);
            return Result<void>::Success();
        }

        Result<void> ValidateTransform(const Math::Transform &transform) {
            if (transform.TryToMatrix().HasError())
                return Result<void>::Failure(MakeError(AnimationErrors::PoseTransformInvalid));
            return Result<void>::Success();
        }

        std::size_t PoseOffset(const Detail::PoseArenaState &state, const std::uint32_t slot) noexcept {
            return static_cast<std::size_t>(slot) * state.jointCount;
        }

        Result<void> ValidateCreateAdmission(const PoseStorageCreateContext &context) {
            if (context.contractVersion != CurrentPoseStorageContractVersion)
                return Result<void>::Failure(MakeError(AnimationErrors::PoseVersionUnsupported));
            if (context.admission == PoseStorageAdmissionState::CancellationRequested)
                return Result<void>::Failure(MakeError(AnimationErrors::PoseEvaluationCancelled));
            if (context.admission != PoseStorageAdmissionState::Accepting)
                return Result<void>::Failure(MakeError(AnimationErrors::PoseAdmissionRejected));
            if (!context.runtime.IsValid() || !context.skeletonGeneration.IsValid())
                return Result<void>::Failure(MakeError(AnimationErrors::IdentityInvalid));
            return Result<void>::Success();
        }

        Result<void> ValidateCreateLimits(const PoseStorageLimits &limits, const std::size_t jointCount) {
            if (const bool invalidPoseLimit =
                    limits.maximumPosesPerFrame == 0 || limits.maximumPosesPerFrame > PoseStorageHardLimits::PosesPerFrame;
                invalidPoseLimit || limits.maximumJointsPerPose == 0 ||
                limits.maximumJointsPerPose > PoseStorageHardLimits::JointsPerPose || jointCount == 0 ||
                jointCount > limits.maximumJointsPerPose)
                return Result<void>::Failure(MakeError(AnimationErrors::PoseLimitExceeded));
            return Result<void>::Success();
        }

        Result<void> InitializeHierarchy(Detail::PoseArenaState &state, const SkeletonAsset &skeleton) {
            const auto &joints = skeleton.Data().joints;
            for (std::uint32_t index = 0; index < state.jointCount; ++index) {
                state.jointLookup.push_back({joints[index].id, index});
                if (!joints[index].parent)
                    continue;
                const auto parent = std::find_if(joints.begin(), joints.begin() + index, [&](const SkeletonJoint &candidate) {
                    return candidate.id == *joints[index].parent;
                });
                if (parent == joints.begin() + index)
                    return Result<void>::Failure(MakeError(AnimationErrors::PoseJointMissing));
                state.parentIndices[index] = static_cast<std::uint32_t>(std::distance(joints.begin(), parent));
            }
            std::ranges::sort(state.jointLookup, [](const Detail::JointLookup &left, const Detail::JointLookup &right) {
                return left.id < right.id;
            });
            return Result<void>::Success();
        }

        Result<void> ValidateFrameBinding(const Detail::PoseArenaState &state, const AnimationFrameId frame, const SkeletonId skeleton,
                                          const SkeletonAssetGeneration generation) {
            if (!frame.IsValid() || !skeleton.IsValid() || !generation.IsValid())
                return Result<void>::Failure(MakeError(AnimationErrors::IdentityInvalid));
            if (skeleton != state.skeleton)
                return Result<void>::Failure(MakeError(AnimationErrors::PoseSkeletonMismatch));
            if (generation != state.skeletonGeneration)
                return Result<void>::Failure(MakeError(AnimationErrors::PoseSkeletonStale));
            if (state.frame.IsValid() && frame.Value() <= state.frame.Value())
                return Result<void>::Failure(MakeError(AnimationErrors::PoseFrameStale));
            return Result<void>::Success();
        }

        Result<void> ValidatePoseCandidate(const Detail::PoseArenaState &state, const AnimationInstanceHandle &instance,
                                           const PoseGeneration generation, const std::span<const Math::Transform> transforms) {
            if (!instance.IsValid() || !generation.IsValid())
                return Result<void>::Failure(MakeError(AnimationErrors::HandleMalformed));
            if (instance.owner != state.runtime)
                return Result<void>::Failure(MakeError(AnimationErrors::HandleOwnerMismatch));
            if (transforms.size() != state.jointCount)
                return Result<void>::Failure(MakeError(AnimationErrors::PoseLimitExceeded));
            for (const Math::Transform &transform : transforms) {
                if (ValidateTransform(transform).HasError())
                    return Result<void>::Failure(MakeError(AnimationErrors::PoseTransformInvalid));
            }
            return Result<void>::Success();
        }

        Result<std::uint32_t> ResolveMutablePoseSlot(const Detail::PoseArenaState &state, const PoseHandle &pose) {
            const auto slot = ResolvePoseSlot(state, pose);
            if (slot.HasError())
                return slot;
            if (state.slots[slot.Value()].leases.load() != 0)
                return Result<std::uint32_t>::Failure(MakeError(AnimationErrors::PoseLeaseConflict));
            return slot;
        }

        Result<std::uint32_t> ResolveActiveMutablePose(Detail::PoseArenaState &state, const PoseHandle &pose) {
            if (const auto owner = OwnerThreadResult(state); owner.HasError())
                return Result<std::uint32_t>::Failure(owner.ErrorValue());
            if (const auto active = ActiveResult(state); active.HasError())
                return Result<std::uint32_t>::Failure(active.ErrorValue());
            return ResolveMutablePoseSlot(state, pose);
        }

        Result<void> PrepareEvaluationMask(Detail::PoseArenaState &state, const std::span<const JointId> requestedJoints) {
            std::ranges::fill(state.evaluationScratch, requestedJoints.empty() ? 1U : 0U);
            for (const JointId joint : requestedJoints) {
                std::uint32_t index = FindJointIndex(state, joint);
                if (index == NoParent)
                    return Result<void>::Failure(MakeError(AnimationErrors::PoseJointMissing));
                while (index != NoParent && state.evaluationScratch[index] == 0) {
                    state.evaluationScratch[index] = 1;
                    index = state.parentIndices[index];
                }
            }
            return Result<void>::Success();
        }

        Result<void> EvaluateDirtyMatrices(Detail::PoseArenaState &state, const std::uint32_t slotIndex) {
            const std::size_t offset = PoseOffset(state, slotIndex);
            for (std::uint32_t index = 0; index < state.jointCount; ++index) {
                if (state.evaluationScratch[index] == 0 || state.dirty[offset + index] == 0)
                    continue;
                const auto local = state.localTransforms[offset + index].TryToMatrix();
                if (local.HasError())
                    return Result<void>::Failure(MakeError(AnimationErrors::PoseTransformInvalid));
                const std::uint32_t parent = state.parentIndices[index];
                state.modelMatrices[offset + index] =
                    parent == NoParent ? local.Value() : Math::Multiply(state.modelMatrices[offset + parent], local.Value());
                state.dirty[offset + index] = 0;
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc PoseReadLease::PoseReadLease(std::shared_ptr<Detail::PoseArenaState>,std::uint32_t) */
    PoseReadLease::PoseReadLease(std::shared_ptr<Detail::PoseArenaState> state, const std::uint32_t slot) noexcept
        : state_(std::move(state)), slot_(slot) {}

    /** @copydoc PoseReadLease::PoseReadLease(PoseReadLease&&) */
    PoseReadLease::PoseReadLease(PoseReadLease &&other) noexcept : state_(std::move(other.state_)), slot_(other.slot_) {
        other.slot_ = Horo::Handle<AnimationPoseSlotTag>::InvalidIndex;
    }

    /** @copydoc PoseReadLease::operator=(PoseReadLease&&) */
    PoseReadLease &PoseReadLease::operator=(PoseReadLease &&other) noexcept {
        if (this == &other)
            return *this;
        Release();
        state_ = std::move(other.state_);
        slot_ = other.slot_;
        other.slot_ = Horo::Handle<AnimationPoseSlotTag>::InvalidIndex;
        return *this;
    }

    /** @copydoc PoseReadLease::~PoseReadLease */
    PoseReadLease::~PoseReadLease() {
        Release();
    }

    /** @copydoc PoseReadLease::IsValid */
    bool PoseReadLease::IsValid() const noexcept {
        return state_ && slot_ < state_->limits.maximumPosesPerFrame && state_->slots[slot_].occupied;
    }

    /** @copydoc PoseReadLease::Handle */
    PoseHandle PoseReadLease::Handle() const noexcept {
        return IsValid() ? state_->slots[slot_].handle : PoseHandle{};
    }

    /** @copydoc PoseReadLease::Frame */
    AnimationFrameId PoseReadLease::Frame() const noexcept {
        return IsValid() ? state_->frame : AnimationFrameId{};
    }

    /** @copydoc PoseReadLease::LocalTransforms */
    std::span<const Math::Transform> PoseReadLease::LocalTransforms() const noexcept {
        if (!IsValid())
            return {};
        return {state_->localTransforms.data() + PoseOffset(*state_, slot_), state_->jointCount};
    }

    /** @copydoc PoseReadLease::ModelSpace */
    Result<Math::Mat4> PoseReadLease::ModelSpace(const JointId joint) const {
        if (!IsValid())
            return Result<Math::Mat4>::Failure(MakeError(AnimationErrors::HandleStale));
        const std::uint32_t jointIndex = FindJointIndex(*state_, joint);
        if (jointIndex == NoParent)
            return Result<Math::Mat4>::Failure(MakeError(AnimationErrors::PoseJointMissing));
        const std::size_t index = PoseOffset(*state_, slot_) + jointIndex;
        if (state_->dirty[index] != 0)
            return Result<Math::Mat4>::Failure(MakeError(AnimationErrors::PoseNotEvaluated));
        return Result<Math::Mat4>::Success(state_->modelMatrices[index]);
    }

    void PoseReadLease::Release() noexcept {
        if (!state_ || slot_ >= state_->limits.maximumPosesPerFrame)
            return;
        state_->slots[slot_].leases.fetch_sub(1);
        state_->activeLeases.fetch_sub(1);
        state_.reset();
        slot_ = Horo::Handle<AnimationPoseSlotTag>::InvalidIndex;
    }

    /** @copydoc PoseFrameArena::Create */
    Result<PoseFrameArena> PoseFrameArena::Create(const SkeletonAsset &skeleton, const PoseStorageCreateContext &context) {
        if (const auto admission = ValidateCreateAdmission(context); admission.HasError())
            return Result<PoseFrameArena>::Failure(admission.ErrorValue());
        const std::size_t jointCount = skeleton.Data().joints.size();
        const PoseStorageLimits &limits = context.limits;
        if (const auto validatedLimits = ValidateCreateLimits(limits, jointCount); validatedLimits.HasError())
            return Result<PoseFrameArena>::Failure(validatedLimits.ErrorValue());

        auto state = std::make_shared<Detail::PoseArenaState>();
        state->runtime = context.runtime;
        state->skeleton = skeleton.Data().skeleton;
        state->skeletonGeneration = context.skeletonGeneration;
        state->limits = limits;
        state->ownerThread = std::this_thread::get_id();
        state->jointCount = static_cast<std::uint32_t>(jointCount);
        state->slots = std::vector<Detail::PoseSlot>(limits.maximumPosesPerFrame);
        const std::size_t elementCount = static_cast<std::size_t>(limits.maximumPosesPerFrame) * jointCount;
        state->localTransforms.resize(elementCount);
        state->modelMatrices.resize(elementCount);
        state->dirty.resize(elementCount);
        state->parentIndices.resize(jointCount, NoParent);
        state->jointLookup.reserve(jointCount);
        state->evaluationScratch.resize(jointCount);

        if (const auto hierarchy = InitializeHierarchy(*state, skeleton); hierarchy.HasError())
            return Result<PoseFrameArena>::Failure(hierarchy.ErrorValue());
        return Result<PoseFrameArena>::Success(PoseFrameArena{std::move(state)});
    }

    /** @copydoc PoseFrameArena::BeginFrame */
    Result<void> PoseFrameArena::BeginFrame(  // NOSONAR(cpp:S5817) Mutable access is reserved for the sole owner facade.
        const AnimationFrameId frame, const SkeletonId skeleton, const SkeletonAssetGeneration generation) {
        if (const auto owner = OwnerThreadResult(*state_); owner.HasError())
            return owner;
        if (state_->lifecycle == Detail::PoseArenaLifecycle::ShutDown)
            return Result<void>::Failure(MakeError(AnimationErrors::PoseAdmissionRejected));
        if (const auto binding = ValidateFrameBinding(*state_, frame, skeleton, generation); binding.HasError())
            return binding;
        if (const auto retired = RetireSlots(*state_); retired.HasError())
            return retired;
        state_->frame = frame;
        state_->lifecycle = Detail::PoseArenaLifecycle::Active;
        return Result<void>::Success();
    }

    /** @copydoc PoseFrameArena::AllocatePose */
    Result<PoseHandle> PoseFrameArena::AllocatePose(  // NOSONAR(cpp:S5817) Mutable access is reserved for the sole owner facade.
        const AnimationInstanceHandle &instance, const PoseGeneration generation, const std::span<const Math::Transform> localTransforms) {
        if (const auto owner = OwnerThreadResult(*state_); owner.HasError())
            return Result<PoseHandle>::Failure(owner.ErrorValue());
        if (const auto active = ActiveResult(*state_); active.HasError())
            return Result<PoseHandle>::Failure(active.ErrorValue());
        if (const auto candidate = ValidatePoseCandidate(*state_, instance, generation, localTransforms); candidate.HasError())
            return Result<PoseHandle>::Failure(candidate.ErrorValue());

        const std::uint32_t slotIndex = state_->usedPoses.load();
        if (slotIndex >= state_->limits.maximumPosesPerFrame) {
            state_->failedAllocations.fetch_add(1);
            return Result<PoseHandle>::Failure(MakeError(AnimationErrors::PoseArenaExhausted));
        }
        Detail::PoseSlot &slot = state_->slots[slotIndex];
        slot.handle = {.instance = instance, .slot = {.index = slotIndex, .generation = slot.storageGeneration}, .generation = generation};
        slot.occupied = true;
        const std::size_t offset = PoseOffset(*state_, slotIndex);
        std::ranges::copy(localTransforms, state_->localTransforms.begin() + static_cast<std::ptrdiff_t>(offset));
        std::fill_n(state_->dirty.data() + offset, state_->jointCount, std::uint8_t{1});
        const std::uint32_t used = slotIndex + 1U;
        state_->usedPoses.store(used);
        state_->peakUsedPoses.store(std::max(used, state_->peakUsedPoses.load()));
        return Result<PoseHandle>::Success(slot.handle);
    }

    /** @copydoc PoseFrameArena::SetLocalTransform */
    Result<void> PoseFrameArena::SetLocalTransform(  // NOSONAR(cpp:S5817) Mutable access is reserved for the sole owner facade.
        const PoseHandle &pose, const JointId joint, const Math::Transform &transform) {
        const auto slotResult = ResolveActiveMutablePose(*state_, pose);
        if (slotResult.HasError())
            return Result<void>::Failure(slotResult.ErrorValue());
        const std::uint32_t slotIndex = slotResult.Value();
        const std::uint32_t jointIndex = FindJointIndex(*state_, joint);
        if (jointIndex == NoParent)
            return Result<void>::Failure(MakeError(AnimationErrors::PoseJointMissing));
        if (ValidateTransform(transform).HasError())
            return Result<void>::Failure(MakeError(AnimationErrors::PoseTransformInvalid));

        const std::size_t offset = PoseOffset(*state_, slotIndex);
        state_->localTransforms[offset + jointIndex] = transform;
        state_->dirty[offset + jointIndex] = 1;
        for (std::uint32_t index = jointIndex + 1U; index < state_->jointCount; ++index) {
            const std::uint32_t parent = state_->parentIndices[index];
            if (parent != NoParent && state_->dirty[offset + parent] != 0)
                state_->dirty[offset + index] = 1;
        }
        return Result<void>::Success();
    }

    /** @copydoc PoseFrameArena::EvaluateModelSpace */
    Result<void> PoseFrameArena::EvaluateModelSpace(  // NOSONAR(cpp:S5817) Mutable access is reserved for the sole owner facade.
        const PoseHandle &pose, const std::span<const JointId> requestedJoints) {
        const auto slotResult = ResolveActiveMutablePose(*state_, pose);
        if (slotResult.HasError())
            return Result<void>::Failure(slotResult.ErrorValue());
        const std::uint32_t slotIndex = slotResult.Value();
        if (const auto prepared = PrepareEvaluationMask(*state_, requestedJoints); prepared.HasError())
            return prepared;
        return EvaluateDirtyMatrices(*state_, slotIndex);
    }

    /** @copydoc PoseFrameArena::AcquireReadLease */
    Result<PoseReadLease> PoseFrameArena::AcquireReadLease(  // NOSONAR(cpp:S5817) Mutable access is reserved for the sole owner facade.
        const PoseHandle &pose) {
        if (const auto owner = OwnerThreadResult(*state_); owner.HasError())
            return Result<PoseReadLease>::Failure(owner.ErrorValue());
        if (const auto active = ActiveResult(*state_); active.HasError())
            return Result<PoseReadLease>::Failure(active.ErrorValue());
        const auto slotResult = ResolvePoseSlot(*state_, pose);
        if (slotResult.HasError())
            return Result<PoseReadLease>::Failure(slotResult.ErrorValue());
        const std::uint32_t slotIndex = slotResult.Value();
        Detail::PoseSlot &slot = state_->slots[slotIndex];
        if (slot.leases.load() >= PoseStorageHardLimits::LeasesPerPose)
            return Result<PoseReadLease>::Failure(MakeError(AnimationErrors::PoseLimitExceeded));
        slot.leases.fetch_add(1);
        state_->activeLeases.fetch_add(1);
        return Result<PoseReadLease>::Success(PoseReadLease{state_, slotIndex});
    }

    /** @copydoc PoseFrameArena::CancelFrame */
    Result<void> PoseFrameArena::CancelFrame() {  // NOSONAR(cpp:S5817) Mutable access is reserved for the sole owner facade.
        using enum Detail::PoseArenaLifecycle;
        if (const auto owner = OwnerThreadResult(*state_); owner.HasError())
            return owner;
        if (state_->lifecycle == Cancelled)
            return Result<void>::Success();
        if (state_->lifecycle == ShutDown)
            return Result<void>::Failure(MakeError(AnimationErrors::PoseAdmissionRejected));
        if (const auto retired = RetireSlots(*state_); retired.HasError())
            return retired;
        state_->lifecycle = Cancelled;
        return Result<void>::Success();
    }

    /** @copydoc PoseFrameArena::Shutdown */
    Result<void> PoseFrameArena::Shutdown() {  // NOSONAR(cpp:S5817) Mutable access is reserved for the sole owner facade.
        using enum Detail::PoseArenaLifecycle;
        if (const auto owner = OwnerThreadResult(*state_); owner.HasError())
            return owner;
        if (state_->lifecycle == ShutDown)
            return Result<void>::Success();
        if (const auto retired = RetireSlots(*state_); retired.HasError())
            return retired;
        state_->lifecycle = ShutDown;
        state_->frame = {};
        return Result<void>::Success();
    }

    /** @copydoc PoseFrameArena::Statistics */
    PoseStorageStatistics PoseFrameArena::Statistics() const noexcept {
        return {.capacityPoses = state_->limits.maximumPosesPerFrame,
                .usedPoses = state_->usedPoses.load(),
                .peakUsedPoses = state_->peakUsedPoses.load(),
                .failedAllocations = state_->failedAllocations.load(),
                .activeLeases = state_->activeLeases.load()};
    }
}  // namespace Horo::Animation
