#include "ui/UiComponents.h"

namespace Horo::Ui {
namespace {

void PushPanelColors(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_WindowBg, palette.panel);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.panel);
  ImGui::PushStyleColor(ImGuiCol_Border, palette.border);
  *colorCount += 3;
}

void PushPanelVars(const HoroRounding &rounding, const HoroDensity &density,
                   int *styleCount) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding.panel);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding.panel);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, density.panelPadding);
  *styleCount += 3;
}

void PushCardColors(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.card);
  ImGui::PushStyleColor(ImGuiCol_Border, palette.border);
  ImGui::PushStyleColor(ImGuiCol_Header, palette.selection);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, palette.selectionHover);
  *colorCount += 4;
}

void PushCardVars(const HoroRounding &rounding, const HoroDensity &density,
                  int *styleCount) {
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding.card);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, density.cardPadding);
  *styleCount += 2;
}

void PushInputColors(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_FrameBg, palette.input);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, palette.inputHover);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, palette.inputActive);
  ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  *colorCount += 4;
}

void PushInputVars(const HoroRounding &rounding, const HoroDensity &density,
                   int *styleCount) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding.input);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, density.inputPadding);
  *styleCount += 2;
}

} // namespace

ScopedPanelStyle::ScopedPanelStyle(const EditorTheme &theme) {
  PushPanelColors(theme.palette, &m_colorCount);
  PushPanelVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedPanelStyle::ScopedPanelStyle(const LauncherTheme &theme) {
  PushPanelColors(theme.palette, &m_colorCount);
  PushPanelVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedPanelStyle::~ScopedPanelStyle() {
  ImGui::PopStyleVar(m_styleCount);
  ImGui::PopStyleColor(m_colorCount);
}

ScopedCardStyle::ScopedCardStyle(const EditorTheme &theme) {
  PushCardColors(theme.palette, &m_colorCount);
  PushCardVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedCardStyle::ScopedCardStyle(const LauncherTheme &theme) {
  PushCardColors(theme.palette, &m_colorCount);
  PushCardVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedCardStyle::~ScopedCardStyle() {
  ImGui::PopStyleVar(m_styleCount);
  ImGui::PopStyleColor(m_colorCount);
}

ScopedInputStyle::ScopedInputStyle(const EditorTheme &theme) {
  PushInputColors(theme.palette, &m_colorCount);
  PushInputVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedInputStyle::ScopedInputStyle(const LauncherTheme &theme) {
  PushInputColors(theme.palette, &m_colorCount);
  PushInputVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedInputStyle::~ScopedInputStyle() {
  ImGui::PopStyleVar(m_styleCount);
  ImGui::PopStyleColor(m_colorCount);
}

void PushPrimaryButtonStyle(const EditorTheme &theme) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, theme.density.buttonPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.rounding.button);
  ImGui::PushStyleColor(ImGuiCol_Button, theme.palette.accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.accentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.palette.accentActive);
  ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
}

void PushPrimaryButtonStyle(const LauncherTheme &theme) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, theme.density.buttonPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.rounding.button);
  ImGui::PushStyleColor(ImGuiCol_Button, theme.palette.accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.accentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.palette.accentActive);
  ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
}

void PushSecondaryButtonStyle(const EditorTheme &theme) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, theme.density.buttonPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.rounding.button);
  ImGui::PushStyleColor(ImGuiCol_Button, theme.palette.panelSoft);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.cardHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.palette.selection);
  ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
}

void PushSecondaryButtonStyle(const LauncherTheme &theme) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, theme.density.buttonPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.rounding.button);
  ImGui::PushStyleColor(ImGuiCol_Button, theme.palette.panelSoft);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.cardHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.palette.selection);
  ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
}

void PopButtonStyle() {
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);
}

void TextMuted(const char *text) { ImGui::TextColored(GetEditorTheme().palette.textMuted, "%s", text); }

void SectionHeader(const char *title) {
  ImGui::TextColored(GetEditorTheme().palette.text, "%s", title);
  ImGui::Separator();
}

} // namespace Horo::Ui
