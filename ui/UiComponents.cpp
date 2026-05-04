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

void PushButtonColorsPrimary(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_Button, palette.accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.accentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.accentActive);
  ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  *colorCount += 4;
}

void PushButtonColorsSecondary(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_Button, palette.panelSoft);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.cardHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.selection);
  ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  *colorCount += 4;
}

void PushButtonVars(const HoroRounding &rounding, const HoroDensity &density,
                     int *styleCount) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, density.buttonPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding.button);
  *styleCount += 2;
}

void PushComboColors(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_FrameBg, palette.input);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, palette.inputHover);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, palette.inputActive);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, palette.panel);
  ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  *colorCount += 5;
}

void PushComboVars(const HoroRounding &rounding, const HoroDensity &density,
                   int *styleCount) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding.input);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, density.inputPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, rounding.panel);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
  *styleCount += 4;
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

ScopedButtonStyle::ScopedButtonStyle(const EditorTheme &theme, ButtonStyleVariant variant) {
  PushButtonVars(theme.rounding, theme.density, &m_styleCount);
  if (variant == ButtonStyleVariant::Primary) {
    PushButtonColorsPrimary(theme.palette, &m_colorCount);
  } else {
    PushButtonColorsSecondary(theme.palette, &m_colorCount);
  }
}

ScopedButtonStyle::ScopedButtonStyle(const LauncherTheme &theme, ButtonStyleVariant variant) {
  PushButtonVars(theme.rounding, theme.density, &m_styleCount);
  if (variant == ButtonStyleVariant::Primary) {
    PushButtonColorsPrimary(theme.palette, &m_colorCount);
  } else {
    PushButtonColorsSecondary(theme.palette, &m_colorCount);
  }
}

ScopedButtonStyle::~ScopedButtonStyle() {
  ImGui::PopStyleColor(m_colorCount);
  ImGui::PopStyleVar(m_styleCount);
}

void TextMuted(const EditorTheme &theme, const char *text) { ImGui::TextColored(theme.palette.textMuted, "%s", text); }

void TextMuted(const LauncherTheme &theme, const char *text) { ImGui::TextColored(theme.palette.textMuted, "%s", text); }

void SectionHeader(const EditorTheme &theme, const char *title) {
  ImGui::TextColored(theme.palette.text, "%s", title);
  ImGui::Separator();
}

void SectionHeader(const LauncherTheme &theme, const char *title) {
  ImGui::TextColored(theme.palette.text, "%s", title);
  ImGui::Separator();
}

bool Button(const EditorTheme &theme, ButtonStyleVariant variant,
            const char *label, const ImVec2 &size) {
  ScopedButtonStyle style(theme, variant);
  return ImGui::Button(label, size);
}

bool Button(const LauncherTheme &theme, ButtonStyleVariant variant,
            const char *label, const ImVec2 &size) {
  ScopedButtonStyle style(theme, variant);
  return ImGui::Button(label, size);
}

bool RenderPrimaryButton(const LauncherTheme &theme, const char *label,
                         const ImVec2 &size) {
  return Button(theme, ButtonStyleVariant::Primary, label, size);
}

bool RenderSecondaryButton(const LauncherTheme &theme, const char *label,
                            const ImVec2 &size) {
  return Button(theme, ButtonStyleVariant::Secondary, label, size);
}

bool RenderRecentProjectButton(const LauncherTheme &theme, const char *title,
                                const ImVec2 &size) {
  return Button(theme, ButtonStyleVariant::Secondary, title, size);
}

void RenderLabeledInput(const LauncherTheme &theme, const char *title,
                        const char *id, char *buffer, size_t bufferSize,
                        float inputWidth) {
  ImGui::TextDisabled("%s", title);
  ImGui::SetNextItemWidth(inputWidth);
  InputText(theme, id, buffer, bufferSize);
}

bool InputText(const EditorTheme &theme, const char *id, char *buffer,
               size_t bufferSize, ImGuiInputTextFlags flags) {
  ScopedInputStyle style(theme);
  return ImGui::InputText(id, buffer, bufferSize, flags);
}

bool InputText(const LauncherTheme &theme, const char *id, char *buffer,
               size_t bufferSize, ImGuiInputTextFlags flags) {
  ScopedInputStyle style(theme);
  return ImGui::InputText(id, buffer, bufferSize, flags);
}

ScopedComboStyle::ScopedComboStyle(const EditorTheme &theme) {
  PushComboColors(theme.palette, &m_colorCount);
  PushComboVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedComboStyle::ScopedComboStyle(const LauncherTheme &theme) {
  PushComboColors(theme.palette, &m_colorCount);
  PushComboVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedComboStyle::~ScopedComboStyle() {
  ImGui::PopStyleVar(m_styleCount);
  ImGui::PopStyleColor(m_colorCount);
}

bool InputTextWithHint(const EditorTheme &theme, const char *id,
                       const char *hint, char *buffer, size_t bufferSize,
                       ImGuiInputTextFlags flags) {
  ScopedInputStyle style(theme);
  return ImGui::InputTextWithHint(id, hint, buffer, bufferSize, flags);
}

bool InputTextWithHint(const LauncherTheme &theme, const char *id,
                       const char *hint, char *buffer, size_t bufferSize,
                       ImGuiInputTextFlags flags) {
  ScopedInputStyle style(theme);
  return ImGui::InputTextWithHint(id, hint, buffer, bufferSize, flags);
}

bool Combo(const EditorTheme &theme, const char *label, int *currentItem,
           const char *const items[], int itemCount) {
  ScopedComboStyle style(theme);
  return ImGui::Combo(label, currentItem, items, itemCount);
}

bool Combo(const LauncherTheme &theme, const char *label, int *currentItem,
           const char *const items[], int itemCount) {
  ScopedComboStyle style(theme);
  return ImGui::Combo(label, currentItem, items, itemCount);
}

} // namespace Horo::Ui
