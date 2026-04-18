#pragma once

#include <filesystem>

namespace Monolith::Launcher {

std::filesystem::path PickFolderPath(const char* prompt,
                                     const std::filesystem::path& defaultPath = {});

}  // namespace Monolith::Launcher
