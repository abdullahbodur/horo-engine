#pragma once

#include <string_view>

#include <imgui.h>

#include "renderer/Mesh.h"
#include "renderer/RenderTargetHandle.h"
#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {

bool TryGetAssetPreviewHandle(std::string_view assetId, const AssetDef& asset,
                              RenderTargetHandle* outHandle);

ImTextureID ToImTextureId(const RenderTargetHandle& handle);

const Mesh* TryGetAssetPreviewStaticMesh(std::string_view meshPath);

void ClearAssetThumbnailMeshCaches();

} // namespace Horo::Editor
