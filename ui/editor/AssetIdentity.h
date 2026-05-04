#pragma once

#include <string>

#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {
    std::string GenerateAssetGuid();

    std::string MakeAssetDisplayName(const std::string &assetId,
                                     const AssetDef &asset);

    void EnsureAssetIdentity(const std::string &assetId, AssetDef *asset);

    void EnsureAssetIdentity(SceneDocument *doc);
} // namespace Horo::Editor
