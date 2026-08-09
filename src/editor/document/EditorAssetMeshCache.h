#pragma once

/**
 * @file EditorAssetMeshCache.h
 * @brief Bounded owner-thread cache for imported editor mesh payloads used by viewport extraction.
 */

#include "Horo/Assets/AssetId.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Runtime/Render/RenderScene.h"

#include <filesystem>
#include <memory>
#include <unordered_map>

namespace Horo::Editor {
    struct EditorAssetMeshView {
        Render::RenderMeshHandle handle;
        const Render::MeshData *mesh{};
    };

    class EditorAssetMeshCache final {
    public:
        /** @brief Loads, validates, and retains one imported core.mesh editor payload. */
        [[nodiscard]] Result<EditorAssetMeshView> Load(Assets::AssetId asset, const std::filesystem::path &absolutePath);
        /** @brief Returns a previously loaded immutable mesh view. */
        [[nodiscard]] std::optional<EditorAssetMeshView> Find(Assets::AssetId asset) const noexcept;
        void Clear() noexcept;

    private:
        std::unordered_map<Assets::AssetId, std::shared_ptr<const Render::MeshData>, Assets::AssetIdHash> meshes_;
    };
}  // namespace Horo::Editor
