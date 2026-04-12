#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "renderer/RenderBackend.h"
#include "renderer/RenderTypes.h"

namespace Monolith
{

  enum class SceneTextureSemantic : uint8_t
  {
    BaseColor = 0,
    Depth,
    Normal,
    RoughnessMetallic,
    Emissive,
    Velocity,
    Count,
    // Compatibility aliases for pre-GBuffer naming.
    Color = BaseColor,
    Normals = Normal,
    Material = RoughnessMetallic,
    MotionVector = Velocity,
  };

  enum class GiHistorySemantic : uint8_t
  {
    DiffuseIrradiance = 0,
    SpecularIrradiance,
    Validation,
    Moments,
    Count,
  };

  enum class GiHistoryResetReason : uint8_t
  {
    None = 0,
    ViewportResize,
    CameraCut,
    SceneBarrier,
    CameraJitterSequenceChanged,
    TemporalDisabled,
  };

  struct TemporalHistoryState
  {
    bool initialized = false;
    bool temporalEnabled = false;
    bool forceHistoryReset = false;
    bool cameraCut = false;
    bool sceneChanged = false;
    TemporalQualityTier qualityTier = TemporalQualityTier::Disabled;
    uint64_t sceneToken = 0;
    uint64_t cameraToken = 0;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t jitterSequenceSeed = 0;
    uint32_t jitterSequenceLength = 0;
  };

  inline TemporalHistoryState BuildTemporalHistoryState(const RenderFrameTemporalState &state)
  {
    return {
        true,
        state.enableTemporalReprojection,
        state.forceHistoryReset,
        state.cameraCut,
        state.sceneChanged,
        state.jitter.qualityTier,
        state.sceneToken,
        state.cameraToken,
        state.renderWidth,
        state.renderHeight,
        state.jitter.sequenceSeed,
        state.jitter.sequenceLength};
  }

  inline GiHistoryResetReason DetermineGiHistoryResetReason(const TemporalHistoryState &previous,
                                                            const TemporalHistoryState &current)
  {
    if (!previous.initialized)
      return GiHistoryResetReason::None;
    if (current.forceHistoryReset)
      return GiHistoryResetReason::SceneBarrier;
    if (!current.temporalEnabled)
      return GiHistoryResetReason::TemporalDisabled;
    if (!previous.temporalEnabled && current.temporalEnabled)
      return GiHistoryResetReason::SceneBarrier;
    if (previous.renderWidth != current.renderWidth || previous.renderHeight != current.renderHeight)
      return GiHistoryResetReason::ViewportResize;
    if (current.sceneChanged || previous.sceneToken != current.sceneToken)
      return GiHistoryResetReason::SceneBarrier;
    if (current.cameraCut || previous.cameraToken != current.cameraToken)
      return GiHistoryResetReason::CameraCut;
    if (previous.qualityTier != current.qualityTier ||
        previous.jitterSequenceSeed != current.jitterSequenceSeed ||
        previous.jitterSequenceLength != current.jitterSequenceLength)
    {
      return GiHistoryResetReason::CameraJitterSequenceChanged;
    }
    return GiHistoryResetReason::None;
  }

  struct BackendResourceHandle
  {
    RenderBackendId backendId = RenderBackendId::Auto;
    uint64_t resourceId = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t generation = 0;

    bool IsValid() const { return resourceId != 0; }
  };

  struct SceneTextureCatalog
  {
    std::array<BackendResourceHandle, static_cast<size_t>(SceneTextureSemantic::Count)> handles{};
    uint64_t frameSerial = 0;

    void Set(SceneTextureSemantic semantic, const BackendResourceHandle &handle)
    {
      handles[static_cast<size_t>(semantic)] = handle;
    }

    const BackendResourceHandle &Get(SceneTextureSemantic semantic) const
    {
      return handles[static_cast<size_t>(semantic)];
    }

    bool Has(SceneTextureSemantic semantic) const
    {
      return Get(semantic).IsValid();
    }

    bool HasDeferredGBuffer() const
    {
      return Has(SceneTextureSemantic::BaseColor) &&
             Has(SceneTextureSemantic::Depth) &&
             Has(SceneTextureSemantic::Normal) &&
             Has(SceneTextureSemantic::RoughnessMetallic) &&
             Has(SceneTextureSemantic::Emissive);
    }
  };

  struct DeferredGBufferCatalog
  {
    BackendResourceHandle baseColor{};
    BackendResourceHandle depth{};
    BackendResourceHandle normal{};
    BackendResourceHandle roughnessMetallic{};
    BackendResourceHandle emissive{};
    uint64_t frameSerial = 0;

    bool IsComplete() const
    {
      return baseColor.IsValid() &&
             depth.IsValid() &&
             normal.IsValid() &&
             roughnessMetallic.IsValid() &&
             emissive.IsValid();
    }
  };

  inline DeferredGBufferCatalog BuildDeferredGBufferCatalog(const SceneTextureCatalog &catalog)
  {
    return {
        catalog.Get(SceneTextureSemantic::BaseColor),
        catalog.Get(SceneTextureSemantic::Depth),
        catalog.Get(SceneTextureSemantic::Normal),
        catalog.Get(SceneTextureSemantic::RoughnessMetallic),
        catalog.Get(SceneTextureSemantic::Emissive),
        catalog.frameSerial};
  }

  struct GiHistoryCatalog
  {
    std::array<BackendResourceHandle, static_cast<size_t>(GiHistorySemantic::Count)> handles{};
    GiHistoryResetReason lastResetReason = GiHistoryResetReason::None;
    uint64_t revision = 0;
    TemporalHistoryState ownerState{};
    bool validForTemporalReuse = false;

    void Set(GiHistorySemantic semantic, const BackendResourceHandle &handle)
    {
      handles[static_cast<size_t>(semantic)] = handle;
    }

    const BackendResourceHandle &Get(GiHistorySemantic semantic) const
    {
      return handles[static_cast<size_t>(semantic)];
    }

    bool Has(GiHistorySemantic semantic) const
    {
      return Get(semantic).IsValid();
    }
  };

  enum class TemporalInputValidationStatus : uint8_t
  {
    Valid = 0,
    MissingDepth,
    MissingNormal,
    MissingMotionVectors,
    MissingHistory,
    BackendMismatch,
    DimensionMismatch,
  };

  inline const char *ToString(TemporalInputValidationStatus status)
  {
    switch (status)
    {
    case TemporalInputValidationStatus::Valid:
      return "valid";
    case TemporalInputValidationStatus::MissingDepth:
      return "missing depth input";
    case TemporalInputValidationStatus::MissingNormal:
      return "missing normal input";
    case TemporalInputValidationStatus::MissingMotionVectors:
      return "missing motion vector input";
    case TemporalInputValidationStatus::MissingHistory:
      return "missing temporal history input";
    case TemporalInputValidationStatus::BackendMismatch:
      return "temporal inputs come from mismatched backends";
    case TemporalInputValidationStatus::DimensionMismatch:
      return "temporal inputs have mismatched dimensions";
    }
    return "unknown temporal input validation status";
  }

  struct TemporalReprojectionInputContract
  {
    BackendResourceHandle depth{};
    BackendResourceHandle normal{};
    BackendResourceHandle motionVectors{};
    BackendResourceHandle historySurface{};
    uint64_t sceneFrameSerial = 0;
    uint64_t historyRevision = 0;
    GiHistoryResetReason historyResetReason = GiHistoryResetReason::None;
    bool historyValidForReuse = false;
    TemporalInputValidationStatus validationStatus = TemporalInputValidationStatus::MissingDepth;

    bool IsValid() const { return validationStatus == TemporalInputValidationStatus::Valid; }
  };

  inline TemporalReprojectionInputContract
  BuildTemporalReprojectionInputContract(const SceneTextureCatalog &sceneTextures,
                                         const GiHistoryCatalog &history)
  {
    TemporalReprojectionInputContract contract{};
    contract.sceneFrameSerial = sceneTextures.frameSerial;
    contract.historyRevision = history.revision;
    contract.historyResetReason = history.lastResetReason;
    contract.historyValidForReuse = history.validForTemporalReuse;
    contract.depth = sceneTextures.Get(SceneTextureSemantic::Depth);
    contract.normal = sceneTextures.Get(SceneTextureSemantic::Normal);
    contract.motionVectors = sceneTextures.Get(SceneTextureSemantic::MotionVector);
    contract.historySurface = history.Get(GiHistorySemantic::DiffuseIrradiance);

    if (!contract.depth.IsValid())
    {
      contract.validationStatus = TemporalInputValidationStatus::MissingDepth;
      return contract;
    }
    if (!contract.normal.IsValid())
    {
      contract.validationStatus = TemporalInputValidationStatus::MissingNormal;
      return contract;
    }
    if (!contract.motionVectors.IsValid())
    {
      contract.validationStatus = TemporalInputValidationStatus::MissingMotionVectors;
      return contract;
    }
    if (!contract.historySurface.IsValid())
    {
      contract.validationStatus = TemporalInputValidationStatus::MissingHistory;
      return contract;
    }
    if (contract.depth.backendId != contract.normal.backendId ||
        contract.depth.backendId != contract.motionVectors.backendId ||
        contract.depth.backendId != contract.historySurface.backendId)
    {
      contract.validationStatus = TemporalInputValidationStatus::BackendMismatch;
      return contract;
    }
    if (contract.depth.width != contract.normal.width ||
        contract.depth.height != contract.normal.height ||
        contract.depth.width != contract.motionVectors.width ||
        contract.depth.height != contract.motionVectors.height ||
        contract.depth.width != contract.historySurface.width ||
        contract.depth.height != contract.historySurface.height)
    {
      contract.validationStatus = TemporalInputValidationStatus::DimensionMismatch;
      return contract;
    }

    contract.validationStatus = TemporalInputValidationStatus::Valid;
    return contract;
  }

  enum class ScreenSpaceReflectionMissPolicy : uint8_t
  {
    ProbeFallback = 0,
    SkyFallback,
    NoFallback,
  };

  enum class SsrInputValidationStatus : uint8_t
  {
    DisabledBySettings = 0,
    Valid,
    MissingDepth,
    MissingNormal,
    MissingRoughnessMetallic,
    MissingMotionVectors,
    MissingSpecularHistory,
    BackendMismatch,
    DimensionMismatch,
  };

  inline const char *ToString(SsrInputValidationStatus status)
  {
    switch (status)
    {
    case SsrInputValidationStatus::DisabledBySettings:
      return "screen-space reflections disabled by settings";
    case SsrInputValidationStatus::Valid:
      return "valid";
    case SsrInputValidationStatus::MissingDepth:
      return "missing depth input";
    case SsrInputValidationStatus::MissingNormal:
      return "missing normal input";
    case SsrInputValidationStatus::MissingRoughnessMetallic:
      return "missing roughness-metallic input";
    case SsrInputValidationStatus::MissingMotionVectors:
      return "missing motion vector input";
    case SsrInputValidationStatus::MissingSpecularHistory:
      return "missing specular history input";
    case SsrInputValidationStatus::BackendMismatch:
      return "screen-space reflection inputs come from mismatched backends";
    case SsrInputValidationStatus::DimensionMismatch:
      return "screen-space reflection inputs have mismatched dimensions";
    }
    return "unknown screen-space reflection validation status";
  }

  enum class SsrExecutionStatus : uint8_t
  {
    Disabled = 0,
    MissingInputs,
    FallbackOnly,
    Tracing,
  };

  struct ScreenSpaceReflectionQualityConfig
  {
    TemporalQualityTier tier = TemporalQualityTier::Disabled;
    uint32_t maxTraceSteps = 0;
    uint32_t resolveStride = 1;
    uint32_t binaryRefinementSteps = 0;
    float maxRoughnessForTracing = 0.0f;
    float thickness = 0.0f;
    float missFallbackBlend = 1.0f;
    bool enableTemporalAccumulation = false;
  };

  inline ScreenSpaceReflectionQualityConfig BuildScreenSpaceReflectionQualityConfig(TemporalQualityTier tier)
  {
    ScreenSpaceReflectionQualityConfig config{};
    config.tier = tier;
    switch (tier)
    {
    case TemporalQualityTier::Disabled:
      config.maxTraceSteps = 0;
      config.resolveStride = 1;
      config.binaryRefinementSteps = 0;
      config.maxRoughnessForTracing = 0.0f;
      config.thickness = 0.0f;
      config.missFallbackBlend = 1.0f;
      config.enableTemporalAccumulation = false;
      break;
    case TemporalQualityTier::Low:
      config.maxTraceSteps = 12;
      config.resolveStride = 2;
      config.binaryRefinementSteps = 1;
      config.maxRoughnessForTracing = 0.35f;
      config.thickness = 0.20f;
      config.missFallbackBlend = 1.0f;
      config.enableTemporalAccumulation = false;
      break;
    case TemporalQualityTier::Medium:
      config.maxTraceSteps = 24;
      config.resolveStride = 2;
      config.binaryRefinementSteps = 2;
      config.maxRoughnessForTracing = 0.55f;
      config.thickness = 0.30f;
      config.missFallbackBlend = 0.65f;
      config.enableTemporalAccumulation = true;
      break;
    case TemporalQualityTier::High:
      config.maxTraceSteps = 40;
      config.resolveStride = 1;
      config.binaryRefinementSteps = 4;
      config.maxRoughnessForTracing = 0.75f;
      config.thickness = 0.40f;
      config.missFallbackBlend = 0.35f;
      config.enableTemporalAccumulation = true;
      break;
    case TemporalQualityTier::Ultra:
      config.maxTraceSteps = 64;
      config.resolveStride = 1;
      config.binaryRefinementSteps = 6;
      config.maxRoughnessForTracing = 0.90f;
      config.thickness = 0.50f;
      config.missFallbackBlend = 0.20f;
      config.enableTemporalAccumulation = true;
      break;
    }
    return config;
  }

  struct ScreenSpaceReflectionPassContract
  {
    ScreenSpaceReflectionQualityConfig quality{};
    ScreenSpaceReflectionMissPolicy missPolicy = ScreenSpaceReflectionMissPolicy::ProbeFallback;
    BackendResourceHandle depth{};
    BackendResourceHandle normal{};
    BackendResourceHandle roughnessMetallic{};
    BackendResourceHandle motionVectors{};
    BackendResourceHandle historySpecular{};
    uint64_t sceneFrameSerial = 0;
    uint64_t historyRevision = 0;
    GiHistoryResetReason historyResetReason = GiHistoryResetReason::None;
    bool historyValidForReuse = false;
    bool roughnessAwareTracing = false;
    SsrExecutionStatus executionStatus = SsrExecutionStatus::Disabled;
    SsrInputValidationStatus validationStatus = SsrInputValidationStatus::DisabledBySettings;

    bool IsValidForTracing() const
    {
      return validationStatus == SsrInputValidationStatus::Valid &&
             executionStatus == SsrExecutionStatus::Tracing;
    }
  };

  inline ScreenSpaceReflectionPassContract
  BuildScreenSpaceReflectionPassContract(const SceneTextureCatalog &sceneTextures,
                                         const GiHistoryCatalog &history,
                                         TemporalQualityTier qualityTier,
                                         bool enabled,
                                         ScreenSpaceReflectionMissPolicy missPolicy)
  {
    ScreenSpaceReflectionPassContract contract{};
    contract.quality = BuildScreenSpaceReflectionQualityConfig(qualityTier);
    contract.missPolicy = missPolicy;
    contract.sceneFrameSerial = sceneTextures.frameSerial;
    contract.historyRevision = history.revision;
    contract.historyResetReason = history.lastResetReason;
    contract.historyValidForReuse = history.validForTemporalReuse;

    if (!enabled || qualityTier == TemporalQualityTier::Disabled)
    {
      contract.executionStatus = SsrExecutionStatus::Disabled;
      contract.validationStatus = SsrInputValidationStatus::DisabledBySettings;
      return contract;
    }

    contract.depth = sceneTextures.Get(SceneTextureSemantic::Depth);
    contract.normal = sceneTextures.Get(SceneTextureSemantic::Normal);
    contract.roughnessMetallic = sceneTextures.Get(SceneTextureSemantic::RoughnessMetallic);
    contract.motionVectors = sceneTextures.Get(SceneTextureSemantic::MotionVector);
    contract.historySpecular = history.Get(GiHistorySemantic::SpecularIrradiance);
    contract.roughnessAwareTracing = contract.roughnessMetallic.IsValid();

    if (!contract.depth.IsValid())
    {
      contract.executionStatus = SsrExecutionStatus::MissingInputs;
      contract.validationStatus = SsrInputValidationStatus::MissingDepth;
      return contract;
    }
    if (!contract.normal.IsValid())
    {
      contract.executionStatus = SsrExecutionStatus::MissingInputs;
      contract.validationStatus = SsrInputValidationStatus::MissingNormal;
      return contract;
    }
    if (!contract.roughnessMetallic.IsValid())
    {
      contract.executionStatus = SsrExecutionStatus::MissingInputs;
      contract.validationStatus = SsrInputValidationStatus::MissingRoughnessMetallic;
      return contract;
    }
    if (!contract.motionVectors.IsValid())
    {
      contract.executionStatus = SsrExecutionStatus::MissingInputs;
      contract.validationStatus = SsrInputValidationStatus::MissingMotionVectors;
      return contract;
    }
    if (!contract.historySpecular.IsValid())
    {
      contract.executionStatus = SsrExecutionStatus::MissingInputs;
      contract.validationStatus = SsrInputValidationStatus::MissingSpecularHistory;
      return contract;
    }
    if (contract.depth.backendId != contract.normal.backendId ||
        contract.depth.backendId != contract.roughnessMetallic.backendId ||
        contract.depth.backendId != contract.motionVectors.backendId ||
        contract.depth.backendId != contract.historySpecular.backendId)
    {
      contract.executionStatus = SsrExecutionStatus::MissingInputs;
      contract.validationStatus = SsrInputValidationStatus::BackendMismatch;
      return contract;
    }
    if (contract.depth.width != contract.normal.width ||
        contract.depth.height != contract.normal.height ||
        contract.depth.width != contract.roughnessMetallic.width ||
        contract.depth.height != contract.roughnessMetallic.height ||
        contract.depth.width != contract.motionVectors.width ||
        contract.depth.height != contract.motionVectors.height ||
        contract.depth.width != contract.historySpecular.width ||
        contract.depth.height != contract.historySpecular.height)
    {
      contract.executionStatus = SsrExecutionStatus::MissingInputs;
      contract.validationStatus = SsrInputValidationStatus::DimensionMismatch;
      return contract;
    }

    contract.validationStatus = SsrInputValidationStatus::Valid;
    contract.executionStatus =
        (contract.quality.enableTemporalAccumulation && !contract.historyValidForReuse)
            ? SsrExecutionStatus::FallbackOnly
            : SsrExecutionStatus::Tracing;
    return contract;
  }

  enum class SsgiInputValidationStatus : uint8_t
  {
    DisabledBySettings = 0,
    Valid,
    MissingDepth,
    MissingNormal,
    MissingBaseColor,
    MissingEmissive,
    MissingMotionVectors,
    MissingDiffuseHistory,
    BackendMismatch,
    DimensionMismatch,
  };

  inline const char *ToString(SsgiInputValidationStatus status)
  {
    switch (status)
    {
    case SsgiInputValidationStatus::DisabledBySettings:
      return "screen-space global illumination disabled by settings";
    case SsgiInputValidationStatus::Valid:
      return "valid";
    case SsgiInputValidationStatus::MissingDepth:
      return "missing depth input";
    case SsgiInputValidationStatus::MissingNormal:
      return "missing normal input";
    case SsgiInputValidationStatus::MissingBaseColor:
      return "missing base color input";
    case SsgiInputValidationStatus::MissingEmissive:
      return "missing emissive input";
    case SsgiInputValidationStatus::MissingMotionVectors:
      return "missing motion vector input";
    case SsgiInputValidationStatus::MissingDiffuseHistory:
      return "missing diffuse irradiance history input";
    case SsgiInputValidationStatus::BackendMismatch:
      return "screen-space global illumination inputs come from mismatched backends";
    case SsgiInputValidationStatus::DimensionMismatch:
      return "screen-space global illumination inputs have mismatched dimensions";
    }
    return "unknown screen-space global illumination validation status";
  }

  enum class SsgiExecutionStatus : uint8_t
  {
    Disabled = 0,
    MissingInputs,
    FallbackOnly,
    Tracing,
  };

  struct ScreenSpaceGlobalIlluminationQualityConfig
  {
    TemporalQualityTier tier = TemporalQualityTier::Disabled;
    uint32_t maxTraceSteps = 0;
    uint32_t sampleKernelSize = 0;
    uint32_t resolveStride = 1;
    float thickness = 0.0f;
    float temporalHistoryBlend = 0.0f;
    float emissiveContributionScale = 0.0f;
    bool enableTemporalAccumulation = false;
    bool enableEmissiveContribution = false;
  };

  inline ScreenSpaceGlobalIlluminationQualityConfig
  BuildScreenSpaceGlobalIlluminationQualityConfig(TemporalQualityTier tier)
  {
    ScreenSpaceGlobalIlluminationQualityConfig config{};
    config.tier = tier;
    switch (tier)
    {
    case TemporalQualityTier::Disabled:
      config.maxTraceSteps = 0;
      config.sampleKernelSize = 0;
      config.resolveStride = 1;
      config.thickness = 0.0f;
      config.temporalHistoryBlend = 0.0f;
      config.emissiveContributionScale = 0.0f;
      config.enableTemporalAccumulation = false;
      config.enableEmissiveContribution = false;
      break;
    case TemporalQualityTier::Low:
      config.maxTraceSteps = 8;
      config.sampleKernelSize = 4;
      config.resolveStride = 2;
      config.thickness = 0.15f;
      config.temporalHistoryBlend = 0.0f;
      config.emissiveContributionScale = 0.40f;
      config.enableTemporalAccumulation = false;
      config.enableEmissiveContribution = true;
      break;
    case TemporalQualityTier::Medium:
      config.maxTraceSteps = 16;
      config.sampleKernelSize = 8;
      config.resolveStride = 2;
      config.thickness = 0.25f;
      config.temporalHistoryBlend = 0.55f;
      config.emissiveContributionScale = 0.55f;
      config.enableTemporalAccumulation = true;
      config.enableEmissiveContribution = true;
      break;
    case TemporalQualityTier::High:
      config.maxTraceSteps = 28;
      config.sampleKernelSize = 12;
      config.resolveStride = 1;
      config.thickness = 0.35f;
      config.temporalHistoryBlend = 0.75f;
      config.emissiveContributionScale = 0.70f;
      config.enableTemporalAccumulation = true;
      config.enableEmissiveContribution = true;
      break;
    case TemporalQualityTier::Ultra:
      config.maxTraceSteps = 40;
      config.sampleKernelSize = 16;
      config.resolveStride = 1;
      config.thickness = 0.45f;
      config.temporalHistoryBlend = 0.85f;
      config.emissiveContributionScale = 0.85f;
      config.enableTemporalAccumulation = true;
      config.enableEmissiveContribution = true;
      break;
    }
    return config;
  }

  struct ScreenSpaceGlobalIlluminationPassContract
  {
    ScreenSpaceGlobalIlluminationQualityConfig quality{};
    BackendResourceHandle depth{};
    BackendResourceHandle normal{};
    BackendResourceHandle baseColor{};
    BackendResourceHandle emissive{};
    BackendResourceHandle motionVectors{};
    BackendResourceHandle historyDiffuse{};
    uint64_t sceneFrameSerial = 0;
    uint64_t historyRevision = 0;
    GiHistoryResetReason historyResetReason = GiHistoryResetReason::None;
    bool historyValidForReuse = false;
    bool diffuseIndirectApproximationEnabled = false;
    bool emissiveContributionEnabled = false;
    SsgiExecutionStatus executionStatus = SsgiExecutionStatus::Disabled;
    SsgiInputValidationStatus validationStatus = SsgiInputValidationStatus::DisabledBySettings;

    bool IsValidForTracing() const
    {
      return validationStatus == SsgiInputValidationStatus::Valid &&
             executionStatus == SsgiExecutionStatus::Tracing;
    }
  };

  inline ScreenSpaceGlobalIlluminationPassContract
  BuildScreenSpaceGlobalIlluminationPassContract(const SceneTextureCatalog &sceneTextures,
                                                 const GiHistoryCatalog &history,
                                                 TemporalQualityTier qualityTier,
                                                 bool enabled)
  {
    ScreenSpaceGlobalIlluminationPassContract contract{};
    contract.quality = BuildScreenSpaceGlobalIlluminationQualityConfig(qualityTier);
    contract.sceneFrameSerial = sceneTextures.frameSerial;
    contract.historyRevision = history.revision;
    contract.historyResetReason = history.lastResetReason;
    contract.historyValidForReuse = history.validForTemporalReuse;
    contract.diffuseIndirectApproximationEnabled = enabled;
    contract.emissiveContributionEnabled = contract.quality.enableEmissiveContribution;

    if (!enabled || qualityTier == TemporalQualityTier::Disabled)
    {
      contract.executionStatus = SsgiExecutionStatus::Disabled;
      contract.validationStatus = SsgiInputValidationStatus::DisabledBySettings;
      return contract;
    }

    contract.depth = sceneTextures.Get(SceneTextureSemantic::Depth);
    contract.normal = sceneTextures.Get(SceneTextureSemantic::Normal);
    contract.baseColor = sceneTextures.Get(SceneTextureSemantic::BaseColor);
    contract.emissive = sceneTextures.Get(SceneTextureSemantic::Emissive);
    contract.motionVectors = sceneTextures.Get(SceneTextureSemantic::MotionVector);
    contract.historyDiffuse = history.Get(GiHistorySemantic::DiffuseIrradiance);

    if (!contract.depth.IsValid())
    {
      contract.executionStatus = SsgiExecutionStatus::MissingInputs;
      contract.validationStatus = SsgiInputValidationStatus::MissingDepth;
      return contract;
    }
    if (!contract.normal.IsValid())
    {
      contract.executionStatus = SsgiExecutionStatus::MissingInputs;
      contract.validationStatus = SsgiInputValidationStatus::MissingNormal;
      return contract;
    }
    if (!contract.baseColor.IsValid())
    {
      contract.executionStatus = SsgiExecutionStatus::MissingInputs;
      contract.validationStatus = SsgiInputValidationStatus::MissingBaseColor;
      return contract;
    }
    if (!contract.emissive.IsValid())
    {
      contract.executionStatus = SsgiExecutionStatus::MissingInputs;
      contract.validationStatus = SsgiInputValidationStatus::MissingEmissive;
      return contract;
    }
    if (!contract.motionVectors.IsValid())
    {
      contract.executionStatus = SsgiExecutionStatus::MissingInputs;
      contract.validationStatus = SsgiInputValidationStatus::MissingMotionVectors;
      return contract;
    }
    if (!contract.historyDiffuse.IsValid())
    {
      contract.executionStatus = SsgiExecutionStatus::MissingInputs;
      contract.validationStatus = SsgiInputValidationStatus::MissingDiffuseHistory;
      return contract;
    }
    if (contract.depth.backendId != contract.normal.backendId ||
        contract.depth.backendId != contract.baseColor.backendId ||
        contract.depth.backendId != contract.emissive.backendId ||
        contract.depth.backendId != contract.motionVectors.backendId ||
        contract.depth.backendId != contract.historyDiffuse.backendId)
    {
      contract.executionStatus = SsgiExecutionStatus::MissingInputs;
      contract.validationStatus = SsgiInputValidationStatus::BackendMismatch;
      return contract;
    }
    if (contract.depth.width != contract.normal.width ||
        contract.depth.height != contract.normal.height ||
        contract.depth.width != contract.baseColor.width ||
        contract.depth.height != contract.baseColor.height ||
        contract.depth.width != contract.emissive.width ||
        contract.depth.height != contract.emissive.height ||
        contract.depth.width != contract.motionVectors.width ||
        contract.depth.height != contract.motionVectors.height ||
        contract.depth.width != contract.historyDiffuse.width ||
        contract.depth.height != contract.historyDiffuse.height)
    {
      contract.executionStatus = SsgiExecutionStatus::MissingInputs;
      contract.validationStatus = SsgiInputValidationStatus::DimensionMismatch;
      return contract;
    }

    contract.validationStatus = SsgiInputValidationStatus::Valid;
    contract.executionStatus =
        (contract.quality.enableTemporalAccumulation && !contract.historyValidForReuse)
            ? SsgiExecutionStatus::FallbackOnly
            : SsgiExecutionStatus::Tracing;
    return contract;
  }

} // namespace Monolith
