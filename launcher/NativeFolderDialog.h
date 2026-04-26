#pragma once

#include <filesystem>

namespace Horo::Launcher {
    std::filesystem::path
    PickFolderPath(const char *prompt,
                   const std::filesystem::path &defaultPath = {});
} // namespace Horo::Launcher
