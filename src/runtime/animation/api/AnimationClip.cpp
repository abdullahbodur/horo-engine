#include "Horo/Animation/AnimationClip.h"

#include "Horo/Animation/AnimationErrors.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>

namespace Horo::Animation {
    namespace {
        constexpr float MinimumScaleMagnitude = 1.0e-8F;

        template <typename Value> [[nodiscard]] Result<Value> Reject(const ErrorCodeDescriptor &descriptor, const std::string_view detail) {
            return Result<Value>::Failure(MakeError(descriptor, std::string{detail}));
        }

        [[nodiscard]] Result<void> ValidateAdmission(const AnimationClipAdmissionState admission) {
            if (admission == AnimationClipAdmissionState::CancellationRequested)
                return Reject<void>(AnimationErrors::ClipOperationCancelled, "The clip operation was cancelled before it began.");
            if (admission != AnimationClipAdmissionState::Accepting)
                return Reject<void>(AnimationErrors::ClipAdmissionRejected, "The clip owner is not accepting work.");
            return Result<void>::Success();
        }

        [[nodiscard]] bool IsKnown(const AnimationWrapMode value) noexcept {
            return value < AnimationWrapMode::Count;
        }

        [[nodiscard]] bool IsKnown(const AnimationClipKind value) noexcept {
            return value < AnimationClipKind::Count;
        }

        [[nodiscard]] bool IsKnown(const AnimationCompressionScheme value) noexcept {
            return value < AnimationCompressionScheme::Count;
        }

        [[nodiscard]] bool IsKnown(const AnimationInterpolation value) noexcept {
            return value < AnimationInterpolation::Count;
        }

        [[nodiscard]] bool IsFiniteTransform(const Math::Transform &transform) noexcept {
            return Math::IsFinite(transform.translation) && Math::IsFinite(transform.rotation) && Math::IsFinite(transform.scale) &&
                   std::abs(transform.scale.x) > MinimumScaleMagnitude && std::abs(transform.scale.y) > MinimumScaleMagnitude &&
                   std::abs(transform.scale.z) > MinimumScaleMagnitude && transform.rotation.TryNormalized().HasValue();
        }

        [[nodiscard]] Result<void> ValidateLimits(const AnimationClipLimits &limits) {
            if (limits.maximumTracks == 0 || limits.maximumTracks > AnimationClipHardLimits::Tracks || limits.maximumKeysPerTrack == 0 ||
                limits.maximumKeysPerTrack > AnimationClipHardLimits::KeysPerTrack || limits.maximumTotalKeys == 0 ||
                limits.maximumTotalKeys > AnimationClipHardLimits::TotalKeys ||
                limits.maximumBoundaryCrossings > AnimationClipHardLimits::BoundaryCrossings)
                return Reject<void>(AnimationErrors::ClipLimitExceeded, "The requested clip limits exceed hard safety ceilings.");
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateDescriptorIdentity(const AnimationClipDescriptor &descriptor, const SkeletonAsset &skeleton) {
            if (descriptor.contractVersion != CurrentAnimationClipContractVersion)
                return Reject<void>(AnimationErrors::ClipVersionUnsupported, "The clip contract version is not supported.");
            if (!descriptor.id.IsValid() || !descriptor.generation.IsValid() || !descriptor.skeleton.IsValid() ||
                !descriptor.skeletonGeneration.IsValid())
                return Reject<void>(AnimationErrors::IdentityInvalid, "The clip or asset binding uses a reserved identity.");
            if (descriptor.skeleton != skeleton.Data().skeleton || skeleton.Data().contractVersion != CurrentSkeletonAssetContractVersion)
                return Reject<void>(AnimationErrors::ClipSkeletonMismatch, "The clip does not bind the supplied skeleton contract.");
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateDescriptorTiming(const AnimationClipDescriptor &descriptor) {
            if (descriptor.duration.ticks <= 0 || descriptor.duration.ticks > AnimationClipHardLimits::DurationTicks)
                return Reject<void>(AnimationErrors::ClipLimitExceeded, "The clip duration is outside the portable bounded time domain.");
            if (descriptor.sampleRate.numerator == 0 || descriptor.sampleRate.denominator == 0 ||
                descriptor.sampleRate.numerator > AnimationClipHardLimits::SampleRateNumerator ||
                descriptor.sampleRate.denominator > AnimationClipHardLimits::SampleRateDenominator)
                return Reject<void>(AnimationErrors::ClipLimitExceeded, "The clip sample rate is zero or outside finite limits.");
            if (std::gcd(descriptor.sampleRate.numerator, descriptor.sampleRate.denominator) != 1U)
                return Reject<void>(AnimationErrors::ClipMalformed, "The clip sample rate must be a canonical reduced rational.");
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateDescriptorKind(const AnimationClipDescriptor &descriptor) {
            if (!IsKnown(descriptor.wrapMode) || !IsKnown(descriptor.kind) || !IsKnown(descriptor.compression))
                return Reject<void>(AnimationErrors::ClipUnsupported, "The clip uses unknown typed playback metadata.");
            if (descriptor.kind == AnimationClipKind::Additive) {
                if (!descriptor.referencePose || !descriptor.referencePose->id.IsValid() || !descriptor.referencePose->generation.IsValid())
                    return Reject<void>(AnimationErrors::ClipReferencePoseMismatch,
                                        "An additive clip requires an exact reference-pose identity and generation.");
            } else if (descriptor.referencePose) {
                return Reject<void>(AnimationErrors::ClipMalformed, "An absolute clip cannot declare an additive reference pose.");
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateDescriptor(const AnimationClipDescriptor &descriptor, const SkeletonAsset &skeleton) {
            if (const auto identity = ValidateDescriptorIdentity(descriptor, skeleton); identity.HasError())
                return identity;
            if (const auto timing = ValidateDescriptorTiming(descriptor); timing.HasError())
                return timing;
            return ValidateDescriptorKind(descriptor);
        }

        [[nodiscard]] std::uint32_t FindSkeletonJoint(const SkeletonAsset &skeleton, const JointId joint) noexcept {
            const auto joints = skeleton.Data().joints;
            const auto found = std::ranges::find(joints, joint, &SkeletonJoint::id);
            return found == joints.end() ? std::numeric_limits<std::uint32_t>::max()
                                         : static_cast<std::uint32_t>(std::distance(joints.begin(), found));
        }

        [[nodiscard]] Result<void> CanonicalizeKey(AnimationTransformKey &key, const AnimationDeltaTime duration) {
            if (key.time.ticks < 0 || key.time.ticks > duration.ticks || !IsFiniteTransform(key.transform))
                return Reject<void>(AnimationErrors::ClipMalformed, "A clip key has invalid time or transform data.");
            if (!IsKnown(key.interpolation))
                return Reject<void>(AnimationErrors::ClipUnsupported, "A clip key uses an unknown interpolation mode.");
            key.transform.rotation = key.transform.rotation.Normalized();
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> CanonicalizeTrack(AnimationJointTrack &track, const AnimationDeltaTime duration,
                                                     const AnimationClipLimits &limits) {
            if (!track.joint.IsValid())
                return Reject<void>(AnimationErrors::IdentityInvalid, "A clip track has a reserved joint identity.");
            if (track.keys.empty() || track.keys.size() > limits.maximumKeysPerTrack)
                return Reject<void>(AnimationErrors::ClipLimitExceeded, "A clip track has zero or too many keys.");
            std::ranges::sort(track.keys, {}, &AnimationTransformKey::time);
            for (AnimationTransformKey &key : track.keys) {
                if (const auto canonical = CanonicalizeKey(key, duration); canonical.HasError())
                    return canonical;
            }
            if (std::ranges::adjacent_find(track.keys, {}, &AnimationTransformKey::time) != track.keys.end())
                return Reject<void>(AnimationErrors::ClipDuplicateIdentity, "A joint track contains duplicate key times.");
            return Result<void>::Success();
        }

        [[nodiscard]] bool AddWouldOverflow(const std::int64_t left, const std::int64_t right) noexcept {
            return (right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
                   (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right);
        }

        [[nodiscard]] std::int64_t FloorDivide(const std::int64_t value, const std::int64_t divisor) noexcept {
            const std::int64_t quotient = value / divisor;
            const std::int64_t remainder = value % divisor;
            return quotient - static_cast<std::int64_t>(remainder < 0);
        }

        [[nodiscard]] std::int64_t CeilDivide(const std::int64_t value, const std::int64_t divisor) noexcept {
            const std::int64_t quotient = value / divisor;
            const std::int64_t remainder = value % divisor;
            return quotient + static_cast<std::int64_t>(remainder > 0);
        }

        [[nodiscard]] std::uint64_t PositiveDifference(const std::int64_t larger, const std::int64_t smaller) noexcept {
            return static_cast<std::uint64_t>(larger) - static_cast<std::uint64_t>(smaller);
        }

        [[nodiscard]] std::uint64_t CountCrossings(const std::int64_t from, const std::int64_t to,
                                                   const std::int64_t boundaryInterval) noexcept {
            if (to > from)
                return PositiveDifference(FloorDivide(to, boundaryInterval), FloorDivide(from, boundaryInterval));
            if (to < from)
                return PositiveDifference(CeilDivide(from, boundaryInterval), CeilDivide(to, boundaryInterval));
            return 0;
        }

        [[nodiscard]] std::int64_t FloorModulo(const std::int64_t value, const std::int64_t modulus) noexcept {
            const std::int64_t remainder = value % modulus;
            return remainder < 0 ? remainder + modulus : remainder;
        }

        [[nodiscard]] AnimationTime PingPongSampleTime(const std::int64_t phase, const std::int64_t duration) noexcept {
            return {phase <= duration ? phase : (2 * duration) - phase};
        }

        [[nodiscard]] AnimationTraversalDirection PingPongDirection(const std::int64_t phase, const std::int64_t duration,
                                                                    const std::int64_t delta) noexcept {
            using enum AnimationTraversalDirection;
            if (delta == 0)
                return Held;
            const std::int64_t period = 2 * duration;
            const std::int64_t priorPhase = FloorModulo(phase - (delta > 0 ? 1 : -1), period);
            const std::int64_t priorTime = PingPongSampleTime(priorPhase, duration).ticks;
            const std::int64_t currentTime = PingPongSampleTime(phase, duration).ticks;
            return currentTime >= priorTime ? Forward : Reverse;
        }

        [[nodiscard]] AnimationTraversalDirection DirectionBetween(const std::int64_t from, const std::int64_t to) noexcept {
            using enum AnimationTraversalDirection;
            if (to == from)
                return Held;
            return to > from ? Forward : Reverse;
        }

        [[nodiscard]] bool ReachedDirectedBoundary(const AnimationTimeDelta delta, const std::int64_t phase,
                                                   const std::int64_t duration) noexcept {
            return delta.ticks > 0 ? phase == duration : phase == 0;
        }

        [[nodiscard]] Result<void> ValidateCursorRange(const AnimationClipCursor cursor, const std::int64_t upperExclusive,
                                                       const bool mayBeTerminal, const std::string_view detail) {
            if ((!mayBeTerminal && cursor.terminal) || cursor.phase.ticks < 0 || cursor.phase.ticks >= upperExclusive)
                return Reject<void>(AnimationErrors::ClipMalformed, detail);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateCursor(const AnimationClipCursor cursor, const AnimationClipDescriptor &descriptor) {
            using enum AnimationWrapMode;
            switch (descriptor.wrapMode) {
                case Once:
                    return ValidateCursorRange(cursor, descriptor.duration.ticks + 1, true,
                                               "A Once cursor is outside its closed duration interval.");
                case ClampForever:
                    return ValidateCursorRange(cursor, descriptor.duration.ticks + 1, false,
                                               "A clamped cursor is malformed or unexpectedly terminal.");
                case Loop:
                    return ValidateCursorRange(cursor, descriptor.duration.ticks, false, "A loop cursor is not in canonical phase range.");
                case PingPong:
                    return ValidateCursorRange(cursor, 2 * descriptor.duration.ticks, false,
                                               "A ping-pong cursor is not in canonical phase range.");
                case Count:
                    return Reject<void>(AnimationErrors::ClipUnsupported, "The clip wrap mode is unknown.");
            }
            return Reject<void>(AnimationErrors::ClipUnsupported, "The clip wrap mode is unknown.");
        }

        [[nodiscard]] Result<std::uint32_t> BoundedCrossings(const std::uint64_t crossings, const AnimationClipLimits &limits) {
            if (crossings > limits.maximumBoundaryCrossings || crossings > AnimationClipHardLimits::BoundaryCrossings)
                return Reject<std::uint32_t>(AnimationErrors::ClipLimitExceeded,
                                             "The directed interval exceeds its atomic boundary-crossing budget.");
            return Result<std::uint32_t>::Success(static_cast<std::uint32_t>(crossings));
        }

        [[nodiscard]] Result<std::uint32_t> CheckedCrossings(const AnimationClipCursor cursor, const std::int64_t unbounded,
                                                             const std::int64_t duration, const AnimationClipLimits &limits) {
            return BoundedCrossings(CountCrossings(cursor.phase.ticks, unbounded, duration), limits);
        }

        [[nodiscard]] AnimationClipTraversal CyclicTraversal(const std::int64_t phase, const AnimationTime sampleTime,
                                                             const AnimationTraversalDirection direction,
                                                             const std::uint32_t boundaryCrossings) noexcept {
            return {.cursor = {{phase}, false}, .sampleTime = sampleTime, .direction = direction, .boundaryCrossings = boundaryCrossings};
        }

        [[nodiscard]] Result<AnimationClipTraversal> TraverseClamped(const AnimationClipCursor cursor, const AnimationTimeDelta delta,
                                                                     const std::int64_t unbounded, const std::int64_t duration,
                                                                     const AnimationWrapMode wrapMode, const AnimationClipLimits &limits) {
            const std::int64_t phase = std::clamp(unbounded, std::int64_t{0}, duration);
            const bool reachedBoundary = phase != unbounded || ReachedDirectedBoundary(delta, phase, duration);
            const auto crossings = BoundedCrossings(static_cast<std::uint64_t>(reachedBoundary && cursor.phase.ticks != phase), limits);
            if (crossings.HasError())
                return Result<AnimationClipTraversal>::Failure(crossings.ErrorValue());
            const auto direction = DirectionBetween(cursor.phase.ticks, phase);
            return Result<AnimationClipTraversal>::Success({.cursor = {{phase}, wrapMode == AnimationWrapMode::Once && reachedBoundary},
                                                            .sampleTime = {phase},
                                                            .direction = direction,
                                                            .boundaryCrossings = crossings.Value()});
        }

        [[nodiscard]] Result<AnimationClipTraversal> TraverseLoop(const AnimationClipCursor cursor, const AnimationTimeDelta delta,
                                                                  const std::int64_t unbounded, const std::int64_t duration,
                                                                  const AnimationClipLimits &limits) {
            using enum AnimationTraversalDirection;
            const auto crossings = CheckedCrossings(cursor, unbounded, duration, limits);
            if (crossings.HasError())
                return Result<AnimationClipTraversal>::Failure(crossings.ErrorValue());
            const std::int64_t phase = FloorModulo(unbounded, duration);
            const auto direction = delta.ticks > 0 ? Forward : Reverse;
            return Result<AnimationClipTraversal>::Success(CyclicTraversal(phase, {phase}, direction, crossings.Value()));
        }

        [[nodiscard]] Result<AnimationClipTraversal> TraversePingPong(const AnimationClipCursor cursor, const AnimationTimeDelta delta,
                                                                      const std::int64_t unbounded, const std::int64_t duration,
                                                                      const AnimationClipLimits &limits) {
            const auto crossings = CheckedCrossings(cursor, unbounded, duration, limits);
            if (crossings.HasError())
                return Result<AnimationClipTraversal>::Failure(crossings.ErrorValue());
            const std::int64_t phase = FloorModulo(unbounded, 2 * duration);
            return Result<AnimationClipTraversal>::Success(CyclicTraversal(phase, PingPongSampleTime(phase, duration),
                                                                           PingPongDirection(phase, duration, delta.ticks),
                                                                           crossings.Value()));
        }

        [[nodiscard]] Math::Transform Interpolate(const AnimationJointTrack &track, const AnimationTime time) noexcept {
            if (time <= track.keys.front().time)
                return track.keys.front().transform;
            if (time >= track.keys.back().time)
                return track.keys.back().transform;
            const auto right = std::ranges::upper_bound(track.keys, time, {}, &AnimationTransformKey::time);
            const auto &leftKey = *(right - 1);
            if (leftKey.interpolation == AnimationInterpolation::Step)
                return leftKey.transform;
            const auto numerator = static_cast<double>(time.ticks - leftKey.time.ticks);
            const auto denominator = static_cast<double>(right->time.ticks - leftKey.time.ticks);
            auto alpha = static_cast<float>(numerator / denominator);
            // Zero authored endpoint tangents form a bounded cubic Hermite ease.
            if (leftKey.interpolation == AnimationInterpolation::CubicHermite)
                alpha = alpha * alpha * (3.0F - 2.0F * alpha);
            return {.translation = Math::Lerp(leftKey.transform.translation, right->transform.translation, alpha),
                    .rotation = Math::Slerp(leftKey.transform.rotation, right->transform.rotation, alpha),
                    .scale = Math::Lerp(leftKey.transform.scale, right->transform.scale, alpha)};
        }

        [[nodiscard]] Result<void> ValidatePoseSpan(const std::span<const Math::Transform> pose, const std::size_t jointCount,
                                                    const std::string_view description) {
            if (pose.size() != jointCount)
                return Reject<void>(AnimationErrors::ClipLimitExceeded, description);
            if (!std::ranges::all_of(pose, IsFiniteTransform))
                return Reject<void>(AnimationErrors::ClipMalformed, "A sampling pose contains a malformed transform.");
            return Result<void>::Success();
        }

        template <typename Left, typename Right>
        [[nodiscard]] bool SpansOverlap(const std::span<Left> left, const std::span<Right> right) noexcept {
            const auto leftBegin = reinterpret_cast<std::uintptr_t>(left.data());
            const auto rightBegin = reinterpret_cast<std::uintptr_t>(right.data());
            const auto leftEnd = leftBegin + left.size_bytes();
            const auto rightEnd = rightBegin + right.size_bytes();
            return !left.empty() && !right.empty() && leftBegin < rightEnd && rightBegin < leftEnd;
        }

        [[nodiscard]] Result<void> CanonicalizeTracks(AnimationClipData &candidate, const AnimationClipLimits &limits) {
            if (candidate.tracks.size() > limits.maximumTracks)
                return Reject<void>(AnimationErrors::ClipLimitExceeded, "The clip has too many joint tracks.");
            std::size_t totalKeys = 0;
            for (AnimationJointTrack &track : candidate.tracks) {
                if (const auto canonical = CanonicalizeTrack(track, candidate.descriptor.duration, limits); canonical.HasError())
                    return canonical;
                if (track.keys.size() > limits.maximumTotalKeys - totalKeys)
                    return Reject<void>(AnimationErrors::ClipLimitExceeded, "The clip exceeds its total key budget.");
                totalKeys += track.keys.size();
            }
            std::ranges::sort(candidate.tracks, {}, &AnimationJointTrack::joint);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<std::vector<std::uint32_t>> ResolveJointIndices(const AnimationClipData &candidate,
                                                                             const SkeletonAsset &skeleton) {
            std::vector<std::uint32_t> jointIndices;
            jointIndices.reserve(candidate.tracks.size());
            JointId previous{};
            for (const AnimationJointTrack &track : candidate.tracks) {
                if (track.joint == previous)
                    return Reject<std::vector<std::uint32_t>>(AnimationErrors::ClipDuplicateIdentity,
                                                              "The clip contains duplicate joint tracks.");
                const std::uint32_t jointIndex = FindSkeletonJoint(skeleton, track.joint);
                if (jointIndex == std::numeric_limits<std::uint32_t>::max())
                    return Reject<std::vector<std::uint32_t>>(AnimationErrors::ClipJointMissing,
                                                              "A clip track joint is absent from the skeleton.");
                jointIndices.push_back(jointIndex);
                previous = track.joint;
            }
            return Result<std::vector<std::uint32_t>>::Success(std::move(jointIndices));
        }

        [[nodiscard]] std::vector<Math::Transform> CaptureReferencePose(const SkeletonAsset &skeleton) {
            std::vector<Math::Transform> referencePose;
            referencePose.reserve(skeleton.Data().joints.size());
            for (const SkeletonJoint &joint : skeleton.Data().joints)
                referencePose.push_back(joint.referenceLocalTransform);
            return referencePose;
        }

        [[nodiscard]] Result<void> ValidateTraversal(const AnimationClipCursor cursor, const AnimationTimeDelta delta,
                                                     const AnimationClipDescriptor &descriptor, const AnimationClipLimits &limits,
                                                     const AnimationClipAdmissionState admission) {
            if (const auto admitted = ValidateAdmission(admission); admitted.HasError())
                return admitted;
            if (const auto validLimits = ValidateLimits(limits); validLimits.HasError())
                return validLimits;
            if (const auto validCursor = ValidateCursor(cursor, descriptor); validCursor.HasError())
                return validCursor;
            if (!cursor.terminal && delta.ticks != 0 && AddWouldOverflow(cursor.phase.ticks, delta.ticks))
                return Reject<void>(AnimationErrors::ClipTimeOverflow, "The exact cursor and delta cannot be represented together.");
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateSamplingIdentities(const AnimationClipSampleContext &context,
                                                              const AnimationClipDescriptor &descriptor) {
            if (!context.skeleton.IsValid() || !context.skeletonGeneration.IsValid() || !context.clipGeneration.IsValid())
                return Reject<void>(AnimationErrors::IdentityInvalid, "Sampling bindings use a reserved identity.");
            if (context.skeleton != descriptor.skeleton)
                return Reject<void>(AnimationErrors::ClipSkeletonMismatch, "Sampling targets another skeleton identity.");
            if (context.skeletonGeneration != descriptor.skeletonGeneration || context.clipGeneration != descriptor.generation)
                return Reject<void>(AnimationErrors::ClipBindingStale, "Sampling targets a retired clip or skeleton generation.");
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateSampleBinding(const AnimationTime time, const AnimationClipSampleContext &context,
                                                         const AnimationClipDescriptor &descriptor, const std::size_t jointCount) {
            if (const auto admission = ValidateAdmission(context.admission); admission.HasError())
                return admission;
            if (const auto identities = ValidateSamplingIdentities(context, descriptor); identities.HasError())
                return identities;
            if (time.ticks < 0 || time.ticks > descriptor.duration.ticks)
                return Reject<void>(AnimationErrors::ClipMalformed, "Sampling time is outside the closed clip duration.");
            if (context.outputPose.size() != jointCount)
                return Reject<void>(AnimationErrors::ClipLimitExceeded,
                                    "The output pose does not match the clip's canonical skeleton joint count.");
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateAbsoluteInputs(const AnimationClipSampleContext &context) {
            if (!context.basePose.empty() || !context.referenceLocalPose.empty() || context.referencePose)
                return Reject<void>(AnimationErrors::ClipMalformed, "Absolute sampling cannot consume additive pose inputs.");
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateAdditiveInputs(const AnimationClipSampleContext &context,
                                                          const AnimationClipDescriptor &descriptor, const std::size_t jointCount) {
            if (!context.referencePose || context.referencePose != descriptor.referencePose)
                return Reject<void>(AnimationErrors::ClipReferencePoseMismatch,
                                    "The additive reference-pose identity or generation is stale.");
            if (const auto base = ValidatePoseSpan(context.basePose, jointCount, "The additive base pose has the wrong joint count.");
                base.HasError())
                return base;
            if (const auto reference =
                    ValidatePoseSpan(context.referenceLocalPose, jointCount, "The additive reference pose has the wrong joint count.");
                reference.HasError())
                return reference;
            if (const bool outputAliasesBase = context.outputPose.data() == context.basePose.data();
                (SpansOverlap(context.outputPose, context.basePose) && !outputAliasesBase) ||
                SpansOverlap(context.outputPose, context.referenceLocalPose))
                return Reject<void>(AnimationErrors::ClipMalformed,
                                    "Additive output may alias its complete base pose but no other input storage.");
            return Result<void>::Success();
        }

        [[nodiscard]] Math::Transform ApplyAdditive(const Math::Transform &base, const Math::Transform &sample,
                                                    const Math::Transform &reference) noexcept {
            const Math::Quaternion rotationDelta = reference.rotation.Inverse() * sample.rotation;
            return {.translation = base.translation + (sample.translation - reference.translation),
                    .rotation = (base.rotation * rotationDelta).Normalized(),
                    .scale = {base.scale.x * (sample.scale.x / reference.scale.x), base.scale.y * (sample.scale.y / reference.scale.y),
                              base.scale.z * (sample.scale.z / reference.scale.z)}};
        }
    }  // namespace

    /** @copydoc AnimationClipAsset::Create */
    Result<AnimationClipAsset> AnimationClipAsset::Create(AnimationClipData candidate, const SkeletonAsset &skeleton,
                                                          const AnimationClipBuildContext &context) {
        if (const auto admission = ValidateAdmission(context.admission); admission.HasError())
            return Result<AnimationClipAsset>::Failure(admission.ErrorValue());
        if (const auto limits = ValidateLimits(context.limits); limits.HasError())
            return Result<AnimationClipAsset>::Failure(limits.ErrorValue());
        if (const auto descriptor = ValidateDescriptor(candidate.descriptor, skeleton); descriptor.HasError())
            return Result<AnimationClipAsset>::Failure(descriptor.ErrorValue());
        if (context.replacing && (!context.replacing->IsValid() || *context.replacing != candidate.descriptor.id))
            return Reject<AnimationClipAsset>(AnimationErrors::ClipReloadMismatch,
                                              "The candidate does not replace the requested stable clip identity.");
        if (const auto tracks = CanonicalizeTracks(candidate, context.limits); tracks.HasError())
            return Result<AnimationClipAsset>::Failure(tracks.ErrorValue());
        auto jointIndices = ResolveJointIndices(candidate, skeleton);
        if (jointIndices.HasError())
            return Result<AnimationClipAsset>::Failure(jointIndices.ErrorValue());
        return Result<AnimationClipAsset>::Success(
            AnimationClipAsset{std::move(candidate), std::move(jointIndices).Value(), CaptureReferencePose(skeleton)});
    }

    /** @copydoc AnimationClipAsset::Data */
    const AnimationClipData &AnimationClipAsset::Data() const noexcept {
        return data_;
    }

    /** @copydoc AnimationClipAsset::Traverse */
    Result<AnimationClipTraversal> AnimationClipAsset::Traverse(const AnimationClipCursor cursor, const AnimationTimeDelta delta,
                                                                const AnimationClipLimits &limits,
                                                                const AnimationClipAdmissionState admission) const {
        if (const auto valid = ValidateTraversal(cursor, delta, data_.descriptor, limits, admission); valid.HasError())
            return Result<AnimationClipTraversal>::Failure(valid.ErrorValue());
        if (cursor.terminal || delta.ticks == 0) {
            using enum AnimationTraversalDirection;
            return Result<AnimationClipTraversal>::Success({.cursor = cursor, .sampleTime = cursor.phase, .direction = Held});
        }

        const std::int64_t duration = data_.descriptor.duration.ticks;
        const std::int64_t unbounded = cursor.phase.ticks + delta.ticks;
        using enum AnimationWrapMode;
        switch (data_.descriptor.wrapMode) {
            case Once:
            case ClampForever:
                return TraverseClamped(cursor, delta, unbounded, duration, data_.descriptor.wrapMode, limits);
            case Loop:
                return TraverseLoop(cursor, delta, unbounded, duration, limits);
            case PingPong:
                return TraversePingPong(cursor, delta, unbounded, duration, limits);
            case Count:
                return Reject<AnimationClipTraversal>(AnimationErrors::ClipUnsupported, "The clip wrap mode is unknown.");
        }
        return Reject<AnimationClipTraversal>(AnimationErrors::ClipUnsupported, "The clip wrap mode is unknown.");
    }

    /** @copydoc AnimationClipAsset::Sample */
    Result<AnimationClipSample> AnimationClipAsset::Sample(const AnimationTime time, const AnimationClipSampleContext &context) const {
        const auto &descriptor = data_.descriptor;
        const auto jointCount = skeletonReferencePose_.size();
        if (const auto binding = ValidateSampleBinding(time, context, descriptor, jointCount); binding.HasError())
            return Result<AnimationClipSample>::Failure(binding.ErrorValue());
        if (const auto inputs = descriptor.kind == AnimationClipKind::Absolute ? ValidateAbsoluteInputs(context)
                                                                               : ValidateAdditiveInputs(context, descriptor, jointCount);
            inputs.HasError())
            return Result<AnimationClipSample>::Failure(inputs.ErrorValue());
        if (descriptor.kind == AnimationClipKind::Absolute) {
            std::ranges::copy(skeletonReferencePose_, context.outputPose.begin());
        } else if (context.outputPose.data() != context.basePose.data()) {
            std::ranges::copy(context.basePose, context.outputPose.begin());
        }

        for (std::size_t trackIndex = 0; trackIndex < data_.tracks.size(); ++trackIndex) {
            const auto jointIndex = jointIndices_[trackIndex];
            const auto sampled = Interpolate(data_.tracks[trackIndex], time);
            context.outputPose[jointIndex] =
                descriptor.kind == AnimationClipKind::Absolute
                    ? sampled
                    : ApplyAdditive(context.basePose[jointIndex], sampled, context.referenceLocalPose[jointIndex]);
        }
        return Result<AnimationClipSample>::Success({.time = time,
                                                     .sampledTracks = static_cast<std::uint32_t>(data_.tracks.size()),
                                                     .additive = descriptor.kind == AnimationClipKind::Additive});
    }
}  // namespace Horo::Animation
