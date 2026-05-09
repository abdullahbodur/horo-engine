#pragma once

#include <imgui.h>

#include <functional>
#include <span>

#include "ui/HoroTheme.h"

namespace Horo::Ui {

class ScopedPanelStyle {
public:
  explicit ScopedPanelStyle(const EditorTheme &theme);
  explicit ScopedPanelStyle(const LauncherTheme &theme);
  ~ScopedPanelStyle();

  ScopedPanelStyle(const ScopedPanelStyle &) = delete;
  ScopedPanelStyle &operator=(const ScopedPanelStyle &) = delete;

private:
  int m_colorCount = 0;
  int m_styleCount = 0;
};

class ScopedCardStyle {
public:
  explicit ScopedCardStyle(const EditorTheme &theme);
  explicit ScopedCardStyle(const LauncherTheme &theme);
  ~ScopedCardStyle();

  ScopedCardStyle(const ScopedCardStyle &) = delete;
  ScopedCardStyle &operator=(const ScopedCardStyle &) = delete;

private:
  int m_colorCount = 0;
  int m_styleCount = 0;
};

class ScopedInputStyle {
public:
  explicit ScopedInputStyle(const EditorTheme &theme);
  explicit ScopedInputStyle(const LauncherTheme &theme);
  ~ScopedInputStyle();

  ScopedInputStyle(const ScopedInputStyle &) = delete;
  ScopedInputStyle &operator=(const ScopedInputStyle &) = delete;

private:
  int m_colorCount = 0;
  int m_styleCount = 0;
};

enum class ButtonStyleVariant {
  Primary,
  Secondary,
};

class ScopedButtonStyle {
public:
  explicit ScopedButtonStyle(const EditorTheme &theme, ButtonStyleVariant variant);
  explicit ScopedButtonStyle(const LauncherTheme &theme, ButtonStyleVariant variant);
  ~ScopedButtonStyle();

  ScopedButtonStyle(const ScopedButtonStyle &) = delete;
  ScopedButtonStyle &operator=(const ScopedButtonStyle &) = delete;

private:
  int m_colorCount = 0;
  int m_styleCount = 0;
};

void TextMuted(const EditorTheme &theme, const char *text);
void TextMuted(const LauncherTheme &theme, const char *text);

void SectionHeader(const EditorTheme &theme, const char *title);
void SectionHeader(const LauncherTheme &theme, const char *title);

bool Button(const EditorTheme &theme, ButtonStyleVariant variant,
            const char *label, const ImVec2 &size = ImVec2(0, 0));
bool Button(const LauncherTheme &theme, ButtonStyleVariant variant,
            const char *label, const ImVec2 &size = ImVec2(0, 0));

bool RenderPrimaryButton(const LauncherTheme &theme, const char *label,
                         const ImVec2 &size = ImVec2(0, 0));

bool RenderSecondaryButton(const LauncherTheme &theme, const char *label,
                           const ImVec2 &size = ImVec2(0, 0));

bool RenderRecentProjectButton(const LauncherTheme &theme, const char *title,
                               const ImVec2 &size = ImVec2(0, 0));

void RenderLabeledInput(const LauncherTheme &theme, const char *title,
                        const char *id, char *buffer, size_t bufferSize,
                        float inputWidth);

bool InputText(const EditorTheme &theme, const char *id, char *buffer,
               size_t bufferSize, ImGuiInputTextFlags flags = 0);
bool InputText(const LauncherTheme &theme, const char *id, char *buffer,
               size_t bufferSize, ImGuiInputTextFlags flags = 0);

class ScopedComboStyle {
public:
  explicit ScopedComboStyle(const EditorTheme &theme);
  explicit ScopedComboStyle(const LauncherTheme &theme);
  ~ScopedComboStyle();

  ScopedComboStyle(const ScopedComboStyle &) = delete;
  ScopedComboStyle &operator=(const ScopedComboStyle &) = delete;

private:
  int m_colorCount = 0;
  int m_styleCount = 0;
};

bool InputTextWithHint(const EditorTheme &theme, const char *id,
                       const char *hint, char *buffer, size_t bufferSize,
                       ImGuiInputTextFlags flags = 0);

bool InputTextWithHint(const LauncherTheme &theme, const char *id,
                       const char *hint, char *buffer, size_t bufferSize,
                       ImGuiInputTextFlags flags = 0);

bool Combo(const EditorTheme &theme, const char *label, int *currentItem,
           const char *const items[], int itemCount);
bool Combo(const LauncherTheme &theme, const char *label, int *currentItem,
           const char *const items[], int itemCount);

struct EditorTreeRowMetrics {
  ImVec2 framePadding{12.0f, 6.0f};
  ImVec2 itemSpacing{0.0f, 2.0f};
  float rounding = 6.0f;
  float horizontalSlop = 4.0f;
  float verticalInset = 1.0f;
};

struct EditorTreeRowState {
  ImVec2 start{};
  float height = 0.0f;
  float contentLeft = 0.0f;
  float contentRight = 0.0f;
  bool hovered = false;
};

class ScopedEditorTreeRowStyle {
public:
  explicit ScopedEditorTreeRowStyle(const EditorTheme &theme);
  ~ScopedEditorTreeRowStyle();

  ScopedEditorTreeRowStyle(const ScopedEditorTreeRowStyle &) = delete;
  ScopedEditorTreeRowStyle &operator=(const ScopedEditorTreeRowStyle &) = delete;

private:
  int m_colorCount = 0;
  int m_styleCount = 0;
};

EditorTreeRowMetrics GetEditorTreeRowMetrics(const EditorTheme &theme);
EditorTreeRowState BeginEditorTreeRow(const EditorTheme &theme);
void DrawEditorTreeRowBackground(const EditorTheme &theme,
                                 const EditorTreeRowState &row,
                                 bool selected = false);
void EndFixedHeightEditorTreeRow(const EditorTreeRowState &row);

struct EditorTreeSearchSlotConfig {
  bool enabled = false;
  const char *id = "##editor_tree_search";
  const char *placeholder = "Search...";
  char *buffer = nullptr;
  size_t bufferSize = 0;
  float width = 0.0f;
  ImGuiInputTextFlags flags = 0;
  bool showFilterIcon = true;
};

enum class EditorTreeItemKind {
  Node,
  Leaf,
};

struct EditorTreeItemSpec {
  const char *id = nullptr;
  const char *label = "";
  const char *prefixIcon = nullptr;
  const char *suffixIcon = nullptr;
  EditorTreeItemKind kind = EditorTreeItemKind::Node;
  ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_SpanAvailWidth;
  bool selected = false;
  const ImVec4 *normalTextColor = nullptr;
  const ImVec4 *hoveredTextColor = nullptr;
};

struct EditorTreeItemResult {
  EditorTreeRowState row{};
  bool open = false;
  bool suffixClicked = false;
};

bool RenderEditorTreeSearchSlot(const EditorTheme &theme,
                                const EditorTreeSearchSlotConfig &config);
EditorTreeItemResult DrawEditorTreeItem(const EditorTheme &theme,
                                        const EditorTreeItemSpec &item);

bool EditorHeaderIconButton(const EditorTheme &theme, const char *label,
                            const ImVec2 &size);
void ErrorText(const EditorTheme &theme, const char *text);

enum class EditorPanelTab {
  Scene,
  Project,
  Viewport,
  Assets,
  Console,
  Animation,
  MCP,
};

struct EditorPanelTabItem {
  EditorPanelTab tab;
  bool selected = false;
};

// Recursive dropdown item. If childCount > 0 the item renders as a sub-menu
// (children pointer must stay valid during rendering); otherwise it renders as
// a MenuItem and action is called on selection.
struct EditorPanelDropdownItem {
  const char *icon = nullptr;
  const char *label = nullptr;
  std::function<void()> action;
  // Raw pointer avoids the self-referential incomplete-type issue with span.
  const EditorPanelDropdownItem *children = nullptr;
  size_t childCount = 0;
};

struct EditorPanelActionItem {
  const char *icon;                // required font icon (was `label`)
  const char *text = nullptr;      // optional text after icon
  std::span<const EditorPanelDropdownItem> dropdown = {};
  // non-empty → clicking opens an inline popup rendered recursively
  // empty     → clickedActionIndex is set in result (caller handles popup)
  bool enabled = true;             // false → button is hidden and excluded from width
};

struct EditorPanelTopBarResult {
  int clickedTabIndex = -1;
  int clickedActionIndex = -1;
};

const char *EditorPanelTabLabel(EditorPanelTab tab);

EditorPanelTopBarResult RenderEditorPanelTopBar(
    const EditorTheme &theme, const char *id,
    std::span<const EditorPanelTabItem> tabs,
    std::span<const EditorPanelActionItem> actions);

} // namespace Horo::Ui
