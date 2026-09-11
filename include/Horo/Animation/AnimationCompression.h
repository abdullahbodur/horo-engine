#pragma once

/**
 * @file AnimationCompression.h
 * @brief Deterministic animation compression cook profiles and bounded runtime sampling.
 */

#include "Horo/Animation/AnimationClip.h"

#include <compare>
#include <cstdint>
#include <optional>

namespace Horo::Animation {
    /** @brief Portable animation-compression contract version. */
    struct AnimationCompressionContractVersion final {
        std::uint16_t major{};
        std::uint16_t minor{};
        std::uint16_t patch{};
        [[nodiscard]] constexpr auto operator<=>(const AnimationCompressionContractVersion &) const noexcept = default;
    };

    /** @brief Animation-compression contract implemented by this API slice. */
    inline constexpr AnimationCompressionContractVersion CurrentAnimationCompressionContractVersion{1, 0, 0};

    /** @brief Stable built-in cook-profile selection; profiles contain no ambient mutable defaults. */
    enum class AnimationCompressionCookTier : std::uint8_t {
        Lossless,
        Balanced,
        Aggressive,
        Count
    };

    /** @brief Maximum permitted local-transform error for one removed key. */
    struct AnimationCompressionErrorThresholds final {
        float translation{}; /**< Maximum Euclidean translation error in scene units. */
        float rotation{};    /**< Maximum quaternion angular error in radians. */
        float scale{};       /**< Maximum Euclidean scale error. */
        [[nodiscard]] constexpr auto operator<=>(const AnimationCompressionErrorThresholds &) const noexcept = default;
    };

    /** @brief Compile-time safety ceilings for one compression cook and runtime sample. */
    struct AnimationCompressionHardLimits final {
        static constexpr std::uint32_t SourceKeys = AnimationClipHardLimits::TotalKeys;
        static constexpr std::uint32_t OutputKeys = AnimationClipHardLimits::TotalKeys;
        static constexpr std::uint64_t ErrorEvaluations = 64ULL * AnimationClipHardLimits::TotalKeys;
        static constexpr std::uint32_t DecompressedTracks = AnimationClipHardLimits::Tracks;
        static constexpr std::uint32_t KeySearchSteps = 32;
    };

    /** @brief Finite cook work and output policy captured before key reduction begins. */
    struct AnimationCompressionCookLimits final {
        std::uint32_t maximumSourceKeys{AnimationCompressionHardLimits::SourceKeys};
        std::uint32_t maximumOutputKeys{AnimationCompressionHardLimits::OutputKeys};
        std::uint64_t maximumErrorEvaluations{AnimationCompressionHardLimits::ErrorEvaluations};
        [[nodiscard]] constexpr auto operator<=>(const AnimationCompressionCookLimits &) const noexcept = default;
    };

    /** @brief Immutable deterministic cook profile. */
    struct AnimationCompressionCookProfile final {
        AnimationCompressionContractVersion contractVersion{CurrentAnimationCompressionContractVersion};
        AnimationCompressionProfileId id{};
        AnimationCompressionCookTier tier{AnimationCompressionCookTier::Lossless};
        AnimationCompressionScheme scheme{AnimationCompressionScheme::None};
        AnimationCompressionErrorThresholds thresholds{};
        AnimationCompressionCookLimits limits{};
        [[nodiscard]] constexpr auto operator<=>(const AnimationCompressionCookProfile &) const noexcept = default;
    };

    /** @brief Exact immutable source/profile identity required to consume a compressed publication. */
    struct AnimationCompressionCompatibility final {
        AnimationCompressionContractVersion contractVersion{CurrentAnimationCompressionContractVersion};
        AnimationCompressionProfileId profile{};
        AnimationClipId clip{};
        AnimationClipGeneration clipGeneration{};
        SkeletonId skeleton{};
        SkeletonAssetGeneration skeletonGeneration{};
        AnimationCompressionScheme scheme{AnimationCompressionScheme::None};
        [[nodiscard]] auto operator<=>(const AnimationCompressionCompatibility &) const noexcept = default;
    };

    /** @brief Deterministic statistics for one successfully cooked publication. */
    struct AnimationCompressionStatistics final {
        std::uint32_t tracks{};
        std::uint32_t sourceKeys{};
        std::uint32_t outputKeys{};
        std::uint32_t removedKeys{};
        std::uint32_t maximumKeysPerTrack{};
        std::uint64_t errorEvaluations{};
        [[nodiscard]] constexpr auto operator<=>(const AnimationCompressionStatistics &) const noexcept = default;
    };

    /** @brief Captured cook lifecycle, reload identity, and finite resource policy. */
    struct AnimationCompressionCookContext final {
        AnimationClipAdmissionState admission{AnimationClipAdmissionState::Accepting};
        AnimationClipGeneration clipGeneration{};
        SkeletonAssetGeneration skeletonGeneration{};
        std::optional<AnimationCompressionCompatibility> replacing{};
    };

    /** @brief Finite frame-hot decompression policy supplied by the runtime owner. */
    struct AnimationDecompressionBudget final {
        std::uint32_t maximumTracks{AnimationCompressionHardLimits::DecompressedTracks};
        std::uint32_t maximumKeySearchSteps{AnimationCompressionHardLimits::KeySearchSteps};
        [[nodiscard]] constexpr auto operator<=>(const AnimationDecompressionBudget &) const noexcept = default;
    };

    /** @brief Exact immutable bindings and caller-owned storage for one decompressed pose sample. */
    struct AnimationDecompressionContext final {
        AnimationCompressionCompatibility compatibility{};
        AnimationDecompressionBudget budget{};
        AnimationClipSampleContext sample{};
    };

    /**
     * @brief Immutable compressed clip publication with bounded allocation-free runtime sampling.
     *
     * Cook is a load/cook boundary and may allocate. Sample is thread-safe, performs no allocation,
     * blocking I/O, callback, global lookup, or backend dispatch, and writes caller-owned storage only
     * after compatibility, lifecycle, and worst-case work budgets are validated.
     */
    class CompressedAnimationClipAsset final {
    public:
        /** @brief Returns exact source/profile compatibility. @return Borrowed immutable metadata. */
        [[nodiscard]] const AnimationCompressionCompatibility &Compatibility() const noexcept;

        /** @brief Returns deterministic cook statistics. @return Borrowed immutable statistics. */
        [[nodiscard]] const AnimationCompressionStatistics &Statistics() const noexcept;

        /** @brief Returns the canonical reduced clip. @return Borrowed immutable clip publication. */
        [[nodiscard]] const AnimationClipAsset &Clip() const noexcept;

        /**
         * @brief Samples the reduced clip under an exact compatibility and finite work budget.
         * @param time Exact clip-local time in the closed source duration interval.
         * @param context Current publication bindings, budget, and caller-owned pose spans.
         * @return Clip sample metadata or a typed lifecycle, stale, compatibility, or budget failure.
         * @post Success allocates nothing; failure performs no output writes.
         */
        [[nodiscard]] Result<AnimationClipSample> Sample(AnimationTime time, const AnimationDecompressionContext &context) const;

    private:
        friend Result<CompressedAnimationClipAsset> CookAnimationClip(const AnimationClipAsset &, const SkeletonAsset &,
                                                                      const AnimationCompressionCookProfile &,
                                                                      const AnimationCompressionCookContext &);

        /** @brief Stores one fully validated immutable compressed publication. */
        CompressedAnimationClipAsset(AnimationClipAsset clip, const AnimationCompressionCompatibility &compatibility,
                                     const AnimationCompressionStatistics &statistics) noexcept;

        AnimationClipAsset clip_;
        AnimationCompressionCompatibility compatibility_;
        AnimationCompressionStatistics statistics_;
    };

    /**
     * @brief Resolves a canonical built-in compression profile.
     * @param tier Requested provider-neutral cook tier.
     * @return Exact immutable profile or AnimationErrors::CompressionUnsupported.
     */
    [[nodiscard]] Result<AnimationCompressionCookProfile> GetAnimationCompressionCookProfile(AnimationCompressionCookTier tier);

    /**
     * @brief Deterministically reduces a validated clip into an immutable compressed publication.
     * @param source Exact validated source clip publication.
     * @param skeleton Exact immutable skeleton publication bound by the source.
     * @param profile Explicit immutable cook policy.
     * @param context Captured lifecycle and optional reload compatibility.
     * @return Compressed publication or a typed version, profile, binding, lifecycle, or budget failure.
     * @pre Load/cook/control boundary; never invoke from frame-hot animation evaluation.
     * @post Failure publishes no partial or replacement asset.
     */
    [[nodiscard]] Result<CompressedAnimationClipAsset> CookAnimationClip(const AnimationClipAsset &source, const SkeletonAsset &skeleton,
                                                                         const AnimationCompressionCookProfile &profile,
                                                                         const AnimationCompressionCookContext &context = {});
}  // namespace Horo::Animation
