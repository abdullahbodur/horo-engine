/**
 * @file UiFonts.h
 * @brief Font loading and resolution utilities for the Horo UI module.
 *
 * Provides helpers for locating font files on disk and registering them with
 * ImGui's font atlas.  Call @ref LoadFonts or @ref LoadDefaultFont during
 * ImGui initialisation before the first rendered frame.
 */
#pragma once

#include <filesystem>
#include <string>

struct ImGuiIO;

namespace Horo::Ui {

/** @brief Configuration describing a font family to load into ImGui. */
struct FontFamilyConfig {
  std::string relativePath; /**< Path to the font file relative to the project asset root. */
  float size = 16.0f;       /**< Desired font size in pixels. */
};

/** @brief Result returned by @ref ResolveFontPath indicating whether the font was found. */
struct FontResolutionResult {
  std::filesystem::path resolvedPath; /**< Absolute path to the font file, valid only when @ref found is true. */
  bool found = false;                 /**< True if the font file exists at the resolved path. */
};

/**
 * @brief Resolve a font file path from a @ref FontFamilyConfig.
 *
 * Searches known asset directories for the file named by
 * @p config.relativePath.
 *
 * @param config  Font configuration whose @c relativePath is used for lookup.
 * @return A @ref FontResolutionResult with the absolute path and a
 *         @c found flag indicating success.
 */
FontResolutionResult ResolveFontPath(const FontFamilyConfig &config);

/**
 * @brief Load a custom font into the ImGui font atlas.
 *
 * Resolves @p primaryFont via @ref ResolveFontPath, adds it to @p io, and
 * merges the icon font so that Font Awesome glyphs are available.  Falls
 * back to the ImGui default font if the file cannot be found.
 *
 * @param io           ImGui IO object whose font atlas will be updated.
 * @param primaryFont  Configuration for the primary text font to load.
 * @param dpiScale     Per-display content scale (e.g. 2.0 on Retina). Used
 *                     to set @c ImFontConfig::RasterizerDensity so the atlas
 *                     is rasterised at native pixel density without changing
 *                     layout sizes. Pass 1.0f on non-HiDPI displays.
 */
void LoadFonts(ImGuiIO &io, const FontFamilyConfig &primaryFont,
               float dpiScale = 1.0f);

/**
 * @brief Load the built-in ImGui default font with the icon font merged in.
 *
 * Convenience wrapper used when no custom font path is configured.
 *
 * @param io  ImGui IO object whose font atlas will be updated.
 */
void LoadDefaultFont(ImGuiIO &io);

} // namespace Horo::Ui
