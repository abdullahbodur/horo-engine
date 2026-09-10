#pragma once

/**
 * @file VfxQualityPolicy.h
 * @brief Backend-neutral VFX capability, quality-policy, and simulation-domain resolution contracts.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/StrongId.h"
#include "Horo/Vfx/VfxErrors.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Horo::Vfx {
    namespace Detail {
        struct VfxCapabilityRevisionTag;
        struct VfxQualityPolicyRevisionTag;
    }  // namespace Detail

    /** @brief Non-zero generation of one immutable effective VFX capability snapshot. */
    using VfxCapabilityRevision = Foundation::Detail::NonZeroId64<Detail::VfxCapabilityRevisionTag, VfxErrors::CapabilityDataInvalid>;
    /** @brief Non-zero generation of one immutable validated VFX quality policy. */
    using VfxQualityPolicyRevision = Foundation::Detail::NonZeroId64<Detail::VfxQualityPolicyRevisionTag, VfxErrors::QualityPolicyInvalid>;

    /** @brief Closed VFX effect categories with independently bounded degradation behavior. */
    enum class VfxEffectCategory : std::uint8_t {
        Particle,
        Decal,
        Light,
        Volumetric,
        Count
    };
    /** @brief Product recipe preference; never a renderer API, platform, or hardware class. */
    enum class VfxQualityProfile : std::uint8_t {
        Baseline,
        Standard,
        High,
        Ultra,
        Count
    };
    /** @brief Actual usable facts supplied by the composed host and selected device. */
    enum class VfxCapability : std::uint8_t {
        CpuSimulation,
        GpuSimulation,
        IndirectDraw,
        GpuSorting,
        VolumeTextures,
        VectorFields,
        DeferredDecals,
        TransientLights,
        Count
    };
    /** @brief Explicit support state; unknown evidence never admits work. */
    enum class VfxCapabilitySupport : std::uint8_t {
        Unknown,
        Unsupported,
        Available,
        Count
    };
    /** @brief Authored per-emitter simulation-domain preference. */
    enum class SimulationPreference : std::uint8_t {
        Automatic,
        RequireCPU,
        PreferCPU,
        PreferGPU,
        RequireGPU,
        Count
    };
    /** @brief Domain selected by the pure resolver. */
    enum class ResolvedSimulationDomain : std::uint8_t {
        CPU,
        GPU,
        Null,
        Count
    };
    /** @brief Whether the effect participates in authoritative gameplay behavior. */
    enum class VfxRequirementClass : std::uint8_t {
        Cosmetic,
        GameplayRequired,
        Count
    };
    /** @brief Observable authored degradation selected by the resolver. */
    enum class VfxDegradation : std::uint8_t {
        None,
        ReducedCount,
        SimpleCookedVariant,
        CompatibleCpu,
        Substitute,
        NullSuppression,
        Count
    };
    /** @brief Host presentation mode; Null remains an explicit fallback rather than a profile. */
    enum class VfxHostMode : std::uint8_t {
        Interactive,
        Headless,
        Count
    };

    inline constexpr std::size_t VfxCapabilityCount = static_cast<std::size_t>(VfxCapability::Count);
    inline constexpr std::size_t VfxEffectCategoryCount = static_cast<std::size_t>(VfxEffectCategory::Count);
    inline constexpr std::size_t MaximumVfxFallbackVariants = 16;

    /** @brief One explicit support fact in an effective capability snapshot. */
    struct VfxCapabilityFact final {
        VfxCapability capability{};     /**< Closed capability identity. */
        VfxCapabilitySupport support{}; /**< Effective usable support, not raw hardware presence. */
    };

    /** @brief Finite resource ceilings expressed independently by unit. */
    struct VfxResourceLimits final {
        std::uint32_t maximumParticles{};    /**< Aggregate particle count. */
        std::uint32_t maximumDecals{};       /**< Aggregate logical decal count. */
        std::uint32_t maximumLights{};       /**< Aggregate VFX transient light count. */
        std::uint32_t maximumVolumes{};      /**< Aggregate volumetric-effect count. */
        std::uint64_t maximumMemoryBytes{};  /**< Total admitted bytes. */
        double maximumCpuWorkMilliseconds{}; /**< Finite positive CPU work budget. */
        double maximumGpuWorkMilliseconds{}; /**< Finite positive GPU work budget. */
    };

    /** @brief Owned immutable effective VFX facts and device/host limits. */
    class VfxCapabilities final {
    public:
        /**
         * @brief Validates and canonicalizes a complete effective capability snapshot.
         * @param revision Non-zero generation, replaced whenever any fact or limit changes.
         * @param facts Exactly one entry for every VfxCapability.
         * @param limits Explicit finite limits; zero counts mean that category cannot be admitted.
         * @return Immutable snapshot or CapabilityDataInvalid.
         */
        [[nodiscard]] static Result<VfxCapabilities> Create(VfxCapabilityRevision revision, std::span<const VfxCapabilityFact> facts,
                                                            const VfxResourceLimits &limits);

        /** @brief Returns the immutable evidence revision. @return Non-zero revision. */
        [[nodiscard]] VfxCapabilityRevision Revision() const noexcept;
        /** @brief Reads one effective support fact. @param capability Closed capability identity. @return Explicit support state. */
        [[nodiscard]] VfxCapabilitySupport Support(VfxCapability capability) const noexcept;
        /** @brief Returns finite effective limits. @return Immutable limits. */
        [[nodiscard]] const VfxResourceLimits &Limits() const noexcept;

    private:
        VfxCapabilities(VfxCapabilityRevision revision, const std::array<VfxCapabilitySupport, VfxCapabilityCount> &facts,
                        const VfxResourceLimits &limits) noexcept;

        VfxCapabilityRevision revision_{};
        std::array<VfxCapabilitySupport, VfxCapabilityCount> facts_{};
        VfxResourceLimits limits_{};
    };

    /** @brief Candidate policy data validated atomically before publication. */
    struct VfxQualityPolicyDescriptor final {
        VfxQualityPolicyRevision revision{};      /**< Unique policy generation. */
        VfxQualityProfile profile{};              /**< Selected product recipe preference. */
        VfxResourceLimits limits{};               /**< Finite product ceilings. */
        std::uint32_t autoGpuParticleThreshold{}; /**< Automatic chooses GPU strictly above this count. */
    };

    /** @brief Immutable validated VFX quality-policy revision. */
    class VfxQualityPolicy final {
    public:
        /** @brief Validates a complete candidate without altering any prior policy. @param descriptor Candidate data.
         * @return Owned policy or QualityPolicyInvalid; callers retain their previous policy on failure.
         */
        [[nodiscard]] static Result<VfxQualityPolicy> Create(const VfxQualityPolicyDescriptor &descriptor);
        /** @brief Returns the policy revision. @return Non-zero revision. */
        [[nodiscard]] VfxQualityPolicyRevision Revision() const noexcept;
        /** @brief Returns the selected product profile. @return Backend-independent profile. */
        [[nodiscard]] VfxQualityProfile Profile() const noexcept;
        /** @brief Returns finite product ceilings. @return Immutable limits. */
        [[nodiscard]] const VfxResourceLimits &Limits() const noexcept;
        /** @brief Returns the deterministic Automatic particle threshold. @return Positive particle count. */
        [[nodiscard]] std::uint32_t AutoGpuParticleThreshold() const noexcept;

    private:
        explicit VfxQualityPolicy(const VfxQualityPolicyDescriptor &descriptor) noexcept;
        VfxQualityPolicyDescriptor descriptor_{};
    };

    /** @brief Required capability mask represented without allocation or backend-native values. */
    using VfxCapabilityMask = std::array<bool, VfxCapabilityCount>;

    /** @brief Primary compiled effect-unit requirements submitted to deterministic resolution. */
    struct VfxEffectRequirements final {
        VfxEffectCategory category{};                /**< Closed effect category. */
        SimulationPreference preference{};           /**< Authored domain intent. */
        VfxRequirementClass requirementClass{};      /**< Cosmetic or gameplay-required behavior. */
        std::uint32_t requestedCount{};              /**< Positive particle/decal/light/volume count. */
        std::uint32_t minimumAuthoredCount{};        /**< Positive floor for non-Null authored degradation. */
        std::uint64_t bytesPerElement{};             /**< Positive peak bytes per selected element. */
        double cpuWorkMilliseconds{};                /**< Finite non-negative primary CPU cost. */
        double gpuWorkMilliseconds{};                /**< Finite non-negative primary GPU cost. */
        bool cpuMandatory{};                         /**< True for gameplay dependencies or CPU geometry queries. */
        bool hasCpuKernel{};                         /**< Compatible primary CPU kernel is cooked. */
        bool hasGpuKernel{};                         /**< Compatible primary GPU kernel is cooked. */
        VfxCapabilityMask requiredGpuCapabilities{}; /**< Complete primary GPU predicates. */
    };

    /** @brief One cooked authored fallback considered in a fixed degradation order. */
    struct VfxFallbackVariant final {
        std::uint32_t stableId{};                    /**< Non-zero deterministic cooked identity. */
        VfxEffectCategory category{};                /**< Must match the primary effect category. */
        VfxDegradation degradation{};                /**< Explicit observable degradation kind. */
        ResolvedSimulationDomain domain{};           /**< CPU, GPU, or authored Null suppression. */
        std::uint32_t selectedCount{};               /**< Positive unless domain is Null. */
        std::uint64_t bytesPerElement{};             /**< Positive unless domain is Null. */
        double workMilliseconds{};                   /**< Finite non-negative cost in the selected domain. */
        bool gameplayCompatible{};                   /**< Required for gameplay-required requests. */
        VfxCapabilityMask requiredGpuCapabilities{}; /**< GPU predicates for this cooked variant. */
    };

    /** @brief Captured immutable inputs for one pure resolution attempt. */
    struct VfxResolutionRequest final {
        VfxHostMode hostMode{};                                 /**< Interactive or headless composition. */
        VfxQualityProfile requestedProfile{};                   /**< Requested product profile for evidence. */
        VfxCapabilityRevision expectedCapabilityRevision{};     /**< Freshness fence. */
        VfxQualityPolicyRevision expectedPolicyRevision{};      /**< Freshness fence. */
        VfxEffectRequirements requirements{};                   /**< Primary compiled requirements. */
        std::span<const VfxFallbackVariant> fallbackVariants{}; /**< Bounded borrowed cooked variants. */
    };

    /** @brief Immutable successful domain/profile/degradation decision and its evidence generations. */
    struct VfxResolution final {
        VfxQualityProfile requestedProfile{};       /**< Profile retained by the captured policy request. */
        VfxQualityProfile selectedProfile{};        /**< Exact validated policy profile used for admission. */
        ResolvedSimulationDomain domain{};          /**< Selected CPU, GPU, or explicitly authored Null path. */
        VfxDegradation degradation{};               /**< Observable authored degradation, or None. */
        std::uint32_t selectedVariantId{};          /**< Zero denotes the primary non-degraded path. */
        std::uint32_t selectedCount{};              /**< Exact admitted element count; zero only for Null. */
        std::uint64_t selectedMemoryBytes{};        /**< Checked count-bytes product for the selected path. */
        VfxCapabilityRevision capabilityRevision{}; /**< Capability evidence generation. */
        VfxQualityPolicyRevision policyRevision{};  /**< Quality policy generation. */

        [[nodiscard]] constexpr auto operator<=>(const VfxResolution &) const noexcept = default;
    };

    /**
     * @brief Resolves one compiled effect unit without mutation, native calls, or implicit fallback.
     * @param capabilities Exact immutable effective capability snapshot.
     * @param policy Exact immutable validated quality policy.
     * @param request Captured requirements, revisions, host mode, and authored variants.
     * @return Deterministic decision or typed stale/invalid/capability/kernel/variant/limit failure.
     * @note Successful resolution uses bounded stack work and performs no heap allocation. Error construction follows the shared
     *       Foundation Error contract.
     */
    [[nodiscard]] Result<VfxResolution> ResolveSimulationDomain(const VfxCapabilities &capabilities, const VfxQualityPolicy &policy,
                                                                const VfxResolutionRequest &request);

    /**
     * @brief Revalidates prepared resolution evidence before activation.
     * @param resolution Previously prepared immutable decision.
     * @param currentCapabilityRevision Current owner-published capability revision.
     * @param currentPolicyRevision Current owner-published policy revision.
     * @return Success or the precise stale capability/policy error.
     */
    [[nodiscard]] Result<void> ValidateVfxResolutionFreshness(const VfxResolution &resolution,
                                                              VfxCapabilityRevision currentCapabilityRevision,
                                                              VfxQualityPolicyRevision currentPolicyRevision);
}  // namespace Horo::Vfx
