#pragma once

#include <filesystem>
#include <string>

struct ImGuiIO;

namespace Horo::Ui {

struct FontFamilyConfig {
  std::string relativePath;
  float size = 16.0f;
};

struct FontResolutionResult {
  std::filesystem::path resolvedPath;
  bool found = false;
};

FontResolutionResult ResolveFontPath(const FontFamilyConfig &config);

void LoadFonts(ImGuiIO &io, const FontFamilyConfig &primaryFont);

void LoadDefaultFont(ImGuiIO &io);

} // namespace Horo::Ui
