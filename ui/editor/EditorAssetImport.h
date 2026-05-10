/** @file EditorAssetImport.h
 *  @brief Helpers for parsing imported asset paths and deriving mesh metadata.
 */
#pragma once

#include <string>

namespace Horo::Editor {
    /** @brief Returns true when path has an ".obj" extension (case-insensitive).
     *  @param path File path to test.
     *  @return True if path refers to a Wavefront OBJ file.
     */
    bool IsObjFilePath(const std::string &path);

    /** @brief Derives a canonical asset ID from an imported file path.
     *  @param path Absolute or project-relative path to the imported asset file.
     *  @return Asset ID string derived from the path stem and parent directory.
     */
    std::string AssetIdFromImportedPath(const std::string &path);

    /** @brief Derives the mesh tag from an imported OBJ file path.
     *  @param path Absolute or project-relative path to the imported OBJ file.
     *  @return Mesh tag string used to look up geometry within the imported asset.
     */
    std::string MeshTagFromImportedPath(const std::string &path);

    /** @brief Computes a render-scale string that fits the mesh to a target world-space height.
     *  @param meshTag      Tag identifying the mesh within the imported OBJ.
     *  @param targetHeight Desired height in world units (default 2.0).
     *  @return Comma-separated "x,y,z" scale string, or "1.0000,1.0000,1.0000" on failure.
     */
    std::string SuggestRenderScale(const std::string &meshTag,
                                   float targetHeight = 2.0f);
} // namespace Horo::Editor
