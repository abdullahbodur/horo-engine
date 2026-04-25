#pragma once

#include <string>

#include "editor/SceneDocument.h"

namespace Monolith::Editor {
    std::string GenerateAssetGuid();

    std::string MakeAssetDisplayName(const std::string &assetId,
                                     const AssetDef &asset);

    void EnsureAssetIdentity(const std::string &assetId, AssetDef *asset);

    void EnsureAssetIdentity(SceneDocument *doc);
} // namespace Monolith::Editor
