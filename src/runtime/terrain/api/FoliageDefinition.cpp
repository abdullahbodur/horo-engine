#include "Horo/Terrain/FoliageDefinition.h"

#include <algorithm>
#include <limits>

namespace Horo::Terrain {
    namespace {
        constexpr std::uint32_t MaximumSlopeMilliDegrees = 90'000;
        constexpr std::uint32_t MaximumDensityPerSquareKilometer = 100'000'000;
        constexpr std::uint32_t MaximumScalePermille = 100'000;
        constexpr std::uint32_t ProbabilityPartsPerMillion = 1'000'000;
        constexpr std::uint16_t MaximumPermille = 1'000;

        [[nodiscard]] Result<void> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<void>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool ContainsDuplicateMesh(const FoliageVisualAssets &assets) noexcept {
            for (std::size_t index = 0; index < assets.meshLodCount; ++index) {
                for (std::size_t candidate = index + 1; candidate < assets.meshLodCount; ++candidate) {
                    if (assets.meshLods[index] == assets.meshLods[candidate])
                        return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool MeshSlotsAreCanonical(const FoliageVisualAssets &assets) noexcept {
            for (std::size_t index = 0; index < assets.meshLods.size(); ++index) {
                if (assets.meshLods[index].IsValid() != (index < assets.meshLodCount))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool VisualAssetsAreValid(const FoliageVisualAssets &assets) noexcept {
            if (assets.meshLodCount == 0 || assets.meshLodCount > assets.meshLods.size() || !assets.material.IsValid() ||
                !MeshSlotsAreCanonical(assets) || ContainsDuplicateMesh(assets))
                return false;
            return !assets.impostor.has_value() || assets.impostor->IsValid();
        }

        [[nodiscard]] bool PlacementIsValid(const FoliagePlacementDefinition &placement) noexcept {
            return placement.algorithmVersion == CurrentFoliagePlacementAlgorithmVersion &&
                   placement.algorithm < FoliagePlacementAlgorithm::Count && placement.alignment < FoliageSurfaceAlignment::Count &&
                   placement.densityPerSquareKilometer > 0 && placement.densityPerSquareKilometer <= MaximumDensityPerSquareKilometer &&
                   placement.minimumAltitudeMillimeters <= placement.maximumAltitudeMillimeters &&
                   placement.minimumSlopeMilliDegrees <= placement.maximumSlopeMilliDegrees &&
                   placement.maximumSlopeMilliDegrees <= MaximumSlopeMilliDegrees && placement.minimumSeparationMillimeters > 0 &&
                   placement.coordinateQuantumMillimeters > 0 &&
                   placement.coordinateQuantumMillimeters <= placement.minimumSeparationMillimeters;
        }

        [[nodiscard]] bool MeshDistancesAreValid(const FoliageVisualAssets &assets, const FoliageCullingDefinition &culling) noexcept {
            std::uint32_t previous{};
            std::uint32_t minimumGap = std::numeric_limits<std::uint32_t>::max();
            for (std::size_t index = 0; index < culling.meshLodDistanceMillimeters.size(); ++index) {
                if (index < assets.meshLodCount) {
                    if (culling.meshLodDistanceMillimeters[index] <= previous)
                        return false;
                    minimumGap = std::min(minimumGap, culling.meshLodDistanceMillimeters[index] - previous);
                    previous = culling.meshLodDistanceMillimeters[index];
                } else if (culling.meshLodDistanceMillimeters[index] != 0) {
                    return false;
                }
            }
            if (assets.impostor.has_value()) {
                if (culling.impostorStartDistanceMillimeters <= previous)
                    return false;
                minimumGap = std::min(minimumGap, culling.impostorStartDistanceMillimeters - previous);
                previous = culling.impostorStartDistanceMillimeters;
            } else if (culling.impostorStartDistanceMillimeters != 0) {
                return false;
            }
            if (culling.cullDistanceMillimeters <= previous)
                return false;
            minimumGap = std::min(minimumGap, culling.cullDistanceMillimeters - previous);
            return culling.crossFadeDistanceMillimeters <= minimumGap;
        }

        [[nodiscard]] bool WindIsDisabled(const FoliageWindDefinition &wind) noexcept {
            return wind.primaryStrengthPermille == 0 && wind.secondaryStrengthPermille == 0 && wind.primaryFrequencyMilliHertz == 0 &&
                   wind.secondaryFrequencyMilliHertz == 0 && wind.gustProbabilityPerMillion == 0 && wind.gustStrengthPermille == 0 &&
                   wind.branchFlexibilityPermille == 0 && wind.leafFlutterPermille == 0;
        }

        [[nodiscard]] bool WindIsValid(const FoliageWindDefinition &wind) noexcept {
            using enum FoliageWindModel;
            if (wind.model >= Count || wind.primaryStrengthPermille > MaximumPermille || wind.secondaryStrengthPermille > MaximumPermille ||
                wind.gustProbabilityPerMillion > ProbabilityPartsPerMillion || wind.gustStrengthPermille > MaximumPermille ||
                wind.branchFlexibilityPermille > MaximumPermille || wind.leafFlutterPermille > MaximumPermille)
                return false;
            if (wind.model == None)
                return WindIsDisabled(wind);
            if (wind.primaryStrengthPermille == 0 || wind.primaryFrequencyMilliHertz == 0 || wind.branchFlexibilityPermille == 0)
                return false;
            if ((wind.secondaryStrengthPermille == 0) != (wind.secondaryFrequencyMilliHertz == 0))
                return false;
            if ((wind.gustProbabilityPerMillion == 0) != (wind.gustStrengthPermille == 0))
                return false;
            return wind.model == VertexBend ? wind.leafFlutterPermille == 0 : wind.leafFlutterPermille > 0;
        }

        [[nodiscard]] bool CollisionIsValid(const FoliageCollisionDefinition &collision) noexcept {
            if (collision.shape >= FoliageCollisionShape::Count)
                return false;
            if (collision.shape == FoliageCollisionShape::None)
                return collision.radiusMillimeters == 0 && collision.heightMillimeters == 0 && !collision.blocksProjectiles &&
                       !collision.blocksNavigation;
            return collision.radiusMillimeters > 0 && collision.heightMillimeters >= collision.radiusMillimeters * 2ULL;
        }

        [[nodiscard]] Result<void> ValidateCapabilities(const FoliageTypeDefinitionData &data,
                                                        const FoliageDefinitionCapabilitySet capabilities) {
            using enum FoliageDefinitionCapability;
            if (!capabilities.IsValid())
                return Failure(TerrainErrors::FoliageFeatureUnsupported);
            if (const FoliageDefinitionCapability cullingCapability =
                    data.culling.recipe == FoliageCullingRecipe::CpuDirect ? CpuCulling : GpuIndirectCulling;
                !capabilities.Contains(cullingCapability) || (data.assets.impostor.has_value() && !capabilities.Contains(Impostors)) ||
                (data.wind.model != FoliageWindModel::None && !capabilities.Contains(VertexWind)) ||
                (data.collision.shape != FoliageCollisionShape::None && !capabilities.Contains(Collision)) ||
                (data.collision.blocksNavigation && !capabilities.Contains(NavigationBlocking)))
                return Failure(TerrainErrors::FoliageFeatureUnsupported);
            return Result<void>::Success();
        }

        [[nodiscard]] bool HasCurrentDefinition(const FoliageDefinitionAdmissionContext &context) noexcept {
            return context.currentType.has_value() && context.currentRevision.has_value();
        }

        [[nodiscard]] Result<void> ValidateReplacement(const FoliageTypeDefinitionData &candidate,
                                                       const FoliageDefinitionAdmissionRequest &request,
                                                       const FoliageDefinitionAdmissionContext &context) {
            if (!HasCurrentDefinition(context) || !request.expectedCurrentRevision.has_value())
                return Failure(TerrainErrors::ReplacementInvalid);
            if (!context.currentType->IsValid() || !context.currentRevision->IsValid() || !request.expectedCurrentRevision->IsValid())
                return Failure(TerrainErrors::FoliageDefinitionInvalid);
            if (*context.currentType != candidate.type)
                return Failure(TerrainErrors::IdentityUnknown);
            if (*request.expectedCurrentRevision != *context.currentRevision)
                return Failure(TerrainErrors::RevisionStale);
            const auto next = AdvanceTerrainRevision(*context.currentRevision);
            if (next.HasError())
                return Result<void>::Failure(next.ErrorValue());
            if (candidate.revision != next.Value())
                return Failure(TerrainErrors::RevisionStale);
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc FoliageTypeDefinition::Create */
    Result<FoliageTypeDefinition> FoliageTypeDefinition::Create(const FoliageTypeDefinitionData &data,
                                                                const TerrainConfigurationSnapshot &configuration,
                                                                const FoliageDefinitionCapabilitySet capabilities) {
        if (data.contractVersion != CurrentFoliageDefinitionContractVersion || !data.type.IsValid() || !data.revision.IsValid() ||
            !VisualAssetsAreValid(data.assets) || data.minimumScalePermille == 0 || data.minimumScalePermille > data.maximumScalePermille ||
            data.maximumScalePermille > MaximumScalePermille || data.maximumInstances == 0)
            return Result<FoliageTypeDefinition>::Failure(MakeError(TerrainErrors::FoliageDefinitionInvalid));
        if (!PlacementIsValid(data.placement))
            return Result<FoliageTypeDefinition>::Failure(MakeError(TerrainErrors::FoliagePlacementInvalid));
        if (data.culling.recipe >= FoliageCullingRecipe::Count || !MeshDistancesAreValid(data.assets, data.culling))
            return Result<FoliageTypeDefinition>::Failure(MakeError(TerrainErrors::FoliageCullingInvalid));
        if (!WindIsValid(data.wind))
            return Result<FoliageTypeDefinition>::Failure(MakeError(TerrainErrors::FoliageWindInvalid));
        if (!CollisionIsValid(data.collision))
            return Result<FoliageTypeDefinition>::Failure(MakeError(TerrainErrors::FoliageCollisionInvalid));
        if (data.maximumInstances > configuration.Data().limits.maximumActiveFoliageInstances)
            return Result<FoliageTypeDefinition>::Failure(MakeError(TerrainErrors::LimitExceeded));
        if (const auto capabilityResult = ValidateCapabilities(data, capabilities); capabilityResult.HasError())
            return Result<FoliageTypeDefinition>::Failure(capabilityResult.ErrorValue());
        return Result<FoliageTypeDefinition>::Success(FoliageTypeDefinition{data});
    }

    /** @copydoc FoliageTypeDefinition::Data */
    const FoliageTypeDefinitionData &FoliageTypeDefinition::Data() const noexcept {
        return data_;
    }

    FoliageTypeDefinition::FoliageTypeDefinition(const FoliageTypeDefinitionData &data) noexcept : data_(data) {}

    /** @copydoc ValidateFoliageDefinitionAdmission */
    Result<void> ValidateFoliageDefinitionAdmission(const FoliageTypeDefinition &candidate,
                                                    const TerrainConfigurationSnapshot &configuration,
                                                    const FoliageDefinitionAdmissionRequest &request,
                                                    const FoliageDefinitionAdmissionContext &context) {
        if (!context.configuration.IsValid() || !context.capability.IsValid() || !context.capabilities.IsValid())
            return Failure(TerrainErrors::FoliageDefinitionInvalid);
        if (context.lifecycle != TerrainRuntimeLifecycle::Active)
            return Failure(TerrainErrors::LifecycleUnavailable);
        if (configuration.Data().configuration != context.configuration || configuration.Data().capability != context.capability)
            return Failure(TerrainErrors::RevisionStale);
        if (const auto valid = FoliageTypeDefinition::Create(candidate.Data(), configuration, context.capabilities); valid.HasError())
            return Result<void>::Failure(valid.ErrorValue());

        using enum FoliageDefinitionAdmissionKind;
        switch (request.kind) {
            case Insert:
                if (context.currentType.has_value() || context.currentRevision.has_value() || request.expectedCurrentRevision.has_value())
                    return Failure(TerrainErrors::ReplacementInvalid);
                return Result<void>::Success();
            case Replace:
                return ValidateReplacement(candidate.Data(), request, context);
            case Count:
                break;
        }
        return Failure(TerrainErrors::FoliageDefinitionInvalid);
    }
}  // namespace Horo::Terrain
