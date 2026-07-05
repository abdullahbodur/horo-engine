/**
 * @file UiComponents.cpp
 * @brief Implementations for reusable themed ImGui primitives used by Horo UI.
 */
#include "ui/UiComponents.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>

#include "ui/IconsFontAwesome6.h"

namespace Horo::Ui {
namespace {

/** @brief Builds the visible label for a header action button. */
std::string BuildPanelActionLabel(const EditorPanelActionItem &action) {
  std::string label;
  if (action.icon && action.icon[0] != '\0')
    label = action.icon;
  if (action.text && action.text[0] != '\0') {
    if (!label.empty())
      label += "  ";
    label += action.text;
  }
  return label;
}

/** @brief Returns the themed width for a header action button. */
float MeasurePanelActionWidth(const EditorPanelActionItem &action,
                              float minButtonSize) {
  const std::string label = BuildPanelActionLabel(action);
  if (label.empty())
    return minButtonSize;
  return std::max(minButtonSize, ImGui::CalcTextSize(label.c_str()).x + 14.0f);
}

/** @brief Returns the visible part of an ImGui label before any hidden ID suffix. */
std::string_view VisibleImGuiLabel(const char *label) {
  if (!label || label[0] == '\0')
    return {};
  const std::string_view view(label);
  const size_t hiddenIdPos = view.find("##");
  return hiddenIdPos == std::string_view::npos ? view : view.substr(0, hiddenIdPos);
}

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
    const ImVec2 iconSz = font->CalcTextSizeA(targetSize, FLT_MAX, 0.0f, icon);
    dl->AddText(font, targetSize,
                ImVec2(pos.x - iconSz.x * 0.5f, pos.y - iconSz.y * 0.5f),
                color, icon);
}

/** @copydoc GetIconSize(const char *, float) */
ImVec2 GetIconSize(const char *icon, float size) {
    ImFont *font = ImGui::GetFont();
    const float baseFontSize = ImGui::GetFontSize();
    const float targetSize = (size > 0.0f) ? size : baseFontSize;
    return font->CalcTextSizeA(targetSize, FLT_MAX, 0.0f, icon);
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

/** @copydoc InputTextWithLeadingIcon */
bool InputTextWithLeadingIcon(const EditorTheme &theme, const char *id,
                              const char *icon, const char *hint,
                              char *buffer, size_t bufferSize,
                              ImGuiInputTextFlags flags) {
  ScopedInputStyle style(theme);
  const float iconSize = GetIconSize(theme);
  const float iconGap = std::max(6.0f, theme.density.itemSpacing);
  const ImVec2 basePadding = theme.density.inputPadding;
  ImGui::PushStyleVar(
      ImGuiStyleVar_FramePadding,
      ImVec2(basePadding.x + iconSize + iconGap, basePadding.y));
  const bool changed =
      ImGui::InputTextWithHint(id, hint, buffer, bufferSize, flags);
  ImGui::PopStyleVar();

  if (icon && icon[0] != '\0') {
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 iconCenter(min.x + basePadding.x + iconSize * 0.5f,
                            (min.y + max.y) * 0.5f);
    DrawIcon(ImGui::GetWindowDrawList(), icon, iconCenter,
             ImGui::GetColorU32(theme.palette.textMuted), iconSize);
  }

  return changed;
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

namespace {
std::string BuildMultiSelectPreview(int selectedCount, const char *singleSelectedLabel, const char *allLabel) {
  if (selectedCount == 0) return allLabel ? allLabel : "All";
  if (selectedCount == 1) return singleSelectedLabel ? singleSelectedLabel : "1 selected";
  return std::format("{} selected", selectedCount);
}

bool HandleMultiSelectAllOption(std::span<MultiSelectDropdownItem> items, const char* allLabel, bool allSelected) {
  bool changed = false;
  if (ImGui::Selectable(allLabel ? allLabel : "All", allSelected)) {
    for (const MultiSelectDropdownItem &item : items) {
      if (item.selected && *item.selected) {
        *item.selected = false;
        changed = true;
      }
    }
  }
  if (allSelected)
    ImGui::SetItemDefaultFocus();
  return changed;
}

bool HandleMultiSelectItems(std::span<MultiSelectDropdownItem> items) {
  bool changed = false;
  ImGui::Separator();
  for (const MultiSelectDropdownItem &item : items) {
    if (!item.selected)
      continue;
    if (const bool selected = *item.selected;
        ImGui::Selectable(item.label, selected, ImGuiSelectableFlags_DontClosePopups)) {
      *item.selected = !selected;
      changed = true;
    }
    if (*item.selected)
      ImGui::SetItemDefaultFocus();
  }
  return changed;
}
} // namespace

/** @copydoc MultiSelectDropdown */
bool MultiSelectDropdown(const EditorTheme &theme, const char *id,
                         std::span<MultiSelectDropdownItem> items,
                         const char *allLabel) {
  int selectedCount = 0;
  const char *singleSelectedLabel = nullptr;
  for (const MultiSelectDropdownItem &item : items) {
    if (item.selected && *item.selected) {
      ++selectedCount;
      singleSelectedLabel = item.label;
    }
  }

  const std::string preview = BuildMultiSelectPreview(selectedCount, singleSelectedLabel, allLabel);
  bool changed = false;
  ScopedComboStyle style(theme);
  
  if (ImGui::BeginCombo(id, preview.c_str())) {
    changed |= HandleMultiSelectAllOption(items, allLabel, selectedCount == 0);
    changed |= HandleMultiSelectItems(items);
    ImGui::EndCombo();
  }
  return changed;
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
  bool changed = config.showFilterIcon
                     ? InputTextWithLeadingIcon(
                           theme, config.id, ICON_FA_MAGNIFYING_GLASS,
                           config.placeholder, config.buffer,
                           config.bufferSize, config.flags)
                     : InputTextWithHint(theme, config.id, config.placeholder,
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
  case Favorites:
    return "Favorites";
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
    std::string btnLabel = BuildPanelActionLabel(action);
    if (btnLabel.empty())
      btnLabel = "##action";
    const float buttonWidth = MeasurePanelActionWidth(action, actionButtonSize);

    if (const bool clicked =
            EditorHeaderIconButton(theme, btnLabel.c_str(), ImVec2(buttonWidth, 0.0f));
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
  float actionsWidth = 0.0f;
  for (const auto &a : actions) {
    if (!a.enabled)
      continue;
    if (enabledActionCount > 0)
      actionsWidth += kActionSpacing;
    actionsWidth += MeasurePanelActionWidth(a, actionButtonSize);
    ++enabledActionCount;
  }
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

/** @copydoc RenderEditorSectionBar */
EditorSectionBarResult RenderEditorSectionBar(
    const EditorTheme &theme, const char *id, const char *title,
    std::span<const EditorPanelActionItem> actions) {
  constexpr float kHorizontalPadding = 14.0f;
  constexpr float kVerticalPadding = 8.0f;
  constexpr float kActionSpacing = 6.0f;

  EditorSectionBarResult result;
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  const float contentMinX =
      ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x;
  const float contentMaxX =
      ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  const float width = std::max(0.0f, contentMaxX - contentMinX);
  const float height = ImGui::GetTextLineHeight() + kVerticalPadding * 2.0f;

  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(ImVec2(contentMinX, cursor.y),
                          ImVec2(contentMaxX, cursor.y + height),
                          ImGui::GetColorU32(theme.palette.panel));
  drawList->AddRectFilled(ImVec2(contentMinX, cursor.y + height - 1.0f),
                          ImVec2(contentMaxX, cursor.y + height),
                          ImGui::GetColorU32(theme.palette.border));

  ImGui::PushID(id);
  ImGui::SetCursorScreenPos(
      ImVec2(contentMinX + kHorizontalPadding, cursor.y + kVerticalPadding));
  ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
  ImGui::TextUnformatted(title ? title : "");
  ImGui::PopStyleColor();

  int enabledActionCount = 0;
  float actionsWidth = 0.0f;
  const float actionButtonSize = ImGui::GetTextLineHeight() + 4.0f;
  for (const auto &action : actions) {
    if (!action.enabled)
      continue;
    if (enabledActionCount > 0)
      actionsWidth += kActionSpacing;
    actionsWidth += MeasurePanelActionWidth(action, actionButtonSize);
    ++enabledActionCount;
  }

  if (enabledActionCount > 0) {
    const float actionY = cursor.y + (height - ImGui::GetFrameHeight()) * 0.5f;
    ImGui::SetCursorScreenPos(
        ImVec2(contentMaxX - actionsWidth - kHorizontalPadding, actionY));
    result.clickedActionIndex =
        RenderPanelActions(theme, actions, actionButtonSize, kActionSpacing);
  }

  ImGui::PopID();
  ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + height));
  ImGui::Dummy(ImVec2(width, 0.0f));
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
    InputTextWithLeadingIcon(GetEditorTheme(), "##picker_query",
                             ICON_FA_MAGNIFYING_GLASS, hint, queryBuf,
                             queryBufSize);
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
    const char* safeLabel = label ? label : "##checkbox";
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float frameHeight = ImGui::GetFrameHeight();
    const float boxSize = std::round(frameHeight * 0.55f);
    const float yOffset = std::max(0.0f, (frameHeight - boxSize) * 0.5f);

    ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + yOffset));
    ImGui::InvisibleButton(safeLabel, ImVec2(boxSize, boxSize));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if (clicked)
        value = !value;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImVec4 bgColor = theme.palette.input;
    if (value)
        bgColor = theme.palette.inputActive;
    else if (hovered)
        bgColor = theme.palette.inputHover;
    const ImU32 bg = ImGui::GetColorU32(bgColor);
    const ImU32 border = ImGui::GetColorU32(theme.palette.border);
    drawList->AddRectFilled(min, max, bg, theme.rounding.input);
    drawList->AddRect(min, max, border, theme.rounding.input, 0, 1.0f);

    if (value) {
        const ImU32 check = ImGui::GetColorU32(theme.palette.accent);
        const float thickness = std::max(2.0f, boxSize * 0.12f);
        const ImVec2 p0(min.x + boxSize * 0.24f, min.y + boxSize * 0.54f);
        const ImVec2 p1(min.x + boxSize * 0.43f, min.y + boxSize * 0.72f);
        const ImVec2 p2(min.x + boxSize * 0.78f, min.y + boxSize * 0.28f);
        drawList->AddLine(p0, p1, check, thickness);
        drawList->AddLine(p1, p2, check, thickness);
    }

    if (const auto visibleLabel = VisibleImGuiLabel(label); !visibleLabel.empty()) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, cursor.y));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(visibleLabel.data(),
                               visibleLabel.data() + visibleLabel.size());
    }

    if (tooltip && hovered)
        ImGui::SetTooltip("%s", tooltip);
    return clicked;
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
    int colorCount = 0;
    int styleCount = 0;
    const ImVec2 padding = (cfg.padding.x >= 0.0f && cfg.padding.y >= 0.0f)
        ? cfg.padding
        : theme.density.cardPadding;
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          cfg.selected
                              ? ImVec4(theme.palette.selection.x,
                                       theme.palette.selection.y,
                                       theme.palette.selection.z, 0.70f)
                              : theme.palette.card);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.palette.border);
    colorCount += 2;
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.rounding.card);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    styleCount += 2;
    const bool open = ImGui::BeginChild(
        cfg.id, ImVec2(cfg.width, cfg.height),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(styleCount);
    ImGui::PopStyleColor(colorCount);
    cfg.hovered = ImGui::IsWindowHovered();
    return open;
}

/** @copydoc EndEditorCard */
void EndEditorCard() {
    ImGui::EndChild();
}

namespace {

/** @brief Selects the top-tab content color for its current visual state. */
ImVec4 TopTabContentColor(const EditorTheme &theme, bool selected, bool hovered) {
    if (selected)
        return theme.palette.accent;
    if (hovered)
        return theme.palette.text;
    return theme.palette.textMuted;
}

} // namespace

/** @copydoc RenderEditorTopTabBar */
EditorTopTabBarResult RenderEditorTopTabBar(
    const EditorTheme &theme,
    const char *id,
    std::span<const EditorTopTabItem> tabs,
    float width,
    float height) {
    EditorTopTabBarResult result{};
    if (tabs.empty())
        return result;

    const float availableWidth =
        width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const float tabHeight = std::max(32.0f, height);
    const ImVec2 stripMin = ImGui::GetCursorScreenPos();
    const ImVec2 stripMax(stripMin.x + availableWidth, stripMin.y + tabHeight);
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const float rounding = theme.rounding.card;

    drawList->AddRectFilled(stripMin, stripMax,
                            ImGui::GetColorU32(theme.palette.card), rounding);
    drawList->AddRect(stripMin, stripMax,
                      ImGui::GetColorU32(theme.palette.border), rounding, 0, 1.0f);

    const float tabWidth = availableWidth / static_cast<float>(tabs.size());
    constexpr float kIconSize = 16.0f;
    constexpr float kGap = 8.0f;
    ImFont *font = ImGui::GetFont();

    ImGui::PushID(id ? id : "editor_top_tabs");
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        const EditorTopTabItem &tab = tabs[static_cast<size_t>(i)];
        const float tabX = stripMin.x + tabWidth * static_cast<float>(i);
        const ImVec2 tabMin(tabX, stripMin.y);
        const ImVec2 tabMax(tabX + tabWidth, stripMax.y);
        ImGui::SetCursorScreenPos(tabMin);

        ImGui::PushID(tab.id ? tab.id : tab.label);
        ImGui::InvisibleButton("##tab", ImVec2(tabWidth, tabHeight));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            result.clickedIndex = i;
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        if (hovered && !tab.selected) {
            drawList->AddRectFilled(tabMin, tabMax,
                                    ImGui::GetColorU32(theme.palette.cardHover),
                                    0.0f);
        }
        if (i > 0) {
            drawList->AddLine(ImVec2(tabMin.x, tabMin.y),
                              ImVec2(tabMin.x, tabMax.y),
                              ImGui::GetColorU32(theme.palette.border), 1.0f);
        }

        const char *label = tab.label ? tab.label : "";
        const ImVec2 textSize =
            font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, label);
        const float iconWidth = tab.drawIcon ? kIconSize + kGap : 0.0f;
        const float contentWidth = iconWidth + textSize.x;
        const float contentX = tabMin.x + (tabWidth - contentWidth) * 0.5f;
        const float contentY = tabMin.y + (tabHeight - textSize.y) * 0.5f;
        const ImU32 contentColor =
            ImGui::GetColorU32(TopTabContentColor(theme, tab.selected, hovered));

        if (tab.drawIcon) {
            const ImVec2 iconOrigin(contentX,
                                    tabMin.y + (tabHeight - kIconSize) * 0.5f);
            tab.drawIcon(*drawList, iconOrigin, kIconSize, contentColor);
        }
        drawList->AddText(font, font->FontSize,
                          ImVec2(contentX + iconWidth, contentY),
                          contentColor, label);

        if (tab.selected) {
            drawList->AddRectFilled(ImVec2(tabMin.x, tabMax.y - 3.0f),
                                    ImVec2(tabMax.x, tabMax.y),
                                    ImGui::GetColorU32(theme.palette.accent),
                                    1.5f);
        }
    }
    ImGui::PopID();

    ImGui::SetCursorScreenPos(stripMin);
    ImGui::Dummy(ImVec2(availableWidth, tabHeight));
    return result;
}

/** @copydoc RenderEditorSegmentedButtons */
int RenderEditorSegmentedButtons(
    const EditorTheme &theme,
    const char *id,
    std::span<const EditorSegmentedButtonItem> items) {
    int clickedIndex = -1;
    if (items.empty())
        return clickedIndex;

    ImGui::PushID(id ? id : "editor_segments");
    int colorCount = 0;
    int styleCount = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.rounding.button);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 7.0f));
    styleCount += 2;

    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (i > 0)
            ImGui::SameLine(0.0f, 4.0f);

        const EditorSegmentedButtonItem &item = items[static_cast<size_t>(i)];
        const bool selected = item.selected;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              selected ? theme.palette.accent
                                       : theme.palette.card);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              selected ? theme.palette.accentHover
                                       : theme.palette.cardHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              selected ? theme.palette.accentActive
                                       : theme.palette.selection);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              selected ? ImVec4(1, 1, 1, 1)
                                       : theme.palette.textMuted);
        colorCount += 4;

        ImGui::PushID(item.id ? item.id : item.label);
        const char *label = item.label ? item.label : "";
        if (const float labelWidth = ImGui::CalcTextSize(label).x;
            ImGui::Button(
                label, ImVec2(std::max(60.0f, labelWidth + 30.0f), 0.0f)))
            clickedIndex = i;
        ImGui::PopID();
        ImGui::PopStyleColor(4);
        colorCount -= 4;
    }

    if (styleCount > 0)
        ImGui::PopStyleVar(styleCount);
    if (colorCount > 0)
        ImGui::PopStyleColor(colorCount);
    ImGui::PopID();
    return clickedIndex;
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

namespace {
/** @brief Returns the display colour for a status level. */
ImVec4 StatusLevelColor(const EditorTheme& theme, EditorStatusLevel level) {
    using enum EditorStatusLevel;
    switch (level) {
        case Info:    return theme.palette.textMuted;
        case Warning: return ImVec4(1.0f, 0.80f, 0.30f, 1.0f);
        case Error:   return theme.palette.destructive;
        case Success: return ImVec4(0.35f, 0.85f, 0.50f, 1.0f);
    }
    return theme.palette.textMuted;
}

/** @brief Builds visible text for one status-bar section. */
std::string BuildStatusBarText(const EditorStatusBarItem& item) {
    std::string out;
    if (!item.icon.empty()) {
        out += item.icon;
        out += ' ';
    }
    out += item.label;
    if (!item.value.empty()) {
        if (!out.empty())
            out += ": ";
        out += item.value;
    }
    return out;
}

/** @brief Returns the rendered width for one status item. */
float MeasureStatusBarItem(const EditorStatusBarItem& item) {
    const std::string text = BuildStatusBarText(item);
    const float progressWidth = item.showProgress ? 92.0f : 0.0f;
    return ImGui::CalcTextSize(text.c_str()).x + progressWidth + 18.0f;
}

/** @brief Renders one status item at the current cursor position. */
void RenderStatusBarItem(const EditorTheme& theme,
                         const EditorStatusBarItem& item,
                         float width,
                         float height) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 separator =
        ImGui::ColorConvertFloat4ToU32(ImVec4(theme.palette.border.x,
                                              theme.palette.border.y,
                                              theme.palette.border.z, 0.65f));
    dl->AddLine(ImVec2(pos.x + width, pos.y + 5.0f),
                ImVec2(pos.x + width, pos.y + height - 5.0f), separator, 1.0f);

    const std::string text = BuildStatusBarText(item);
    ImGui::SetCursorScreenPos(
        ImVec2(pos.x + 8.0f,
               pos.y + std::max(0.0f, (height - ImGui::GetTextLineHeight()) * 0.5f)));
    ImGui::PushStyleColor(ImGuiCol_Text, StatusLevelColor(theme, item.level));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();

    if (item.showProgress) {
        constexpr float kProgressWidth = 76.0f;
        constexpr float kProgressHeight = 4.0f;
        const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
        const ImVec2 barMin(pos.x + 12.0f + textWidth,
                            pos.y + (height - kProgressHeight) * 0.5f);
        const ImVec2 barMax(barMin.x + kProgressWidth,
                            barMin.y + kProgressHeight);
        const float rounding = kProgressHeight * 0.5f;
        const ImU32 trackColor = ImGui::ColorConvertFloat4ToU32(
            ImVec4(theme.palette.border.x, theme.palette.border.y,
                   theme.palette.border.z, 0.55f));
        const ImVec4 levelColor = StatusLevelColor(theme, item.level);
        const ImU32 fillColor = ImGui::ColorConvertFloat4ToU32(
            ImVec4(levelColor.x, levelColor.y, levelColor.z, 0.95f));
        dl->AddRectFilled(barMin, barMax, trackColor, rounding);

        if (item.progress >= 0.0f) {
            const float t = std::clamp(item.progress, 0.0f, 1.0f);
            dl->AddRectFilled(barMin,
                              ImVec2(barMin.x + kProgressWidth * t, barMax.y),
                              fillColor, rounding);
        } else {
            constexpr float kChunkWidth = 22.0f;
            const float phase = std::fmod(static_cast<float>(ImGui::GetTime()) * 76.0f,
                                          kProgressWidth + kChunkWidth);
            const float chunkMinX = barMin.x + phase - kChunkWidth;
            const float chunkMaxX = std::min(chunkMinX + kChunkWidth, barMax.x);
            if (chunkMaxX > barMin.x) {
                dl->AddRectFilled(ImVec2(std::max(chunkMinX, barMin.x), barMin.y),
                                  ImVec2(chunkMaxX, barMax.y), fillColor,
                                  rounding);
            }
        }
    }

    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(width, height));
}
} // namespace

/** @copydoc RenderEditorStatusBar */
void RenderEditorStatusBar(const EditorTheme& theme,
                           std::span<const EditorStatusBarItem> items,
                           float width,
                           float height) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(start, ImVec2(start.x + width, start.y + height),
                      ImGui::ColorConvertFloat4ToU32(theme.palette.panel), 0.0f);
    dl->AddLine(start, ImVec2(start.x + width, start.y),
                ImGui::ColorConvertFloat4ToU32(theme.palette.border), 1.0f);

    float leftX = start.x;
    for (const EditorStatusBarItem& item : items) {
        if (item.side != EditorStatusBarSide::Left)
            continue;
        const float itemW = MeasureStatusBarItem(item);
        if (leftX + itemW > start.x + width)
            break;
        ImGui::SetCursorScreenPos(ImVec2(leftX, start.y));
        RenderStatusBarItem(theme, item, itemW, height);
        leftX += itemW;
    }

    float rightX = start.x + width;
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        if (it->side != EditorStatusBarSide::Right)
            continue;
        const float itemW = MeasureStatusBarItem(*it);
        if (rightX - itemW < leftX)
            break;
        rightX -= itemW;
        ImGui::SetCursorScreenPos(ImVec2(rightX, start.y));
        RenderStatusBarItem(theme, *it, itemW, height);
    }

    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(width, height));
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

/** @copydoc DrawRefreshIcon */
void DrawRefreshIcon(ImDrawList &drawList, ImVec2 origin, float size, ImU32 color) {
    const ImVec2 center(origin.x + size * 0.5f, origin.y + size * 0.5f);
    const float r = size * 0.38f;
    const float thick = std::max(1.5f, size * 0.12f);
    constexpr float kPi = std::numbers::pi_v<float>;
    // Open ring from ~10 o'clock to ~12 o'clock
    drawList.PathArcTo(center, r, kPi * 0.65f, kPi * 1.85f, 20);
    drawList.PathStroke(color, 0, thick);
    // Arrowhead at the open end (upper-right)
    const float ax = center.x + std::cos(kPi * 1.85f) * r;
    const float ay = center.y + std::sin(kPi * 1.85f) * r;
    const float tipX = ax + size * 0.14f;
    const float tipY = ay - size * 0.14f;
    drawList.AddTriangleFilled(
        ImVec2(tipX, tipY),
        ImVec2(ax + size * 0.02f, ay + size * 0.1f),
        ImVec2(ax - size * 0.12f, ay - size * 0.04f),
        color);
}

/** @copydoc DrawDocumentIcon */
void DrawDocumentIcon(ImDrawList &drawList, ImVec2 origin, float size, ImU32 color) {
    const float m = size * 0.06f;
    // Document body
    drawList.AddRectFilled(ImVec2(origin.x + m, origin.y + m),
                           ImVec2(origin.x + size - m, origin.y + size - m),
                           color, size * 0.1f);
    // Folded corner
    const float fold = size * 0.28f;
    drawList.AddTriangleFilled(
        ImVec2(origin.x + size - m - fold, origin.y + m),
        ImVec2(origin.x + size - m, origin.y + m),
        ImVec2(origin.x + size - m, origin.y + m + fold),
        IM_COL32(8, 10, 18, 255));
    drawList.AddTriangleFilled(
        ImVec2(origin.x + size - m - fold, origin.y + m),
        ImVec2(origin.x + size - m - fold, origin.y + m + fold),
        ImVec2(origin.x + size - m, origin.y + m + fold),
        IM_COL32(2, 4, 10, 255));
    // Text lines
    const float lineY0 = origin.y + size * 0.42f;
    const float lineGap = size * 0.14f;
    const float lineL = origin.x + size * 0.18f;
    const float lineR = origin.x + size * 0.65f;
    const float lineThick = std::max(1.0f, size * 0.08f);
    for (int i = 0; i < 3; ++i) {
        const float lineOffset = lineGap * static_cast<float>(i);
        drawList.AddRectFilled(ImVec2(lineL, lineY0 + lineOffset),
                               ImVec2(lineR + (i == 2 ? -size * 0.2f : 0.0f),
                                      lineY0 + lineThick + lineOffset),
                               IM_COL32(2, 4, 10, 200));
    }
}

/** @copydoc RenderEditorIconButton */
bool RenderEditorIconButton(
    const EditorTheme &theme,
    const char *label,
    const std::function<void(ImDrawList &, ImVec2, float, ImU32)> &drawIcon,
    const ImVec2 &size) {
    const std::string_view visibleLabel = VisibleImGuiLabel(label);
    const float iconSize = 14.0f;
    const float iconTextGap = visibleLabel.empty() ? 0.0f : 6.0f;
    const float padX = 10.0f;
    const float padY = 6.0f;
    const float fontHeight = ImGui::GetFontSize();
    const ImVec2 textSize = ImGui::CalcTextSize(visibleLabel.data(),
                                                visibleLabel.data() + visibleLabel.size());
    const float contentW = padX + iconSize + iconTextGap + textSize.x + padX;
    const float contentH = std::max(fontHeight, iconSize) + padY * 2;
    const ImVec2 btnSize(size.x > 0 ? size.x : contentW,
                         size.y > 0 ? size.y : contentH);

    const char *buttonId = (label && label[0] != '\0') ? label : "##editor_icon_button";
    ImGui::InvisibleButton(buttonId, btnSize);
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();

    ImU32 bg;
    if (clicked)
        bg = ImGui::GetColorU32(theme.palette.inputActive);
    else if (hovered)
        bg = ImGui::GetColorU32(theme.palette.cardHover);
    else
        bg = ImGui::GetColorU32(theme.palette.card);

    dl->AddRectFilled(min, max, bg, theme.rounding.card);
    dl->AddRect(min, max, ImGui::GetColorU32(theme.palette.border),
                theme.rounding.card, 0, 0.8f);

    const ImU32 iconColor = ImGui::GetColorU32(
        hovered ? theme.palette.text : theme.palette.textMuted);
    const float visualContentW = iconSize + iconTextGap + textSize.x;
    const ImVec2 iconOrigin(min.x + (btnSize.x - visualContentW) * 0.5f,
                            min.y + (btnSize.y - iconSize) * 0.5f);
    drawIcon(*dl, iconOrigin, iconSize, iconColor);

    if (!visibleLabel.empty()) {
        dl->AddText(ImVec2(iconOrigin.x + iconSize + iconTextGap,
                           min.y + (btnSize.y - fontHeight) * 0.5f),
                    ImGui::GetColorU32(hovered ? theme.palette.text
                                               : theme.palette.textMuted),
                    visibleLabel.data(), visibleLabel.data() + visibleLabel.size());
    }

    return clicked;
}

/** @brief Measures a dialog footer button including icon, text, and padding. */
float MeasureDialogFooterButtonWidth(const EditorDialogFooterButton &button) {
    const char *label = button.label ? button.label : "";
    if (button.width > 0.0f)
        return button.width;
    const float iconWidth = button.drawIcon ? 14.0f + 8.0f : 0.0f;
    return std::max(104.0f, ImGui::CalcTextSize(label).x + iconWidth + 30.0f);
}

/** @brief Draws one dialog footer action button. */
bool RenderDialogFooterButton(const EditorTheme &theme,
                              const EditorDialogFooterButton &button,
                              float width) {
    const char *label = button.label ? button.label : "";
    constexpr float kFooterButtonHeight = 34.0f;
    const bool primary = button.style == EditorDialogFooterButtonStyle::Primary;
    const bool destructive =
        button.style == EditorDialogFooterButtonStyle::Destructive;

    int colorCount = 0;
    int styleCount = 0;
    if (primary) {
        PushButtonColorsPrimary(theme.palette, &colorCount);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        ++colorCount;
    } else if (destructive) {
        ImGui::PushStyleColor(ImGuiCol_Button, theme.palette.destructive);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(theme.palette.destructive.x * 1.15f,
                                     theme.palette.destructive.y * 1.15f,
                                     theme.palette.destructive.z * 1.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.palette.destructive);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        colorCount += 4;
    } else {
        PushButtonColorsSecondary(theme.palette, &colorCount);
    }
    PushButtonVars(theme.rounding, theme.density, &styleCount);

    ImGui::BeginDisabled(!button.enabled);
    bool clicked = false;
    if (button.drawIcon) {
        clicked = RenderEditorIconButton(theme, label, button.drawIcon,
                                         ImVec2(width, kFooterButtonHeight));
    } else {
        clicked = ImGui::Button(label, ImVec2(width, kFooterButtonHeight));
    }
    ImGui::EndDisabled();

    ImGui::PopStyleVar(styleCount);
    ImGui::PopStyleColor(colorCount);
    return clicked && button.enabled;
}

/** @copydoc RenderEditorDialogFooter */
int RenderEditorDialogFooter(const EditorTheme &theme,
                             const EditorDialogFooterConfig &cfg) {
    const char *id = cfg.id ? cfg.id : "##editor_dialog_footer";
    const float height = std::max(52.0f, cfg.height);
    int clickedIndex = -1;

    constexpr float kButtonSpacing = 10.0f;
    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.palette.modal);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 10.0f));
    ImGui::BeginChild(id, ImVec2(0.0f, height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);

    const float avail = ImGui::GetContentRegionAvail().x;
    constexpr float kButtonHeight = 34.0f;
    const float rowY = (ImGui::GetWindowHeight() - kButtonHeight) * 0.5f;
    float totalButtonWidth = 0.0f;
    for (int i = 0; i < static_cast<int>(cfg.buttons.size()); ++i) {
        if (i > 0)
            totalButtonWidth += kButtonSpacing;
        totalButtonWidth += MeasureDialogFooterButtonWidth(cfg.buttons[static_cast<size_t>(i)]);
    }

    if (cfg.progress.label || cfg.progress.value || cfg.progress.progress >= 0.0f) {
        ImGui::SetCursorPosY(rowY + 8.0f);
        if (cfg.progress.label) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
            ImGui::TextUnformatted(cfg.progress.label);
            ImGui::PopStyleColor();
        }
        if (cfg.progress.value) {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
            ImGui::TextUnformatted(cfg.progress.value);
            ImGui::PopStyleColor();
        }
        if (cfg.progress.progress >= 0.0f) {
            ImGui::SameLine(0.0f, 14.0f);
            const float reservedForPercent = ImGui::CalcTextSize("100%").x + 18.0f;
            const float availableForBar =
                std::max(48.0f, avail - totalButtonWidth - 76.0f - reservedForPercent);
            const float barWidth = std::min(cfg.progress.barWidth, availableForBar);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme.palette.accent);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.palette.input);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            ImGui::ProgressBar(std::clamp(cfg.progress.progress, 0.0f, 1.0f),
                               ImVec2(barWidth, 8.0f), "");
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
            ImGui::Text("%.0f%%", cfg.progress.progress * 100.0f);
            ImGui::PopStyleColor();
        }
    }

    if (const float buttonStartX = avail - totalButtonWidth;
        buttonStartX > 0.0f)
        ImGui::SetCursorPosX(buttonStartX);
    ImGui::SetCursorPosY(rowY);

    for (int i = 0; i < static_cast<int>(cfg.buttons.size()); ++i) {
        if (i > 0)
            ImGui::SameLine(0.0f, kButtonSpacing);
        const EditorDialogFooterButton &button = cfg.buttons[static_cast<size_t>(i)];
        ImGui::PushID(button.id ? button.id : button.label);
        if (const float buttonWidth = MeasureDialogFooterButtonWidth(button);
            RenderDialogFooterButton(theme, button, buttonWidth))
            clickedIndex = i;
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return clickedIndex;
}

/** @copydoc RenderBuildFooterBar */
bool RenderBuildFooterBar(const EditorTheme &theme,
                          const EditorBuildFooterConfig &footer) {
    std::array<EditorDialogFooterButton, 3> buttons{};
    int count = 0;
    if (footer.showRebuild) {
        buttons[static_cast<size_t>(count++)] =
            EditorDialogFooterButton{"rebuild", "Rebuild",
                                     EditorDialogFooterButtonStyle::Secondary,
                                     DrawRefreshIcon, 118.0f, true};
    }
    if (footer.showExportLog) {
        buttons[static_cast<size_t>(count++)] =
            EditorDialogFooterButton{"export_log", "Export Log",
                                     EditorDialogFooterButtonStyle::Secondary,
                                     DrawDocumentIcon, 138.0f, true};
    }
    buttons[static_cast<size_t>(count++)] =
        EditorDialogFooterButton{"close", "Close",
                                 EditorDialogFooterButtonStyle::Primary,
                                 nullptr, 104.0f, true};

    const EditorDialogFooterConfig cfg{
        "##build_footer",
        58.0f,
        {"Build Progress:", footer.statusLabel, footer.progress, 300.0f},
        std::span<const EditorDialogFooterButton>(buttons.data(),
                                                  static_cast<size_t>(count)),
    };
    const int clicked = RenderEditorDialogFooter(theme, cfg);
    int index = 0;
    if (footer.showRebuild) {
        if (clicked == index && footer.onRebuild)
            footer.onRebuild();
        ++index;
    }
    if (footer.showExportLog) {
        if (clicked == index && footer.onExportLog)
            footer.onExportLog();
        ++index;
    }
    if (clicked == index) {
        if (footer.onClose)
            footer.onClose();
        return true;
    }
    return false;
}

/** @copydoc RenderBuildProgressCard */
void RenderBuildProgressCard(const EditorTheme &theme, float progress,
                             float height) {
    const float h = height > 0.0f ? height : 86.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.palette.card);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.palette.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.rounding.card);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 14.0f));
    ImGui::BeginChild("##build_progress_card", ImVec2(0.0f, h),
                      ImGuiChildFlags_Border,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
    ImGui::TextUnformatted("Build Progress");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 12.0f));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme.palette.accent);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.palette.input);
    const float percentW = ImGui::CalcTextSize("100%").x + 8.0f;
    const float barW = std::max(40.0f, ImGui::GetContentRegionAvail().x - percentW - 8.0f);
    ImGui::ProgressBar(progress, ImVec2(barW, 8.0f), "");
    ImGui::PopStyleColor(2);
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
    ImGui::Text("%.0f%%", progress * 100.0f);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

/** @copydoc RenderRecentRunsCard */
void RenderRecentRunsCard(const EditorTheme &theme,
                          const std::vector<RecentRunEntry> &entries,
                          int maxShow) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.palette.card);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.palette.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.rounding.card);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 14.0f));
    ImGui::BeginChild("##recent_runs_card", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Border,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
    ImGui::TextUnformatted("Recent Runs");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    if (entries.empty()) {
        Horo::Ui::TextMuted(theme, "No history yet.");
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        return;
    }

    const int count = (maxShow > 0) ? std::min(maxShow, static_cast<int>(entries.size()))
                                    : static_cast<int>(entries.size());

    for (int displayed = 0; displayed < count; ++displayed) {
        const int i = static_cast<int>(entries.size()) - 1 - displayed;
        const auto &e = entries[i];
        if (displayed > 0)
            ImGui::Dummy(ImVec2(0.0f, 12.0f));

        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const float rowHeight = 48.0f;
        const float iconRadius = 8.0f;
        const ImVec2 rowMax(rowMin.x + ImGui::GetContentRegionAvail().x,
                            rowMin.y + rowHeight);
        const auto rowClickable = static_cast<bool>(e.onClick);
        bool rowHovered = false;
        bool rowClicked = false;

        if (rowClickable) {
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(rowMin);
            ImGui::InvisibleButton("##recent_run_entry",
                                   ImVec2(rowMax.x - rowMin.x, rowHeight));
            rowHovered = ImGui::IsItemHovered();
            rowClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            ImGui::PopID();
        }

        const ImU32 statusColor = ImGui::GetColorU32(
            e.succeeded ? ImVec4(0.3f, 0.85f, 0.4f, 1.0f)
                        : ImVec4(0.95f, 0.3f, 0.3f, 1.0f));
        ImDrawList *dl = ImGui::GetWindowDrawList();
        if (rowHovered) {
            const ImVec4 hover = theme.palette.cardHover;
            dl->AddRectFilled(rowMin, rowMax,
                              ImGui::GetColorU32(
                                  ImVec4(hover.x, hover.y, hover.z, 0.32f)),
                              theme.rounding.card);
        }

        const ImVec2 iconCenter(rowMin.x + iconRadius,
                                rowMin.y + rowHeight * 0.5f - 2.0f);
        dl->AddCircleFilled(iconCenter, iconRadius, statusColor, 18);
        const ImU32 markColor = ImGui::GetColorU32(ImVec4(0.03f, 0.08f, 0.12f, 1.0f));
        if (e.succeeded) {
            dl->AddLine(ImVec2(iconCenter.x - 3.2f, iconCenter.y),
                        ImVec2(iconCenter.x - 0.8f, iconCenter.y + 2.6f),
                        markColor, 2.0f);
            dl->AddLine(ImVec2(iconCenter.x - 0.8f, iconCenter.y + 2.6f),
                        ImVec2(iconCenter.x + 4.0f, iconCenter.y - 3.4f),
                        markColor, 2.0f);
        } else {
            dl->AddLine(ImVec2(iconCenter.x - 3.4f, iconCenter.y - 3.4f),
                        ImVec2(iconCenter.x + 3.4f, iconCenter.y + 3.4f),
                        markColor, 2.0f);
            dl->AddLine(ImVec2(iconCenter.x + 3.4f, iconCenter.y - 3.4f),
                        ImVec2(iconCenter.x - 3.4f, iconCenter.y + 3.4f),
                        markColor, 2.0f);
        }

        const float textX = rowMin.x + 28.0f;
        ImGui::SetCursorScreenPos(ImVec2(textX, rowMin.y + 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
        ImGui::TextUnformatted(e.label.c_str());
        ImGui::PopStyleColor();

        ImGui::SetCursorScreenPos(ImVec2(textX, rowMin.y + 22.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
        // Combine platform and status: "macOS · Failed" or "Windows · Succeeded".
        std::string detailLine;
        if (!e.platformLabel.empty())
            detailLine = std::format("{}  ·  {}", e.platformLabel, e.detail);
        else
            detailLine = e.detail;
        ImGui::TextUnformatted(detailLine.c_str());
        ImGui::PopStyleColor();

        if (!e.duration.empty()) {
            const float durW = ImGui::CalcTextSize(e.duration.c_str()).x;
            const float rightX = rowMin.x + ImGui::GetContentRegionAvail().x - durW;
            ImGui::SetCursorScreenPos(ImVec2(rightX, rowMin.y + 20.0f));
            Horo::Ui::TextMuted(theme, e.duration.c_str());
        }

        ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowHeight));

        if (rowClicked)
            e.onClick();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

/** @copydoc RenderLogViewer */
void RenderLogViewer(const EditorTheme &theme, const std::string &logText,
                     float height) {
    static int logFilter = 0; // 0=All, 1=Warnings, 2=Errors

    const std::array<EditorSegmentedButtonItem, 3> filters = {{
        {"all", "All", logFilter == 0},
        {"warnings", "Warnings", logFilter == 1},
        {"errors", "Errors", logFilter == 2},
    }};
    if (const int clicked =
            Horo::Ui::RenderEditorSegmentedButtons(theme, "##log_filters", filters);
        clicked >= 0)
        logFilter = clicked;

    std::string visibleLog;
    if (!logText.empty()) {
        std::istringstream stream(logText);
        std::string line;
        while (std::getline(stream, line)) {
            const bool isWarn = line.find("[WARN]") != std::string::npos ||
                                line.find("warning:") != std::string::npos;
            const bool isErr = line.find("[ERROR]") != std::string::npos ||
                               line.find("error:") != std::string::npos;
            if (logFilter == 1 && !isWarn)
                continue;
            if (logFilter == 2 && !isErr)
                continue;
            visibleLog += line;
            visibleLog.push_back('\n');
        }
    }

    ImGui::Spacing();

    // ── Scrollable, selectable log area ──────────────────────────────
    const float filterHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    const float h = height > 0.0f
        ? std::max(80.0f, height - filterHeight)
        : ImGui::GetContentRegionAvail().y;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.palette.card);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.palette.border);
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.rounding.card);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 12.0f));

    const float logWidth = ImGui::GetContentRegionAvail().x;

    if (visibleLog.empty()) {
        static std::string emptyLogText =
            "Log output will appear here during builds.";
        ImGui::InputTextMultiline("##log_viewer_text", emptyLogText.data(),
                                  emptyLogText.size() + 1, ImVec2(logWidth, h),
                                  ImGuiInputTextFlags_ReadOnly);
    } else {
        ImGui::InputTextMultiline("##log_viewer_text", visibleLog.data(),
                                  visibleLog.size() + 1, ImVec2(logWidth, h),
                                  ImGuiInputTextFlags_ReadOnly);
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

/** @copydoc RenderBuildSummaryCard */
void RenderBuildSummaryCard(const EditorTheme &theme,
                            const std::pair<const char *, const char *> (&cells)[4],
                            const ImVec4 &statusAccent) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImFont *font = ImGui::GetFont();
    const float baseFontSize = font->FontSize;
    const float labelFontSize = std::max(11.0f, baseFontSize * 0.74f);
    const float valueFontSize = std::max(13.0f, baseFontSize * 0.90f);
    const float rounding = theme.rounding.card;

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 cardMin = ImGui::GetCursorScreenPos();
    const ImVec2 cardMax(cardMin.x + avail.x, cardMin.y + 44.0f);
    const float cellW = avail.x / 4.0f;

    // Card background + border
    dl->AddRectFilled(cardMin, cardMax,
                      ImGui::GetColorU32(theme.palette.card), rounding);
    dl->AddRect(cardMin, cardMax,
                ImGui::GetColorU32(theme.palette.border), rounding, 0, 0.8f);

    for (int i = 0; i < 4; ++i) {
        const ImVec2 cellMin(
            cardMin.x + cellW * static_cast<float>(i), cardMin.y);
        if (i > 0)
            dl->AddLine(ImVec2(cellMin.x, cardMin.y + 8.0f),
                        ImVec2(cellMin.x, cardMax.y - 8.0f),
                        ImGui::GetColorU32(theme.palette.border), 1.0f);

        const float pad = 12.0f;
        // Label
        dl->AddText(font, labelFontSize,
                    ImVec2(cellMin.x + pad, cellMin.y + 5.0f),
                    ImGui::GetColorU32(theme.palette.textMuted),
                    cells[i].first);

        // Value — use accent for Status column
        const bool useAccent = (i == 2 && statusAccent.w > 0.0f);
        const ImU32 valColor = ImGui::GetColorU32(
            useAccent ? statusAccent : theme.palette.text);
        dl->AddText(font, valueFontSize,
                    ImVec2(cellMin.x + pad, cellMin.y + 23.0f),
                    valColor, cells[i].second);
    }

    ImGui::Dummy(ImVec2(avail.x, 44.0f));
}

namespace {

/** @brief Returns a selectable card's background color. */
ImU32 SelectableCardBackground(const EditorTheme &theme,
                               const SelectableCardEntry &entry,
                               bool hovered) {
    if (entry.disabled) {
        return ImGui::GetColorU32(
            ImVec4(theme.palette.card.x * 0.65f,
                   theme.palette.card.y * 0.65f,
                   theme.palette.card.z * 0.65f, 1.0f));
    }
    if (entry.selected) {
        return ImGui::GetColorU32(
            ImVec4(theme.palette.accent.x * 0.18f,
                   theme.palette.accent.y * 0.18f,
                   theme.palette.accent.z * 0.18f, 1.0f));
    }
    return ImGui::GetColorU32(hovered ? theme.palette.cardHover
                                     : theme.palette.card);
}

/** @brief Returns a selectable card's border color. */
ImU32 SelectableCardBorder(const EditorTheme &theme,
                           const SelectableCardEntry &entry) {
    if (entry.disabled) {
        return ImGui::GetColorU32(
            ImVec4(theme.palette.border.x * 0.55f,
                   theme.palette.border.y * 0.55f,
                   theme.palette.border.z * 0.55f, 1.0f));
    }
    return ImGui::GetColorU32(entry.selected ? theme.palette.accent
                                            : theme.palette.border);
}

/** @brief Returns a selectable card's icon color. */
ImU32 SelectableCardIconColor(const EditorTheme &theme,
                              const SelectableCardEntry &entry) {
    if (entry.disabled) {
        return ImGui::GetColorU32(
            ImVec4(theme.palette.textMuted.x * 0.40f,
                   theme.palette.textMuted.y * 0.40f,
                   theme.palette.textMuted.z * 0.40f, 1.0f));
    }
    return ImGui::GetColorU32(entry.selected ? theme.palette.accent
                                            : theme.palette.textMuted);
}

/** @brief Returns a selectable card's title color. */
ImU32 SelectableCardTitleColor(const EditorTheme &theme,
                               const SelectableCardEntry &entry) {
    if (!entry.disabled)
        return ImGui::GetColorU32(theme.palette.text);
    return ImGui::GetColorU32(
        ImVec4(theme.palette.textMuted.x * 0.50f,
               theme.palette.textMuted.y * 0.50f,
               theme.palette.textMuted.z * 0.50f, 1.0f));
}

/** @brief Returns a selectable card's subtitle color. */
ImU32 SelectableCardSubtitleColor(const EditorTheme &theme,
                                  const SelectableCardEntry &entry) {
    if (!entry.disabled)
        return ImGui::GetColorU32(theme.palette.textMuted);
    return ImGui::GetColorU32(
        ImVec4(theme.palette.textMuted.x * 0.35f,
               theme.palette.textMuted.y * 0.35f,
               theme.palette.textMuted.z * 0.35f, 1.0f));
}

struct SelectableCardMetrics {
    float width;
    float height;
    float titleSize;
    float subtitleSize;
    float rounding;
    float iconSize;
    ImFont *font;
};

/** @brief Draws one selectable card using the current interaction state. */
void DrawSelectableCard(const EditorTheme &theme,
                        const SelectableCardEntry &entry,
                        const ImVec2 &cardMin,
                        const SelectableCardMetrics &metrics,
                        bool hovered) {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 cardMax(cardMin.x + metrics.width,
                         cardMin.y + metrics.height);
    drawList->AddRectFilled(
        cardMin, cardMax, SelectableCardBackground(theme, entry, hovered),
        metrics.rounding);
    drawList->AddRect(
        cardMin, cardMax, SelectableCardBorder(theme, entry), metrics.rounding,
        0, entry.selected ? 2.0f : 0.8f);

    if (entry.drawIcon) {
        const ImVec2 iconOrigin(
            cardMin.x + 12.0f,
            cardMin.y + (metrics.height - metrics.iconSize) * 0.5f);
        entry.drawIcon(*drawList, iconOrigin, metrics.iconSize,
                       SelectableCardIconColor(theme, entry));
    }

    const float textX = cardMin.x + (entry.drawIcon ? 50.0f : 14.0f);
    drawList->AddText(metrics.font, metrics.titleSize,
                      ImVec2(textX, cardMin.y + 8.0f),
                      SelectableCardTitleColor(theme, entry),
                      entry.title.c_str());

    if (!entry.subtitle.empty()) {
        drawList->AddText(metrics.font, metrics.subtitleSize,
                          ImVec2(textX, cardMin.y + 30.0f),
                          SelectableCardSubtitleColor(theme, entry),
                          entry.subtitle.c_str());
    }
}

} // namespace

/** @copydoc RenderSelectableCardGrid */
void RenderSelectableCardGrid(const EditorTheme &theme,
                              std::vector<SelectableCardEntry> &entries,
                              const std::function<void(int)> &onToggle,
                              int columns) {
    if (entries.empty()) return;

    const float availW = ImGui::GetContentRegionAvail().x;
    const int cols = columns > 0 ? columns
                                 : std::max(1, std::min(3, static_cast<int>(entries.size())));
    const float gap = 8.0f;
    const float cardW =
        (availW - gap * static_cast<float>(cols - 1)) /
        static_cast<float>(cols);
    ImFont *font = ImGui::GetFont();
    const SelectableCardMetrics metrics{
        cardW,
        52.0f,
        std::max(13.0f, font->FontSize * 0.88f),
        std::max(11.0f, font->FontSize * 0.72f),
        theme.rounding.card,
        26.0f,
        font,
    };

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (i > 0 && i % cols != 0) ImGui::SameLine(0.0f, gap);
        if (i > 0 && i % cols == 0) ImGui::Spacing();

        auto &entry = entries[static_cast<size_t>(i)];
        const ImVec2 cardMin = ImGui::GetCursorScreenPos();

        const std::string id = std::format("##card_{}", entry.id);
        ImGui::InvisibleButton(id.c_str(),
                               ImVec2(metrics.width, metrics.height));
        if (!entry.disabled && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            entry.selected = !entry.selected;
            if (onToggle) onToggle(i);
        }

        // Tooltip on hover
        if (!entry.tooltip.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", entry.tooltip.c_str());

        const bool hovered = !entry.disabled && ImGui::IsItemHovered();
        DrawSelectableCard(theme, entry, cardMin, metrics, hovered);
    }
}

} // namespace Horo::Ui
