#include "ui/HoroTheme.h"

namespace Horo::Ui {
namespace {

constexpr HoroPalette kPalette{
    .backgroundTop = ImVec4(0.02f, 0.04f, 0.08f, 1.0f),
    .backgroundBottom = ImVec4(0.01f, 0.02f, 0.05f, 1.0f),
    .panel = ImVec4(0.06f, 0.10f, 0.16f, 0.82f),
    .panelSoft = ImVec4(0.08f, 0.13f, 0.20f, 0.64f),
    .card = ImVec4(0.05f, 0.10f, 0.17f, 0.76f),
    .cardHover = ImVec4(0.08f, 0.15f, 0.25f, 0.84f),
    .border = ImVec4(0.15f, 0.24f, 0.35f, 0.64f),
    .text = ImVec4(0.96f, 0.98f, 1.0f, 1.0f),
    .textMuted = ImVec4(0.62f, 0.69f, 0.78f, 1.0f),
    .accent = ImVec4(0.20f, 0.41f, 0.68f, 1.0f),
    .accentHover = ImVec4(0.24f, 0.46f, 0.75f, 1.0f),
    .accentActive = ImVec4(0.18f, 0.36f, 0.62f, 1.0f),
    .selection = ImVec4(0.14f, 0.31f, 0.52f, 0.92f),
    .selectionHover = ImVec4(0.18f, 0.37f, 0.60f, 0.96f),
    .input = ImVec4(0.04f, 0.08f, 0.13f, 1.0f),
    .inputHover = ImVec4(0.06f, 0.11f, 0.18f, 1.0f),
    .inputActive = ImVec4(0.08f, 0.15f, 0.25f, 1.0f),
    .modal = ImVec4(0.05f, 0.09f, 0.15f, 0.98f),
    .destructive = ImVec4(0.95f, 0.35f, 0.32f, 1.0f),
};

constexpr HoroRounding kLauncherRounding{
    .panel = 12.0f,
    .card = 8.0f,
    .button = 8.0f,
    .input = 6.0f,
    .tab = 6.0f,
};

constexpr HoroDensity kLauncherDensity{
    .panelPadding = ImVec2(18.0f, 18.0f),
    .cardPadding = ImVec2(16.0f, 14.0f),
    .buttonPadding = ImVec2(16.0f, 9.0f),
    .inputPadding = ImVec2(12.0f, 9.0f),
    .itemSpacing = 8.0f,
};

constexpr HoroRounding kEditorRounding{
    .panel = 7.0f,
    .card = 6.0f,
    .button = 5.0f,
    .input = 5.0f,
    .tab = 5.0f,
};

constexpr HoroDensity kEditorDensity{
    .panelPadding = ImVec2(10.0f, 8.0f),
    .cardPadding = ImVec2(10.0f, 8.0f),
    .buttonPadding = ImVec2(9.0f, 5.0f),
    .inputPadding = ImVec2(8.0f, 5.0f),
    .itemSpacing = 5.0f,
};

constexpr HoroPalette MakeEditorPalette() {
  HoroPalette palette = kPalette;
  palette.panel.w = 0.88f;
  return palette;
}

} // namespace

const LauncherTheme &GetLauncherTheme() {
  static const LauncherTheme theme{
      .palette = kPalette,
      .rounding = kLauncherRounding,
      .density = kLauncherDensity,
  };
  return theme;
}

const EditorTheme &GetEditorTheme() {
  static const EditorTheme theme{
      .palette = MakeEditorPalette(),
      .rounding = kEditorRounding,
      .density = kEditorDensity,
  };
  return theme;
}

} // namespace Horo::Ui
