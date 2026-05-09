/** @file NativeFolderDialog.h
 *  @brief Exposes a platform-native folder picker dialog for the launcher. */
#pragma once

#include <filesystem>

namespace Horo::Launcher {

/** @brief Opens a platform-native folder selection dialog and returns the chosen path.
 *  @param prompt       Title string displayed in the dialog window.
 *  @param defaultPath  Directory shown when the dialog first opens; may be empty.
 *  @return The selected directory path, or an empty path if the user cancelled. */
std::filesystem::path
PickFolderPath(const char *prompt,
               const std::filesystem::path &defaultPath = {});

} // namespace Horo::Launcher
