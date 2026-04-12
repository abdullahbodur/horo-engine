#pragma once

#include <filesystem>

namespace Monolith::Standalone {

std::filesystem::path PickFolderPath(const char* prompt,
                                     const std::filesystem::path& defaultPath = {});

}  // namespace Monolith::Standalone
