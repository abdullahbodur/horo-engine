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

/** @brief Opens a native file picker filtered for common texture image formats.
 *  @return Absolute path to the selected file, or an empty string if cancelled.
 */
std::string PickTextureFilePath();
} // namespace Horo::Editor
