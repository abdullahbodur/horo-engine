/**
 * @file AssetIdentity.h
 * @brief Utilities for generating and ensuring stable identity fields on asset definitions.
 */
#pragma once

#include <string>

#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {
    /**
     * @brief Generates a new unique GUID string for an asset.
     * @return A newly generated GUID string.
     */
    std::string GenerateAssetGuid();

    /**
     * @brief Derives a human-readable display name from an asset ID and its definition.
     * @param assetId Logical asset identifier.
     * @param asset   The asset definition to derive the name from.
     * @return A display name string suitable for showing in the editor UI.
     */
    std::string MakeAssetDisplayName(const std::string &assetId,
                                     const AssetDef &asset);

    /**
     * @brief Ensures the given asset definition has a valid GUID and display name,
     *        generating them if missing.
     * @param assetId Logical asset identifier used to derive the display name.
     * @param asset   The asset definition to update in place.
     */
    void EnsureAssetIdentity(const std::string &assetId, AssetDef *asset);

    /**
     * @brief Ensures all assets in the document have valid GUIDs and display names,
     *        generating them for any asset that is missing identity fields.
     * @param doc The scene document whose assets are updated in place.
     */
    void EnsureAssetIdentity(SceneDocument *doc);
} // namespace Horo::Editor
