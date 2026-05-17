/**
 * @file UiComponents.cpp
 * @brief Implementations for reusable themed ImGui primitives used by Horo UI.
 */
#include "ui/UiComponents.h"

#include <cstdarg>
#include <format>
#include <string>

#include "ui/IconsFontAwesome6.h"

namespace Horo::Ui {
namespace {

/** @brief Push panel window colours and track pushed colour count. */
void PushPanelColors(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_WindowBg, palette.panel);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.panel);
  ImGui::PushStyleColor(ImGuiCol_Border, palette.border);
  *colorCount += 3;
}

/** @brief Push panel rounding/padding style vars and track push count. */
void PushPanelVars(const HoroRounding &rounding, const HoroDensity &density,
                   int *styleCount) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding.panel);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding.panel);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, density.panelPadding);
  *styleCount += 3;
}

/** @brief Push card-specific background, border, and header colours. */
void PushCardColors(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.card);
  ImGui::PushStyleColor(ImGuiCol_Border, palette.border);
  ImGui::PushStyleColor(ImGuiCol_Header, palette.selection);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, palette.selectionHover);
  *colorCount += 4;
}

/** @brief Push card child-window style vars (rounding and padding). */
void PushCardVars(const HoroRounding &rounding, const HoroDensity &density,
                  int *styleCount) {
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding.card);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, density.cardPadding);
  *styleCount += 2;
}

/** @brief Push text-input background and text colours. */
void PushInputColors(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_FrameBg, palette.input);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, palette.inputHover);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, palette.inputActive);
  ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  *colorCount += 4;
}

/** @brief Push text-input rounding/padding style vars. */
void PushInputVars(const HoroRounding &rounding, const HoroDensity &density,
                   int *styleCount) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding.input);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, density.inputPadding);
  *styleCount += 2;
}

/** @brief Push the primary button colour set. */
void PushButtonColorsPrimary(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_Button, palette.accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.accentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.accentActive);
  ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  *colorCount += 4;
}

/** @brief Push the secondary button colour set. */
void PushButtonColorsSecondary(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_Button, palette.panelSoft);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.cardHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.selection);
  ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  *colorCount += 4;
}

/** @brief Push shared button style vars (padding and rounding). */
void PushButtonVars(const HoroRounding &rounding, const HoroDensity &density,
                    int *styleCount) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, density.buttonPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding.button);
  *styleCount += 2;
}

/** @brief Push combo-box frame, popup, and text colours. */
void PushComboColors(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_FrameBg, palette.input);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, palette.inputHover);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, palette.inputActive);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, palette.panel);
  ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  *colorCount += 5;
}

/** @brief Push combo-box rounding/padding/popup style vars. */
void PushComboVars(const HoroRounding &rounding, const HoroDensity &density,
                   int *styleCount) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding.input);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, density.inputPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, rounding.panel);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
  *styleCount += 4;
}

/** @brief Push fully transparent tree-header colours for custom row backgrounds. */
void PushTransparentHeaderColors(int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
  *colorCount += 3;
}

/** @brief Push transparent button colours while preserving theme text colour. */
void PushTransparentButtonColors(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  *colorCount += 4;
}

/**
 * @brief Recursively render nested dropdown-menu items for panel action popups.
 * @param items Menu items in the current submenu depth.
 */
void RenderEditorDropdownItems(std::span<const EditorPanelDropdownItem> items) {
  for (const auto &item : items) {
    std::string display;
    if (item.icon && item.icon[0] != '\0') {
      display = item.icon;
      display += "  ";
    }
    if (item.label)
      display += item.label;

    if (item.childCount > 0 && item.children != nullptr) {
      if (ImGui::BeginMenu(display.c_str())) {
        RenderEditorDropdownItems(
            std::span<const EditorPanelDropdownItem>(item.children,
                                                     item.childCount));
        ImGui::EndMenu();
      }
    } else {
      if (ImGui::MenuItem(display.c_str()) && item.action)
        item.action();
    }
  }
}

} // namespace

/** @copydoc ScopedPanelStyle::ScopedPanelStyle(const EditorTheme &) */
ScopedPanelStyle::ScopedPanelStyle(const EditorTheme &theme) {
  PushPanelColors(theme.palette, &m_colorCount);
  PushPanelVars(theme.rounding, theme.density, &m_styleCount);
}

/** @copydoc ScopedPanelStyle::ScopedPanelStyle(const LauncherTheme &) */
ScopedPanelStyle::ScopedPanelStyle(const LauncherTheme &theme) {
  PushPanelColors(theme.palette, &m_colorCount);
  PushPanelVars(theme.rounding, theme.density, &m_styleCount);
}

/** @copydoc ScopedPanelStyle::~ScopedPanelStyle */
ScopedPanelStyle::~ScopedPanelStyle() {
  ImGui::PopStyleVar(m_styleCount);
  ImGui::PopStyleColor(m_colorCount);
}

/** @copydoc ScopedCardStyle::ScopedCardStyle(const EditorTheme &) */
ScopedCardStyle::ScopedCardStyle(const EditorTheme &theme) {
  PushCardColors(theme.palette, &m_colorCount);
  PushCardVars(theme.rounding, theme.density, &m_styleCount);
}

/** @copydoc ScopedCardStyle::ScopedCardStyle(const LauncherTheme &) */
ScopedCardStyle::ScopedCardStyle(const LauncherTheme &theme) {
  PushCardColors(theme.palette, &m_colorCount);
  PushCardVars(theme.rounding, theme.density, &m_styleCount);
}

/** @copydoc ScopedCardStyle::~ScopedCardStyle */
ScopedCardStyle::~ScopedCardStyle() {
  ImGui::PopStyleVar(m_styleCount);
  ImGui::PopStyleColor(m_colorCount);
}

/** @copydoc ScopedInputStyle::ScopedInputStyle(const EditorTheme &) */
ScopedInputStyle::ScopedInputStyle(const EditorTheme &theme) {
  PushInputColors(theme.palette, &m_colorCount);
  PushInputVars(theme.rounding, theme.density, &m_styleCount);
}

/** @copydoc ScopedInputStyle::ScopedInputStyle(const LauncherTheme &) */
ScopedInputStyle::ScopedInputStyle(const LauncherTheme &theme) {
  PushInputColors(theme.palette, &m_colorCount);
  PushInputVars(theme.rounding, theme.density, &m_styleCount);
}

/** @copydoc ScopedInputStyle::~ScopedInputStyle */
ScopedInputStyle::~ScopedInputStyle() {
  ImGui::PopStyleVar(m_styleCount);
  ImGui::PopStyleColor(m_colorCount);
}

/** @copydoc ScopedButtonStyle::ScopedButtonStyle(const EditorTheme &, ButtonStyleVariant) */
ScopedButtonStyle::ScopedButtonStyle(const EditorTheme &theme, ButtonStyleVariant variant) {
  PushButtonVars(theme.rounding, theme.density, &m_styleCount);
  if (variant == ButtonStyleVariant::Primary) {
    PushButtonColorsPrimary(theme.palette, &m_colorCount);
  } else {
    PushButtonColorsSecondary(theme.palette, &m_colorCount);
  }
}

/** @copydoc ScopedButtonStyle::ScopedButtonStyle(const LauncherTheme &, ButtonStyleVariant) */
ScopedButtonStyle::ScopedButtonStyle(const LauncherTheme &theme, ButtonStyleVariant variant) {
  PushButtonVars(theme.rounding, theme.density, &m_styleCount);
  if (variant == ButtonStyleVariant::Primary) {
    PushButtonColorsPrimary(theme.palette, &m_colorCount);
  } else {
    PushButtonColorsSecondary(theme.palette, &m_colorCount);
  }
}

/** @copydoc ScopedButtonStyle::~ScopedButtonStyle */
ScopedButtonStyle::~ScopedButtonStyle() {
  ImGui::PopStyleColor(m_colorCount);
  ImGui::PopStyleVar(m_styleCount);
}

/** @copydoc TextMuted(const EditorTheme &, const char *) */
void TextMuted(const EditorTheme &theme, const char *text) { ImGui::TextColored(theme.palette.textMuted, "%s", text); }

/** @copydoc TextMuted(const LauncherTheme &, const char *) */
void TextMuted(const LauncherTheme &theme, const char *text) { ImGui::TextColored(theme.palette.textMuted, "%s", text); }

/** @copydoc DrawIcon */
void DrawIcon(ImDrawList *dl, const char *icon, ImVec2 pos,
              ImU32 color, float size) {
    ImFont *font = ImGui::GetFont();
    const float baseFontSize = ImGui::GetFontSize();
    const float targetSize = (size > 0.0f) ? size : baseFontSize;
    const float oldScale = font->Scale;
    font->Scale = targetSize / baseFontSize;
    const ImVec2 iconSz = ImGui::CalcTextSize(icon);
    dl->AddText(ImVec2(pos.x - iconSz.x * 0.5f, pos.y - iconSz.y * 0.5f), color, icon);
    font->Scale = oldScale;
}

/** @copydoc GetIconSize(const char *, float) */
ImVec2 GetIconSize(const char *icon, float size) {
    ImFont *font = ImGui::GetFont();
    const float baseFontSize = ImGui::GetFontSize();
    const float targetSize = (size > 0.0f) ? size : baseFontSize;
    const float oldScale = font->Scale;
    font->Scale = targetSize / baseFontSize;
    const ImVec2 sz = ImGui::CalcTextSize(icon);
    font->Scale = oldScale;
    return sz;
}

/** @copydoc GetIconSize(const EditorTheme &) */
float GetIconSize(const EditorTheme &theme) {
    return theme.density.iconSize > 0.0f ? theme.density.iconSize : ImGui::GetFontSize();
}

/** @copydoc GetIconSize(const LauncherTheme &) */
float GetIconSize(const LauncherTheme &theme) {
    return theme.density.iconSize > 0.0f ? theme.density.iconSize : ImGui::GetFontSize();
}

/** @copydoc SectionHeader(const EditorTheme &, const char *) */
void SectionHeader(const EditorTheme &theme, const char *title) {
  ImGui::TextColored(theme.palette.text, "%s", title);
  ImGui::Separator();
}

/** @copydoc SectionHeader(const LauncherTheme &, const char *) */
void SectionHeader(const LauncherTheme &theme, const char *title) {
  ImGui::TextColored(theme.palette.text, "%s", title);
  ImGui::Separator();
}

/** @copydoc RenderEditorSectionDivider */
void RenderEditorSectionDivider(const char *title) {
  ImGui::SeparatorText(title);
}

/** @copydoc Button(const EditorTheme &, ButtonStyleVariant, const char *, const ImVec2 &) */
bool Button(const EditorTheme &theme, ButtonStyleVariant variant,
            const char *label, const ImVec2 &size) {
  ScopedButtonStyle style(theme, variant);
  return ImGui::Button(label, size);
}

/** @copydoc Button(const LauncherTheme &, ButtonStyleVariant, const char *, const ImVec2 &) */
bool Button(const LauncherTheme &theme, ButtonStyleVariant variant,
            const char *label, const ImVec2 &size) {
  ScopedButtonStyle style(theme, variant);
  return ImGui::Button(label, size);
}

/** @copydoc RenderPrimaryButton */
bool RenderPrimaryButton(const LauncherTheme &theme, const char *label,
                         const ImVec2 &size) {
  return Button(theme, ButtonStyleVariant::Primary, label, size);
}

/** @copydoc RenderSecondaryButton */
bool RenderSecondaryButton(const LauncherTheme &theme, const char *label,
                            const ImVec2 &size) {
  return Button(theme, ButtonStyleVariant::Secondary, label, size);
}

/** @copydoc RenderRecentProjectButton */
bool RenderRecentProjectButton(const LauncherTheme &theme, const char *title,
                                const ImVec2 &size) {
  return Button(theme, ButtonStyleVariant::Secondary, title, size);
}

/** @copydoc RenderLabeledInput */
void RenderLabeledInput(const LauncherTheme &theme, const char *title,
                        const char *id, char *buffer, size_t bufferSize,
                        float inputWidth) {
  ImGui::TextDisabled("%s", title);
  ImGui::SetNextItemWidth(inputWidth);
  InputText(theme, id, buffer, bufferSize);
}

/** @copydoc InputText(const EditorTheme &, const char *, char *, size_t, ImGuiInputTextFlags) */
bool InputText(const EditorTheme &theme, const char *id, char *buffer,
               size_t bufferSize, ImGuiInputTextFlags flags) {
  ScopedInputStyle style(theme);
  return ImGui::InputText(id, buffer, bufferSize, flags);
}

/** @copydoc InputText(const LauncherTheme &, const char *, char *, size_t, ImGuiInputTextFlags) */
bool InputText(const LauncherTheme &theme, const char *id, char *buffer,
               size_t bufferSize, ImGuiInputTextFlags flags) {
  ScopedInputStyle style(theme);
  return ImGui::InputText(id, buffer, bufferSize, flags);
}

/** @copydoc ScopedComboStyle::ScopedComboStyle(const EditorTheme &) */
ScopedComboStyle::ScopedComboStyle(const EditorTheme &theme) {
  PushComboColors(theme.palette, &m_colorCount);
  PushComboVars(theme.rounding, theme.density, &m_styleCount);
}

/** @copydoc ScopedComboStyle::ScopedComboStyle(const LauncherTheme &) */
ScopedComboStyle::ScopedComboStyle(const LauncherTheme &theme) {
  PushComboColors(theme.palette, &m_colorCount);
  PushComboVars(theme.rounding, theme.density, &m_styleCount);
}

/** @copydoc ScopedComboStyle::~ScopedComboStyle */
ScopedComboStyle::~ScopedComboStyle() {
  ImGui::PopStyleVar(m_styleCount);
  ImGui::PopStyleColor(m_colorCount);
}

/** @copydoc InputTextWithHint(const EditorTheme &, const char *, const char *, char *, size_t, ImGuiInputTextFlags) */
bool InputTextWithHint(const EditorTheme &theme, const char *id,
                       const char *hint, char *buffer, size_t bufferSize,
                       ImGuiInputTextFlags flags) {
  ScopedInputStyle style(theme);
  return ImGui::InputTextWithHint(id, hint, buffer, bufferSize, flags);
}

/** @copydoc InputTextWithHint(const LauncherTheme &, const char *, const char *, char *, size_t, ImGuiInputTextFlags) */
bool InputTextWithHint(const LauncherTheme &theme, const char *id,
                       const char *hint, char *buffer, size_t bufferSize,
                       ImGuiInputTextFlags flags) {
  ScopedInputStyle style(theme);
  return ImGui::InputTextWithHint(id, hint, buffer, bufferSize, flags);
}

/** @copydoc Combo(const EditorTheme &, const char *, int *, const char *const[], int) */
bool Combo(const EditorTheme &theme, const char *label, int *currentItem,
           const char *const items[], int itemCount) {
  ScopedComboStyle style(theme);
  return ImGui::Combo(label, currentItem, items, itemCount);
}

/** @copydoc Combo(const LauncherTheme &, const char *, int *, const char *const[], int) */
bool Combo(const LauncherTheme &theme, const char *label, int *currentItem,
           const char *const items[], int itemCount) {
  ScopedComboStyle style(theme);
  return ImGui::Combo(label, currentItem, items, itemCount);
}

/** @copydoc GetEditorTreeRowMetrics */
EditorTreeRowMetrics GetEditorTreeRowMetrics(const EditorTheme & /*theme*/) {
  return EditorTreeRowMetrics{};
}

/** @copydoc ScopedEditorTreeRowStyle::ScopedEditorTreeRowStyle */
ScopedEditorTreeRowStyle::ScopedEditorTreeRowStyle(const EditorTheme &theme) {
  const EditorTreeRowMetrics metrics = GetEditorTreeRowMetrics(theme);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, metrics.framePadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, metrics.rounding);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, metrics.itemSpacing);
  m_styleCount += 3;
  PushTransparentHeaderColors(&m_colorCount);
}

/** @copydoc ScopedEditorTreeRowStyle::~ScopedEditorTreeRowStyle */
ScopedEditorTreeRowStyle::~ScopedEditorTreeRowStyle() {
  ImGui::PopStyleColor(m_colorCount);
  ImGui::PopStyleVar(m_styleCount);
}

/** @copydoc BeginEditorTreeRow */
EditorTreeRowState BeginEditorTreeRow(const EditorTheme &theme) {
  const EditorTreeRowMetrics metrics = GetEditorTreeRowMetrics(theme);
  const float windowLeft = ImGui::GetWindowPos().x;
  const float contentLeft = windowLeft + ImGui::GetWindowContentRegionMin().x;
  const float contentRight = windowLeft + ImGui::GetWindowContentRegionMax().x;
  const ImVec2 mousePos = ImGui::GetIO().MousePos;
  const ImVec2 rowStart = ImGui::GetCursorScreenPos();
  const float rowHeight = ImGui::GetFrameHeight();
  const bool windowHovered = ImGui::IsWindowHovered(
      ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

  return EditorTreeRowState{
      .start = rowStart,
      .height = rowHeight,
      .contentLeft = contentLeft,
      .contentRight = contentRight,
      .hovered = windowHovered &&
                 mousePos.x >= contentLeft - metrics.horizontalSlop &&
                 mousePos.x <= contentRight + metrics.horizontalSlop &&
                 mousePos.y >= rowStart.y &&
                 mousePos.y <= rowStart.y + rowHeight,
  };
}

/** @copydoc DrawEditorTreeRowBackground */
void DrawEditorTreeRowBackground(const EditorTheme &theme,
                                 const EditorTreeRowState &row,
                                 bool selected) {
  if (!selected && !row.hovered)
    return;

  const EditorTreeRowMetrics metrics = GetEditorTreeRowMetrics(theme);
  const ImVec4 fill = selected ? theme.palette.selection : theme.palette.selectionHover;
  ImGui::GetWindowDrawList()->AddRectFilled(
      ImVec2(row.contentLeft - metrics.horizontalSlop,
             row.start.y + metrics.verticalInset),
      ImVec2(row.contentRight + metrics.horizontalSlop,
             row.start.y + row.height - metrics.verticalInset),
      ImGui::ColorConvertFloat4ToU32(fill), metrics.rounding);
}

/** @copydoc EndFixedHeightEditorTreeRow */
void EndFixedHeightEditorTreeRow(const EditorTreeRowState &row) {
  const float cursorEndY = ImGui::GetCursorScreenPos().y;
  const float rowEndY = row.start.y + row.height;
  if (cursorEndY < rowEndY)
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowEndY - cursorEndY));
}

/** @copydoc RenderEditorTreeSearchSlot */
bool RenderEditorTreeSearchSlot(const EditorTheme &theme,
                                const EditorTreeSearchSlotConfig &config) {
  if (!config.enabled || config.buffer == nullptr || config.bufferSize == 0)
    return false;

  if (config.width > 0.0f)
    ImGui::SetNextItemWidth(config.width);
  ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
  bool changed = InputTextWithHint(theme, config.id, config.placeholder,
                                   config.buffer, config.bufferSize,
                                   config.flags);
  ImGui::PopItemFlag();

  if (config.showFilterIcon && config.buffer != nullptr) {
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
    if (config.buffer[0] != '\0') {
      ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
      if (ImGui::SmallButton(ICON_FA_XMARK)) {
        config.buffer[0] = '\0';
        changed = true;
      }
      ImGui::PopStyleColor();
    } else {
      ImGui::TextDisabled("%s", ICON_FA_FILTER);
    }
  }

  return changed;
}

/** @copydoc DrawEditorTreeItem */
EditorTreeItemResult DrawEditorTreeItem(const EditorTheme &theme,
                                        const EditorTreeItemSpec &item) {
  EditorTreeItemResult result;
  result.row = BeginEditorTreeRow(theme);
  DrawEditorTreeRowBackground(theme, result.row, item.selected);

  std::string displayLabel = item.label == nullptr ? "" : item.label;
  if (item.prefixIcon != nullptr && item.prefixIcon[0] != '\0') {
    displayLabel = item.prefixIcon;
    if (item.label != nullptr && item.label[0] != '\0') {
      displayLabel += " ";
      displayLabel += item.label;
    }
  }

  const ImVec4 *textColor =
      result.row.hovered && item.hoveredTextColor != nullptr
          ? item.hoveredTextColor
          : item.normalTextColor;
  const bool pushedTextColor = textColor != nullptr;
  if (pushedTextColor)
    ImGui::PushStyleColor(ImGuiCol_Text, *textColor);

  if (item.kind == EditorTreeItemKind::Node) {
    ImGui::AlignTextToFramePadding();
    const char *treeId = item.id != nullptr ? item.id : displayLabel.c_str();
    result.open = ImGui::TreeNodeEx(treeId, item.treeFlags, "%s",
                                    displayLabel.c_str());
  } else {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(displayLabel.c_str());
    EndFixedHeightEditorTreeRow(result.row);
  }

  if (pushedTextColor)
    ImGui::PopStyleColor();

  if (item.suffixIcon != nullptr && item.suffixIcon[0] != '\0') {
    const float buttonSize = std::max(1.0f, result.row.height - 4.0f);
    const ImVec2 cursorBackup = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(result.row.contentRight - buttonSize,
                                     result.row.start.y +
                                         (result.row.height - buttonSize) * 0.5f));
    ImGui::PushID(item.id != nullptr ? item.id : displayLabel.c_str());
    result.suffixClicked =
        EditorHeaderIconButton(theme, item.suffixIcon,
                               ImVec2(buttonSize, buttonSize));
    ImGui::PopID();
    ImGui::SetCursorScreenPos(cursorBackup);
  }

  return result;
}

/** @copydoc EditorHeaderIconButton */
bool EditorHeaderIconButton(const EditorTheme &theme, const char *label,
                            const ImVec2 &size) {
  int colorCount = 0;
  PushTransparentButtonColors(theme.palette, &colorCount);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 0.0f));
  const bool clicked = ImGui::Button(label, size);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(colorCount);
  return clicked;
}

/** @copydoc ErrorText */
void ErrorText(const EditorTheme &theme, const char *text) {
  ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.destructive);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

/** @copydoc EditorPanelTabLabel */
const char *EditorPanelTabLabel(EditorPanelTab tab) {
  using enum EditorPanelTab;
  switch (tab) {
  case Scene:
    return "Scene";
  case Project:
    return "Project";
  case Viewport:
    return "Viewport";
  case Assets:
    return "Assets";
  case Console:
    return "Console";
  case Animation:
    return "Animation";
  case MCP:
    return "MCP";
  }
  return "Unknown";
}

namespace {
/** @brief Renders the tab buttons in the panel top bar. Returns clicked tab index or -1. */
int RenderPanelTabs(const EditorTheme& theme,
                    std::span<const EditorPanelTabItem> tabs,
                    float maxTabRight,
                    float kTabHorizontalPadding,
                    float kTabVerticalPadding,
                    float kTabSpacing,
                    float kUnderlineThickness) {
  int clickedTab = -1;
  for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
    if (i > 0)
      ImGui::SameLine(0.0f, kTabSpacing);

    const EditorPanelTabItem &item = tabs[i];
    const char *label = EditorPanelTabLabel(item.tab);
    const float tabWidth = ImGui::CalcTextSize(label).x + kTabHorizontalPadding * 2.0f;
    if (const float cursorX = ImGui::GetCursorPosX();
        cursorX >= maxTabRight || cursorX + tabWidth > maxTabRight)
      break;

    ImGui::PushID(i);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.cardHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,
        item.selected ? theme.palette.text : theme.palette.textMuted);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(kTabHorizontalPadding, kTabVerticalPadding));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

    const ImVec2 btnPos = ImGui::GetCursorScreenPos();
    if (ImGui::Button(label, ImVec2(tabWidth, 0.0f)))
      clickedTab = i;
    if (item.selected) {
      const ImVec2 btnMax = ImGui::GetItemRectMax();
      ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(btnPos.x, btnMax.y - kUnderlineThickness),
          ImVec2(btnMax.x, btnMax.y),
          ImGui::ColorConvertFloat4ToU32(theme.palette.accent));
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopID();
  }
  return clickedTab;
}

/** @brief Renders the action buttons in the panel top bar. Returns clicked action index or -1. */
int RenderPanelActions(const EditorTheme& theme,
                       std::span<const EditorPanelActionItem> actions,
                       float actionButtonSize,
                       float kActionSpacing) {
  int clickedAction = -1;
  int renderedCount = 0;
  for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
    const EditorPanelActionItem &action = actions[i];
    if (!action.enabled)
      continue;
    if (renderedCount > 0)
      ImGui::SameLine(0.0f, kActionSpacing);
    ++renderedCount;

    ImGui::PushID(i);
    std::string btnLabel;
    if (action.icon)
      btnLabel = action.icon;
    if (action.text && action.text[0] != '\0') {
      btnLabel += "  ";
      btnLabel += action.text;
    }

    if (const bool clicked =
            EditorHeaderIconButton(theme, btnLabel.c_str(), ImVec2(actionButtonSize, 0.0f));
        clicked && !action.dropdown.empty()) {
      ImGui::OpenPopup(std::format("##action_popup_{}", i).c_str());
    } else if (clicked) {
      clickedAction = i;
    }

    if (!action.dropdown.empty()) {
      const std::string popupId = std::format("##action_popup_{}", i);
      if (ImGui::BeginPopup(popupId.c_str())) {
        RenderEditorDropdownItems(action.dropdown);
        ImGui::EndPopup();
      }
    }
    ImGui::PopID();
  }
  return clickedAction;
}
}  // namespace

/** @copydoc RenderEditorPanelTopBar */
EditorPanelTopBarResult RenderEditorPanelTopBar(
    const EditorTheme &theme, const char *id,
    std::span<const EditorPanelTabItem> tabs,
    std::span<const EditorPanelActionItem> actions) {
  constexpr float kTabHorizontalPadding = 12.0f;
  constexpr float kTabVerticalPadding = 6.0f;
  constexpr float kTabSpacing = 2.0f;
  constexpr float kActionSpacing = 6.0f;
  constexpr float kHeaderBottomPadding = 8.0f;
  constexpr float kUnderlineThickness = 2.0f;

  EditorPanelTopBarResult result;
  const float headerY = ImGui::GetCursorPosY();
  const float actionButtonSize = ImGui::GetTextLineHeight() + 4.0f;

  int enabledActionCount = 0;
  for (const auto &a : actions)
    if (a.enabled) ++enabledActionCount;

  const float actionsWidth = enabledActionCount == 0
                                 ? 0.0f
                                 : actionButtonSize * static_cast<float>(enabledActionCount) +
                                       kActionSpacing *
                                           static_cast<float>(enabledActionCount - 1);
  const float rightEdge = ImGui::GetWindowContentRegionMax().x;
  const float maxTabRight = enabledActionCount == 0
                                ? rightEdge
                                : rightEdge - actionsWidth - kActionSpacing;

  ImGui::PushID(id);

  // Draw full-width gray baseline
  {
    const ImVec2 tabOrigin   = ImGui::GetCursorScreenPos();
    const float tabRowBottom = tabOrigin.y + ImGui::GetFontSize() + kTabVerticalPadding * 2.0f;
    const float winX         = ImGui::GetWindowPos().x;
    const float dividerLeft  = winX + ImGui::GetWindowContentRegionMin().x;
    const float dividerRight = winX + ImGui::GetWindowContentRegionMax().x;
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(dividerLeft,  tabRowBottom - kUnderlineThickness),
        ImVec2(dividerRight, tabRowBottom),
        ImGui::ColorConvertFloat4ToU32(theme.palette.border));
  }

  result.clickedTabIndex = RenderPanelTabs(theme, tabs, maxTabRight,
      kTabHorizontalPadding, kTabVerticalPadding, kTabSpacing, kUnderlineThickness);

  if (enabledActionCount > 0) {
    ImGui::SetCursorPos(ImVec2(rightEdge - actionsWidth, headerY + 1.0f));
    result.clickedActionIndex = RenderPanelActions(theme, actions, actionButtonSize, kActionSpacing);
  }

  ImGui::PopID();

  if (const float minCursorY = headerY + ImGui::GetFrameHeight() + kHeaderBottomPadding;
      ImGui::GetCursorPosY() < minCursorY)
    ImGui::SetCursorPosY(minCursorY);

  return result;
}

/** @copydoc BeginEditorModal */
bool BeginEditorModal(const EditorModalConfig& cfg, bool openThisFrame) {
    if (openThisFrame && cfg.id)
        ImGui::OpenPopup(cfg.id);
    if (cfg.width > 0.0f) {
        ImGui::SetNextWindowSize(ImVec2(cfg.width, 0.0f), ImGuiCond_Appearing);
        // Center horizontally; vertical position will adjust with auto-resize
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(ImVec2(center.x - cfg.width * 0.5f, center.y * 0.35f),
                                ImGuiCond_Always);
    }
    const ImGuiWindowFlags flags = cfg.autoResize
        ? ImGuiWindowFlags_AlwaysAutoResize
        : ImGuiWindowFlags_None;
    return ImGui::BeginPopupModal(cfg.id, nullptr, flags);
}

/** @copydoc EndEditorModal */
void EndEditorModal() {
    ImGui::EndPopup();
}

/** @copydoc RenderModalTitleBar */
bool RenderModalTitleBar(const EditorTheme &theme, const char *title) {
    constexpr float kCloseButtonSize = 22.0f;

    // Title on the left
    ImGui::TextUnformatted(title);

    // Close button on the right
    const float winRight = ImGui::GetWindowContentRegionMax().x;
    const float btnX = winRight - kCloseButtonSize;
    ImGui::SameLine(btnX);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.destructive);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImVec4(theme.palette.destructive.x - 0.1f,
               theme.palette.destructive.y,
               theme.palette.destructive.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));

    const bool clicked = ImGui::Button(ICON_FA_XMARK, ImVec2(kCloseButtonSize, kCloseButtonSize));

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    return clicked;
}

/** @copydoc RenderEditorModalFooter */
EditorModalFooterResult RenderEditorModalFooter(
    const EditorTheme& theme,
    const char* confirmLabel,
    EditorModalFooterStyle style,
    const char* alternateLabel,
    float buttonWidth) {

    EditorModalFooterResult result;
    ImGui::Spacing();

    if (style == EditorModalFooterStyle::ThreeWay && alternateLabel) {
        int nc = 0; int nv = 0;
        PushButtonColorsSecondary(theme.palette, &nc);
        PushButtonVars(theme.rounding, theme.density, &nv);
        if (ImGui::Button(alternateLabel, ImVec2(buttonWidth, 0.0f))) {
            result.alternate = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(nc);
        ImGui::PopStyleVar(nv);
        ImGui::SameLine();
    }

    {
        int nc = 0; int nv = 0;
        PushButtonColorsSecondary(theme.palette, &nc);
        PushButtonVars(theme.rounding, theme.density, &nv);
        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f))) {
            result.cancelled = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(nc);
        ImGui::PopStyleVar(nv);
    }

    ImGui::SameLine();

    {
        int nc = 0; int nv = 0;
        if (style == EditorModalFooterStyle::DestructiveCancel) {
            ImGui::PushStyleColor(ImGuiCol_Button,        theme.palette.destructive);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(theme.palette.destructive.x + 0.1f,
                       theme.palette.destructive.y,
                       theme.palette.destructive.z,
                       1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                ImVec4(theme.palette.destructive.x - 0.05f,
                       theme.palette.destructive.y,
                       theme.palette.destructive.z,
                       1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
            nc = 4;
            PushButtonVars(theme.rounding, theme.density, &nv);
        } else {
            PushButtonColorsPrimary(theme.palette, &nc);
            PushButtonVars(theme.rounding, theme.density, &nv);
        }
        if (ImGui::Button(confirmLabel, ImVec2(buttonWidth, 0.0f))) {
            result.confirmed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(nc);
        ImGui::PopStyleVar(nv);
    }

    return result;
}

/** @copydoc BeginEditorPickerModal */
bool BeginEditorPickerModal(const EditorPickerConfig& cfg,
                            bool openThisFrame,
                            char* queryBuf, size_t queryBufSize) {
    if (openThisFrame && cfg.id)
        ImGui::OpenPopup(cfg.id);
    if (cfg.width > 0.0f)
        ImGui::SetNextWindowSize(ImVec2(cfg.width, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(cfg.id, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return false;
    if (cfg.prompt)
        ImGui::TextDisabled("%s", cfg.prompt);
    ImGui::SetNextItemWidth(cfg.width - 32.0f);
    const char* hint = cfg.fieldHint ? cfg.fieldHint : "Search...";
    ImGui::InputTextWithHint("##picker_query", hint, queryBuf, queryBufSize);
    ImGui::Separator();
    ImGui::BeginChild("##picker_scroll", ImVec2(0.0f, 240.0f), false);
    return true;
}

/** @copydoc EditorPickerModalRow */
bool EditorPickerModalRow(const char* label, bool selected) {
    return ImGui::Selectable(label, selected);
}

/** @copydoc EndEditorPickerModal */
void EndEditorPickerModal(bool& openFlag, std::string* query) {
    ImGui::EndChild();
    ImGui::Separator();
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        openFlag = false;
        if (query) query->clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ── Group B: Input field primitives ──────────────────────────────────────────

/** @copydoc RenderEditorLabeledInput */
bool RenderEditorLabeledInput(const char* label, const char* id,
                              char* buf, size_t bufSize,
                              float width, const char* hint) {
    if (label)
        ImGui::TextDisabled("%s", label);
    if (width > 0.0f)
        ImGui::SetNextItemWidth(width);
    else
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (hint)
        return ImGui::InputTextWithHint(id, hint, buf, bufSize);
    return ImGui::InputText(id, buf, bufSize);
}

/** @copydoc RenderEditorCheckbox */
bool RenderEditorCheckbox(const EditorTheme& theme, const char* label,
                          bool& value, const char* tooltip) {
    ImGui::PushStyleColor(ImGuiCol_CheckMark, theme.palette.accent);
    const bool changed = ImGui::Checkbox(label, &value);
    ImGui::PopStyleColor();
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    return changed;
}

/** @copydoc RenderEditorToggle */
bool RenderEditorToggle(const EditorTheme& theme, const char* id,
                        const char* label, bool& value) {
    constexpr float pillW  = 36.0f;
    constexpr float pillH  = 18.0f;
    constexpr float radius = pillH * 0.5f;

    ImGui::InvisibleButton(id, ImVec2(pillW, pillH));
    const bool clicked = ImGui::IsItemClicked();
    if (clicked)
        value = !value;

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();

    const ImVec4& bg = value ? theme.palette.accent : theme.palette.panelSoft;
    dl->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(bg), radius);
    dl->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(theme.palette.border),
                radius, 0, 1.0f);

    const float cx = value ? max.x - radius : min.x + radius;
    const float cy = min.y + radius;
    dl->AddCircleFilled(ImVec2(cx, cy), radius - 3.0f,
                        ImGui::ColorConvertFloat4ToU32(theme.palette.text));

    if (label && label[0] != '\0') {
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextUnformatted(label);
    }

    return clicked;
}

/** @copydoc RenderEditorDragFloat */
bool RenderEditorDragFloat(const char* label, const char* id,
                           float& value,
                           float speed,
                           const DragFloatOptions& options) {
    if (label)
        ImGui::TextDisabled("%s", label);
    const float w = (options.width > 0.0f) ? options.width : ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(w);
    return ImGui::DragFloat(id, &value, speed, options.vmin, options.vmax, options.fmt);
}

/** @copydoc RenderEditorDragFloat3 */
bool RenderEditorDragFloat3(const char* label, const char* id,
                            float value[3],
                            float speed,
                            const DragFloatOptions& options) {
    if (label)
        ImGui::TextDisabled("%s", label);
    const float w = (options.width > 0.0f) ? options.width : ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(w);
    return ImGui::DragFloat3(id, value, speed, options.vmin, options.vmax, options.fmt);
}

/** @copydoc RenderEditorSliderFloat */
bool RenderEditorSliderFloat(const char* label, const char* id,
                             float& value, float vmin, float vmax,
                             const char* fmt, float width) {
    if (label)
        ImGui::TextDisabled("%s", label);
    const float w = (width > 0.0f) ? width : ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(w);
    return ImGui::SliderFloat(id, &value, vmin, vmax, fmt);
}

/** @copydoc RenderEditorColorEdit3 */
bool RenderEditorColorEdit3(const char* label, const char* id,
                            float color[3], float width) {
    if (label)
        ImGui::TextDisabled("%s", label);
    const float w = (width > 0.0f) ? width : ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(w);
    return ImGui::ColorEdit3(id, color);
}

/** @copydoc BeginEditorPropertyRow */
bool BeginEditorPropertyRow(const char* label, float labelWidth) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelWidth);
    return true;
}

/** @copydoc EndEditorPropertyRow */
void EndEditorPropertyRow() {
    // Intentionally empty; exists for RAII symmetry and future use.
}

/** @copydoc BeginEditorCard */
bool BeginEditorCard(const EditorTheme& theme, EditorCardConfig& cfg) {
    if (cfg.selected)
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
            ImVec4(theme.palette.selection.x, theme.palette.selection.y,
                   theme.palette.selection.z, 0.70f));
    const bool open = ImGui::BeginChild(
        cfg.id, ImVec2(cfg.width, cfg.height),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (cfg.selected)
        ImGui::PopStyleColor();
    cfg.hovered = ImGui::IsWindowHovered();
    return open;
}

/** @copydoc EndEditorCard */
void EndEditorCard() {
    ImGui::EndChild();
}

/** @copydoc RenderEditorStatusText */
void RenderEditorStatusText(const EditorTheme& theme,
                            EditorStatusLevel level,
                            const char* text) {
    using enum EditorStatusLevel;
    ImVec4 color;
    switch (level) {
        case Info:    color = theme.palette.textMuted;              break;
        case Warning: color = ImVec4(1.0f, 0.80f, 0.30f, 1.0f);   break;
        case Error:   color = theme.palette.destructive;            break;
        case Success: color = ImVec4(0.35f, 0.85f, 0.50f, 1.0f);  break;
    }
    ImGui::TextColored(color, "%s", text);
}

// ─── Group D: Settings modal primitives ──────────────────────────────────────

namespace {
/** @brief Renders a single vertical tab row. Returns true if clicked. */
bool RenderVerticalTabRow(const EditorTheme& theme,
                          const EditorVerticalTabItem& item,
                          float availWidth) {
    constexpr float kRowHeight = 54.0f;
    constexpr float kRowPaddingX = 10.0f;
    constexpr float kRowPaddingY = 8.0f;

    const char* tabId = item.id ? item.id : "";
    ImGui::PushID(tabId);

    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    const ImVec2 rowSize(availWidth, kRowHeight);

    ImGui::InvisibleButton("##vtab_hit", rowSize);
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 bg;
    if (item.selected)
        bg = theme.palette.selection;
    else if (hovered)
        bg = theme.palette.cardHover;
    else
        bg = ImVec4(0, 0, 0, 0);
    if (bg.w > 0.0f) {
        dl->AddRectFilled(
            rowStart,
            ImVec2(rowStart.x + rowSize.x, rowStart.y + rowSize.y),
            ImGui::ColorConvertFloat4ToU32(bg),
            theme.rounding.button);
    }

    if (item.selected) {
        dl->AddRectFilled(
            rowStart,
            ImVec2(rowStart.x + 3.0f, rowStart.y + rowSize.y),
            ImGui::ColorConvertFloat4ToU32(theme.palette.accent),
            theme.rounding.button);
    }

    float textX = rowStart.x + kRowPaddingX;
    const float textY = rowStart.y + kRowPaddingY;

    if (item.icon && item.icon[0] != '\0') {
        const ImVec2 iconSize = ImGui::CalcTextSize(item.icon);
        dl->AddText(
            ImVec2(textX, textY),
            ImGui::ColorConvertFloat4ToU32(
                item.selected ? theme.palette.text : theme.palette.textMuted),
            item.icon);
        textX += iconSize.x + 8.0f;
    }

    if (item.label && item.label[0] != '\0') {
        dl->AddText(
            ImVec2(textX, textY),
            ImGui::ColorConvertFloat4ToU32(theme.palette.text),
            item.label);
    }

    if (item.description && item.description[0] != '\0') {
        dl->AddText(
            ImVec2(textX, textY + ImGui::GetFontSize() + 2.0f),
            ImGui::ColorConvertFloat4ToU32(theme.palette.textMuted),
            item.description);
    }

    ImGui::PopID();
    return clicked;
}
}  // namespace

/** @copydoc RenderEditorVerticalTabs */
EditorVerticalTabResult RenderEditorVerticalTabs(
    const EditorTheme& theme,
    const char* id,
    std::span<const EditorVerticalTabItem> tabs,
    float width) {

    EditorVerticalTabResult result;
    if (!id || tabs.empty() || width <= 0.0f)
        return result;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.palette.panelSoft);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 4.0f));
    ImGui::BeginChild(id, ImVec2(width, 0.0f), ImGuiChildFlags_Borders);

    const float availWidth = ImGui::GetContentRegionAvail().x;

    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        if (RenderVerticalTabRow(theme, tabs[i], availWidth))
            result.clickedIndex = i;
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    return result;
}

/** @copydoc BeginEditorSettingsCard */
bool BeginEditorSettingsCard(const EditorTheme& theme,
                             const char* id,
                             const char* title) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.palette.card);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.palette.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.rounding.card);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    const bool visible = ImGui::BeginChild(
        id ? id : "##settings_card",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    if (visible && title && title[0] != '\0') {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    return visible;
}

/** @copydoc EndEditorSettingsCard */
void EndEditorSettingsCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

/** @copydoc RenderEditorSettingText */
void RenderEditorSettingText(const EditorTheme& theme,
                             const char* label,
                             const char* description) {
    if (label && label[0] != '\0') {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
    }
    if (description && description[0] != '\0') {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
        ImGui::TextWrapped("%s", description);
        ImGui::PopStyleColor();
    }
}

/** @copydoc RenderEditorSettingsFooter */
EditorSettingsFooterResult RenderEditorSettingsFooter(
    const EditorTheme& theme,
    bool canApply,
    float buttonWidth) {

    EditorSettingsFooterResult result;

    // Right-align: compute total width then SameLine-advance to align right edge.
    constexpr float kButtonSpacing = 8.0f;
    const float totalWidth = buttonWidth * 3.0f + kButtonSpacing * 2.0f;
    if (const float avail = ImGui::GetContentRegionAvail().x; avail > totalWidth)
        ImGui::Dummy(ImVec2(avail - totalWidth, 0.0f));
    ImGui::SameLine();

    // Cancel (secondary)
    {
        int nc = 0; int nv = 0;
        PushButtonColorsSecondary(theme.palette, &nc);
        PushButtonVars(theme.rounding, theme.density, &nv);
        if (ImGui::Button("Cancel##settings_test/footer_cancel",
                          ImVec2(buttonWidth, 0.0f))) {
            result.cancelled = true;
        }
        ImGui::PopStyleColor(nc);
        ImGui::PopStyleVar(nv);
    }
    ImGui::SameLine(0.0f, kButtonSpacing);

    // Apply (secondary, disabled when canApply == false)
    {
        int nc = 0; int nv = 0;
        PushButtonColorsSecondary(theme.palette, &nc);
        PushButtonVars(theme.rounding, theme.density, &nv);
        ImGui::BeginDisabled(!canApply);
        if (ImGui::Button("Apply##settings_test/footer_apply",
                          ImVec2(buttonWidth, 0.0f))) {
            result.applied = true;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor(nc);
        ImGui::PopStyleVar(nv);
    }
    ImGui::SameLine(0.0f, kButtonSpacing);

    // OK (primary)
    {
        int nc = 0; int nv = 0;
        PushButtonColorsPrimary(theme.palette, &nc);
        PushButtonVars(theme.rounding, theme.density, &nv);
        if (ImGui::Button("OK##settings_test/footer_ok",
                          ImVec2(buttonWidth, 0.0f))) {
            result.accepted = true;
        }
        ImGui::PopStyleColor(nc);
        ImGui::PopStyleVar(nv);
    }

    return result;
}

} // namespace Horo::Ui
