#include "Horo/Animation/AnimationCompression.h"

#include "Horo/Animation/AnimationErrors.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace Horo::Animation {
    namespace {
        template <typename Value>
        [[nodiscard]] Result<Value> RejectCompression(const ErrorCodeDescriptor &descriptor, const std::string_view detail) {
            auto error = MakeError(descriptor, std::string{detail});
            return Result<Value>::Failure(std::move(error));
        }

        [[nodiscard]] Result<void> ValidateAdmission(const AnimationClipAdmissionState admission) {
            if (admission == AnimationClipAdmissionState::CancellationRequested)
                return RejectCompression<void>(AnimationErrors::CompressionOperationCancelled,
                                               "Compression was cancelled before work began.");
            if (admission != AnimationClipAdmissionState::Accepting)
                return RejectCompression<void>(AnimationErrors::CompressionAdmissionRejected,
                                               "The compression owner is not accepting work.");
            return Result<void>::Success();
        }

        [[nodiscard]] bool IsKnown(const AnimationCompressionCookTier value) noexcept {
            return value < AnimationCompressionCookTier::Count;
        }

        [[nodiscard]] bool IsKnown(const AnimationCompressionScheme value) noexcept {
            return value < AnimationCompressionScheme::Count;
        }

        [[nodiscard]] bool IsFiniteNonNegative(const float value) noexcept {
            return std::isfinite(value) && value >= 0.0F;
        }

        [[nodiscard]] Result<void> ValidateThresholds(const AnimationCompressionCookProfile &profile) {
            if (!IsFiniteNonNegative(profile.thresholds.translation) || !IsFiniteNonNegative(profile.thresholds.rotation) ||
                !IsFiniteNonNegative(profile.thresholds.scale))
                return RejectCompression<void>(AnimationErrors::CompressionProfileMalformed,
                                               "Compression thresholds must be finite and non-negative.");
            if (profile.scheme != AnimationCompressionScheme::None)
                return Result<void>::Success();
            if (profile.thresholds.translation != 0.0F || profile.thresholds.rotation != 0.0F || profile.thresholds.scale != 0.0F)
                return RejectCompression<void>(AnimationErrors::CompressionProfileMalformed,
                                               "A full-precision profile cannot declare lossy error thresholds.");
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateCookLimits(const AnimationCompressionCookLimits &limits) {
            if (limits.maximumSourceKeys == 0 || limits.maximumSourceKeys > AnimationCompressionHardLimits::SourceKeys ||
                limits.maximumOutputKeys == 0 || limits.maximumOutputKeys > AnimationCompressionHardLimits::OutputKeys ||
                limits.maximumErrorEvaluations > AnimationCompressionHardLimits::ErrorEvaluations)
                return RejectCompression<void>(AnimationErrors::CompressionProfileMalformed,
                                               "Compression limits are zero or exceed hard ceilings.");
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateProfile(const AnimationCompressionCookProfile &profile) {
            if (profile.contractVersion != CurrentAnimationCompressionContractVersion)
                return RejectCompression<void>(AnimationErrors::CompressionVersionUnsupported,
                                               "The cook profile contract version is unsupported.");
            if (!profile.id.IsValid())
                return RejectCompression<void>(AnimationErrors::IdentityInvalid, "The compression profile uses the reserved identity.");
            if (!IsKnown(profile.tier) || !IsKnown(profile.scheme))
                return RejectCompression<void>(AnimationErrors::CompressionUnsupported, "The cook profile uses an unknown tier or scheme.");
            if (const auto thresholds = ValidateThresholds(profile); thresholds.HasError())
                return thresholds;
            return ValidateCookLimits(profile.limits);
        }

        [[nodiscard]] std::uint32_t CountKeys(const AnimationClipData &data) noexcept {
            std::uint64_t total = 0;
            for (const auto &track : data.tracks)
                total += track.keys.size();
            return static_cast<std::uint32_t>(total);
        }

        [[nodiscard]] Math::Transform InterpolateLinear(const AnimationTransformKey &left, const AnimationTransformKey &right,
                                                        const AnimationTime time) noexcept {
            const auto numerator = static_cast<double>(time.ticks - left.time.ticks);
            const auto denominator = static_cast<double>(right.time.ticks - left.time.ticks);
            const auto alpha = static_cast<float>(numerator / denominator);
            return {.translation = Math::Lerp(left.transform.translation, right.transform.translation, alpha),
                    .rotation = Math::Slerp(left.transform.rotation, right.transform.rotation, alpha),
                    .scale = Math::Lerp(left.transform.scale, right.transform.scale, alpha)};
        }

        [[nodiscard]] float QuaternionAngularError(const Math::Quaternion &left, const Math::Quaternion &right) noexcept {
            const float dot = std::clamp(std::abs(left.x * right.x + left.y * right.y + left.z * right.z + left.w * right.w), 0.0F, 1.0F);
            return 2.0F * std::acos(dot);
        }

        struct RemovalEvaluation final {
            bool removable{};
            float normalizedError{};
        };

        [[nodiscard]] float NormalizeError(const float error, const float threshold) noexcept {
            if (error == 0.0F)
                return 0.0F;
            if (threshold == 0.0F)
                return std::numeric_limits<float>::infinity();
            return error / threshold;
        }

        [[nodiscard]] Result<RemovalEvaluation> EvaluateRemoval(const AnimationTransformKey &left, const AnimationTransformKey &candidate,
                                                                const AnimationTransformKey &right,
                                                                const AnimationCompressionCookProfile &profile,
                                                                std::uint64_t &evaluations) {
            if (left.interpolation != AnimationInterpolation::Linear || candidate.interpolation != AnimationInterpolation::Linear)
                return Result<RemovalEvaluation>::Success({.normalizedError = std::numeric_limits<float>::infinity()});
            if (evaluations >= profile.limits.maximumErrorEvaluations)
                return RejectCompression<RemovalEvaluation>(AnimationErrors::CompressionBudgetExceeded,
                                                            "Key reduction exhausted its error-evaluation budget.");
            ++evaluations;
            const auto predicted = InterpolateLinear(left, right, candidate.time);
            const float normalizedError = std::max(
                {NormalizeError(Math::Length(candidate.transform.translation - predicted.translation), profile.thresholds.translation),
                 NormalizeError(QuaternionAngularError(candidate.transform.rotation, predicted.rotation), profile.thresholds.rotation),
                 NormalizeError(Math::Length(candidate.transform.scale - predicted.scale), profile.thresholds.scale)});
            return Result<RemovalEvaluation>::Success({.removable = normalizedError <= 1.0F, .normalizedError = normalizedError});
        }

        [[nodiscard]] Result<void> ReduceSinglePass(AnimationJointTrack &track, const AnimationCompressionCookProfile &profile,
                                                    std::uint64_t &evaluations) {
            if (track.keys.size() < 3)
                return Result<void>::Success();
            std::vector<AnimationTransformKey> reduced;
            reduced.reserve(track.keys.size());
            reduced.push_back(track.keys.front());
            bool removedPrevious = false;
            for (std::size_t index = 1; index + 1 < track.keys.size(); ++index) {
                auto evaluation = removedPrevious
                                      ? Result<RemovalEvaluation>::Success({})
                                      : EvaluateRemoval(reduced.back(), track.keys[index], track.keys[index + 1], profile, evaluations);
                if (evaluation.HasError())
                    return Result<void>::Failure(evaluation.ErrorValue());
                removedPrevious = evaluation.Value().removable;
                if (!removedPrevious)
                    reduced.push_back(track.keys[index]);
            }
            reduced.push_back(track.keys.back());
            track.keys = std::move(reduced);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ReduceAdaptive(AnimationJointTrack &track, const AnimationCompressionCookProfile &profile,
                                                  std::uint64_t &evaluations) {
            if (track.keys.size() < 3)
                return Result<void>::Success();
            std::vector<bool> retained(track.keys.size(), false);
            retained.front() = true;
            retained.back() = true;
            std::vector<std::pair<std::size_t, std::size_t>> pending{{0, track.keys.size() - 1}};
            while (!pending.empty()) {
                const auto [begin, end] = pending.back();
                pending.pop_back();
                std::size_t split = end;
                float maximumError = 1.0F;
                for (std::size_t index = begin + 1; index < end; ++index) {
                    auto evaluation = EvaluateRemoval(track.keys[begin], track.keys[index], track.keys[end], profile, evaluations);
                    if (evaluation.HasError())
                        return Result<void>::Failure(evaluation.ErrorValue());
                    if (!evaluation.Value().removable && evaluation.Value().normalizedError > maximumError) {
                        split = index;
                        maximumError = evaluation.Value().normalizedError;
                    }
                }
                if (split != end) {
                    retained[split] = true;
                    pending.emplace_back(split, end);
                    pending.emplace_back(begin, split);
                }
            }
            std::vector<AnimationTransformKey> reduced;
            reduced.reserve(track.keys.size());
            for (std::size_t index = 0; index < track.keys.size(); ++index) {
                if (retained[index])
                    reduced.push_back(track.keys[index]);
            }
            track.keys = std::move(reduced);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ReduceTracks(AnimationClipData &data, const AnimationCompressionCookProfile &profile,
                                                AnimationCompressionStatistics &statistics) {
            for (auto &track : data.tracks) {
                Result<void> reduced = Result<void>::Success();
                if (profile.scheme == AnimationCompressionScheme::Linear)
                    reduced = ReduceSinglePass(track, profile, statistics.errorEvaluations);
                else if (profile.scheme == AnimationCompressionScheme::Adaptive)
                    reduced = ReduceAdaptive(track, profile, statistics.errorEvaluations);
                if (reduced.HasError())
                    return reduced;
                statistics.maximumKeysPerTrack = std::max(statistics.maximumKeysPerTrack, static_cast<std::uint32_t>(track.keys.size()));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] AnimationCompressionCompatibility MakeCompatibility(const AnimationClipDescriptor &descriptor,
                                                                          const AnimationCompressionCookProfile &profile) noexcept {
            return {.profile = profile.id,
                    .clip = descriptor.id,
                    .clipGeneration = descriptor.generation,
                    .skeleton = descriptor.skeleton,
                    .skeletonGeneration = descriptor.skeletonGeneration,
                    .scheme = profile.scheme};
        }

        [[nodiscard]] bool SameReloadDomain(const AnimationCompressionCompatibility &left,
                                            const AnimationCompressionCompatibility &right) noexcept {
            return left.contractVersion == right.contractVersion && left.profile == right.profile && left.clip == right.clip &&
                   left.skeleton == right.skeleton && left.scheme == right.scheme;
        }

        [[nodiscard]] std::uint32_t RequiredSearchSteps(const std::uint32_t keys) noexcept {
            if (keys <= 1)
                return 0;
            std::uint32_t steps = 0;
            std::uint32_t remaining = keys;
            while (remaining != 0) {
                ++steps;
                remaining >>= 1U;
            }
            return steps;
        }

        [[nodiscard]] Result<void> ValidateCompatibility(const AnimationCompressionCompatibility &actual,
                                                         const AnimationCompressionCompatibility &requested) {
            if (requested.contractVersion != CurrentAnimationCompressionContractVersion)
                return RejectCompression<void>(AnimationErrors::CompressionVersionUnsupported,
                                               "The decompression compatibility version is unsupported.");
            if (requested.profile != actual.profile || requested.clip != actual.clip || requested.skeleton != actual.skeleton ||
                requested.scheme != actual.scheme)
                return RejectCompression<void>(AnimationErrors::CompressionUnsupported,
                                               "The decompression request targets another clip, profile, or representation.");
            if (requested.clipGeneration != actual.clipGeneration || requested.skeletonGeneration != actual.skeletonGeneration)
                return RejectCompression<void>(AnimationErrors::CompressionBindingStale,
                                               "The decompression request targets a retired clip or skeleton generation.");
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateDecompressionBudget(const AnimationCompressionStatistics &statistics,
                                                               const AnimationDecompressionBudget &budget) {
            if (budget.maximumTracks > AnimationCompressionHardLimits::DecompressedTracks ||
                budget.maximumKeySearchSteps > AnimationCompressionHardLimits::KeySearchSteps || statistics.tracks > budget.maximumTracks ||
                RequiredSearchSteps(statistics.maximumKeysPerTrack) > budget.maximumKeySearchSteps)
                return RejectCompression<void>(AnimationErrors::CompressionBudgetExceeded,
                                               "The compressed clip exceeds the captured frame-hot decompression budget.");
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateDecompression(const AnimationCompressionCompatibility &actual,
                                                         const AnimationCompressionStatistics &statistics,
                                                         const AnimationDecompressionContext &context) {
            if (const auto admission = ValidateAdmission(context.sample.admission); admission.HasError())
                return admission;
            if (const auto compatibility = ValidateCompatibility(actual, context.compatibility); compatibility.HasError())
                return compatibility;
            return ValidateDecompressionBudget(statistics, context.budget);
        }
    }  // namespace

    /** @copydoc CompressedAnimationClipAsset::CompressedAnimationClipAsset */
    CompressedAnimationClipAsset::CompressedAnimationClipAsset(AnimationClipAsset clip,
                                                               const AnimationCompressionCompatibility &compatibility,
                                                               const AnimationCompressionStatistics &statistics) noexcept
        : clip_(std::move(clip)), compatibility_(compatibility), statistics_(statistics) {}

    /** @copydoc CompressedAnimationClipAsset::Compatibility */
    const AnimationCompressionCompatibility &CompressedAnimationClipAsset::Compatibility() const noexcept {
        return compatibility_;
    }

    /** @copydoc CompressedAnimationClipAsset::Statistics */
    const AnimationCompressionStatistics &CompressedAnimationClipAsset::Statistics() const noexcept {
        return statistics_;
    }

    /** @copydoc CompressedAnimationClipAsset::Clip */
    const AnimationClipAsset &CompressedAnimationClipAsset::Clip() const noexcept {
        return clip_;
    }

    /** @copydoc CompressedAnimationClipAsset::Sample */
    Result<AnimationClipSample> CompressedAnimationClipAsset::Sample(const AnimationTime time,
                                                                     const AnimationDecompressionContext &context) const {
        if (const auto valid = ValidateDecompression(compatibility_, statistics_, context); valid.HasError())
            return Result<AnimationClipSample>::Failure(valid.ErrorValue());
        return clip_.Sample(time, context.sample);
    }

    /** @copydoc GetAnimationCompressionCookProfile */
    Result<AnimationCompressionCookProfile> GetAnimationCompressionCookProfile(const AnimationCompressionCookTier tier) {
        using enum AnimationCompressionCookTier;
        std::uint64_t idValue = 0;
        AnimationCompressionCookProfile profile{.tier = tier};
        switch (tier) {
            case Lossless:
                idValue = 1;
                profile.scheme = AnimationCompressionScheme::None;
                break;
            case Balanced:
                idValue = 2;
                profile.scheme = AnimationCompressionScheme::Linear;
                profile.thresholds = {.translation = 0.001F, .rotation = 0.001F, .scale = 0.001F};
                break;
            case Aggressive:
                idValue = 3;
                profile.scheme = AnimationCompressionScheme::Adaptive;
                profile.thresholds = {.translation = 0.01F, .rotation = 0.01F, .scale = 0.01F};
                break;
            case Count:
                return RejectCompression<AnimationCompressionCookProfile>(AnimationErrors::CompressionUnsupported,
                                                                          "The requested compression cook tier is unknown.");
        }
        auto identity = AnimationCompressionProfileId::Create(idValue);
        if (identity.HasError())
            return Result<AnimationCompressionCookProfile>::Failure(identity.ErrorValue());
        profile.id = identity.Value();
        return Result<AnimationCompressionCookProfile>::Success(profile);
    }

    /** @copydoc CookAnimationClip */
    Result<CompressedAnimationClipAsset> CookAnimationClip(const AnimationClipAsset &source, const SkeletonAsset &skeleton,
                                                           const AnimationCompressionCookProfile &profile,
                                                           const AnimationCompressionCookContext &context) {
        if (const auto admission = ValidateAdmission(context.admission); admission.HasError())
            return Result<CompressedAnimationClipAsset>::Failure(admission.ErrorValue());
        if (const auto validProfile = ValidateProfile(profile); validProfile.HasError())
            return Result<CompressedAnimationClipAsset>::Failure(validProfile.ErrorValue());
        const auto &sourceData = source.Data();
        if (sourceData.descriptor.compression != AnimationCompressionScheme::None)
            return RejectCompression<CompressedAnimationClipAsset>(AnimationErrors::CompressionUnsupported,
                                                                   "A compressed publication cannot be used as a new compression source.");
        if (sourceData.descriptor.skeleton != skeleton.Data().skeleton)
            return RejectCompression<CompressedAnimationClipAsset>(AnimationErrors::ClipSkeletonMismatch,
                                                                   "The compression source does not bind the supplied skeleton.");
        if (!context.clipGeneration.IsValid() || !context.skeletonGeneration.IsValid())
            return RejectCompression<CompressedAnimationClipAsset>(AnimationErrors::IdentityInvalid,
                                                                   "Compression owner bindings use a reserved generation.");
        if (sourceData.descriptor.generation != context.clipGeneration ||
            sourceData.descriptor.skeletonGeneration != context.skeletonGeneration)
            return RejectCompression<CompressedAnimationClipAsset>(AnimationErrors::CompressionBindingStale,
                                                                   "The compression source binds a retired clip or skeleton generation.");

        AnimationClipData reduced = sourceData;
        const auto compatibility = MakeCompatibility(reduced.descriptor, profile);
        if (context.replacing && !SameReloadDomain(*context.replacing, compatibility))
            return RejectCompression<CompressedAnimationClipAsset>(AnimationErrors::CompressionReloadMismatch,
                                                                   "The replacement source or profile domain does not match.");

        AnimationCompressionStatistics statistics{.tracks = static_cast<std::uint32_t>(reduced.tracks.size()),
                                                  .sourceKeys = CountKeys(reduced)};
        if (statistics.sourceKeys > profile.limits.maximumSourceKeys)
            return RejectCompression<CompressedAnimationClipAsset>(AnimationErrors::CompressionBudgetExceeded,
                                                                   "The source clip exceeds the profile key budget.");
        reduced.descriptor.compression = profile.scheme;
        if (const auto reducedTracks = ReduceTracks(reduced, profile, statistics); reducedTracks.HasError())
            return Result<CompressedAnimationClipAsset>::Failure(reducedTracks.ErrorValue());
        statistics.outputKeys = CountKeys(reduced);
        statistics.removedKeys = statistics.sourceKeys - statistics.outputKeys;
        if (statistics.outputKeys > profile.limits.maximumOutputKeys)
            return RejectCompression<CompressedAnimationClipAsset>(AnimationErrors::CompressionBudgetExceeded,
                                                                   "The compressed clip exceeds the profile output-key budget.");

        auto clip = AnimationClipAsset::Create(std::move(reduced), skeleton);
        if (clip.HasError())
            return Result<CompressedAnimationClipAsset>::Failure(clip.ErrorValue());
        return Result<CompressedAnimationClipAsset>::Success(
            CompressedAnimationClipAsset{std::move(clip).Value(), compatibility, statistics});
    }
}  // namespace Horo::Animation
