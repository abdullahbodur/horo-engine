/**
 * @file HoroTheme.cpp
 * @brief Shared launcher/editor theme singletons, preset registry, and
 *        central ImGui style application.
 */
#include "ui/HoroTheme.h"

#include <array>

namespace Horo::Ui {
namespace {

/**
 * @brief Base colour palette shared with the launcher — also the Dark Blue
 *        editor preset before the editor-specific alpha adjustment.
 */
constexpr HoroPalette kPalette{
    .backgroundTop = ImVec4(1.0f / 255.0f, 7.0f / 255.0f, 17.0f / 255.0f, 1.0f),
    .backgroundBottom = ImVec4(1.0f / 255.0f, 7.0f / 255.0f, 17.0f / 255.0f, 1.0f),
    .panel = ImVec4(0.05f, 0.09f, 0.15f, 0.94f),
    .panelSoft = ImVec4(0.07f, 0.11f, 0.18f, 0.90f),
    .card = ImVec4(0.05f, 0.10f, 0.17f, 0.76f),
    .cardHover = ImVec4(0.08f, 0.15f, 0.25f, 0.84f),
    .border = ImVec4(0.16f, 0.27f, 0.42f, 0.68f),
    .text = ImVec4(0.96f, 0.98f, 1.0f, 1.0f),
    .textMuted = ImVec4(0.68f, 0.74f, 0.84f, 1.0f),
    .accent = ImVec4(0.23f, 0.54f, 0.93f, 1.0f),
    .accentHover = ImVec4(0.28f, 0.60f, 0.99f, 1.0f),
    .accentActive = ImVec4(0.18f, 0.46f, 0.82f, 1.0f),
    .selection = ImVec4(0.14f, 0.31f, 0.52f, 0.92f),
    .selectionHover = ImVec4(0.18f, 0.37f, 0.60f, 0.96f),
    .input = ImVec4(0.04f, 0.08f, 0.13f, 1.0f),
    .inputHover = ImVec4(0.06f, 0.11f, 0.18f, 1.0f),
    .inputActive = ImVec4(0.08f, 0.15f, 0.25f, 1.0f),
    .modal = ImVec4(0.05f, 0.09f, 0.15f, 0.98f),
    .destructive = ImVec4(0.95f, 0.35f, 0.32f, 1.0f),
};

/** @brief Graphite preset: neutral dark gray workspace with cool gray-blue accent. */
constexpr HoroPalette kGraphitePalette{
    .backgroundTop = ImVec4(0.08f, 0.08f, 0.10f, 1.0f),
    .backgroundBottom = ImVec4(0.08f, 0.08f, 0.10f, 1.0f),
    .panel = ImVec4(0.12f, 0.12f, 0.14f, 0.94f),
    .panelSoft = ImVec4(0.15f, 0.15f, 0.17f, 0.90f),
    .card = ImVec4(0.10f, 0.10f, 0.12f, 0.76f),
    .cardHover = ImVec4(0.18f, 0.18f, 0.21f, 0.84f),
    .border = ImVec4(0.30f, 0.32f, 0.36f, 0.70f),
    .text = ImVec4(0.95f, 0.96f, 0.97f, 1.0f),
    .textMuted = ImVec4(0.68f, 0.70f, 0.74f, 1.0f),
    .accent = ImVec4(0.48f, 0.54f, 0.64f, 1.0f),
    .accentHover = ImVec4(0.56f, 0.62f, 0.72f, 1.0f),
    .accentActive = ImVec4(0.40f, 0.45f, 0.55f, 1.0f),
    .selection = ImVec4(0.24f, 0.26f, 0.32f, 0.92f),
    .selectionHover = ImVec4(0.30f, 0.32f, 0.38f, 0.96f),
    .input = ImVec4(0.08f, 0.08f, 0.10f, 1.0f),
    .inputHover = ImVec4(0.12f, 0.12f, 0.14f, 1.0f),
    .inputActive = ImVec4(0.17f, 0.17f, 0.20f, 1.0f),
    .modal = ImVec4(0.11f, 0.11f, 0.13f, 0.98f),
    .destructive = ImVec4(0.93f, 0.40f, 0.36f, 1.0f),
};

/** @brief High Contrast preset: near-black panels, bright borders, vivid cyan accent. */
constexpr HoroPalette kHighContrastPalette{
    .backgroundTop = ImVec4(0.0f, 0.0f, 0.0f, 1.0f),
    .backgroundBottom = ImVec4(0.0f, 0.0f, 0.0f, 1.0f),
    .panel = ImVec4(0.02f, 0.02f, 0.03f, 0.98f),
    .panelSoft = ImVec4(0.06f, 0.06f, 0.07f, 0.94f),
    .card = ImVec4(0.03f, 0.03f, 0.04f, 0.90f),
    .cardHover = ImVec4(0.12f, 0.14f, 0.18f, 0.96f),
    .border = ImVec4(0.55f, 0.60f, 0.70f, 1.0f),
    .text = ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
    .textMuted = ImVec4(0.85f, 0.87f, 0.92f, 1.0f),
    .accent = ImVec4(0.00f, 0.80f, 1.00f, 1.0f),
    .accentHover = ImVec4(0.15f, 0.90f, 1.00f, 1.0f),
    .accentActive = ImVec4(0.00f, 0.60f, 0.90f, 1.0f),
    .selection = ImVec4(0.00f, 0.45f, 0.70f, 0.92f),
    .selectionHover = ImVec4(0.10f, 0.55f, 0.80f, 0.96f),
    .input = ImVec4(0.02f, 0.02f, 0.03f, 1.0f),
    .inputHover = ImVec4(0.07f, 0.08f, 0.10f, 1.0f),
    .inputActive = ImVec4(0.12f, 0.14f, 0.18f, 1.0f),
    .modal = ImVec4(0.03f, 0.03f, 0.04f, 0.99f),
    .destructive = ImVec4(1.00f, 0.38f, 0.32f, 1.0f),
};

/** @brief Corner radii preset used by launcher widgets and windows. */
constexpr HoroRounding kLauncherRounding{
    .window = 16.0f,
    .panel = 12.0f,
    .card = 10.0f,
    .button = 8.0f,
    .input = 6.0f,
    .tab = 6.0f,
};

/** @brief Spacing and padding preset used by launcher layouts. */
constexpr HoroDensity kLauncherDensity{
    .panelPadding = ImVec2(18.0f, 18.0f),
    .cardPadding = ImVec2(16.0f, 14.0f),
    .buttonPadding = ImVec2(16.0f, 9.0f),
    .inputPadding = ImVec2(12.0f, 9.0f),
    .itemSpacing = 8.0f,
    .iconSize = 16.0f,
};

/** @brief Corner radii preset used by dense editor widgets. */
constexpr HoroRounding kEditorRounding{
    .panel = 7.0f,
    .card = 6.0f,
    .button = 5.0f,
    .input = 5.0f,
    .tab = 5.0f,
};

/** @brief Spacing and padding preset used by editor layouts. */
constexpr HoroDensity kEditorDensity{
    .panelPadding = ImVec2(10.0f, 8.0f),
    .cardPadding = ImVec2(10.0f, 8.0f),
    .buttonPadding = ImVec2(10.0f, 7.0f),
    .inputPadding = ImVec2(12.0f, 8.0f),
    .itemSpacing = 5.0f,
    .iconSize = 18.0f,
};

/**
 * @brief Full list of supported presets in display order.
 *
 * Kept as a static array rather than a function-local so EditorThemePresets()
 * can return a std::span referencing stable storage.
 */
constexpr std::array<EditorThemePreset, 3> kPresetList = {
    EditorThemePreset::DarkBlue,
    EditorThemePreset::Graphite,
    EditorThemePreset::HighContrast,
};

/**
 * @brief Derive the editor palette from a preset base by tweaking the
 *        semi-transparent panel fill.
 *
 * DarkBlue historically used panel.w = 0.88 instead of the launcher 0.94.
 * The two alternative presets inherit the same editor-specific translucency
 * so the editor aesthetic is consistent.
 * @param base Source palette to copy and adjust.
 * @return Palette with editor-specific panel alpha.
 */
constexpr HoroPalette MakeEditorPalette(HoroPalette base) {
  base.panel.w = 0.88f;
  return base;
}

/** @brief Mutable preset selector. Single-threaded editor owns the value. */
EditorThemePreset &MutableCurrentPreset() {
  static EditorThemePreset current = EditorThemePreset::DarkBlue;
  return current;
}

} // namespace

/** @copydoc GetLauncherTheme */
const LauncherTheme &GetLauncherTheme() {
  static const LauncherTheme theme{
      .palette = kPalette,
      .rounding = kLauncherRounding,
      .density = kLauncherDensity,
  };
  return theme;
}

/** @copydoc EditorThemePresetId */
const char *EditorThemePresetId(EditorThemePreset preset) {
  using enum EditorThemePreset;
  switch (preset) {
  case DarkBlue:
    return "darkBlue";
  case Graphite:
    return "graphite";
  case HighContrast:
    return "highContrast";
  }
  return "darkBlue";
}

/** @copydoc EditorThemePresetLabel */
const char *EditorThemePresetLabel(EditorThemePreset preset) {
  using enum EditorThemePreset;
  switch (preset) {
  case DarkBlue:
    return "Dark Blue";
  case Graphite:
    return "Graphite";
  case HighContrast:
    return "High Contrast";
  }
  return "Dark Blue";
}

/** @copydoc ParseEditorThemePreset */
EditorThemePreset ParseEditorThemePreset(std::string_view id, bool *ok) {
  for (const EditorThemePreset preset : kPresetList) {
    if (id == EditorThemePresetId(preset)) {
      if (ok)
        *ok = true;
      return preset;
    }
  }
  if (ok)
    *ok = false;
  return EditorThemePreset::DarkBlue;
}

/** @copydoc EditorThemePresets */
std::span<const EditorThemePreset> EditorThemePresets() {
  return std::span<const EditorThemePreset>(kPresetList.data(), kPresetList.size());
}

/** @copydoc SetEditorThemePreset */
void SetEditorThemePreset(EditorThemePreset preset) {
  MutableCurrentPreset() = preset;
}

/** @copydoc GetEditorThemePreset */
EditorThemePreset GetEditorThemePreset() {
  return MutableCurrentPreset();
}

/** @copydoc GetEditorTheme */
const EditorTheme &GetEditorTheme() {
  switch (GetEditorThemePreset()) {
  case EditorThemePreset::DarkBlue: {
    static const EditorTheme theme{
        .palette = MakeEditorPalette(kPalette),
        .rounding = kEditorRounding,
        .density = kEditorDensity,
    };
    return theme;
  }
  case EditorThemePreset::Graphite: {
    static const EditorTheme theme{
        .palette = MakeEditorPalette(kGraphitePalette),
        .rounding = kEditorRounding,
        .density = kEditorDensity,
    };
    return theme;
  }
  case EditorThemePreset::HighContrast: {
    static const EditorTheme theme{
        .palette = MakeEditorPalette(kHighContrastPalette),
        .rounding = kEditorRounding,
        .density = kEditorDensity,
    };
    return theme;
  }
  }
  static const EditorTheme fallback{
      .palette = MakeEditorPalette(kPalette),
      .rounding = kEditorRounding,
      .density = kEditorDensity,
  };
  return fallback;
}

/** @copydoc ApplyEditorTheme */
void ApplyEditorTheme(ImGuiStyle &style) {
  const EditorTheme &theme = GetEditorTheme();
  const HoroPalette &p = theme.palette;
  const HoroRounding &r = theme.rounding;
  const HoroDensity &d = theme.density;

  style.Colors[ImGuiCol_Text] = p.text;
  style.Colors[ImGuiCol_TextDisabled] = p.textMuted;
  style.Colors[ImGuiCol_WindowBg] = p.panel;
  style.Colors[ImGuiCol_ChildBg] = p.card;
  style.Colors[ImGuiCol_PopupBg] = p.modal;
  style.Colors[ImGuiCol_Border] = p.border;
  style.Colors[ImGuiCol_FrameBg] = p.input;
  style.Colors[ImGuiCol_FrameBgHovered] = p.inputHover;
  style.Colors[ImGuiCol_FrameBgActive] = p.inputActive;
  style.Colors[ImGuiCol_TitleBg] = p.panel;
  style.Colors[ImGuiCol_TitleBgActive] = p.panelSoft;
  style.Colors[ImGuiCol_MenuBarBg] = p.panel;
  style.Colors[ImGuiCol_ScrollbarBg] = p.input;
  style.Colors[ImGuiCol_ScrollbarGrab] = p.border;
  style.Colors[ImGuiCol_ScrollbarGrabHovered] = p.accent;
  style.Colors[ImGuiCol_ScrollbarGrabActive] = p.accentActive;
  style.Colors[ImGuiCol_CheckMark] = p.accent;
  style.Colors[ImGuiCol_SliderGrab] = p.accent;
  style.Colors[ImGuiCol_SliderGrabActive] = p.accentActive;
  style.Colors[ImGuiCol_Button] = p.card;
  style.Colors[ImGuiCol_ButtonHovered] = p.cardHover;
  style.Colors[ImGuiCol_ButtonActive] = p.accentActive;
  style.Colors[ImGuiCol_Header] = p.selection;
  style.Colors[ImGuiCol_HeaderHovered] = p.selectionHover;
  style.Colors[ImGuiCol_HeaderActive] = p.accentActive;
  style.Colors[ImGuiCol_Separator] = p.border;
  style.Colors[ImGuiCol_ResizeGrip] = p.border;
  style.Colors[ImGuiCol_ResizeGripHovered] = p.accent;
  style.Colors[ImGuiCol_ResizeGripActive] = p.accentActive;
  style.Colors[ImGuiCol_Tab] = p.card;
  style.Colors[ImGuiCol_TabHovered] = p.cardHover;
  style.Colors[ImGuiCol_TabActive] = p.accent;
  style.Colors[ImGuiCol_TabUnfocused] = p.card;
  style.Colors[ImGuiCol_TabUnfocusedActive] = p.panelSoft;

  style.WindowRounding = r.window;
  style.ChildRounding = r.card;
  style.FrameRounding = r.input;
  style.PopupRounding = r.panel;
  style.ScrollbarRounding = r.button;
  style.GrabRounding = r.button;
  style.TabRounding = r.tab;

  style.WindowPadding = d.panelPadding;
  style.FramePadding = d.inputPadding;
  style.ItemSpacing = ImVec2(d.itemSpacing, d.itemSpacing);
  style.ItemInnerSpacing = ImVec2(d.itemSpacing * 0.5f, d.itemSpacing * 0.5f);
  style.CellPadding = d.cardPadding;
}

} // namespace Horo::Ui
