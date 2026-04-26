#pragma once

#include <string>

namespace Horo::Editor {
    bool IsObjFilePath(const std::string &path);

    std::string AssetIdFromImportedPath(const std::string &path);

    std::string MeshTagFromImportedPath(const std::string &path);

    // Returns a "x,y,z" render-scale string that makes the mesh approximately
    // targetHeight world units tall. Returns "1.0000,1.0000,1.0000" if the OBJ
    // cannot be parsed or if the mesh has zero height.
    std::string SuggestRenderScale(const std::string &meshTag,
                                   float targetHeight = 2.0f);
} // namespace Horo::Editor
