/** @file EditorFilePickerUtils.h
 *  @brief Platform file-picker dialogs for selecting asset source files in the editor.
 */
#pragma once

#include <string>

namespace Horo::Editor {
/** @brief Opens a native file picker filtered for Wavefront OBJ files.
 *  @return Absolute path to the selected file, or an empty string if cancelled.
 */
std::string PickObjFilePath();

/** @brief Opens a native file picker filtered for the engine's supported mesh source formats.
 *
 *  Currently accepts @c .obj and @c .fbx. Use this in editor flows that route
 *  through @ref AssetImporterRegistry::FindByExtension; the extension is validated
 *  in code, so a slightly looser filter is acceptable.
 *  @return Absolute path to the selected file, or an empty string if cancelled.
 */
std::string PickMeshFilePath();

/** @brief Opens a native file picker filtered for common texture image formats.
 *  @return Absolute path to the selected file, or an empty string if cancelled.
 */
std::string PickTextureFilePath();
} // namespace Horo::Editor
