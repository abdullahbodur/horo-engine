#include "Horo/Destruction/DestructibleDescriptor.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Horo::Destruction {
    namespace {
        constexpr std::uint32_t PreCooked = DestructionFeatureBit<DestructionFeature::PreCookedFracture>;
        constexpr std::uint32_t Support = DestructionFeatureBit<DestructionFeature::CookedSupport>;
        constexpr std::uint32_t Staged = DestructionFeatureBit<DestructionFeature::StagedActivation>;
        constexpr std::uint32_t Hierarchy = DestructionFeatureBit<DestructionFeature::HierarchicalFracture>;
        constexpr std::uint32_t Debris = DestructionFeatureBit<DestructionFeature::CosmeticDebris>;
        constexpr std::uint32_t Dormancy = DestructionFeatureBit<DestructionFeature::DurableDormancy>;
        constexpr std::uint32_t Replication = DestructionFeatureBit<DestructionFeature::AuthoritativeReplication>;
        constexpr std::uint32_t RuntimeGeometry = DestructionFeatureBit<DestructionFeature::RuntimeGeometryGeneration>;

        constexpr DestructionFeatureSet BaselineFeatures{PreCooked | Support | Debris | Dormancy | Replication};
        constexpr DestructionFeatureSet StandardFeatures{BaselineFeatures.bits | Staged};
        constexpr DestructionFeatureSet HighFeatures{StandardFeatures.bits | Hierarchy};

        constexpr DestructionLimits
            BaselineLimits{64,   1, 64, 128, 512, 256, 16ULL * 1024ULL * 1024ULL, 32ULL * 1024ULL * 1024ULL, 64ULL * 1024ULL * 1024ULL,
                           8'192};
        constexpr DestructionLimits StandardLimits{256,
                                                   4,
                                                   256,
                                                   1'024,
                                                   4'096,
                                                   1'024,
                                                   64ULL * 1024ULL * 1024ULL,
                                                   128ULL * 1024ULL * 1024ULL,
                                                   256ULL * 1024ULL * 1024ULL,
                                                   65'536};
        constexpr DestructionLimits HighLimits{DestructionHardLimits::ChunksPerDestructible, DestructionHardLimits::HierarchyDepth,
                                               DestructionHardLimits::ActiveChunkBodies,     DestructionHardLimits::EventsPerTransition,
                                               DestructionHardLimits::EventJournalEntries,   DestructionHardLimits::CosmeticDebrisParticles,
                                               DestructionHardLimits::ArtifactBytes,         DestructionHardLimits::TransitionBytes,
                                               DestructionHardLimits::ResidentBytes,         DestructionHardLimits::WorkItemsPerTransition};

        template <typename Enum> [[nodiscard]] constexpr bool IsKnown(const Enum value, const Enum count) noexcept {
            return value < count;
        }

        [[nodiscard]] constexpr std::array<std::uint64_t, 10> LimitValues(const DestructionLimits &limits) noexcept {
            return {limits.maximumChunksPerDestructible, limits.maximumHierarchyDepth,      limits.maximumActiveChunkBodies,
                    limits.maximumEventsPerTransition,   limits.maximumEventJournalEntries, limits.maximumCosmeticDebrisParticles,
                    limits.maximumArtifactBytes,         limits.maximumTransitionBytes,     limits.maximumResidentBytes,
                    limits.maximumWorkItemsPerTransition};
        }

        [[nodiscard]] constexpr bool IsPositive(const DestructionLimits &limits) noexcept {
            const auto values = LimitValues(limits);
            return std::ranges::all_of(values, [](const std::uint64_t value) {
                return value > 0;
            });
        }

        [[nodiscard]] constexpr bool FitsWithin(const DestructionLimits &value, const DestructionLimits &ceiling) noexcept {
            const auto values = LimitValues(value);
            const auto ceilings = LimitValues(ceiling);
            return std::ranges::equal(values, ceilings, [](const std::uint64_t actual, const std::uint64_t maximum) {
                return actual <= maximum;
            });
        }

        [[nodiscard]] constexpr bool IsInternallyConsistent(const DestructionLimits &limits) noexcept {
            return limits.maximumActiveChunkBodies <= limits.maximumChunksPerDestructible &&
                   limits.maximumEventsPerTransition <= limits.maximumEventJournalEntries &&
                   limits.maximumArtifactBytes <= limits.maximumTransitionBytes &&
                   limits.maximumTransitionBytes <= limits.maximumResidentBytes &&
                   limits.maximumChunksPerDestructible <= limits.maximumWorkItemsPerTransition;
        }

        [[nodiscard]] constexpr bool Requires(const DestructibleDescriptorData &data, const DestructionFeature feature) noexcept {
            return data.features.required.Contains(feature);
        }

        [[nodiscard]] constexpr bool HasKnownPolicies(const DestructibleDescriptorData &data) noexcept {
            return IsKnown(data.behavior.trigger, DestructionTriggerPolicy::Count) &&
                   IsKnown(data.behavior.support, DestructionSupportPolicy::Count) &&
                   IsKnown(data.behavior.repair, DestructionRepairPolicy::Count) &&
                   IsKnown(data.cleanup.chunkRetention, DestructionChunkRetention::Count) &&
                   IsKnown(data.cleanup.debris, DestructionDebrisPolicy::Count) &&
                   IsKnown(data.replication, DestructionReplicationIntent::Count);
        }

        [[nodiscard]] constexpr bool HasCoherentFeaturePolicies(const DestructibleDescriptorData &data) noexcept {
            const bool validSupport =
                data.behavior.support == DestructionSupportPolicy::Disabled || Requires(data, DestructionFeature::CookedSupport);
            const bool validHierarchy = data.behavior.support != DestructionSupportPolicy::CookedHierarchy ||
                                        Requires(data, DestructionFeature::HierarchicalFracture);
            const bool validRetention = data.cleanup.chunkRetention != DestructionChunkRetention::DurableDormancy ||
                                        Requires(data, DestructionFeature::DurableDormancy);
            const bool validDebris =
                data.cleanup.debris != DestructionDebrisPolicy::FiniteLifetime || Requires(data, DestructionFeature::CosmeticDebris);
            const bool validReplication = data.replication != DestructionReplicationIntent::ServerAuthoritative ||
                                          Requires(data, DestructionFeature::AuthoritativeReplication);
            return validSupport && validHierarchy && validRetention && validDebris && validReplication;
        }

        [[nodiscard]] Result<void> ValidateFeaturePolicy(const DestructibleDescriptorData &data, const DestructionTierProfile &profile) {
            if (!data.features.required.IsValid() || !data.features.optional.IsValid() ||
                (data.features.required.bits & data.features.optional.bits) != 0)
                return Result<void>::Failure(MakeError(DestructionErrors::DescriptorInvalid));
            if (((data.features.required.bits | data.features.optional.bits) & RuntimeGeometry) != 0)
                return Result<void>::Failure(MakeError(DestructionErrors::RuntimeGeometryUnsupported));
            if (!Requires(data, DestructionFeature::PreCookedFracture))
                return Result<void>::Failure(MakeError(DestructionErrors::DescriptorInvalid));
            if ((data.features.required.bits & ~profile.supportedFeatures.bits) != 0)
                return Result<void>::Failure(MakeError(DestructionErrors::FeatureUnsatisfied));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidatePolicies(const DestructibleDescriptorData &data) {
            if (!HasKnownPolicies(data))
                return Result<void>::Failure(MakeError(DestructionErrors::DescriptorInvalid));

            if (!HasCoherentFeaturePolicies(data))
                return Result<void>::Failure(MakeError(DestructionErrors::DescriptorInvalid));

            const bool finiteLifetime = std::isfinite(data.cleanup.debrisLifetimeSeconds);
            if (!finiteLifetime || (data.cleanup.debris == DestructionDebrisPolicy::Disabled ? data.cleanup.debrisLifetimeSeconds != 0.0F
                                                                                             : data.cleanup.debrisLifetimeSeconds <= 0.0F))
                return Result<void>::Failure(MakeError(DestructionErrors::DescriptorInvalid));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateFootprint(const DestructionArtifactFootprint &footprint, const DestructionLimits &limits) {
            if (footprint.chunkCount == 0 || footprint.hierarchyDepth == 0 || footprint.artifactBytes == 0 ||
                footprint.peakTransitionBytes == 0 || footprint.peakResidentBytes == 0 || footprint.peakWorkItemsPerTransition == 0 ||
                footprint.peakActiveChunkBodies > footprint.chunkCount ||
                footprint.peakEventsPerTransition > footprint.requestedEventJournalEntries ||
                footprint.artifactBytes > footprint.peakTransitionBytes || footprint.peakTransitionBytes > footprint.peakResidentBytes)
                return Result<void>::Failure(MakeError(DestructionErrors::DescriptorInvalid));

            const DestructionLimits actual{footprint.chunkCount,
                                           footprint.hierarchyDepth,
                                           footprint.peakActiveChunkBodies,
                                           footprint.peakEventsPerTransition,
                                           footprint.requestedEventJournalEntries,
                                           footprint.peakCosmeticDebrisParticles,
                                           footprint.artifactBytes,
                                           footprint.peakTransitionBytes,
                                           footprint.peakResidentBytes,
                                           footprint.peakWorkItemsPerTransition};
            if (!FitsWithin(actual, limits))
                return Result<void>::Failure(MakeError(DestructionErrors::LimitExceeded));
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc GetDestructionTierProfile */
    Result<DestructionTierProfile> GetDestructionTierProfile(const DestructionFeatureTier tier) {
        switch (tier) {
            case DestructionFeatureTier::Baseline:
                return Result<DestructionTierProfile>::Success({tier, BaselineFeatures, BaselineLimits});
            case DestructionFeatureTier::Standard:
                return Result<DestructionTierProfile>::Success({tier, StandardFeatures, StandardLimits});
            case DestructionFeatureTier::High:
                return Result<DestructionTierProfile>::Success({tier, HighFeatures, HighLimits});
            case DestructionFeatureTier::Count:
                break;
        }
        return Result<DestructionTierProfile>::Failure(MakeError(DestructionErrors::TierInvalid));
    }

    /** @copydoc DestructibleDescriptor::Create */
    Result<DestructibleDescriptor> DestructibleDescriptor::Create(const DestructibleDescriptorData &data) {
        if (data.contractVersion != CurrentDestructibleDescriptorContractVersion || !data.destructible.IsValid() ||
            !data.content.IsValid() || !data.configurationRevision.IsValid())
            return Result<DestructibleDescriptor>::Failure(MakeError(DestructionErrors::DescriptorInvalid));

        auto profile = GetDestructionTierProfile(data.tier);
        if (profile.HasError())
            return Result<DestructibleDescriptor>::Failure(profile.ErrorValue());
        if (auto features = ValidateFeaturePolicy(data, profile.Value()); features.HasError())
            return Result<DestructibleDescriptor>::Failure(features.ErrorValue());
        if (!IsPositive(data.limits) || !IsInternallyConsistent(data.limits) || !FitsWithin(data.limits, profile.Value().limits))
            return Result<DestructibleDescriptor>::Failure(MakeError(DestructionErrors::LimitProfileInvalid));
        if (!std::isfinite(data.health.maximumHealth) || !std::isfinite(data.health.damagedHealthThreshold) ||
            !std::isfinite(data.health.fractureHealthThreshold) || data.health.maximumHealth <= 0.0F ||
            data.health.fractureHealthThreshold < 0.0F || data.health.fractureHealthThreshold >= data.health.damagedHealthThreshold ||
            data.health.damagedHealthThreshold >= data.health.maximumHealth)
            return Result<DestructibleDescriptor>::Failure(MakeError(DestructionErrors::DescriptorInvalid));
        if (auto policies = ValidatePolicies(data); policies.HasError())
            return Result<DestructibleDescriptor>::Failure(policies.ErrorValue());

        const DestructionFeatureSet effective{data.features.required.bits |
                                              (data.features.optional.bits & profile.Value().supportedFeatures.bits)};
        return Result<DestructibleDescriptor>::Success(DestructibleDescriptor{data, effective});
    }

    /** @copydoc DestructibleDescriptor::Data */
    const DestructibleDescriptorData &DestructibleDescriptor::Data() const noexcept {
        return data_;
    }

    /** @copydoc DestructibleDescriptor::EffectiveFeatures */
    DestructionFeatureSet DestructibleDescriptor::EffectiveFeatures() const noexcept {
        return effectiveFeatures_;
    }

    DestructibleDescriptor::DestructibleDescriptor(const DestructibleDescriptorData &data,
                                                   const DestructionFeatureSet effectiveFeatures) noexcept
        : data_(data), effectiveFeatures_(effectiveFeatures) {}

    /** @copydoc AdmitDestructibleDescriptor */
    Result<void> AdmitDestructibleDescriptor(const DestructibleDescriptor &descriptor, const DestructionHandle submittedTarget,
                                             const DestructionHandle currentTarget,
                                             const DestructionConfigurationRevision currentConfigurationRevision,
                                             const FractureArtifactContentIdentity &currentContent,
                                             const DestructionArtifactFootprint &footprint) {
        if (auto target = ValidateDestructionHandleAccess(submittedTarget, currentTarget); target.HasError())
            return target;
        const auto &data = descriptor.Data();
        if (submittedTarget.destructible != data.destructible)
            return Result<void>::Failure(MakeError(DestructionErrors::IdentityUnknown));
        if (!currentConfigurationRevision.IsValid())
            return Result<void>::Failure(MakeError(DestructionErrors::IdentityInvalid));
        if (data.configurationRevision != currentConfigurationRevision)
            return Result<void>::Failure(MakeError(DestructionErrors::StaleConfiguration));
        if (auto content = ValidateFractureContentAccess(data.content, currentContent); content.HasError())
            return content;
        if (descriptor.Data().cleanup.debris == DestructionDebrisPolicy::Disabled && footprint.peakCosmeticDebrisParticles != 0)
            return Result<void>::Failure(MakeError(DestructionErrors::DescriptorInvalid));
        return ValidateFootprint(footprint, data.limits);
    }
}  // namespace Horo::Destruction
