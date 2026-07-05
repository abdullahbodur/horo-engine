/**
 * @file HoroTheme.h
 * @brief Theme token structures, preset registry, and accessors for launcher/editor UI styling.
 */
#pragma once

#include <imgui.h>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Ui {

/**
 * @brief Full colour palette for a Horo UI theme.
 *
 * Every colour token is an ImVec4 in linear sRGB + alpha.  Panels, the
 * launcher, and all editor widgets share this single vocabulary so that a
 * palette swap replaces the entire look without touching widget code.
 */
struct HoroPalette {
  ImVec4 backgroundTop{};     /**< Top colour of the window gradient background. */
  ImVec4 backgroundBottom{};  /**< Bottom colour of the window gradient background. */
  ImVec4 panel{};             /**< Background fill for top-level editor panels. */
  ImVec4 panelSoft{};         /**< Slightly lighter panel variant used for secondary
                                *   surfaces and disabled button backgrounds. */
  ImVec4 card{};              /**< Background fill for card / tile child windows. */
  ImVec4 cardHover{};         /**< Card background when the cursor is over the card. */
  ImVec4 border{};            /**< Default border colour for windows, inputs, and cards. */
  ImVec4 text{};              /**< Primary text colour. */
  ImVec4 textMuted{};         /**< Secondary / hint text colour (lower contrast than text). */
  ImVec4 accent{};            /**< Brand accent — used for selected tabs, primary buttons,
                                *   toggle-on state, and CheckMark tint. */
  ImVec4 accentHover{};       /**< Accent brightened for hover state. */
  ImVec4 accentActive{};      /**< Accent darkened for pressed / active state. */
  ImVec4 selection{};         /**< Background tint for selected tree rows and cards. */
  ImVec4 selectionHover{};    /**< Selection tint on hover (slightly brighter). */
  ImVec4 input{};             /**< Background fill for text inputs and combo boxes. */
  ImVec4 inputHover{};        /**< Input background on hover. */
  ImVec4 inputActive{};       /**< Input background when focused / active. */
  ImVec4 modal{};             /**< Background fill for modal popup windows. */
  ImVec4 destructive{};       /**< Danger colour used for destructive-action buttons
                                *   (delete, discard) and error status text. */
};

/**
 * @brief Corner rounding radii for a Horo UI theme.
 *
 * Each radius corresponds to a specific widget class.  A value of 0 means
 * sharp corners.  Keeping them separate allows the launcher to use large
 * round windows while the editor uses tighter, tool-like radii.
 */
struct HoroRounding {
  float window = 0.0f; /**< Rounding for top-level OS-style windows. */
  float panel  = 0.0f; /**< Rounding for panel and child-window frames. */
  float card   = 0.0f; /**< Rounding for card / tile child windows. */
  float button = 0.0f; /**< Rounding for buttons (primary, secondary, icon). */
  float input  = 0.0f; /**< Rounding for text inputs, combo boxes, and sliders. */
  float tab    = 0.0f; /**< Rounding for the selected-tab underline pill (unused by
                         *   ImGui; consumed by RenderEditorPanelTopBar). */
};

/**
 * @brief Spacing and padding tokens for a Horo UI theme.
 *
 * These map directly to ImGui style vars that are pushed/popped by the Scoped*
 * helpers.  Keeping them in a struct means the launcher (spacious) and the
 * editor (compact) can share the same widget code with different density values.
 */
struct HoroDensity {
  ImVec2 panelPadding{};    /**< ImGuiStyleVar_WindowPadding applied inside panels. */
  ImVec2 cardPadding{};     /**< ImGuiStyleVar_WindowPadding applied inside card child windows. */
  ImVec2 buttonPadding{};   /**< ImGuiStyleVar_FramePadding applied to buttons. */
  ImVec2 inputPadding{};    /**< ImGuiStyleVar_FramePadding applied to inputs and combos. */
  float  itemSpacing = 0.0f; /**< Vertical gap between consecutive widgets
                               *   (ImGuiStyleVar_ItemSpacing.y). */
  float  iconSize    = 0.0f; /**< Target pixel size for FontAwesome icons; 0 = use current font size. */
};

/**
 * @brief Theme bundle used by the launcher window.
 *
 * Aggregates a colour palette, rounding, and density variant tuned for the
 * launcher's large-surface, high-contrast aesthetic.  Obtain the singleton via
 * GetLauncherTheme().
 */
struct LauncherTheme {
  HoroPalette  palette{};  /**< Colour tokens for the launcher. */
  HoroRounding rounding{}; /**< Corner radii for the launcher. */
  HoroDensity  density{};  /**< Spacing and padding for the launcher. */
};

/**
 * @brief Theme bundle used by all editor panels and widgets.
 *
 * Aggregates a colour palette, rounding, and density variant tuned for the
 * dense, tool-style editor aesthetic.  Obtain the singleton via GetEditorTheme().
 */
struct EditorTheme {
  HoroPalette  palette{};  /**< Colour tokens for the editor. */
  HoroRounding rounding{}; /**< Corner radii for the editor. */
  HoroDensity  density{};  /**< Spacing and padding for the editor. */
};

/**
 * @brief Selectable editor colour preset.
 *
 * Presets ship with the application and map to fixed palettes.  `DarkBlue` is
 * the default and preserves the original Horo editor look; `Graphite` and
 * `HighContrast` are alternative, accessibility-focused palettes.
 */
enum class EditorThemePreset {
  DarkBlue,     /**< Default Horo palette: deep blue panels, blue accent. */
  Graphite,     /**< Neutral dark workspace: slate borders, cool gray-blue accent. */
  HighContrast, /**< Maximum contrast: near-black panels, vivid cyan accent, pure white text. */
};

/** @brief Display-ready editor theme preset descriptor. */
struct EditorThemePresetDescriptor {
  std::string id;          /**< Stable persisted id. */
  std::string label;       /**< Human-facing name. */
  std::string description; /**< Short explanatory text. */
  HoroPalette palette{};   /**< Preview palette for the preset. */
  bool builtin = false;    /**< True for compiled-in presets. */
};

/** @brief Result of loading user-defined theme presets from config JSON. */
struct EditorThemeConfigLoadResult {
  bool ok = true;           /**< False when the file existed but could not be parsed. */
  bool loadedFromDisk = false; /**< True when a config file was found. */
  size_t customThemeCount = 0; /**< Number of custom themes registered. */
  std::string error;       /**< Error description when ok is false. */
};

/**
 * @brief Returns the stable string identifier for a preset.
 *
 * These ids are persisted on disk in `~/.horo/editor_settings.json`.  They are
 * intentionally short and camelCase, separate from the display labels.
 * @param preset Preset to look up.
 * @return Null-terminated string: `darkBlue`, `graphite`, or `highContrast`.
 */
const char *EditorThemePresetId(EditorThemePreset preset);

/**
 * @brief Returns the human-facing display label for a preset.
 * @param preset Preset to look up.
 * @return Null-terminated string: `Dark Blue`, `Graphite`, or `High Contrast`.
 */
const char *EditorThemePresetLabel(EditorThemePreset preset);

/**
 * @brief Parses a persisted preset id back into a preset enum.
 *
 * Unknown or empty ids fall back to `DarkBlue` so corrupt/legacy settings
 * cannot crash the editor.  The optional @p ok output reports whether the
 * input was recognised.
 * @param id  Persisted id; matched case-sensitively against EditorThemePresetId values.
 * @param ok  Optional output; set to true when @p id matches a known preset, false otherwise.
 * @return Parsed preset, or `DarkBlue` when @p id is unknown.
 */
EditorThemePreset ParseEditorThemePreset(std::string_view id, bool *ok = nullptr);

/**
 * @brief Returns the full list of supported presets in display order.
 *
 * The returned span references static storage and is safe to cache.
 * @return Span listing `DarkBlue`, `Graphite`, `HighContrast`.
 */
std::span<const EditorThemePreset> EditorThemePresets();

/** @brief Returns all built-in and user-defined editor theme preset descriptors. */
std::vector<EditorThemePresetDescriptor> EditorThemePresetOptions();

/** @brief Returns true when @p id resolves to a built-in or user-defined preset. */
bool IsEditorThemePresetIdKnown(std::string_view id);

/** @brief Loads user-defined editor themes from a config JSON file.
 *
 * Expected shape:
 * @code{.json}
 * {
 *   "editorThemes": [
 *     {
 *       "id": "midnight",
 *       "name": "Midnight",
 *       "description": "Cool dark theme",
 *       "palette": {
 *         "panel": "#10131a",
 *         "accent": [0.2, 0.55, 1.0, 1.0]
 *       }
 *     }
 *   ]
 * }
 * @endcode
 *
 * Missing palette keys inherit from Dark Blue. Colours may be #RRGGBB,
 * #RRGGBBAA, or [r,g,b,a] arrays in 0..1.
 */
EditorThemeConfigLoadResult
LoadEditorThemeConfig(const std::filesystem::path &configPath);

/**
 * @brief Stores @p preset as the active editor theme preset.
 *
 * The call only updates internal state; it does NOT reapply ImGui style
 * colours.  Call ApplyEditorTheme(ImGui::GetStyle()) after this to push the
 * new palette into ImGui.
 * @param preset Preset to activate.
 */
void SetEditorThemePreset(EditorThemePreset preset);

/** @brief Stores the active editor theme by persisted id. Unknown ids fall back to darkBlue. */
void SetEditorThemePresetId(std::string_view id);

/**
 * @brief Returns the preset currently selected as the editor theme.
 * @return Active preset; defaults to `DarkBlue` until changed.
 */
EditorThemePreset GetEditorThemePreset();

/** @brief Returns the currently active editor theme id. */
std::string_view GetEditorThemePresetId();

/**
 * @brief Returns the application-wide launcher theme singleton.
 *
 * The theme is constructed once and the reference remains valid for the
 * lifetime of the process.  Never call this before ImGui has been initialised.
 * @return Const reference to the shared LauncherTheme instance.
 */
const LauncherTheme &GetLauncherTheme();

/**
 * @brief Returns the editor theme for the currently selected preset.
 *
 * The reference points to static storage per preset and remains valid for
 * the lifetime of the process.  The referenced object is effectively
 * immutable, so any cached references become stale after
 * SetEditorThemePreset() — callers that rebind to preset changes should
 * re-fetch via GetEditorTheme() each frame.
 * @return Const reference to the EditorTheme instance for the active preset.
 */
const EditorTheme &GetEditorTheme();

/**
 * @brief Maps the active editor theme onto an ImGui style struct.
 *
 * Writes palette colours, rounding, and density into @p style so ImGui will
 * render with the currently selected preset.  This is the single central
 * point where the editor's ImGui colour theme is derived from HoroPalette;
 * both `EditorLayer::Init` and the Settings modal's Apply/OK path call it.
 * @param style ImGui style target (typically `ImGui::GetStyle()`).
 */
void ApplyEditorTheme(ImGuiStyle &style);

} // namespace Horo::Ui
