#pragma once

#include "Horo/Assets/AssetImporter.h"
#include <memory>

namespace Horo::Assets
{
[[nodiscard]] std::shared_ptr<const IAssetImporter> CreateFbxMeshImporter();
[[nodiscard]] Result<void> RegisterFbxMeshImporter(AssetImporterCatalog &catalog);
} // namespace Horo::Assets
