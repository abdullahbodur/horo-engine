#include "Horo/Vfx/VfxQualityPolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Horo::Vfx {
    namespace {
        enum class Fit : std::uint8_t {
            Accepted,
            MissingKernel,
            Unsupported,
            Limit
        };

        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] constexpr bool IsKnown(const VfxEffectCategory value) noexcept {
            return value < VfxEffectCategory::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const VfxQualityProfile value) noexcept {
            return value < VfxQualityProfile::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const VfxCapability value) noexcept {
            return value < VfxCapability::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const VfxCapabilitySupport value) noexcept {
            return value < VfxCapabilitySupport::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const SimulationPreference value) noexcept {
            return value < SimulationPreference::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const ResolvedSimulationDomain value) noexcept {
            return value < ResolvedSimulationDomain::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const VfxRequirementClass value) noexcept {
            return value < VfxRequirementClass::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const VfxDegradation value) noexcept {
            return value < VfxDegradation::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const VfxHostMode value) noexcept {
            return value < VfxHostMode::Count;
        }

        [[nodiscard]] bool IsFinitePositive(const double value) noexcept {
            return std::isfinite(value) && value > 0.0;
        }

        [[nodiscard]] bool IsFiniteNonNegative(const double value) noexcept {
            return std::isfinite(value) && value >= 0.0;
        }

        [[nodiscard]] bool ValidatePolicyLimits(const VfxResourceLimits &limits) noexcept {
            return limits.maximumParticles > 0 && limits.maximumDecals > 0 && limits.maximumLights > 0 && limits.maximumVolumes > 0 &&
                   limits.maximumMemoryBytes > 0 && IsFinitePositive(limits.maximumCpuWorkMilliseconds) &&
                   IsFinitePositive(limits.maximumGpuWorkMilliseconds);
        }

        [[nodiscard]] bool ValidateCapabilityLimits(const VfxResourceLimits &limits) noexcept {
            return limits.maximumMemoryBytes > 0 && IsFiniteNonNegative(limits.maximumCpuWorkMilliseconds) &&
                   IsFiniteNonNegative(limits.maximumGpuWorkMilliseconds);
        }

        [[nodiscard]] std::uint32_t CategoryLimit(const VfxResourceLimits &limits, const VfxEffectCategory category) noexcept {
            switch (category) {
                case VfxEffectCategory::Particle:
                    return limits.maximumParticles;
                case VfxEffectCategory::Decal:
                    return limits.maximumDecals;
                case VfxEffectCategory::Light:
                    return limits.maximumLights;
                case VfxEffectCategory::Volumetric:
                    return limits.maximumVolumes;
                case VfxEffectCategory::Count:
                    return 0;
            }
            return 0;
        }

        [[nodiscard]] bool TryMultiply(const std::uint32_t count, const std::uint64_t bytesPerElement, std::uint64_t &total) noexcept {
            if (count != 0 && bytesPerElement > std::numeric_limits<std::uint64_t>::max() / count)
                return false;
            total = static_cast<std::uint64_t>(count) * bytesPerElement;
            return true;
        }

        [[nodiscard]] bool HasCapabilities(const VfxCapabilities &capabilities, const VfxCapabilityMask &required) noexcept {
            for (std::size_t index = 0; index < required.size(); ++index) {
                if (required[index] && capabilities.Support(static_cast<VfxCapability>(index)) != VfxCapabilitySupport::Available)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool HasAnyCapability(const VfxCapabilityMask &required) noexcept {
            return std::find(required.begin(), required.end(), true) != required.end();
        }

        [[nodiscard]] bool ValidateRequestIdentity(const VfxResolutionRequest &request) noexcept {
            return IsKnown(request.hostMode) && IsKnown(request.requestedProfile) && request.expectedCapabilityRevision.IsValid() &&
                   request.expectedPolicyRevision.IsValid();
        }

        [[nodiscard]] bool ValidateRequirementShape(const VfxEffectRequirements &requirements) noexcept {
            return IsKnown(requirements.category) && IsKnown(requirements.preference) && IsKnown(requirements.requirementClass) &&
                   requirements.requestedCount > 0 && requirements.minimumAuthoredCount > 0 &&
                   requirements.minimumAuthoredCount <= requirements.requestedCount && requirements.bytesPerElement > 0;
        }

        [[nodiscard]] bool ValidateRequirementWork(const VfxEffectRequirements &requirements) noexcept {
            return IsFiniteNonNegative(requirements.cpuWorkMilliseconds) && IsFiniteNonNegative(requirements.gpuWorkMilliseconds) &&
                   (!requirements.hasCpuKernel || IsFinitePositive(requirements.cpuWorkMilliseconds)) &&
                   (!requirements.hasGpuKernel || IsFinitePositive(requirements.gpuWorkMilliseconds));
        }

        [[nodiscard]] bool ValidateVariantIdentity(const VfxEffectRequirements &requirements, const VfxFallbackVariant &variant) noexcept {
            return variant.stableId != 0 && variant.category == requirements.category && IsKnown(variant.degradation) &&
                   variant.degradation != VfxDegradation::None && IsKnown(variant.domain) && IsFiniteNonNegative(variant.workMilliseconds);
        }

        [[nodiscard]] bool ValidateNullVariant(const VfxFallbackVariant &variant) noexcept {
            return variant.degradation == VfxDegradation::NullSuppression && variant.selectedCount == 0 && variant.bytesPerElement == 0 &&
                   variant.workMilliseconds == 0.0 && !variant.gameplayCompatible && !HasAnyCapability(variant.requiredGpuCapabilities);
        }

        [[nodiscard]] bool ValidateActiveVariant(const VfxEffectRequirements &requirements, const VfxFallbackVariant &variant) noexcept {
            return variant.selectedCount >= requirements.minimumAuthoredCount && variant.selectedCount <= requirements.requestedCount &&
                   variant.bytesPerElement > 0 && IsFinitePositive(variant.workMilliseconds);
        }

        [[nodiscard]] bool ValidateVariantPayload(const VfxEffectRequirements &requirements, const VfxFallbackVariant &variant) noexcept {
            return variant.domain == ResolvedSimulationDomain::Null ? ValidateNullVariant(variant)
                                                                    : ValidateActiveVariant(requirements, variant);
        }

        [[nodiscard]] bool ValidateVariantSemantics(const VfxEffectRequirements &requirements, const VfxFallbackVariant &variant) noexcept {
            if (variant.domain == ResolvedSimulationDomain::CPU && HasAnyCapability(variant.requiredGpuCapabilities))
                return false;
            if (variant.degradation == VfxDegradation::ReducedCount && variant.selectedCount >= requirements.requestedCount)
                return false;
            return variant.degradation != VfxDegradation::CompatibleCpu || variant.domain == ResolvedSimulationDomain::CPU;
        }

        [[nodiscard]] bool InsertUniqueVariantId(const std::uint32_t id, std::array<std::uint32_t, MaximumVfxFallbackVariants> &ids,
                                                 std::size_t &idCount) noexcept {
            const auto end = ids.begin() + static_cast<std::ptrdiff_t>(idCount);
            if (std::find(ids.begin(), end, id) != end)
                return false;
            ids[idCount++] = id;
            return true;
        }

        [[nodiscard]] Fit FitsLimits(const VfxCapabilities &capabilities, const VfxQualityPolicy &policy, const VfxEffectCategory category,
                                     const ResolvedSimulationDomain domain, const std::uint32_t count, const std::uint64_t bytesPerElement,
                                     const double workMilliseconds, std::uint64_t &memoryBytes) noexcept {
            const auto countLimit = std::min(CategoryLimit(capabilities.Limits(), category), CategoryLimit(policy.Limits(), category));
            if (count > countLimit || !TryMultiply(count, bytesPerElement, memoryBytes) ||
                memoryBytes > std::min(capabilities.Limits().maximumMemoryBytes, policy.Limits().maximumMemoryBytes))
                return Fit::Limit;

            if (domain == ResolvedSimulationDomain::CPU &&
                workMilliseconds > std::min(capabilities.Limits().maximumCpuWorkMilliseconds, policy.Limits().maximumCpuWorkMilliseconds))
                return Fit::Limit;
            if (domain == ResolvedSimulationDomain::GPU &&
                workMilliseconds > std::min(capabilities.Limits().maximumGpuWorkMilliseconds, policy.Limits().maximumGpuWorkMilliseconds))
                return Fit::Limit;
            return Fit::Accepted;
        }

        [[nodiscard]] Fit TryPrimary(const VfxCapabilities &capabilities, const VfxQualityPolicy &policy,
                                     const VfxEffectRequirements &requirements, const ResolvedSimulationDomain domain,
                                     std::uint64_t &memoryBytes) noexcept {
            if (domain == ResolvedSimulationDomain::CPU) {
                if (!requirements.hasCpuKernel)
                    return Fit::MissingKernel;
                if (capabilities.Support(VfxCapability::CpuSimulation) != VfxCapabilitySupport::Available)
                    return Fit::Unsupported;
                return FitsLimits(capabilities, policy, requirements.category, domain, requirements.requestedCount,
                                  requirements.bytesPerElement, requirements.cpuWorkMilliseconds, memoryBytes);
            }
            if (domain == ResolvedSimulationDomain::GPU) {
                if (!requirements.hasGpuKernel)
                    return Fit::MissingKernel;
                if (capabilities.Support(VfxCapability::GpuSimulation) != VfxCapabilitySupport::Available ||
                    !HasCapabilities(capabilities, requirements.requiredGpuCapabilities))
                    return Fit::Unsupported;
                return FitsLimits(capabilities, policy, requirements.category, domain, requirements.requestedCount,
                                  requirements.bytesPerElement, requirements.gpuWorkMilliseconds, memoryBytes);
            }
            return Fit::Unsupported;
        }

        [[nodiscard]] bool ValidateRequirements(const VfxResolutionRequest &request) noexcept {
            const auto &requirements = request.requirements;
            if (!ValidateRequestIdentity(request) || !ValidateRequirementShape(requirements) || !ValidateRequirementWork(requirements) ||
                request.fallbackVariants.size() > MaximumVfxFallbackVariants)
                return false;

            std::array<std::uint32_t, MaximumVfxFallbackVariants> ids{};
            std::size_t idCount{};
            for (const auto &variant : request.fallbackVariants) {
                if (!ValidateVariantIdentity(requirements, variant) || !ValidateVariantPayload(requirements, variant) ||
                    !ValidateVariantSemantics(requirements, variant) || !InsertUniqueVariantId(variant.stableId, ids, idCount))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool HostAllowsDomain(const VfxResolutionRequest &request, const ResolvedSimulationDomain domain) noexcept {
            return request.hostMode != VfxHostMode::Headless || domain != ResolvedSimulationDomain::GPU;
        }

        [[nodiscard]] bool RequirementAllowsDomain(const VfxEffectRequirements &requirements,
                                                   const ResolvedSimulationDomain domain) noexcept {
            if (requirements.cpuMandatory && domain != ResolvedSimulationDomain::CPU)
                return false;
            if (requirements.preference == SimulationPreference::RequireCPU && domain != ResolvedSimulationDomain::CPU)
                return false;
            return requirements.preference != SimulationPreference::RequireGPU || domain == ResolvedSimulationDomain::GPU;
        }

        [[nodiscard]] bool ClassAllowsVariant(const VfxResolutionRequest &request, const VfxFallbackVariant &variant) noexcept {
            if (request.requirements.requirementClass == VfxRequirementClass::GameplayRequired)
                return variant.gameplayCompatible && variant.domain != ResolvedSimulationDomain::Null;
            return variant.domain != ResolvedSimulationDomain::Null || request.hostMode == VfxHostMode::Headless;
        }

        [[nodiscard]] bool DomainAllowedForVariant(const VfxResolutionRequest &request, const VfxFallbackVariant &variant) noexcept {
            return HostAllowsDomain(request, variant.domain) && RequirementAllowsDomain(request.requirements, variant.domain) &&
                   ClassAllowsVariant(request, variant);
        }

        [[nodiscard]] Fit TryVariant(const VfxCapabilities &capabilities, const VfxQualityPolicy &policy,
                                     const VfxResolutionRequest &request, const VfxFallbackVariant &variant,
                                     std::uint64_t &memoryBytes) noexcept {
            if (!DomainAllowedForVariant(request, variant))
                return Fit::Unsupported;
            if (variant.domain == ResolvedSimulationDomain::Null) {
                memoryBytes = 0;
                return Fit::Accepted;
            }
            if (variant.domain == ResolvedSimulationDomain::CPU &&
                capabilities.Support(VfxCapability::CpuSimulation) != VfxCapabilitySupport::Available)
                return Fit::Unsupported;
            if (variant.domain == ResolvedSimulationDomain::GPU &&
                (capabilities.Support(VfxCapability::GpuSimulation) != VfxCapabilitySupport::Available ||
                 !HasCapabilities(capabilities, variant.requiredGpuCapabilities)))
                return Fit::Unsupported;
            return FitsLimits(capabilities, policy, variant.category, variant.domain, variant.selectedCount, variant.bytesPerElement,
                              variant.workMilliseconds, memoryBytes);
        }

        [[nodiscard]] Result<VfxResolution> MakeResolution(const VfxCapabilities &capabilities, const VfxQualityPolicy &policy,
                                                           const VfxResolutionRequest &request, const ResolvedSimulationDomain domain,
                                                           const VfxDegradation degradation, const std::uint32_t variantId,
                                                           const std::uint32_t count, const std::uint64_t memoryBytes) {
            return Result<VfxResolution>::Success({.requestedProfile = request.requestedProfile,
                                                   .selectedProfile = policy.Profile(),
                                                   .domain = domain,
                                                   .degradation = degradation,
                                                   .selectedVariantId = variantId,
                                                   .selectedCount = count,
                                                   .selectedMemoryBytes = memoryBytes,
                                                   .capabilityRevision = capabilities.Revision(),
                                                   .policyRevision = policy.Revision()});
        }

        [[nodiscard]] bool CanonicalizeCapabilityFacts(const std::span<const VfxCapabilityFact> facts,
                                                       std::array<VfxCapabilitySupport, VfxCapabilityCount> &canonical) noexcept {
            std::array<bool, VfxCapabilityCount> present{};
            for (const auto &fact : facts) {
                if (!IsKnown(fact.capability) || !IsKnown(fact.support))
                    return false;
                const auto index = static_cast<std::size_t>(fact.capability);
                if (present[index])
                    return false;
                present[index] = true;
                canonical[index] = fact.support;
            }
            return std::find(present.begin(), present.end(), false) == present.end();
        }

        [[nodiscard]] bool GpuFactsAreConsistent(const std::array<VfxCapabilitySupport, VfxCapabilityCount> &canonical) noexcept {
            const auto available = [&canonical](const VfxCapability capability) {
                return canonical[static_cast<std::size_t>(capability)] == VfxCapabilitySupport::Available;
            };
            const bool requiresGpuSimulation = available(VfxCapability::IndirectDraw) || available(VfxCapability::GpuSorting) ||
                                               available(VfxCapability::VolumeTextures) || available(VfxCapability::VectorFields);
            return !requiresGpuSimulation || available(VfxCapability::GpuSimulation);
        }

        [[nodiscard]] bool WorkLimitsAreConsistent(const std::array<VfxCapabilitySupport, VfxCapabilityCount> &canonical,
                                                   const VfxResourceLimits &limits) noexcept {
            const auto available = [&canonical](const VfxCapability capability) {
                return canonical[static_cast<std::size_t>(capability)] == VfxCapabilitySupport::Available;
            };
            return (!available(VfxCapability::CpuSimulation) || limits.maximumCpuWorkMilliseconds > 0.0) &&
                   (!available(VfxCapability::GpuSimulation) || limits.maximumGpuWorkMilliseconds > 0.0);
        }

        struct PrimaryOrder {
            std::array<ResolvedSimulationDomain, 2> domains{};
            std::size_t count{};
        };

        void AppendPrimary(PrimaryOrder &order, const VfxHostMode hostMode, const ResolvedSimulationDomain domain) noexcept {
            if (hostMode != VfxHostMode::Headless || domain != ResolvedSimulationDomain::GPU)
                order.domains[order.count++] = domain;
        }

        [[nodiscard]] PrimaryOrder AutomaticPrimaryOrder(const VfxQualityPolicy &policy, const VfxResolutionRequest &request) noexcept {
            PrimaryOrder order{};
            const bool preferGpu = request.requirements.category == VfxEffectCategory::Particle &&
                                   request.requirements.requestedCount > policy.AutoGpuParticleThreshold();
            AppendPrimary(order, request.hostMode, preferGpu ? ResolvedSimulationDomain::GPU : ResolvedSimulationDomain::CPU);
            AppendPrimary(order, request.hostMode, preferGpu ? ResolvedSimulationDomain::CPU : ResolvedSimulationDomain::GPU);
            return order;
        }

        [[nodiscard]] PrimaryOrder PreferredPrimaryOrder(const VfxQualityPolicy &policy, const VfxResolutionRequest &request) noexcept {
            PrimaryOrder order{};
            switch (request.requirements.preference) {
                case SimulationPreference::RequireCPU:
                    AppendPrimary(order, request.hostMode, ResolvedSimulationDomain::CPU);
                    break;
                case SimulationPreference::RequireGPU:
                    AppendPrimary(order, request.hostMode, ResolvedSimulationDomain::GPU);
                    break;
                case SimulationPreference::PreferCPU:
                    AppendPrimary(order, request.hostMode, ResolvedSimulationDomain::CPU);
                    AppendPrimary(order, request.hostMode, ResolvedSimulationDomain::GPU);
                    break;
                case SimulationPreference::PreferGPU:
                    AppendPrimary(order, request.hostMode, ResolvedSimulationDomain::GPU);
                    AppendPrimary(order, request.hostMode, ResolvedSimulationDomain::CPU);
                    break;
                case SimulationPreference::Automatic:
                    return AutomaticPrimaryOrder(policy, request);
                case SimulationPreference::Count:
                    break;
            }
            return order;
        }

        [[nodiscard]] PrimaryOrder BuildPrimaryOrder(const VfxQualityPolicy &policy, const VfxResolutionRequest &request) noexcept {
            if (request.requirements.cpuMandatory || request.requirements.requirementClass == VfxRequirementClass::GameplayRequired) {
                PrimaryOrder order{};
                AppendPrimary(order, request.hostMode, ResolvedSimulationDomain::CPU);
                return order;
            }
            return PreferredPrimaryOrder(policy, request);
        }

        struct ResolutionCandidate {
            const VfxFallbackVariant *variant{};
            ResolvedSimulationDomain domain{ResolvedSimulationDomain::CPU};
            std::uint64_t memoryBytes{};
            bool found{};
        };

        struct PrimaryEvaluation {
            ResolutionCandidate candidate{};
            Fit strongestFailure{Fit::Unsupported};
        };

        [[nodiscard]] PrimaryEvaluation EvaluatePrimaries(const VfxCapabilities &capabilities, const VfxQualityPolicy &policy,
                                                          const VfxResolutionRequest &request, const PrimaryOrder &order) noexcept {
            PrimaryEvaluation evaluation{};
            for (std::size_t index = 0; index < order.count; ++index) {
                std::uint64_t memoryBytes{};
                const auto fit = TryPrimary(capabilities, policy, request.requirements, order.domains[index], memoryBytes);
                if (fit == Fit::Accepted)
                    return {.candidate = {.domain = order.domains[index], .memoryBytes = memoryBytes, .found = true},
                            .strongestFailure = evaluation.strongestFailure};
                if (fit == Fit::Limit || (fit == Fit::MissingKernel && evaluation.strongestFailure == Fit::Unsupported))
                    evaluation.strongestFailure = fit;
            }
            return evaluation;
        }

        [[nodiscard]] ResolutionCandidate FindVariantAtRank(const VfxCapabilities &capabilities, const VfxQualityPolicy &policy,
                                                            const VfxResolutionRequest &request, const std::uint8_t rank) noexcept {
            ResolutionCandidate selected{};
            for (const auto &variant : request.fallbackVariants) {
                if (static_cast<std::uint8_t>(variant.degradation) != rank)
                    continue;
                std::uint64_t memoryBytes{};
                if (TryVariant(capabilities, policy, request, variant, memoryBytes) != Fit::Accepted)
                    continue;
                if (!selected.found || variant.stableId < selected.variant->stableId)
                    selected = {.variant = &variant, .domain = variant.domain, .memoryBytes = memoryBytes, .found = true};
            }
            return selected;
        }

        [[nodiscard]] ResolutionCandidate EvaluateFallbacks(const VfxCapabilities &capabilities, const VfxQualityPolicy &policy,
                                                            const VfxResolutionRequest &request) noexcept {
            for (std::uint8_t rank = static_cast<std::uint8_t>(VfxDegradation::ReducedCount);
                 rank < static_cast<std::uint8_t>(VfxDegradation::Count); ++rank) {
                auto selected = FindVariantAtRank(capabilities, policy, request, rank);
                if (selected.found)
                    return selected;
            }
            return {};
        }

        [[nodiscard]] Result<VfxResolution> ResolveFailure(const VfxResolutionRequest &request, const Fit strongestFailure) {
            if (!request.fallbackVariants.empty())
                return Failure<VfxResolution>(VfxErrors::MissingVariant);
            if (strongestFailure == Fit::Limit)
                return Failure<VfxResolution>(VfxErrors::LimitExceeded);
            if (strongestFailure == Fit::MissingKernel)
                return Failure<VfxResolution>(VfxErrors::MissingKernel);
            return Failure<VfxResolution>(VfxErrors::UnsupportedCapability);
        }
    }  // namespace

    /** @copydoc VfxCapabilities::Create */
    Result<VfxCapabilities> VfxCapabilities::Create(const VfxCapabilityRevision revision, const std::span<const VfxCapabilityFact> facts,
                                                    const VfxResourceLimits limits) {
        if (!revision.IsValid() || facts.size() != VfxCapabilityCount || !ValidateCapabilityLimits(limits))
            return Failure<VfxCapabilities>(VfxErrors::CapabilityDataInvalid);

        std::array<VfxCapabilitySupport, VfxCapabilityCount> canonical{};
        if (!CanonicalizeCapabilityFacts(facts, canonical) || !GpuFactsAreConsistent(canonical) ||
            !WorkLimitsAreConsistent(canonical, limits))
            return Failure<VfxCapabilities>(VfxErrors::CapabilityDataInvalid);
        return Result<VfxCapabilities>::Success(VfxCapabilities{revision, canonical, limits});
    }

    VfxCapabilities::VfxCapabilities(const VfxCapabilityRevision revision, const std::array<VfxCapabilitySupport, VfxCapabilityCount> facts,
                                     const VfxResourceLimits limits) noexcept
        : revision_(revision), facts_(facts), limits_(limits) {}

    /** @copydoc VfxCapabilities::Revision */
    VfxCapabilityRevision VfxCapabilities::Revision() const noexcept {
        return revision_;
    }

    /** @copydoc VfxCapabilities::Support */
    VfxCapabilitySupport VfxCapabilities::Support(const VfxCapability capability) const noexcept {
        if (!IsKnown(capability))
            return VfxCapabilitySupport::Unknown;
        return facts_[static_cast<std::size_t>(capability)];
    }

    /** @copydoc VfxCapabilities::Limits */
    const VfxResourceLimits &VfxCapabilities::Limits() const noexcept {
        return limits_;
    }

    /** @copydoc VfxQualityPolicy::Create */
    Result<VfxQualityPolicy> VfxQualityPolicy::Create(const VfxQualityPolicyDescriptor &descriptor) {
        if (!descriptor.revision.IsValid() || !IsKnown(descriptor.profile) || !ValidatePolicyLimits(descriptor.limits) ||
            descriptor.autoGpuParticleThreshold == 0 || descriptor.autoGpuParticleThreshold > descriptor.limits.maximumParticles)
            return Failure<VfxQualityPolicy>(VfxErrors::QualityPolicyInvalid);
        return Result<VfxQualityPolicy>::Success(VfxQualityPolicy{descriptor});
    }

    VfxQualityPolicy::VfxQualityPolicy(const VfxQualityPolicyDescriptor descriptor) noexcept : descriptor_(descriptor) {}

    /** @copydoc VfxQualityPolicy::Revision */
    VfxQualityPolicyRevision VfxQualityPolicy::Revision() const noexcept {
        return descriptor_.revision;
    }

    /** @copydoc VfxQualityPolicy::Profile */
    VfxQualityProfile VfxQualityPolicy::Profile() const noexcept {
        return descriptor_.profile;
    }

    /** @copydoc VfxQualityPolicy::Limits */
    const VfxResourceLimits &VfxQualityPolicy::Limits() const noexcept {
        return descriptor_.limits;
    }

    /** @copydoc VfxQualityPolicy::AutoGpuParticleThreshold */
    std::uint32_t VfxQualityPolicy::AutoGpuParticleThreshold() const noexcept {
        return descriptor_.autoGpuParticleThreshold;
    }

    /** @copydoc ResolveSimulationDomain */
    Result<VfxResolution> ResolveSimulationDomain(const VfxCapabilities &capabilities, const VfxQualityPolicy &policy,
                                                  const VfxResolutionRequest &request) {
        if (!ValidateRequirements(request))
            return Failure<VfxResolution>(VfxErrors::RequirementInvalid);
        if (request.expectedCapabilityRevision != capabilities.Revision())
            return Failure<VfxResolution>(VfxErrors::CapabilityRevisionStale);
        if (request.expectedPolicyRevision != policy.Revision())
            return Failure<VfxResolution>(VfxErrors::QualityPolicyRevisionStale);
        if (request.requestedProfile != policy.Profile())
            return Failure<VfxResolution>(VfxErrors::QualityPolicyInvalid);
        if (request.requirements.cpuMandatory && request.requirements.preference == SimulationPreference::RequireGPU)
            return Failure<VfxResolution>(VfxErrors::DomainConflict);

        const auto primaryEvaluation = EvaluatePrimaries(capabilities, policy, request, BuildPrimaryOrder(policy, request));
        if (primaryEvaluation.candidate.found)
            return MakeResolution(capabilities, policy, request, primaryEvaluation.candidate.domain, VfxDegradation::None, 0,
                                  request.requirements.requestedCount, primaryEvaluation.candidate.memoryBytes);

        const auto fallback = EvaluateFallbacks(capabilities, policy, request);
        if (fallback.found)
            return MakeResolution(capabilities, policy, request, fallback.domain, fallback.variant->degradation, fallback.variant->stableId,
                                  fallback.variant->selectedCount, fallback.memoryBytes);
        return ResolveFailure(request, primaryEvaluation.strongestFailure);
    }

    /** @copydoc ValidateVfxResolutionFreshness */
    Result<void> ValidateVfxResolutionFreshness(const VfxResolution &resolution, const VfxCapabilityRevision currentCapabilityRevision,
                                                const VfxQualityPolicyRevision currentPolicyRevision) {
        if (!currentCapabilityRevision.IsValid() || resolution.capabilityRevision != currentCapabilityRevision)
            return Failure<void>(VfxErrors::CapabilityRevisionStale);
        if (!currentPolicyRevision.IsValid() || resolution.policyRevision != currentPolicyRevision)
            return Failure<void>(VfxErrors::QualityPolicyRevisionStale);
        return Result<void>::Success();
    }
}  // namespace Horo::Vfx
