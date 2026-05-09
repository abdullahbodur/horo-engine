#include "ui/UiComponents.h"

#include <format>
#include <string>

#include "ui/IconsFontAwesome6.h"

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

void PushTransparentHeaderColors(int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
  *colorCount += 3;
}

void PushTransparentButtonColors(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  *colorCount += 4;
}

// Renders a span of EditorPanelDropdownItems recursively. Items with children
// become sub-menus; items without children become MenuItems that call action().
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

EditorTreeRowMetrics GetEditorTreeRowMetrics(const EditorTheme & /*theme*/) {
  return EditorTreeRowMetrics{};
}

ScopedEditorTreeRowStyle::ScopedEditorTreeRowStyle(const EditorTheme &theme) {
  const EditorTreeRowMetrics metrics = GetEditorTreeRowMetrics(theme);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, metrics.framePadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, metrics.rounding);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, metrics.itemSpacing);
  m_styleCount += 3;
  PushTransparentHeaderColors(&m_colorCount);
}

ScopedEditorTreeRowStyle::~ScopedEditorTreeRowStyle() {
  ImGui::PopStyleColor(m_colorCount);
  ImGui::PopStyleVar(m_styleCount);
}

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

void EndFixedHeightEditorTreeRow(const EditorTreeRowState &row) {
  const float cursorEndY = ImGui::GetCursorScreenPos().y;
  const float rowEndY = row.start.y + row.height;
  if (cursorEndY < rowEndY)
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowEndY - cursorEndY));
}

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

EditorTreeItemResult DrawEditorTreeItem(const EditorTheme &theme,
                                        const EditorTreeItemSpec &item) {
  EditorTreeItemResult result;
  result.row = BeginEditorTreeRow(theme);
  DrawEditorTreeRowBackground(theme, result.row, item.selected);

  std::string displayLabel = item.label == nullptr ? "" : item.label;
  if (item.prefixIcon != nullptr && item.prefixIcon[0] != '\0') {
    displayLabel = item.prefixIcon;
    if (item.label != nullptr && item.label[0] != '\0') {
      displayLabel += "  ";
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

void ErrorText(const EditorTheme &theme, const char *text) {
  ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.destructive);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

const char *EditorPanelTabLabel(EditorPanelTab tab) {
  switch (tab) {
  case EditorPanelTab::Scene:
    return "Scene";
  case EditorPanelTab::Project:
    return "Project";
  case EditorPanelTab::Viewport:
    return "Viewport";
  case EditorPanelTab::Assets:
    return "Assets";
  case EditorPanelTab::Console:
    return "Console";
  case EditorPanelTab::Animation:
    return "Animation";
  case EditorPanelTab::MCP:
    return "MCP";
  }
  return "Unknown";
}

EditorPanelTopBarResult RenderEditorPanelTopBar(
    const EditorTheme &theme, const char *id,
    std::span<const EditorPanelTabItem> tabs,
    std::span<const EditorPanelActionItem> actions) {
  constexpr float kTabHorizontalPadding = 12.0f;
  constexpr float kTabVerticalPadding = 6.0f;
  constexpr float kTabSpacing = 2.0f;
  constexpr float kActionSpacing = 6.0f;
  constexpr float kHeaderBottomPadding = 8.0f;

  EditorPanelTopBarResult result;
  const float headerY = ImGui::GetCursorPosY();
  const float actionButtonSize = ImGui::GetTextLineHeight() + 4.0f;

  // Only enabled actions contribute to width and are rendered.
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

  constexpr float kUnderlineThickness = 2.0f;

  // Draw full-width gray baseline at the same Y band as the selected-tab blue
  // underline. tabRowBottom must use kTabVerticalPadding — the same padding
  // pushed onto the style stack when buttons are rendered — so the two rects
  // share an identical bottom edge.
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

  for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
    if (i > 0)
      ImGui::SameLine(0.0f, kTabSpacing);

    if (ImGui::GetCursorPosX() >= maxTabRight)
      break;

    const EditorPanelTabItem &item = tabs[i];
    const char *label = EditorPanelTabLabel(item.tab);
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    const float tabWidth = labelSize.x + kTabHorizontalPadding * 2.0f;
    if (ImGui::GetCursorPosX() + tabWidth > maxTabRight)
      break;

    ImGui::PushID(i);
    const ImVec4 textColor =
        item.selected ? theme.palette.text : theme.palette.textMuted;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.cardHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(kTabHorizontalPadding, kTabVerticalPadding));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

    const ImVec2 btnPos = ImGui::GetCursorScreenPos();
    if (ImGui::Button(label, ImVec2(tabWidth, 0.0f)))
      result.clickedTabIndex = i;
    const ImVec2 btnMax = ImGui::GetItemRectMax();

    if (item.selected) {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(
          ImVec2(btnPos.x, btnMax.y - kUnderlineThickness),
          ImVec2(btnMax.x, btnMax.y),
          ImGui::ColorConvertFloat4ToU32(theme.palette.accent));
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopID();
  }

  if (enabledActionCount > 0) {
    ImGui::SetCursorPos(
        ImVec2(rightEdge - actionsWidth, headerY + 1.0f));

    int renderedCount = 0;
    for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
      const EditorPanelActionItem &action = actions[i];
      if (!action.enabled)
        continue;

      if (renderedCount > 0)
        ImGui::SameLine(0.0f, kActionSpacing);
      ++renderedCount;

      const ImVec2 btnSize(actionButtonSize, 0.0f);
      ImGui::PushID(i);

      std::string btnLabel;
      if (action.icon)
        btnLabel = action.icon;
      if (action.text && action.text[0] != '\0') {
        btnLabel += "  ";
        btnLabel += action.text;
      }

      const bool clicked =
          EditorHeaderIconButton(theme, btnLabel.c_str(), btnSize);

      if (clicked) {
        if (!action.dropdown.empty()) {
          const std::string popupId = std::format("##action_popup_{}", i);
          ImGui::OpenPopup(popupId.c_str());
        } else {
          result.clickedActionIndex = i;
        }
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
  }

  ImGui::PopID();

  const float minCursorY =
      headerY + ImGui::GetFrameHeight() + kHeaderBottomPadding;
  if (ImGui::GetCursorPosY() < minCursorY)
    ImGui::SetCursorPosY(minCursorY);

  return result;
}

} // namespace Horo::Ui
