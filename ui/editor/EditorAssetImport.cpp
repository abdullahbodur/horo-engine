#include "ui/editor/EditorAssetImport.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

#include "renderer/ObjLoader.h"

namespace Horo::Editor {
    bool IsObjFilePath(const std::string &path) {
        if (path.empty())
            return false;
        const std::filesystem::path p(path);
        std::string ext = p.extension().string();
        std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return ext == ".obj";
    }

    std::string AssetIdFromImportedPath(const std::string &path) {
        if (path.empty())
            return {};
        return std::filesystem::path(path).stem().string();
    }

    std::string MeshTagFromImportedPath(const std::string &path) {
        if (path.empty())
            return {};
        const std::filesystem::path src(path);
        return (std::filesystem::path("assets/models") / src.filename())
                .generic_string();
    }

    std::string SuggestRenderScale(const std::string &meshTag, float targetHeight) {
        auto aabb = ObjLoader::ComputeAABB(meshTag);
        if (!aabb.valid)
            return "1.0000,1.0000,1.0000";
        float height = aabb.max.y - aabb.min.y;
        if (height < 1e-6f)
            return "1.0000,1.0000,1.0000";
        float scale = targetHeight / height;
        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(4);
        out << scale << ',' << scale << ',' << scale;
        return out.str();
    }
} // namespace Horo::Editor
