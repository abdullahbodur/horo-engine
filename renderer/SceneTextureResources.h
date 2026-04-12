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

} // namespace Monolith
