#include "Horo/Cinematic/CinematicErrors.h"
#include "Horo/Cinematic/SequenceAsset.h"

#include <algorithm>
#include <format>
#include <limits>
#include <string>
#include <utility>

namespace Horo::Cinematic {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failed(const ErrorCodeDescriptor &descriptor, std::string message = {}) {
            return Result<T>::Failure(MakeError(descriptor, std::move(message)));
        }

        [[nodiscard]] const ErrorCodeDescriptor &ReferenceError(const SequenceReferenceAvailability availability) {
            using enum SequenceReferenceAvailability;
            switch (availability) {
                case Missing:
                    return CinematicErrors::SequenceReferenceMissing;
                case Moved:
                    return CinematicErrors::SequenceReferenceMoved;
                case Unloadable:
                    return CinematicErrors::SequenceReferenceUnloadable;
                case TypeMismatch:
                    return CinematicErrors::SequenceReferenceTypeMismatch;
                case Cycle:
                    return CinematicErrors::SequenceReferenceCycle;
                case Available:
                case Count:
                    break;
            }
            return CinematicErrors::SequenceSchemaMalformed;
        }

        [[nodiscard]] Result<const SequenceResolvedReference *> ResolveReference(
            const SequenceAssetReference &required, const std::span<const SequenceResolvedReference> resolved) {
            const SequenceResolvedReference *match{};
            for (const SequenceResolvedReference &candidate : resolved) {
                if (candidate.asset != required.asset)
                    continue;
                if (match != nullptr)
                    return Failed<const SequenceResolvedReference *>(CinematicErrors::SequenceSchemaDuplicate,
                                                                     std::format("Cook snapshot contains duplicate evidence for asset {}.",
                                                                                 required.asset.ToString()));
                match = &candidate;
            }
            if (match == nullptr)
                return Failed<const SequenceResolvedReference *>(CinematicErrors::SequenceReferenceMissing,
                                                                 std::format("Track dependency {} is absent from the cook snapshot.",
                                                                             required.asset.ToString()));
            if (match->kind != required.kind || match->availability == SequenceReferenceAvailability::TypeMismatch)
                return Failed<
                    const SequenceResolvedReference *>(CinematicErrors::SequenceReferenceTypeMismatch,
                                                       std::format("Track dependency {} has a different asset type in the cook snapshot.",
                                                                   required.asset.ToString()));
            if (match->availability != SequenceReferenceAvailability::Available)
                return Failed<
                    const SequenceResolvedReference *>(ReferenceError(match->availability),
                                                       std::format("Track dependency {} is not available in the exact cook snapshot.",
                                                                   required.asset.ToString()));
            return Result<const SequenceResolvedReference *>::Success(match);
        }

        [[nodiscard]] Result<void> AccumulateCookReference(const SequenceTrackSchema &track, const SequenceAssetReference &dependency,
                                                           const SequenceCookLimits &limits,
                                                           const std::span<const SequenceResolvedReference> references,
                                                           SequenceCookPlan &plan) {
            if (plan.referenceCount == std::numeric_limits<std::uint32_t>::max())
                return Result<void>::Failure(MakeError(CinematicErrors::SequenceCookTierExceeded));
            ++plan.referenceCount;
            if (plan.referenceCount > limits.maximumReferences)
                return Result<void>::Failure(
                    MakeError(CinematicErrors::SequenceCookTierExceeded,
                              std::format("Sequence dependency count exceeds the selected tier limit of {}.", limits.maximumReferences)));
            auto resolved = ResolveReference(dependency, references);
            if (resolved.HasError()) {
                Error contextual = resolved.ErrorValue();
                contextual.message = std::format("Track {}: {}", track.id.stableValue, contextual.message);
                return Result<void>::Failure(std::move(contextual));
            }
            if (dependency.kind != SequenceReferenceKind::SubSequence)
                return Result<void>::Success();
            const std::uint32_t childDepth = resolved.Value()->rootInclusiveNestingDepth;
            if (childDepth == 0 || childDepth >= std::numeric_limits<std::uint32_t>::max())
                return Result<void>::Failure(
                    MakeError(CinematicErrors::SequenceSchemaMalformed, "Sub-sequence dependency has an invalid nesting depth."));
            plan.rootInclusiveNestingDepth = std::max(plan.rootInclusiveNestingDepth, childDepth + 1U);
            if (plan.rootInclusiveNestingDepth > limits.maximumNestingDepth)
                return Result<void>::Failure(
                    MakeError(CinematicErrors::SequenceCookTierExceeded,
                              std::format("Sub-sequence rooted at track {} reaches depth {}; the selected tier permits {}.",
                                          track.id.stableValue, plan.rootInclusiveNestingDepth, limits.maximumNestingDepth)));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> AccumulateCookTrack(const SequenceTrackSchema &track, const SequenceCookLimits &limits,
                                                       const std::span<const SequenceResolvedReference> references,
                                                       SequenceCookPlan &plan) {
            if (track.keyframeCount > limits.maximumKeysPerTrack)
                return Result<void>::Failure(MakeError(CinematicErrors::SequenceCookTierExceeded,
                                                       std::format("Track {} has {} keys; the selected tier permits {}.",
                                                                   track.id.stableValue, track.keyframeCount, limits.maximumKeysPerTrack)));
            plan.keyframeCount += track.keyframeCount;
            for (const SequenceAssetReference &dependency : track.references) {
                if (auto accumulated = AccumulateCookReference(track, dependency, limits, references, plan); accumulated.HasError())
                    return accumulated;
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc GetSequenceCookProfile */
    Result<SequenceCookProfile> GetSequenceCookProfile(const SequenceCookTier tier) {
        using enum SequenceCookTier;
        switch (tier) {
            case Compact:
                return Result<SequenceCookProfile>::Success({tier, {32, 4'096, 1, 256}});
            case Standard:
                return Result<SequenceCookProfile>::Success({tier, {256, 16'384, 4, 2'048}});
            case Large:
                return Result<SequenceCookProfile>::Success({tier, {1'024, 65'536, 8, 4'096}});
            case Count:
                break;
        }
        return Failed<SequenceCookProfile>(CinematicErrors::SequenceSchemaMalformed, "Unknown sequence cook tier.");
    }

    /** @copydoc BuildSequenceCookPlan */
    Result<SequenceCookPlan> BuildSequenceCookPlan(const SequenceAsset &asset, const SequenceCookProfile &profile,
                                                   const std::span<const SequenceResolvedReference> references) {
        if (auto canonical = GetSequenceCookProfile(profile.tier); canonical.HasError() || canonical.Value() != profile)
            return Failed<SequenceCookPlan>(CinematicErrors::SequenceSchemaMalformed, "Sequence cook profile is not canonical.");

        const SequenceAssetData &data = asset.Data();
        if (data.tracks.size() > profile.limits.maximumTracks)
            return Failed<SequenceCookPlan>(CinematicErrors::SequenceCookTierExceeded,
                                            std::format("Sequence '{}' has {} tracks; the selected tier permits {}.", data.name,
                                                        data.tracks.size(), profile.limits.maximumTracks));

        SequenceCookPlan plan{profile, static_cast<std::uint32_t>(data.tracks.size()), 0, 0, 1};
        for (const SequenceTrackSchema &track : data.tracks) {
            if (auto accumulated = AccumulateCookTrack(track, profile.limits, references, plan); accumulated.HasError())
                return Result<SequenceCookPlan>::Failure(accumulated.ErrorValue());
        }
        return Result<SequenceCookPlan>::Success(plan);
    }
}  // namespace Horo::Cinematic
