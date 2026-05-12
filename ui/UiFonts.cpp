/**
 * @file UiFonts.cpp
 * @brief Runtime implementations for font-path resolution and ImGui font setup.
 */
#include "ui/UiFonts.h"

#include <imgui.h>

#include <array>

#include "core/Logger.h"
#include "core/ProjectPath.h"

namespace Horo::Ui {

/** @copydoc ResolveFontPath */
FontResolutionResult ResolveFontPath(const FontFamilyConfig &config) {
  if (config.relativePath.empty())
    return {};

  const std::filesystem::path relPath(config.relativePath);

  // Reject absolute paths — relativePath must be relative.
  if (relPath.is_absolute())
    return {};

  // Reject path traversal — prevent escaping candidate root directories.
  for (const auto &part : relPath) {
    if (part == "..")
      return {};
  }

  const std::array<std::filesystem::path, 4> candidates = {
      ProjectPath::ResolveSdk(relPath.string()),
      ProjectPath::Root() / relPath,
      ProjectPath::Root() / "engine" / relPath,
      ProjectPath::Root() / "horo-engine" / relPath,
  };

  for (const auto &candidate : candidates) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec) && !ec)
      return {candidate, true};
  }
  return {};
}

/** @copydoc LoadFonts */
void LoadFonts(ImGuiIO &io, const FontFamilyConfig &primaryFont) {
  if (const FontResolutionResult resolved = ResolveFontPath(primaryFont);
      resolved.found) {
    if (ImFont *font = io.Fonts->AddFontFromFileTTF(
            resolved.resolvedPath.string().c_str(), primaryFont.size))
      io.FontDefault = font;
    else
      LogWarn("[UiFonts] Failed to load font from '{}'",
              resolved.resolvedPath.string());
  }

  if (!io.FontDefault)
    io.FontDefault = io.Fonts->AddFontDefault();
}

/** @copydoc LoadDefaultFont */
void LoadDefaultFont(ImGuiIO &io) { io.FontDefault = io.Fonts->AddFontDefault(); }

} // namespace Horo::Ui
