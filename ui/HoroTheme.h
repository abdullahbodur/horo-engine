/**
 * @file HoroTheme.h
 * @brief Theme token structures and accessors for launcher/editor UI styling.
 */
#pragma once

#include <imgui.h>

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
 * @brief Returns the application-wide launcher theme singleton.
 *
 * The theme is constructed once and the reference remains valid for the
 * lifetime of the process.  Never call this before ImGui has been initialised.
 * @return Const reference to the shared LauncherTheme instance.
 */
const LauncherTheme &GetLauncherTheme();

/**
 * @brief Returns the application-wide editor theme singleton.
 *
 * The theme is constructed once and the reference remains valid for the
 * lifetime of the process.  Never call this before ImGui has been initialised.
 * @return Const reference to the shared EditorTheme instance.
 */
const EditorTheme &GetEditorTheme();

} // namespace Horo::Ui
