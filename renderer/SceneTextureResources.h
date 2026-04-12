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

  enum class TemporalGiResolveValidationStatus : uint8_t
  {
    DisabledBySettings = 0,
    Valid,
    MissingSsrContractInputs,
    MissingSsgiContractInputs,
    MissingDiffuseHistory,
    MissingSpecularHistory,
    MissingValidationHistory,
    MissingMomentsHistory,
    BackendMismatch,
    DimensionMismatch,
  };

  inline const char *ToString(TemporalGiResolveValidationStatus status)
  {
    switch (status)
    {
    case TemporalGiResolveValidationStatus::DisabledBySettings:
      return "temporal GI resolve disabled by settings";
    case TemporalGiResolveValidationStatus::Valid:
      return "valid";
    case TemporalGiResolveValidationStatus::MissingSsrContractInputs:
      return "screen-space reflection contract is missing resolve inputs";
    case TemporalGiResolveValidationStatus::MissingSsgiContractInputs:
      return "screen-space global illumination contract is missing resolve inputs";
    case TemporalGiResolveValidationStatus::MissingDiffuseHistory:
      return "missing diffuse irradiance history input";
    case TemporalGiResolveValidationStatus::MissingSpecularHistory:
      return "missing specular irradiance history input";
    case TemporalGiResolveValidationStatus::MissingValidationHistory:
      return "missing temporal validation history input";
    case TemporalGiResolveValidationStatus::MissingMomentsHistory:
      return "missing temporal moments history input";
    case TemporalGiResolveValidationStatus::BackendMismatch:
      return "temporal GI resolve inputs come from mismatched backends";
    case TemporalGiResolveValidationStatus::DimensionMismatch:
      return "temporal GI resolve inputs have mismatched dimensions";
    }
    return "unknown temporal GI resolve validation status";
  }

  enum class TemporalGiResolveExecutionStatus : uint8_t
  {
    Disabled = 0,
    MissingInputs,
    ResetAndFallback,
    FallbackOnly,
    ResolveAndAccumulate,
  };

  enum class TemporalHistoryClampMode : uint8_t
  {
    None = 0,
    NeighborhoodMinMax,
  };

  enum class TemporalHistoryRejectionMode : uint8_t
  {
    None = 0,
    RejectOnMotionDisocclusion,
    RejectAllHistory,
  };

  struct TemporalGiResolvePassContract
  {
    ScreenSpaceReflectionPassContract ssr{};
    ScreenSpaceGlobalIlluminationPassContract ssgi{};
    BackendResourceHandle historyDiffuse{};
    BackendResourceHandle historySpecular{};
    BackendResourceHandle historyValidation{};
    BackendResourceHandle historyMoments{};
    uint64_t sceneFrameSerial = 0;
    uint64_t historyRevision = 0;
    GiHistoryResetReason historyResetReason = GiHistoryResetReason::None;
    bool historyValidForReuse = false;
    bool resetHistoryBeforeResolve = false;
    bool rejectHistoryThisFrame = false;
    bool clampHistoryThisFrame = false;
    TemporalHistoryClampMode clampMode = TemporalHistoryClampMode::None;
    TemporalHistoryRejectionMode rejectionMode = TemporalHistoryRejectionMode::None;
    bool resolveDiffuseEnabled = false;
    bool resolveSpecularEnabled = false;
    TemporalGiResolveExecutionStatus executionStatus = TemporalGiResolveExecutionStatus::Disabled;
    TemporalGiResolveValidationStatus validationStatus =
        TemporalGiResolveValidationStatus::DisabledBySettings;

    bool IsValidForResolve() const
    {
      return validationStatus == TemporalGiResolveValidationStatus::Valid &&
             executionStatus == TemporalGiResolveExecutionStatus::ResolveAndAccumulate;
    }
  };

  inline TemporalGiResolvePassContract BuildTemporalGiResolvePassContract(
      const ScreenSpaceReflectionPassContract &ssrContract,
      const ScreenSpaceGlobalIlluminationPassContract &ssgiContract,
      const GiHistoryCatalog &history)
  {
    TemporalGiResolvePassContract contract{};
    contract.ssr = ssrContract;
    contract.ssgi = ssgiContract;
    contract.sceneFrameSerial = std::max(ssrContract.sceneFrameSerial, ssgiContract.sceneFrameSerial);
    contract.historyRevision = history.revision;
    contract.historyResetReason = history.lastResetReason;
    contract.historyValidForReuse = history.validForTemporalReuse;
    contract.historyDiffuse = history.Get(GiHistorySemantic::DiffuseIrradiance);
    contract.historySpecular = history.Get(GiHistorySemantic::SpecularIrradiance);
    contract.historyValidation = history.Get(GiHistorySemantic::Validation);
    contract.historyMoments = history.Get(GiHistorySemantic::Moments);

    const bool temporalRequested =
        ssrContract.quality.enableTemporalAccumulation ||
        ssgiContract.quality.enableTemporalAccumulation;
    if (!temporalRequested)
    {
      contract.executionStatus = TemporalGiResolveExecutionStatus::Disabled;
      contract.validationStatus = TemporalGiResolveValidationStatus::DisabledBySettings;
      return contract;
    }

    if (ssrContract.validationStatus != SsrInputValidationStatus::Valid)
    {
      contract.executionStatus = TemporalGiResolveExecutionStatus::MissingInputs;
      contract.validationStatus = TemporalGiResolveValidationStatus::MissingSsrContractInputs;
      return contract;
    }
    if (ssgiContract.validationStatus != SsgiInputValidationStatus::Valid)
    {
      contract.executionStatus = TemporalGiResolveExecutionStatus::MissingInputs;
      contract.validationStatus = TemporalGiResolveValidationStatus::MissingSsgiContractInputs;
      return contract;
    }
    if (!contract.historyDiffuse.IsValid())
    {
      contract.executionStatus = TemporalGiResolveExecutionStatus::MissingInputs;
      contract.validationStatus = TemporalGiResolveValidationStatus::MissingDiffuseHistory;
      return contract;
    }
    if (!contract.historySpecular.IsValid())
    {
      contract.executionStatus = TemporalGiResolveExecutionStatus::MissingInputs;
      contract.validationStatus = TemporalGiResolveValidationStatus::MissingSpecularHistory;
      return contract;
    }
    if (!contract.historyValidation.IsValid())
    {
      contract.executionStatus = TemporalGiResolveExecutionStatus::MissingInputs;
      contract.validationStatus = TemporalGiResolveValidationStatus::MissingValidationHistory;
      return contract;
    }
    if (!contract.historyMoments.IsValid())
    {
      contract.executionStatus = TemporalGiResolveExecutionStatus::MissingInputs;
      contract.validationStatus = TemporalGiResolveValidationStatus::MissingMomentsHistory;
      return contract;
    }

    const BackendResourceHandle reference = ssgiContract.depth;
    const bool backendMismatch =
        reference.backendId != ssrContract.depth.backendId ||
        reference.backendId != contract.historyDiffuse.backendId ||
        reference.backendId != contract.historySpecular.backendId ||
        reference.backendId != contract.historyValidation.backendId ||
        reference.backendId != contract.historyMoments.backendId;
    if (backendMismatch)
    {
      contract.executionStatus = TemporalGiResolveExecutionStatus::MissingInputs;
      contract.validationStatus = TemporalGiResolveValidationStatus::BackendMismatch;
      return contract;
    }

    const bool dimensionMismatch =
        reference.width != ssrContract.depth.width ||
        reference.height != ssrContract.depth.height ||
        reference.width != contract.historyDiffuse.width ||
        reference.height != contract.historyDiffuse.height ||
        reference.width != contract.historySpecular.width ||
        reference.height != contract.historySpecular.height ||
        reference.width != contract.historyValidation.width ||
        reference.height != contract.historyValidation.height ||
        reference.width != contract.historyMoments.width ||
        reference.height != contract.historyMoments.height;
    if (dimensionMismatch)
    {
      contract.executionStatus = TemporalGiResolveExecutionStatus::MissingInputs;
      contract.validationStatus = TemporalGiResolveValidationStatus::DimensionMismatch;
      return contract;
    }

    contract.resolveDiffuseEnabled = ssgiContract.executionStatus != SsgiExecutionStatus::MissingInputs;
    contract.resolveSpecularEnabled = ssrContract.executionStatus != SsrExecutionStatus::MissingInputs;
    contract.resetHistoryBeforeResolve =
        contract.historyResetReason != GiHistoryResetReason::None || !contract.historyValidForReuse;
    contract.rejectHistoryThisFrame = contract.resetHistoryBeforeResolve;
    contract.clampHistoryThisFrame = !contract.resetHistoryBeforeResolve;
    contract.clampMode = contract.clampHistoryThisFrame
                             ? TemporalHistoryClampMode::NeighborhoodMinMax
                             : TemporalHistoryClampMode::None;
    contract.rejectionMode = contract.resetHistoryBeforeResolve
                                 ? TemporalHistoryRejectionMode::RejectAllHistory
                                 : TemporalHistoryRejectionMode::RejectOnMotionDisocclusion;
    contract.validationStatus = TemporalGiResolveValidationStatus::Valid;

    if (contract.resetHistoryBeforeResolve)
    {
      contract.executionStatus = TemporalGiResolveExecutionStatus::ResetAndFallback;
      return contract;
    }

    if (ssrContract.executionStatus == SsrExecutionStatus::FallbackOnly ||
        ssgiContract.executionStatus == SsgiExecutionStatus::FallbackOnly)
    {
      contract.executionStatus = TemporalGiResolveExecutionStatus::FallbackOnly;
      return contract;
    }

    contract.executionStatus = TemporalGiResolveExecutionStatus::ResolveAndAccumulate;
    return contract;
  }

  enum class LightingCompositeValidationStatus : uint8_t
  {
    Valid = 0,
    MissingBaseColor,
    MissingEmissive,
    MissingTemporalResolveInputs,
    BackendMismatch,
    DimensionMismatch,
  };

  inline const char *ToString(LightingCompositeValidationStatus status)
  {
    switch (status)
    {
    case LightingCompositeValidationStatus::Valid:
      return "valid";
    case LightingCompositeValidationStatus::MissingBaseColor:
      return "missing base color input";
    case LightingCompositeValidationStatus::MissingEmissive:
      return "missing emissive input";
    case LightingCompositeValidationStatus::MissingTemporalResolveInputs:
      return "temporal resolve pass contract is missing composite inputs";
    case LightingCompositeValidationStatus::BackendMismatch:
      return "lighting composite inputs come from mismatched backends";
    case LightingCompositeValidationStatus::DimensionMismatch:
      return "lighting composite inputs have mismatched dimensions";
    }
    return "unknown lighting composite validation status";
  }

  enum class LightingCompositeExecutionStatus : uint8_t
  {
    MissingInputs = 0,
    DirectLightingOnlyFallback,
    CompositeWithIndirect,
  };

  struct LightingCompositePassContract
  {
    BackendResourceHandle baseColor{};
    BackendResourceHandle emissive{};
    BackendResourceHandle resolvedDiffuse{};
    BackendResourceHandle resolvedSpecular{};
    uint64_t sceneFrameSerial = 0;
    uint64_t historyRevision = 0;
    bool includeIndirectDiffuse = false;
    bool includeIndirectSpecular = false;
    bool fallbackToDirectLightingOnly = true;
    bool temporalStabilityExpected = false;
    LightingCompositeExecutionStatus executionStatus = LightingCompositeExecutionStatus::MissingInputs;
    LightingCompositeValidationStatus validationStatus = LightingCompositeValidationStatus::MissingBaseColor;

    bool IsValidForComposite() const
    {
      return validationStatus == LightingCompositeValidationStatus::Valid &&
             executionStatus == LightingCompositeExecutionStatus::CompositeWithIndirect;
    }
  };

  inline LightingCompositePassContract
  BuildLightingCompositePassContract(const SceneTextureCatalog &sceneTextures,
                                     const TemporalGiResolvePassContract &resolveContract)
  {
    LightingCompositePassContract contract{};
    contract.baseColor = sceneTextures.Get(SceneTextureSemantic::BaseColor);
    contract.emissive = sceneTextures.Get(SceneTextureSemantic::Emissive);
    contract.resolvedDiffuse = resolveContract.historyDiffuse;
    contract.resolvedSpecular = resolveContract.historySpecular;
    contract.sceneFrameSerial = sceneTextures.frameSerial;
    contract.historyRevision = resolveContract.historyRevision;

    if (!contract.baseColor.IsValid())
    {
      contract.validationStatus = LightingCompositeValidationStatus::MissingBaseColor;
      contract.executionStatus = LightingCompositeExecutionStatus::MissingInputs;
      return contract;
    }
    if (!contract.emissive.IsValid())
    {
      contract.validationStatus = LightingCompositeValidationStatus::MissingEmissive;
      contract.executionStatus = LightingCompositeExecutionStatus::MissingInputs;
      return contract;
    }
    if (resolveContract.validationStatus != TemporalGiResolveValidationStatus::Valid)
    {
      contract.validationStatus = LightingCompositeValidationStatus::MissingTemporalResolveInputs;
      contract.executionStatus = LightingCompositeExecutionStatus::DirectLightingOnlyFallback;
      contract.fallbackToDirectLightingOnly = true;
      return contract;
    }

    const bool backendMismatch =
        contract.baseColor.backendId != contract.emissive.backendId ||
        contract.baseColor.backendId != contract.resolvedDiffuse.backendId ||
        contract.baseColor.backendId != contract.resolvedSpecular.backendId;
    if (backendMismatch)
    {
      contract.validationStatus = LightingCompositeValidationStatus::BackendMismatch;
      contract.executionStatus = LightingCompositeExecutionStatus::MissingInputs;
      return contract;
    }

    const bool dimensionMismatch =
        contract.baseColor.width != contract.emissive.width ||
        contract.baseColor.height != contract.emissive.height ||
        contract.baseColor.width != contract.resolvedDiffuse.width ||
        contract.baseColor.height != contract.resolvedDiffuse.height ||
        contract.baseColor.width != contract.resolvedSpecular.width ||
        contract.baseColor.height != contract.resolvedSpecular.height;
    if (dimensionMismatch)
    {
      contract.validationStatus = LightingCompositeValidationStatus::DimensionMismatch;
      contract.executionStatus = LightingCompositeExecutionStatus::MissingInputs;
      return contract;
    }

    contract.validationStatus = LightingCompositeValidationStatus::Valid;
    contract.includeIndirectDiffuse = resolveContract.resolveDiffuseEnabled;
    contract.includeIndirectSpecular = resolveContract.resolveSpecularEnabled;
    contract.fallbackToDirectLightingOnly =
        resolveContract.executionStatus == TemporalGiResolveExecutionStatus::ResetAndFallback ||
        resolveContract.executionStatus == TemporalGiResolveExecutionStatus::FallbackOnly;
    contract.temporalStabilityExpected =
        resolveContract.executionStatus == TemporalGiResolveExecutionStatus::ResolveAndAccumulate &&
        resolveContract.clampHistoryThisFrame &&
        !resolveContract.rejectHistoryThisFrame;
    contract.executionStatus = contract.fallbackToDirectLightingOnly
                                   ? LightingCompositeExecutionStatus::DirectLightingOnlyFallback
                                   : LightingCompositeExecutionStatus::CompositeWithIndirect;
    return contract;
  }

  enum class SceneTracingRepresentationOwnership : uint8_t
  {
    BackendOwnedPersistent = 0,
    BackendOwnedPerFrame,
    ExternalTransient,
  };

  enum class SceneTracingRepresentationUpdatePolicy : uint8_t
  {
    Static = 0,
    SceneBarrierOnly,
    PerFrameAndSceneBarrier,
    IncrementalStreaming,
  };

  struct MeshDistanceFieldTracingStructure
  {
    BackendResourceHandle atlas{};
    uint32_t meshCount = 0;
    uint32_t instanceCount = 0;
    uint32_t pageCount = 0;
    uint64_t revision = 0;

    bool IsValid() const { return atlas.IsValid(); }
  };

  struct GlobalDistanceFieldTracingStructure
  {
    BackendResourceHandle volume{};
    uint32_t clipmapCount = 0;
    uint32_t voxelResolution = 0;
    float worldExtent = 0.0f;
    uint64_t revision = 0;

    bool IsValid() const { return volume.IsValid(); }
  };

  struct SceneTracingDebugState
  {
    bool visualizationReady = false;
    bool usesClosestApproximation = false;
    bool temporalHistoryStable = false;
    GiHistoryResetReason lastHistoryResetReason = GiHistoryResetReason::None;
    SsrExecutionStatus ssrExecutionStatus = SsrExecutionStatus::Disabled;
    SsgiExecutionStatus ssgiExecutionStatus = SsgiExecutionStatus::Disabled;
    TemporalGiResolveExecutionStatus temporalResolveExecutionStatus =
        TemporalGiResolveExecutionStatus::Disabled;
  };

  enum class SceneTracingRepresentationValidationStatus : uint8_t
  {
    DisabledBySettings = 0,
    Valid,
    MissingScreenSpaceContracts,
    MissingMeshDistanceField,
    MissingGlobalDistanceField,
    BackendMismatch,
    DimensionMismatch,
  };

  inline const char *ToString(SceneTracingRepresentationValidationStatus status)
  {
    switch (status)
    {
    case SceneTracingRepresentationValidationStatus::DisabledBySettings:
      return "scene tracing representation disabled by settings";
    case SceneTracingRepresentationValidationStatus::Valid:
      return "valid";
    case SceneTracingRepresentationValidationStatus::MissingScreenSpaceContracts:
      return "scene tracing representation is missing required screen-space contracts";
    case SceneTracingRepresentationValidationStatus::MissingMeshDistanceField:
      return "scene tracing representation is missing mesh distance fields";
    case SceneTracingRepresentationValidationStatus::MissingGlobalDistanceField:
      return "scene tracing representation is missing global distance field";
    case SceneTracingRepresentationValidationStatus::BackendMismatch:
      return "scene tracing representation inputs come from mismatched backends";
    case SceneTracingRepresentationValidationStatus::DimensionMismatch:
      return "scene tracing representation inputs have mismatched dimensions";
    }
    return "unknown scene tracing representation validation status";
  }

  enum class SceneTracingRepresentationState : uint8_t
  {
    Disabled = 0,
    Unavailable,
    MeshDistanceFieldOnly,
    GlobalDistanceFieldOnly,
    HybridReady,
  };

  struct SceneTracingRepresentationContract
  {
    MeshDistanceFieldTracingStructure mesh{};
    GlobalDistanceFieldTracingStructure global{};
    ScreenSpaceReflectionPassContract ssr{};
    ScreenSpaceGlobalIlluminationPassContract ssgi{};
    TemporalGiResolvePassContract temporalResolve{};
    BackendResourceHandle referenceDepth{};
    uint64_t sceneFrameSerial = 0;
    uint64_t historyRevision = 0;
    SceneTracingRepresentationOwnership ownership =
        SceneTracingRepresentationOwnership::BackendOwnedPersistent;
    SceneTracingRepresentationUpdatePolicy updatePolicy =
        SceneTracingRepresentationUpdatePolicy::PerFrameAndSceneBarrier;
    SceneTracingDebugState debug{};
    SceneTracingRepresentationState state = SceneTracingRepresentationState::Unavailable;
    SceneTracingRepresentationValidationStatus validationStatus =
        SceneTracingRepresentationValidationStatus::DisabledBySettings;

    bool IsValidForOffscreenQueries() const
    {
      return validationStatus == SceneTracingRepresentationValidationStatus::Valid &&
             state == SceneTracingRepresentationState::HybridReady;
    }
  };

  inline SceneTracingRepresentationContract BuildSceneTracingRepresentationContract(
      const ScreenSpaceReflectionPassContract &ssrContract,
      const ScreenSpaceGlobalIlluminationPassContract &ssgiContract,
      const TemporalGiResolvePassContract &resolveContract,
      const MeshDistanceFieldTracingStructure &meshStructure,
      const GlobalDistanceFieldTracingStructure &globalStructure,
      SceneTracingRepresentationOwnership ownership,
      SceneTracingRepresentationUpdatePolicy updatePolicy,
      bool enabled)
  {
    SceneTracingRepresentationContract contract{};
    contract.ssr = ssrContract;
    contract.ssgi = ssgiContract;
    contract.temporalResolve = resolveContract;
    contract.mesh = meshStructure;
    contract.global = globalStructure;
    contract.ownership = ownership;
    contract.updatePolicy = updatePolicy;
    contract.sceneFrameSerial = std::max(ssrContract.sceneFrameSerial, ssgiContract.sceneFrameSerial);
    contract.historyRevision = resolveContract.historyRevision;
    contract.referenceDepth =
        ssgiContract.depth.IsValid() ? ssgiContract.depth : ssrContract.depth;
    contract.debug.lastHistoryResetReason = resolveContract.historyResetReason;
    contract.debug.ssrExecutionStatus = ssrContract.executionStatus;
    contract.debug.ssgiExecutionStatus = ssgiContract.executionStatus;
    contract.debug.temporalResolveExecutionStatus = resolveContract.executionStatus;
    contract.debug.temporalHistoryStable =
        resolveContract.executionStatus == TemporalGiResolveExecutionStatus::ResolveAndAccumulate &&
        resolveContract.clampHistoryThisFrame &&
        !resolveContract.rejectHistoryThisFrame;

    if (!enabled)
    {
      contract.state = SceneTracingRepresentationState::Disabled;
      contract.validationStatus = SceneTracingRepresentationValidationStatus::DisabledBySettings;
      return contract;
    }

    if (ssrContract.validationStatus != SsrInputValidationStatus::Valid ||
        ssgiContract.validationStatus != SsgiInputValidationStatus::Valid ||
        resolveContract.validationStatus != TemporalGiResolveValidationStatus::Valid)
    {
      contract.state = SceneTracingRepresentationState::Unavailable;
      contract.validationStatus = SceneTracingRepresentationValidationStatus::MissingScreenSpaceContracts;
      return contract;
    }

    if (!meshStructure.IsValid())
    {
      contract.state = globalStructure.IsValid()
                           ? SceneTracingRepresentationState::GlobalDistanceFieldOnly
                           : SceneTracingRepresentationState::Unavailable;
      contract.validationStatus = SceneTracingRepresentationValidationStatus::MissingMeshDistanceField;
      contract.debug.visualizationReady = globalStructure.IsValid();
      contract.debug.usesClosestApproximation = true;
      return contract;
    }
    if (!globalStructure.IsValid())
    {
      contract.state = SceneTracingRepresentationState::MeshDistanceFieldOnly;
      contract.validationStatus = SceneTracingRepresentationValidationStatus::MissingGlobalDistanceField;
      contract.debug.visualizationReady = true;
      contract.debug.usesClosestApproximation = true;
      return contract;
    }

    if (contract.referenceDepth.backendId != meshStructure.atlas.backendId ||
        contract.referenceDepth.backendId != globalStructure.volume.backendId)
    {
      contract.state = SceneTracingRepresentationState::Unavailable;
      contract.validationStatus = SceneTracingRepresentationValidationStatus::BackendMismatch;
      return contract;
    }

    if (contract.referenceDepth.width != meshStructure.atlas.width ||
        contract.referenceDepth.height != meshStructure.atlas.height ||
        contract.referenceDepth.width != globalStructure.volume.width ||
        contract.referenceDepth.height != globalStructure.volume.height)
    {
      contract.state = SceneTracingRepresentationState::Unavailable;
      contract.validationStatus = SceneTracingRepresentationValidationStatus::DimensionMismatch;
      return contract;
    }

    contract.state = SceneTracingRepresentationState::HybridReady;
    contract.validationStatus = SceneTracingRepresentationValidationStatus::Valid;
    contract.debug.visualizationReady = true;
    contract.debug.usesClosestApproximation = false;
    return contract;
  }

  enum class CachedHitLightingCapturePolicy : uint8_t
  {
    Disabled = 0,
    ScreenSpaceMissesOnly,
    ScreenSpaceMissesAndDisocclusions,
    FullReseedOnInvalidation,
  };

  enum class CachedHitLightingUpdatePolicy : uint8_t
  {
    Static = 0,
    SceneBarrierOnly,
    PerFrameBudgeted,
  };

  enum class CachedHitLightingInvalidationReason : uint8_t
  {
    None = 0,
    ViewportResize,
    CameraCut,
    SceneBarrier,
    TemporalDisabled,
    SceneTracingInvalid,
  };

  inline CachedHitLightingInvalidationReason ToCachedHitLightingInvalidationReason(
      GiHistoryResetReason reason)
  {
    switch (reason)
    {
    case GiHistoryResetReason::None:
      return CachedHitLightingInvalidationReason::None;
    case GiHistoryResetReason::ViewportResize:
      return CachedHitLightingInvalidationReason::ViewportResize;
    case GiHistoryResetReason::CameraCut:
      return CachedHitLightingInvalidationReason::CameraCut;
    case GiHistoryResetReason::SceneBarrier:
    case GiHistoryResetReason::CameraJitterSequenceChanged:
      return CachedHitLightingInvalidationReason::SceneBarrier;
    case GiHistoryResetReason::TemporalDisabled:
      return CachedHitLightingInvalidationReason::TemporalDisabled;
    }
    return CachedHitLightingInvalidationReason::None;
  }

  enum class CachedHitLightingRepresentationValidationStatus : uint8_t
  {
    DisabledBySettings = 0,
    Valid,
    MissingSceneTracingRepresentation,
    MissingLightingCompositeContract,
    MissingCacheSurface,
    BackendMismatch,
    DimensionMismatch,
  };

  enum class CachedHitLightingRepresentationState : uint8_t
  {
    Disabled = 0,
    Unavailable,
    Invalidated,
    Warmup,
    Ready,
  };

  enum class CachedHitLightingLookupIntegrationPoint : uint8_t
  {
    None = 0,
    SsrMiss = 1u << 0u,
    SsgiMiss = 1u << 1u,
    LightingComposite = 1u << 2u,
  };

  inline uint8_t CachedHitLightingLookupMask(CachedHitLightingLookupIntegrationPoint point)
  {
    return static_cast<uint8_t>(point);
  }

  struct CachedHitLightingLookupPolicy
  {
    uint8_t integrationMask = 0;
    bool allowApproximationFallback = true;
    bool requireTemporalStability = true;

    bool Uses(CachedHitLightingLookupIntegrationPoint point) const
    {
      return (integrationMask & CachedHitLightingLookupMask(point)) != 0u;
    }
  };

  struct CachedHitLightingDebugState
  {
    bool cacheAllocated = false;
    bool lookupEnabled = false;
    bool captureRequested = false;
    uint32_t captureBudget = 0;
    uint32_t capturesCommitted = 0;
    uint32_t residentSurfaceCount = 0;
    float estimatedHitRatio = 0.0f;
    CachedHitLightingInvalidationReason lastInvalidationReason =
        CachedHitLightingInvalidationReason::None;
    SceneTracingRepresentationValidationStatus tracingValidationStatus =
        SceneTracingRepresentationValidationStatus::DisabledBySettings;
    LightingCompositeValidationStatus compositeValidationStatus =
        LightingCompositeValidationStatus::MissingBaseColor;
  };

  struct CachedHitLightingRepresentationContract
  {
    BackendResourceHandle radianceCache{};
    BackendResourceHandle momentsCache{};
    BackendResourceHandle referenceDepth{};
    SceneTracingRepresentationContract sceneTracing{};
    LightingCompositePassContract lightingComposite{};
    uint64_t sceneFrameSerial = 0;
    uint64_t historyRevision = 0;
    uint64_t cacheRevision = 0;
    uint32_t maxSurfaceCount = 0;
    uint32_t residentSurfaceCount = 0;
    CachedHitLightingCapturePolicy capturePolicy = CachedHitLightingCapturePolicy::Disabled;
    CachedHitLightingUpdatePolicy updatePolicy = CachedHitLightingUpdatePolicy::Static;
    CachedHitLightingLookupPolicy lookupPolicy{};
    CachedHitLightingDebugState debug{};
    CachedHitLightingRepresentationValidationStatus validationStatus =
        CachedHitLightingRepresentationValidationStatus::DisabledBySettings;
    CachedHitLightingRepresentationState state = CachedHitLightingRepresentationState::Unavailable;

    bool IsValidForLookup() const
    {
      return validationStatus == CachedHitLightingRepresentationValidationStatus::Valid &&
             state == CachedHitLightingRepresentationState::Ready;
    }
  };

  inline CachedHitLightingRepresentationContract BuildCachedHitLightingRepresentationContract(
      const SceneTracingRepresentationContract &sceneTracingContract,
      const LightingCompositePassContract &lightingCompositeContract,
      const BackendResourceHandle &radianceCacheHandle,
      const BackendResourceHandle &momentsCacheHandle,
      uint64_t cacheRevision,
      uint32_t maxSurfaceCount,
      uint32_t residentSurfaceCount,
      CachedHitLightingCapturePolicy capturePolicy,
      CachedHitLightingUpdatePolicy updatePolicy,
      CachedHitLightingInvalidationReason invalidationReason,
      bool enabled)
  {
    CachedHitLightingRepresentationContract contract{};
    contract.radianceCache = radianceCacheHandle;
    contract.momentsCache = momentsCacheHandle;
    contract.sceneTracing = sceneTracingContract;
    contract.lightingComposite = lightingCompositeContract;
    contract.sceneFrameSerial = std::max(sceneTracingContract.sceneFrameSerial,
                                         lightingCompositeContract.sceneFrameSerial);
    contract.historyRevision = std::max(sceneTracingContract.historyRevision,
                                        lightingCompositeContract.historyRevision);
    contract.cacheRevision = cacheRevision;
    contract.maxSurfaceCount = maxSurfaceCount;
    contract.residentSurfaceCount = std::min(residentSurfaceCount, maxSurfaceCount);
    contract.capturePolicy = capturePolicy;
    contract.updatePolicy = updatePolicy;
    contract.referenceDepth = sceneTracingContract.referenceDepth.IsValid()
                                  ? sceneTracingContract.referenceDepth
                                  : lightingCompositeContract.baseColor;
    contract.debug.cacheAllocated = radianceCacheHandle.IsValid() && momentsCacheHandle.IsValid();
    contract.debug.lastInvalidationReason = invalidationReason;
    contract.debug.tracingValidationStatus = sceneTracingContract.validationStatus;
    contract.debug.compositeValidationStatus = lightingCompositeContract.validationStatus;
    contract.debug.residentSurfaceCount = contract.residentSurfaceCount;

    contract.lookupPolicy.integrationMask =
        CachedHitLightingLookupMask(CachedHitLightingLookupIntegrationPoint::LightingComposite);
    if (sceneTracingContract.ssr.validationStatus == SsrInputValidationStatus::Valid)
    {
      contract.lookupPolicy.integrationMask |=
          CachedHitLightingLookupMask(CachedHitLightingLookupIntegrationPoint::SsrMiss);
    }
    if (sceneTracingContract.ssgi.validationStatus == SsgiInputValidationStatus::Valid)
    {
      contract.lookupPolicy.integrationMask |=
          CachedHitLightingLookupMask(CachedHitLightingLookupIntegrationPoint::SsgiMiss);
    }
    contract.lookupPolicy.allowApproximationFallback = sceneTracingContract.debug.usesClosestApproximation;
    contract.lookupPolicy.requireTemporalStability = true;

    if (!enabled)
    {
      contract.state = CachedHitLightingRepresentationState::Disabled;
      contract.validationStatus = CachedHitLightingRepresentationValidationStatus::DisabledBySettings;
      return contract;
    }

    if (!sceneTracingContract.IsValidForOffscreenQueries())
    {
      contract.state = CachedHitLightingRepresentationState::Unavailable;
      contract.validationStatus =
          CachedHitLightingRepresentationValidationStatus::MissingSceneTracingRepresentation;
      return contract;
    }

    if (lightingCompositeContract.validationStatus != LightingCompositeValidationStatus::Valid)
    {
      contract.state = CachedHitLightingRepresentationState::Unavailable;
      contract.validationStatus =
          CachedHitLightingRepresentationValidationStatus::MissingLightingCompositeContract;
      return contract;
    }

    if (!radianceCacheHandle.IsValid() || !momentsCacheHandle.IsValid())
    {
      contract.state = CachedHitLightingRepresentationState::Unavailable;
      contract.validationStatus = CachedHitLightingRepresentationValidationStatus::MissingCacheSurface;
      return contract;
    }

    if (contract.referenceDepth.backendId != radianceCacheHandle.backendId ||
        contract.referenceDepth.backendId != momentsCacheHandle.backendId)
    {
      contract.state = CachedHitLightingRepresentationState::Unavailable;
      contract.validationStatus = CachedHitLightingRepresentationValidationStatus::BackendMismatch;
      return contract;
    }

    if (contract.referenceDepth.width != radianceCacheHandle.width ||
        contract.referenceDepth.height != radianceCacheHandle.height ||
        contract.referenceDepth.width != momentsCacheHandle.width ||
        contract.referenceDepth.height != momentsCacheHandle.height)
    {
      contract.state = CachedHitLightingRepresentationState::Unavailable;
      contract.validationStatus = CachedHitLightingRepresentationValidationStatus::DimensionMismatch;
      return contract;
    }

    contract.validationStatus = CachedHitLightingRepresentationValidationStatus::Valid;
    contract.debug.captureBudget = std::max(1u, contract.maxSurfaceCount / 16u);
    contract.debug.captureRequested =
        invalidationReason != CachedHitLightingInvalidationReason::None ||
        contract.sceneTracing.ssr.executionStatus == SsrExecutionStatus::FallbackOnly ||
        contract.sceneTracing.ssgi.executionStatus == SsgiExecutionStatus::FallbackOnly;
    contract.debug.capturesCommitted =
        contract.debug.captureRequested
            ? std::min(contract.debug.captureBudget, contract.maxSurfaceCount - contract.residentSurfaceCount)
            : 0u;

    if (contract.maxSurfaceCount > 0u)
    {
      contract.debug.estimatedHitRatio =
          static_cast<float>(contract.residentSurfaceCount) / static_cast<float>(contract.maxSurfaceCount);
    }

    if (invalidationReason != CachedHitLightingInvalidationReason::None)
    {
      contract.state = CachedHitLightingRepresentationState::Invalidated;
      contract.debug.lookupEnabled = false;
      return contract;
    }

    if (!sceneTracingContract.debug.temporalHistoryStable ||
        lightingCompositeContract.executionStatus ==
            LightingCompositeExecutionStatus::DirectLightingOnlyFallback)
    {
      contract.state = CachedHitLightingRepresentationState::Warmup;
      contract.debug.lookupEnabled = false;
      return contract;
    }

    contract.state = CachedHitLightingRepresentationState::Ready;
    contract.debug.lookupEnabled = true;
    return contract;
  }

} // namespace Monolith
