#pragma once

/**
 * @file ObjMeshImporter.h
 * @brief Built-in OBJ mesh importer contribution registered through AssetImporterCatalog.
 */

#include "Horo/Assets/AssetImporter.h"

#include <memory>

namespace Horo::Assets
{

[[nodiscard]] std::shared_ptr<const IAssetImporter> CreateObjMeshImporter();
[[nodiscard]] Result<void> RegisterObjMeshImporter(AssetImporterCatalog &catalog);

/** @brief Registers all built-in importers into the catalog. */
[[nodiscard]] Result<void> RegisterAllBuiltinImporters(AssetImporterCatalog &catalog);

} // namespace Horo::Assets
