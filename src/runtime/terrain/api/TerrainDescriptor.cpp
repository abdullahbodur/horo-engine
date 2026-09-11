#include "Horo/Terrain/TerrainDescriptor.h"

#include <algorithm>
#include <array>
#include <bit>
#include <ranges>

namespace Horo::Terrain {
    namespace {
        constexpr TerrainDescriptorLimits BaselineLimits{4'097,
                                                         128,
                                                         4,
                                                         4,
                                                         256,
                                                         1'024,
                                                         262'144,
                                                         256ULL * 1024 * 1024,
                                                         256ULL * 1024 * 1024,
                                                         128ULL * 1024 * 1024,
                                                         128ULL * 1024 * 1024,
                                                         1'048'576};
        constexpr TerrainDescriptorLimits StandardLimits{8'193,
                                                         128,
                                                         6,
                                                         8,
                                                         512,
                                                         2'048,
                                                         524'288,
                                                         512ULL * 1024 * 1024,
                                                         512ULL * 1024 * 1024,
                                                         256ULL * 1024 * 1024,
                                                         256ULL * 1024 * 1024,
                                                         2'097'152};
        constexpr TerrainDescriptorLimits HighLimits{16'385,
                                                     256,
                                                     8,
                                                     12,
                                                     1'024,
                                                     4'096,
                                                     1'048'576,
                                                     1024ULL * 1024 * 1024,
                                                     1024ULL * 1024 * 1024,
                                                     512ULL * 1024 * 1024,
                                                     512ULL * 1024 * 1024,
                                                     4'194'304};
        constexpr TerrainDescriptorLimits UltraLimits{TerrainDescriptorHardLimits::SamplesPerAxis,
                                                      TerrainDescriptorHardLimits::TileInteriorQuads,
                                                      TerrainDescriptorHardLimits::LodLevels,
                                                      TerrainDescriptorHardLimits::LayersPerTile,
                                                      TerrainDescriptorHardLimits::ActiveTerrainTiles,
                                                      TerrainDescriptorHardLimits::ActiveFoliageClusters,
                                                      TerrainDescriptorHardLimits::ActiveFoliageInstances,
                                                      TerrainDescriptorHardLimits::ResidentTerrainBytes,
                                                      TerrainDescriptorHardLimits::ResidentFoliageBytes,
                                                      TerrainDescriptorHardLimits::StagingBytes,
                                                      TerrainDescriptorHardLimits::RetiringBytes,
                                                      TerrainDescriptorHardLimits::WorkItems};

        [[nodiscard]] constexpr std::array<std::uint64_t, 12> LimitValues(const TerrainDescriptorLimits &limits) noexcept {
            return {limits.maximumSamplesPerAxis,         limits.maximumTileInteriorQuads,    limits.maximumLodLevels,
                    limits.maximumLayersPerTile,          limits.maximumActiveTerrainTiles,   limits.maximumActiveFoliageClusters,
                    limits.maximumActiveFoliageInstances, limits.maximumResidentTerrainBytes, limits.maximumResidentFoliageBytes,
                    limits.maximumStagingBytes,           limits.maximumRetiringBytes,        limits.maximumWorkItems};
        }

        [[nodiscard]] constexpr bool RequiredLimitsArePositive(const TerrainDescriptorLimits &limits) noexcept {
            return limits.maximumSamplesPerAxis > 0 && limits.maximumTileInteriorQuads > 0 && limits.maximumLodLevels > 0 &&
                   limits.maximumLayersPerTile > 0 && limits.maximumActiveTerrainTiles > 0 && limits.maximumResidentTerrainBytes > 0 &&
                   limits.maximumStagingBytes > 0 && limits.maximumRetiringBytes > 0 && limits.maximumWorkItems > 0;
        }

        [[nodiscard]] constexpr bool FoliageLimitsAreConsistent(const TerrainDescriptorLimits &limits) noexcept {
            const bool foliageDisabled = limits.maximumActiveFoliageClusters == 0 && limits.maximumActiveFoliageInstances == 0 &&
                                         limits.maximumResidentFoliageBytes == 0;
            const bool foliageEnabled = limits.maximumActiveFoliageClusters > 0 && limits.maximumActiveFoliageInstances > 0 &&
                                        limits.maximumResidentFoliageBytes > 0 &&
                                        limits.maximumActiveFoliageClusters <= limits.maximumActiveFoliageInstances;
            return foliageDisabled || foliageEnabled;
        }

        [[nodiscard]] constexpr bool FitsWithin(const TerrainDescriptorLimits &value, const TerrainDescriptorLimits &ceiling) noexcept {
            return std::ranges::equal(LimitValues(value), LimitValues(ceiling),
                                      [](const std::uint64_t actual, const std::uint64_t maximum) {
                return actual <= maximum;
            });
        }

        [[nodiscard]] constexpr bool LimitsAreInternallyConsistent(const TerrainDescriptorLimits &limits) noexcept {
            return std::has_single_bit(limits.maximumTileInteriorQuads) && limits.maximumTileInteriorQuads < limits.maximumSamplesPerAxis &&
                   FoliageLimitsAreConsistent(limits) && limits.maximumActiveTerrainTiles <= limits.maximumWorkItems &&
                   limits.maximumActiveFoliageInstances <= limits.maximumWorkItems;
        }

        [[nodiscard]] bool BoundsAreValid(const TerrainRevisionedBounds &bounds) noexcept {
            const auto minimum = bounds.minimum.Millimeters();
            const auto maximum = bounds.maximum.Millimeters();
            return bounds.revision.IsValid() && minimum[0] < maximum[0] && minimum[1] <= maximum[1] && minimum[2] < maximum[2];
        }

        [[nodiscard]] constexpr bool GridIsValid(const TerrainGridShape &grid, const TerrainDescriptorLimits &limits) noexcept {
            return grid.samplesX >= 2 && grid.samplesZ >= 2 && grid.samplesX <= limits.maximumSamplesPerAxis &&
                   grid.samplesZ <= limits.maximumSamplesPerAxis && grid.tileInteriorQuads > 0 &&
                   std::has_single_bit(grid.tileInteriorQuads) && grid.tileInteriorQuads <= limits.maximumTileInteriorQuads &&
                   (grid.samplesX - 1U) % grid.tileInteriorQuads == 0 && (grid.samplesZ - 1U) % grid.tileInteriorQuads == 0 &&
                   grid.lodLevels > 0 && grid.lodLevels <= limits.maximumLodLevels && grid.layersPerTile > 0 &&
                   grid.layersPerTile <= limits.maximumLayersPerTile;
        }

        [[nodiscard]] constexpr bool FootprintIsPopulated(const TerrainDescriptorFootprint &footprint) noexcept {
            return footprint.activeTerrainTiles > 0 && footprint.residentTerrainBytes > 0 && footprint.stagingBytes > 0 &&
                   footprint.retiringBytes > 0 && footprint.workItems > 0;
        }

        [[nodiscard]] constexpr bool FoliageFootprintIsConsistent(const TerrainDescriptorFootprint &footprint) noexcept {
            const bool foliageAbsent =
                footprint.activeFoliageClusters == 0 && footprint.activeFoliageInstances == 0 && footprint.residentFoliageBytes == 0;
            const bool foliagePresent = footprint.activeFoliageClusters > 0 && footprint.activeFoliageInstances > 0 &&
                                        footprint.residentFoliageBytes > 0 &&
                                        footprint.activeFoliageClusters <= footprint.activeFoliageInstances;
            return foliageAbsent || foliagePresent;
        }

        [[nodiscard]] constexpr TerrainDescriptorLimits FootprintLimits(const TerrainGridShape &grid,
                                                                        const TerrainDescriptorFootprint &footprint) noexcept {
            return {std::max(grid.samplesX, grid.samplesZ),
                    grid.tileInteriorQuads,
                    grid.lodLevels,
                    grid.layersPerTile,
                    footprint.activeTerrainTiles,
                    footprint.activeFoliageClusters,
                    footprint.activeFoliageInstances,
                    footprint.residentTerrainBytes,
                    footprint.residentFoliageBytes,
                    footprint.stagingBytes,
                    footprint.retiringBytes,
                    footprint.workItems};
        }

        [[nodiscard]] Result<void> ValidateDatasetFacts(const TerrainDatasetDescriptorData &data, const TerrainDescriptorLimits &limits) {
            if (data.contractVersion != CurrentTerrainDescriptorContractVersion || !data.dataset.IsValid() || !data.content.IsValid() ||
                !BoundsAreValid(data.bounds) || !GridIsValid(data.grid, limits) || !FootprintIsPopulated(data.footprint) ||
                !FoliageFootprintIsConsistent(data.footprint))
                return Result<void>::Failure(MakeError(TerrainErrors::DescriptorInvalid));
            if (!FitsWithin(FootprintLimits(data.grid, data.footprint), limits))
                return Result<void>::Failure(MakeError(TerrainErrors::LimitExceeded));
            return Result<void>::Success();
        }

        [[nodiscard]] bool HasCurrentState(const TerrainDescriptorAdmissionContext &context) noexcept {
            return context.currentDataset.has_value() && context.currentContent.has_value() && context.currentBounds.has_value();
        }

        [[nodiscard]] Result<void> ValidateContext(const TerrainConfigurationSnapshotData &configuration,
                                                   const TerrainDescriptorAdmissionContext &context) {
            if (!context.configuration.IsValid() || !context.capability.IsValid() || !context.supportedTiers.IsValid())
                return Result<void>::Failure(MakeError(TerrainErrors::DescriptorInvalid));
            if (context.lifecycle != TerrainRuntimeLifecycle::Active)
                return Result<void>::Failure(MakeError(TerrainErrors::LifecycleUnavailable));
            if (configuration.configuration != context.configuration || configuration.capability != context.capability)
                return Result<void>::Failure(MakeError(TerrainErrors::RevisionStale));
            if (auto resolved = ResolveTerrainFeatureTier(configuration.tier, context.supportedTiers); resolved.HasError())
                return Result<void>::Failure(resolved.ErrorValue());
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateInsert(const TerrainDescriptorAdmissionRequest &request,
                                                  const TerrainDescriptorAdmissionContext &context) {
            if (context.currentDataset.has_value() || context.currentContent.has_value() || context.currentBounds.has_value() ||
                request.expectedCurrentContent.has_value())
                return Result<void>::Failure(MakeError(TerrainErrors::ReplacementInvalid));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateReplacement(const TerrainDatasetDescriptorData &candidate,
                                                       const TerrainDescriptorAdmissionRequest &request,
                                                       const TerrainDescriptorAdmissionContext &context) {
            if (!HasCurrentState(context) || !request.expectedCurrentContent.has_value())
                return Result<void>::Failure(MakeError(TerrainErrors::ReplacementInvalid));
            if (!context.currentDataset->IsValid() || !context.currentContent->IsValid() || !BoundsAreValid(*context.currentBounds))
                return Result<void>::Failure(MakeError(TerrainErrors::DescriptorInvalid));
            if (*context.currentDataset != candidate.dataset)
                return Result<void>::Failure(MakeError(TerrainErrors::IdentityUnknown));
            if (*request.expectedCurrentContent != *context.currentContent)
                return Result<void>::Failure(MakeError(TerrainErrors::RevisionStale));
            auto next = AdvanceTerrainRevision(*context.currentContent);
            if (next.HasError())
                return Result<void>::Failure(next.ErrorValue());
            if (candidate.content != next.Value())
                return Result<void>::Failure(MakeError(TerrainErrors::RevisionStale));
            const TerrainRevisionedBounds &currentBounds = *context.currentBounds;
            const bool boundsChanged =
                candidate.bounds.minimum != currentBounds.minimum || candidate.bounds.maximum != currentBounds.maximum;
            if (!boundsChanged && candidate.bounds.revision != currentBounds.revision)
                return Result<void>::Failure(MakeError(TerrainErrors::RevisionStale));
            if (boundsChanged) {
                auto nextBounds = AdvanceTerrainRevision(currentBounds.revision);
                if (nextBounds.HasError())
                    return Result<void>::Failure(nextBounds.ErrorValue());
                if (candidate.bounds.revision != nextBounds.Value())
                    return Result<void>::Failure(MakeError(TerrainErrors::RevisionStale));
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc GetTerrainTierProfile */
    Result<TerrainTierProfile> GetTerrainTierProfile(const TerrainFeatureTier tier) {
        using enum TerrainFeatureTier;
        switch (tier) {
            case Baseline:
                return Result<TerrainTierProfile>::Success({tier, CurrentTerrainTierProfileRevision, BaselineLimits});
            case Standard:
                return Result<TerrainTierProfile>::Success({tier, CurrentTerrainTierProfileRevision, StandardLimits});
            case High:
                return Result<TerrainTierProfile>::Success({tier, CurrentTerrainTierProfileRevision, HighLimits});
            case Ultra:
                return Result<TerrainTierProfile>::Success({tier, CurrentTerrainTierProfileRevision, UltraLimits});
            case Count:
                break;
        }
        return Result<TerrainTierProfile>::Failure(MakeError(TerrainErrors::TierInvalid));
    }

    /** @copydoc ResolveTerrainFeatureTier */
    Result<TerrainFeatureTier> ResolveTerrainFeatureTier(const TerrainFeatureTier requested, const TerrainFeatureTierSet supported) {
        if (GetTerrainTierProfile(requested).HasError() || !supported.IsValid())
            return Result<TerrainFeatureTier>::Failure(MakeError(TerrainErrors::TierInvalid));
        if (!supported.Contains(requested))
            return Result<TerrainFeatureTier>::Failure(MakeError(TerrainErrors::TierUnsupported));
        return Result<TerrainFeatureTier>::Success(requested);
    }

    /** @copydoc TerrainConfigurationSnapshot::Create */
    Result<TerrainConfigurationSnapshot> TerrainConfigurationSnapshot::Create(const TerrainConfigurationSnapshotData &data) {
        if (data.contractVersion != CurrentTerrainDescriptorContractVersion ||
            data.tierProfileRevision != CurrentTerrainTierProfileRevision || !data.configuration.IsValid() || !data.capability.IsValid())
            return Result<TerrainConfigurationSnapshot>::Failure(MakeError(TerrainErrors::DescriptorInvalid));
        auto profile = GetTerrainTierProfile(data.tier);
        if (profile.HasError())
            return Result<TerrainConfigurationSnapshot>::Failure(profile.ErrorValue());
        if (!RequiredLimitsArePositive(data.limits) || !LimitsAreInternallyConsistent(data.limits) ||
            !FitsWithin(data.limits, profile.Value().limits))
            return Result<TerrainConfigurationSnapshot>::Failure(MakeError(TerrainErrors::LimitProfileInvalid));
        return Result<TerrainConfigurationSnapshot>::Success(TerrainConfigurationSnapshot{data});
    }

    /** @copydoc TerrainConfigurationSnapshot::Data */
    const TerrainConfigurationSnapshotData &TerrainConfigurationSnapshot::Data() const noexcept {
        return data_;
    }

    TerrainConfigurationSnapshot::TerrainConfigurationSnapshot(const TerrainConfigurationSnapshotData &data) noexcept : data_(data) {}

    /** @copydoc TerrainDatasetDescriptor::Create */
    Result<TerrainDatasetDescriptor> TerrainDatasetDescriptor::Create(const TerrainDatasetDescriptorData &data,
                                                                      const TerrainConfigurationSnapshot &configuration) {
        if (auto valid = ValidateDatasetFacts(data, configuration.Data().limits); valid.HasError())
            return Result<TerrainDatasetDescriptor>::Failure(valid.ErrorValue());
        return Result<TerrainDatasetDescriptor>::Success(TerrainDatasetDescriptor{data});
    }

    /** @copydoc TerrainDatasetDescriptor::Data */
    const TerrainDatasetDescriptorData &TerrainDatasetDescriptor::Data() const noexcept {
        return data_;
    }

    TerrainDatasetDescriptor::TerrainDatasetDescriptor(const TerrainDatasetDescriptorData &data) noexcept : data_(data) {}

    /** @copydoc ValidateTerrainDescriptorAdmission */
    Result<void> ValidateTerrainDescriptorAdmission(const TerrainDatasetDescriptor &descriptor,
                                                    const TerrainConfigurationSnapshot &configuration,
                                                    const TerrainDescriptorAdmissionRequest &request,
                                                    const TerrainDescriptorAdmissionContext &context) {
        if (auto validContext = ValidateContext(configuration.Data(), context); validContext.HasError())
            return validContext;
        using enum TerrainDescriptorAdmissionKind;
        switch (request.kind) {
            case Insert:
                return ValidateInsert(request, context);
            case Replace:
                return ValidateReplacement(descriptor.Data(), request, context);
            case Count:
                break;
        }
        return Result<void>::Failure(MakeError(TerrainErrors::DescriptorInvalid));
    }
}  // namespace Horo::Terrain
