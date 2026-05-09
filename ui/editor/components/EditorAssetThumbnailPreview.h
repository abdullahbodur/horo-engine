/** @file EditorAssetThumbnailPreview.h
 *  @brief Helpers for resolving and converting asset thumbnail render targets. */
#pragma once

#include <string_view>

#include <imgui.h>

#include "renderer/Mesh.h"
#include "renderer/RenderTargetHandle.h"
#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {

/** @brief Attempts to retrieve the render-target handle for an asset's thumbnail.
 *  @param assetId  Unique identifier of the asset whose thumbnail is requested.
 *  @param asset    Asset definition containing mesh and texture references.
 *  @param outHandle Output parameter populated with the handle on success.
 *  @return True if a valid render target was found and outHandle was written. */
bool TryGetAssetPreviewHandle(std::string_view assetId, const AssetDef& asset,
                              RenderTargetHandle* outHandle);

/** @brief Converts a render-target handle to an ImGui texture identifier.
 *  @param handle Render-target handle obtained from the renderer.
 *  @return An ImTextureID suitable for use with ImGui::Image. */
ImTextureID ToImTextureId(const RenderTargetHandle& handle);

/** @brief Returns a pointer to the static mesh used for previewing an asset at the given path.
 *  @param meshPath Relative or absolute path to the mesh asset.
 *  @return Non-owning pointer to the cached Mesh, or nullptr if the mesh could not be loaded. */
const Mesh* TryGetAssetPreviewStaticMesh(std::string_view meshPath);

/** @brief Clears all cached mesh objects used for asset thumbnail rendering. */
void ClearAssetThumbnailMeshCaches();

} // namespace Horo::Editor
