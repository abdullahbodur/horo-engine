#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "renderer/RenderBackend.h"

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
  };

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

} // namespace Monolith
