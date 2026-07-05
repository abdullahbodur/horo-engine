/**
 * @file EditorUserSettings.h
 * @brief Global editor user settings persisted to `~/.horo/editor_settings.json`.
 *
 * These settings are intentionally separate from MCP settings
 * (`~/.horo/settings.json`) and from project-scoped workspace state
 * (`<project>/.horo/editor_workspace.json`). They represent per-user editor
 * preferences such as the chosen theme preset that should follow the user
 * across projects.
 */
#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "ui/HoroTheme.h"

namespace Horo::Editor {

// ── Build / Toolchain Settings ──────────────────────────────────────────────

/** @brief Per-platform build enablement and toolchain selection.
 *
 *  Each platform stores its own enabled flag and optional toolchain string.
 *  Defaults to disabled; call MakeDefaultBuildToolchainSettings() for
 *  host-safe defaults where the current host platform is enabled and the
 *  remaining platforms stay disabled until the user opts in. */
struct BuildPlatformSettings {
    /** @brief Whether this platform is available as a build target. */
    bool enabled = false;
    /** @brief Toolchain profile name or path; empty string means "auto-detect" / default. */
    std::string toolchain;

    bool operator==(const BuildPlatformSettings &) const = default;
};

/** @brief Build / toolchain configuration for all supported host platforms.
 *
 *  Carries one @ref BuildPlatformSettings entry per platform.  Equality
 *  comparison is memberwise so draft/original dirty-tracking works with a
 *  single `==` check. */
struct BuildToolchainSettings {
    /** @brief macOS build target settings. */
    BuildPlatformSettings macOS{};
    /** @brief Windows build target settings. */
    BuildPlatformSettings windows{};
    /** @brief Linux build target settings. */
    BuildPlatformSettings linux_{};

    bool operator==(const BuildToolchainSettings &) const = default;
};

/** @brief Returns host-safe defaults.
 *
 *  The current host platform is enabled; all other platforms are disabled.
 *  Toolchain strings are empty (auto-detect).  This is the starting state
 *  for a fresh settings file and the fallback when JSON is missing or
 *  malformed. */
BuildToolchainSettings MakeDefaultBuildToolchainSettings();

// ── EditorUserSettings ──────────────────────────────────────────────────────

/** @brief User-level editor preferences. */
struct EditorUserSettings {
    /** @brief Currently selected editor theme preset; defaults to Dark Blue. */
    Horo::Ui::EditorThemePreset themePreset = Horo::Ui::EditorThemePreset::DarkBlue;
    /** @brief Persisted editor theme id; may reference built-in or custom config themes. */
    std::string themePresetId = Horo::Ui::EditorThemePresetId(Horo::Ui::EditorThemePreset::DarkBlue);
    /** @brief Build / toolchain platform preferences; defaults to host-safe values. */
    BuildToolchainSettings buildToolchain = MakeDefaultBuildToolchainSettings();
};

/**
 * @brief Document envelope that preserves unknown JSON on save.
 *
 * Mirrors the pattern used by `EditorWorkspaceDocument` and
 * `McpSettingsDocument`: we keep the raw root JSON so unknown keys (set by
 * future editor versions or external tooling) survive a load → save cycle
 * without being dropped.
 */
struct EditorUserSettingsDocument {
    EditorUserSettings settings{};                       /**< Parsed settings with defaults. */
    nlohmann::json rootJson = nlohmann::json::object();  /**< Full JSON root, preserved on save. */
    bool loadedFromDisk = false;                          /**< True when a file was present and read. */
    bool parseError = false;                              /**< True when JSON parsing or shape validation failed. */
    std::string error;                                    /**< Populated with load/parse error description. */
};

/**
 * @brief Returns the resolved path of the editor user settings file.
 *
 * On all platforms this is `<home>/.horo/editor_settings.json`, using the
 * same home-directory resolution as `Mcp::ResolveMcpSettingsDirectory()`.
 * @return Absolute path to the settings file (may not yet exist on disk).
 */
std::filesystem::path ResolveEditorUserSettingsPath();

/**
 * @brief Loads the editor user settings document.
 *
 * Behaviour:
 * - Missing file: returns default settings (`DarkBlue`), `loadedFromDisk=false`, no error.
 * - Valid JSON with known preset: returns parsed settings.
 * - Valid JSON with unknown preset: returns `DarkBlue` and sets `error` to an
 *   explanatory message while leaving `parseError=false` (the file itself parsed).
 * - Invalid JSON / non-object root: returns defaults, sets `parseError=true`, sets `error`.
 * @return Loaded document, never throws.
 */
EditorUserSettingsDocument LoadEditorUserSettingsDocument();

/**
 * @brief Saves the editor user settings document to disk.
 *
 * Preserves unknown root and editor keys from `doc->rootJson`. Creates the
 * settings directory if necessary. On success updates `loadedFromDisk=true`
 * and clears `parseError`/`error`. Returns false (and populates `outError`)
 * when the document is null, the directory cannot be created, or writing
 * fails.
 * @param doc      In/out document to persist; must be non-null.
 * @param outError Optional output populated on failure; cleared on success.
 * @return True on successful save, false otherwise.
 */
bool SaveEditorUserSettingsDocument(EditorUserSettingsDocument *doc,
                                    std::string *outError);

} // namespace Horo::Editor
