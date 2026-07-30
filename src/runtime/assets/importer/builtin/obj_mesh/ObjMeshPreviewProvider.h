#pragma once

/**
 * @file ObjMeshPreviewProvider.h
 * @brief Built-in rendering-neutral preview provider for the mesh editor payload.
 */

#include "Horo/Assets/AssetPreview.h"

#include <memory>

namespace Horo::Assets {
    /** @brief Selects the canonical camera used by the built-in mesh preview provider. */
    enum class BuiltinMeshPreviewView {
        Isometric,
        NegativeX,
    };

    /**
     * @brief Creates the built-in mesh-payload preview provider.
     * @param view Canonical camera selected for the source format.
     * @return Shared immutable preview provider.
     */
    [[nodiscard]] std::shared_ptr<const IAssetPreviewProvider> CreateBuiltinMeshPreviewProvider(BuiltinMeshPreviewView view);
}  // namespace Horo::Assets
