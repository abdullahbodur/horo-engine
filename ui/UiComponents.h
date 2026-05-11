/**
 * @file UiComponents.h
 * @brief Reusable ImGui widget primitives for all Horo editor and launcher UI.
 *
 * ## Conventions
 *
 * - **Scoped\*** classes push ImGui colour/style vars in their constructor and
 *   pop them in their destructor (RAII). Place one on the stack before calling
 *   ImGui widgets; it cleans up automatically at the end of the scope.
 * - **Render\*** free functions emit one self-contained widget: they push their
 *   own styles, render, and pop before returning.
 * - **Begin / End** pairs follow ImGui's own pattern: call Begin, render the
 *   body, then call End. Begin returns false when the widget is closed or
 *   invisible; End must still be called unless the docs say otherwise.
 * - Functions that accept both EditorTheme and LauncherTheme are overloaded so
 *   the call site never needs a theme-type branch.
 * - All declarations live in namespace Horo::Ui.
 */
#pragma once

#include <imgui.h>

#include <functional>
#include <span>
#include <string>

#include "ui/HoroTheme.h"

namespace Horo::Ui {

// ─────────────────────────────────────────────────────────────────────────────
// Scoped style helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief RAII guard that pushes panel-themed ImGui colours and style vars.
 *
 * Applies WindowBg, ChildBg, Border, WindowRounding, ChildRounding, and
 * WindowPadding from the supplied theme.  Restore happens in the destructor,
 * making it safe to use even when an early return exits the scope.
 *
 * @par Example
 * @code
 * {
 *   Horo::Ui::ScopedPanelStyle style(theme);
 *   ImGui::Begin("MyPanel", nullptr, flags);
 *   // ... widgets ...
 *   ImGui::End();
 * }  // style restored here
 * @endcode
 */
class ScopedPanelStyle {
public:
  /** @brief Constructs and immediately pushes editor-theme panel styles. */
  explicit ScopedPanelStyle(const EditorTheme &theme);

  /** @brief Constructs and immediately pushes launcher-theme panel styles. */
  explicit ScopedPanelStyle(const LauncherTheme &theme);

  /** @brief Pops all colours and style vars pushed by the constructor. */
  ~ScopedPanelStyle();

  ScopedPanelStyle(const ScopedPanelStyle &) = delete;
  ScopedPanelStyle &operator=(const ScopedPanelStyle &) = delete;

private:
  int m_colorCount = 0; /**< Number of ImGui colour entries pushed. */
  int m_styleCount = 0; /**< Number of ImGui style var entries pushed. */
};

/**
 * @brief RAII guard that pushes card/tile-themed ImGui colours and style vars.
 *
 * Applies ChildBg, Border, Header, HeaderHovered, ChildRounding, and
 * WindowPadding appropriate for asset tiles and card child windows.
 */
class ScopedCardStyle {
public:
  /** @brief Pushes editor-theme card styles. */
  explicit ScopedCardStyle(const EditorTheme &theme);

  /** @brief Pushes launcher-theme card styles. */
  explicit ScopedCardStyle(const LauncherTheme &theme);

  /** @brief Pops all pushed colours and style vars. */
  ~ScopedCardStyle();

  ScopedCardStyle(const ScopedCardStyle &) = delete;
  ScopedCardStyle &operator=(const ScopedCardStyle &) = delete;

private:
  int m_colorCount = 0; /**< Number of ImGui colour entries pushed. */
  int m_styleCount = 0; /**< Number of ImGui style var entries pushed. */
};

/**
 * @brief RAII guard that pushes input-field-themed ImGui colours and style vars.
 *
 * Applies FrameBg, FrameBgHovered, FrameBgActive, Text, FrameRounding, and
 * FramePadding from the supplied theme's input tokens.
 */
class ScopedInputStyle {
public:
  /** @brief Pushes editor-theme input styles. */
  explicit ScopedInputStyle(const EditorTheme &theme);

  /** @brief Pushes launcher-theme input styles. */
  explicit ScopedInputStyle(const LauncherTheme &theme);

  /** @brief Pops all pushed colours and style vars. */
  ~ScopedInputStyle();

  ScopedInputStyle(const ScopedInputStyle &) = delete;
  ScopedInputStyle &operator=(const ScopedInputStyle &) = delete;

private:
  int m_colorCount = 0; /**< Number of ImGui colour entries pushed. */
  int m_styleCount = 0; /**< Number of ImGui style var entries pushed. */
};

/**
 * @brief Visual variant for Button / ScopedButtonStyle.
 */
enum class ButtonStyleVariant {
  Primary,   /**< Filled accent-colour button; use for the default action. */
  Secondary, /**< Softer background button; use for secondary / cancel actions. */
};

/**
 * @brief RAII guard that pushes button-themed ImGui colours and style vars.
 *
 * Applies Button, ButtonHovered, ButtonActive, Text, FramePadding, and
 * FrameRounding from the theme using the selected variant.
 */
class ScopedButtonStyle {
public:
  /** @brief Pushes editor-theme button styles for the given variant. */
  explicit ScopedButtonStyle(const EditorTheme &theme, ButtonStyleVariant variant);

  /** @brief Pushes launcher-theme button styles for the given variant. */
  explicit ScopedButtonStyle(const LauncherTheme &theme, ButtonStyleVariant variant);

  /** @brief Pops all pushed colours and style vars. */
  ~ScopedButtonStyle();

  ScopedButtonStyle(const ScopedButtonStyle &) = delete;
  ScopedButtonStyle &operator=(const ScopedButtonStyle &) = delete;

private:
  int m_colorCount = 0; /**< Number of ImGui colour entries pushed. */
  int m_styleCount = 0; /**< Number of ImGui style var entries pushed. */
};

// ─────────────────────────────────────────────────────────────────────────────
// Typography helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Renders @p text using the theme's muted (secondary) text colour.
 * @param theme Active editor theme.
 * @param text  Null-terminated string to display.
 */
void TextMuted(const EditorTheme &theme, const char *text);

/**
 * @brief Renders @p text using the theme's muted text colour (launcher variant).
 */
void TextMuted(const LauncherTheme &theme, const char *text);

/**
 * @brief Renders a section heading followed by a full-width separator.
 *
 * Use this to visually group related widgets inside a panel or modal.
 * The heading is rendered in the theme's primary text colour.
 * @param theme  Active editor theme.
 * @param title  Null-terminated heading string.
 */
void SectionHeader(const EditorTheme &theme, const char *title);

/** @brief Launcher-theme variant of SectionHeader(). */
void SectionHeader(const LauncherTheme &theme, const char *title);

/**
 * @brief Renders an inline separator with embedded title text (─── Title ───).
 *
 * Wraps ImGui::SeparatorText.  No theme parameter is needed because ImGui
 * derives the separator colour from the current style.  Use this inside the
 * bottom dock or any panel where a labelled horizontal rule is needed.
 * @param title  Null-terminated label to embed in the separator line.
 */
void RenderEditorSectionDivider(const char *title);

// ─────────────────────────────────────────────────────────────────────────────
// Button helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Renders a themed button for the editor.
 *
 * Pushes the appropriate button colours for the given variant, calls
 * ImGui::Button, and pops before returning.
 * @param theme   Active editor theme.
 * @param variant Primary (accent fill) or Secondary (soft fill).
 * @param label   Button label (may include icon prefix).
 * @param size    Explicit pixel size; pass ImVec2(0,0) for auto-size.
 * @return True on the frame the button is clicked.
 */
bool Button(const EditorTheme &theme, ButtonStyleVariant variant,
            const char *label, const ImVec2 &size = ImVec2(0, 0));

/** @brief Launcher-theme variant of Button(). */
bool Button(const LauncherTheme &theme, ButtonStyleVariant variant,
            const char *label, const ImVec2 &size = ImVec2(0, 0));

/**
 * @brief Convenience wrapper: renders a Primary-variant button (launcher theme).
 * @return True on click.
 */
bool RenderPrimaryButton(const LauncherTheme &theme, const char *label,
                         const ImVec2 &size = ImVec2(0, 0));

/**
 * @brief Convenience wrapper: renders a Secondary-variant button (launcher theme).
 * @return True on click.
 */
bool RenderSecondaryButton(const LauncherTheme &theme, const char *label,
                           const ImVec2 &size = ImVec2(0, 0));

/**
 * @brief Renders a full-width recent-project list entry button (launcher theme).
 *
 * Styled as a wide card-like button; intended for the recent projects list in
 * the launcher welcome screen.
 * @return True on click.
 */
bool RenderRecentProjectButton(const LauncherTheme &theme, const char *title,
                               const ImVec2 &size = ImVec2(0, 0));

// ─────────────────────────────────────────────────────────────────────────────
// Text input helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Renders a labelled text-input pair for the launcher (label above field).
 *
 * Draws a TextDisabled label, then a themed InputText at the given width.
 * @param theme       Active launcher theme.
 * @param title       Label string shown above the input field.
 * @param id          ImGui widget id (use "##…" to hide it).
 * @param buffer      Writable character buffer for the input text.
 * @param bufferSize  Size of @p buffer in bytes.
 * @param inputWidth  Explicit pixel width for the input field.
 */
void RenderLabeledInput(const LauncherTheme &theme, const char *title,
                        const char *id, char *buffer, size_t bufferSize,
                        float inputWidth);

/**
 * @brief Renders a themed single-line text input (editor theme).
 *
 * Applies ScopedInputStyle, calls ImGui::InputText, and restores styles.
 * @param id         ImGui widget id.
 * @param buffer     Writable text buffer.
 * @param bufferSize Size of @p buffer in bytes.
 * @param flags      Optional ImGuiInputTextFlags.
 * @return True when the buffer content has changed this frame.
 */
bool InputText(const EditorTheme &theme, const char *id, char *buffer,
               size_t bufferSize, ImGuiInputTextFlags flags = 0);

/** @brief Launcher-theme variant of InputText(). */
bool InputText(const LauncherTheme &theme, const char *id, char *buffer,
               size_t bufferSize, ImGuiInputTextFlags flags = 0);

/**
 * @brief RAII guard that pushes combo-box-themed ImGui colours and style vars.
 *
 * Applies FrameBg, FrameBgHovered, FrameBgActive, PopupBg, Text,
 * FrameRounding, FramePadding, PopupRounding, and PopupBorderSize.
 */
class ScopedComboStyle {
public:
  /** @brief Pushes editor-theme combo styles. */
  explicit ScopedComboStyle(const EditorTheme &theme);

  /** @brief Pushes launcher-theme combo styles. */
  explicit ScopedComboStyle(const LauncherTheme &theme);

  /** @brief Pops all pushed colours and style vars. */
  ~ScopedComboStyle();

  ScopedComboStyle(const ScopedComboStyle &) = delete;
  ScopedComboStyle &operator=(const ScopedComboStyle &) = delete;

private:
  int m_colorCount = 0; /**< Number of ImGui colour entries pushed. */
  int m_styleCount = 0; /**< Number of ImGui style var entries pushed. */
};

/**
 * @brief Renders a themed single-line text input with placeholder hint (editor).
 *
 * Equivalent to InputText but calls ImGui::InputTextWithHint so the @p hint
 * string is shown in a muted style when the buffer is empty.
 * @param id         ImGui widget id.
 * @param hint       Placeholder text shown when the buffer is empty.
 * @param buffer     Writable text buffer.
 * @param bufferSize Size of @p buffer in bytes.
 * @param flags      Optional ImGuiInputTextFlags.
 * @return True when the buffer content changes this frame.
 */
bool InputTextWithHint(const EditorTheme &theme, const char *id,
                       const char *hint, char *buffer, size_t bufferSize,
                       ImGuiInputTextFlags flags = 0);

/** @brief Launcher-theme variant of InputTextWithHint(). */
bool InputTextWithHint(const LauncherTheme &theme, const char *id,
                       const char *hint, char *buffer, size_t bufferSize,
                       ImGuiInputTextFlags flags = 0);

/**
 * @brief Renders a themed combo box backed by a C string array (editor theme).
 *
 * Applies ScopedComboStyle and calls ImGui::Combo with the supplied items array.
 * @param label       Visible label rendered to the right of the combo.
 * @param currentItem In/out index of the selected item.
 * @param items       Null-terminated C string array; length must be @p itemCount.
 * @param itemCount   Number of entries in @p items.
 * @return True when the selection changes this frame.
 */
bool Combo(const EditorTheme &theme, const char *label, int *currentItem,
           const char *const items[], int itemCount);

/** @brief Launcher-theme variant of Combo(). */
bool Combo(const LauncherTheme &theme, const char *label, int *currentItem,
           const char *const items[], int itemCount);

// ─────────────────────────────────────────────────────────────────────────────
// Tree row primitives
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Layout constants for a single tree row.
 *
 * Returned by GetEditorTreeRowMetrics() and consumed by BeginEditorTreeRow()
 * to ensure every row in a tree view has a consistent fixed height and inset.
 */
struct EditorTreeRowMetrics {
  ImVec2 framePadding{12.0f, 6.0f}; /**< ImGuiStyleVar_FramePadding pushed for each row. */
  ImVec2 itemSpacing{0.0f, 2.0f};   /**< ImGuiStyleVar_ItemSpacing pushed for each row. */
  float  rounding       = 6.0f;     /**< Corner rounding for the selection highlight rect. */
  float  horizontalSlop = 4.0f;     /**< Extra horizontal margin on each side of the highlight. */
  float  verticalInset  = 1.0f;     /**< Vertical inset so the highlight does not touch row edges. */
};

/**
 * @brief Snapshot of a tree row's geometry after BeginEditorTreeRow() returns.
 *
 * Used by DrawEditorTreeRowBackground() to paint the hover/selection highlight
 * and by EndFixedHeightEditorTreeRow() to advance the cursor to the next row.
 */
struct EditorTreeRowState {
  ImVec2 start{};        /**< Cursor position at the moment BeginEditorTreeRow() was called. */
  float  height = 0.0f;  /**< Computed row height in pixels. */
  float  contentLeft  = 0.0f; /**< Left edge of the content area (after indent). */
  float  contentRight = 0.0f; /**< Right edge of the content area. */
  bool   hovered = false;     /**< True if the cursor is inside this row's rectangle. */
};

/**
 * @brief RAII guard that pushes tree-row ImGui style vars.
 *
 * Pushes FramePadding, ItemSpacing, and transparent header colours so that
 * ImGui::TreeNodeEx does not paint its own background (Horo draws the
 * highlight manually via DrawEditorTreeRowBackground()).
 */
class ScopedEditorTreeRowStyle {
public:
  /** @brief Pushes tree-row style vars for the given editor theme. */
  explicit ScopedEditorTreeRowStyle(const EditorTheme &theme);

  /** @brief Pops all pushed colours and style vars. */
  ~ScopedEditorTreeRowStyle();

  ScopedEditorTreeRowStyle(const ScopedEditorTreeRowStyle &) = delete;
  ScopedEditorTreeRowStyle &operator=(const ScopedEditorTreeRowStyle &) = delete;

private:
  int m_colorCount = 0; /**< Number of ImGui colour entries pushed. */
  int m_styleCount = 0; /**< Number of ImGui style var entries pushed. */
};

/** @brief Returns the layout constants for editor tree rows derived from @p theme. */
EditorTreeRowMetrics GetEditorTreeRowMetrics(const EditorTheme &theme);

/**
 * @brief Begins a fixed-height tree row and returns its geometry snapshot.
 *
 * Records the current cursor position, computes the row height from the
 * theme's density, and checks whether the cursor is hovering this row.
 * Call EndFixedHeightEditorTreeRow() after all widgets for this row have
 * been rendered.
 * @param theme Active editor theme.
 * @return Snapshot describing this row's position and hover state.
 */
EditorTreeRowState BeginEditorTreeRow(const EditorTheme &theme);

/**
 * @brief Paints the hover or selection highlight rectangle for a tree row.
 *
 * Uses the window draw-list to fill a rounded rect behind the row content.
 * Must be called after BeginEditorTreeRow() and before EndFixedHeightEditorTreeRow().
 * @param theme    Active editor theme.
 * @param row      Geometry snapshot from BeginEditorTreeRow().
 * @param selected True to draw the selection colour; false draws hover colour
 *                 only when the row is hovered.
 */
void DrawEditorTreeRowBackground(const EditorTheme &theme,
                                 const EditorTreeRowState &row,
                                 bool selected = false);

/**
 * @brief Ends a fixed-height tree row by advancing the cursor past the row.
 *
 * Ensures the cursor sits at the correct Y position for the next row
 * regardless of how many widgets were rendered inside this row.
 * @param row Geometry snapshot from BeginEditorTreeRow().
 */
void EndFixedHeightEditorTreeRow(const EditorTreeRowState &row);

// ─────────────────────────────────────────────────────────────────────────────
// Tree item primitives
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Configuration for the optional search bar rendered above a tree view.
 */
struct EditorTreeSearchSlotConfig {
  bool                enabled      = false;               /**< When false the search bar is not rendered. */
  const char         *id           = "##editor_tree_search"; /**< ImGui widget id for the input. */
  const char         *placeholder  = "Search...";         /**< Hint text shown in the empty input. */
  char               *buffer       = nullptr;             /**< Writable buffer that receives user input. */
  size_t              bufferSize   = 0;                   /**< Size of @p buffer in bytes. */
  float               width        = 0.0f;                /**< Input width; 0 = full available width. */
  ImGuiInputTextFlags flags        = 0;                   /**< Optional ImGuiInputTextFlags. */
  bool                showFilterIcon = true;              /**< Prepend a filter/funnel icon to the field. */
};

/**
 * @brief Controls whether a tree item is a collapsible node or a terminal leaf.
 */
enum class EditorTreeItemKind {
  Node, /**< Renders with ImGui::TreeNodeEx (can have children, has arrow). */
  Leaf, /**< Renders as a flat selectable row with no expand arrow. */
};

/**
 * @brief Full specification for a single editor tree item.
 *
 * Pass this to DrawEditorTreeItem() to render a row that can have a prefix
 * icon, a label, and a suffix icon — all with consistent fixed height and
 * selection highlight.
 */
struct EditorTreeItemSpec {
  const char        *id               = nullptr;                          /**< ImGui widget id (required). */
  const char        *label            = "";                               /**< Visible label string. */
  const char        *prefixIcon       = nullptr;                          /**< Optional Font Awesome icon shown left of label. */
  const char        *suffixIcon       = nullptr;                          /**< Optional icon shown right-aligned; click reported via suffixClicked. */
  EditorTreeItemKind kind             = EditorTreeItemKind::Node;         /**< Node (collapsible) or Leaf (flat). */
  ImGuiTreeNodeFlags treeFlags        = ImGuiTreeNodeFlags_SpanAvailWidth; /**< Extra flags forwarded to ImGui::TreeNodeEx. */
  bool               selected         = false;                            /**< Draws selection highlight when true. */
  const ImVec4      *normalTextColor  = nullptr;                          /**< Overrides text colour; nullptr = theme default. */
  const ImVec4      *hoveredTextColor = nullptr;                          /**< Overrides text colour on hover; nullptr = same as normal. */
};

/**
 * @brief Return value from DrawEditorTreeItem().
 */
struct EditorTreeItemResult {
  EditorTreeRowState row{};             /**< Geometry of the rendered row (for custom overlays). */
  bool               open = false;      /**< True when a Node item is expanded (children should be rendered). */
  bool               suffixClicked = false; /**< True when the suffix icon was clicked this frame. */
};

/**
 * @brief Renders the optional search bar slot above a tree panel.
 *
 * When EditorTreeSearchSlotConfig::enabled is false this is a no-op.
 * Otherwise renders a themed InputTextWithHint with an optional filter icon.
 * @param theme  Active editor theme.
 * @param config Search slot configuration.
 * @return True when the buffer contents changed this frame.
 */
bool RenderEditorTreeSearchSlot(const EditorTheme &theme,
                                const EditorTreeSearchSlotConfig &config);

/**
 * @brief Renders a single tree row (node or leaf) with optional prefix/suffix icons.
 *
 * Handles hover highlight, selection highlight, text colouring, and suffix icon
 * hit detection.  For Node items the caller must check EditorTreeItemResult::open
 * and render children before calling ImGui::TreePop().
 * @param theme Active editor theme.
 * @param item  Full row specification.
 * @return Result containing geometry, open state, and suffix click flag.
 */
EditorTreeItemResult DrawEditorTreeItem(const EditorTheme &theme,
                                        const EditorTreeItemSpec &item);

// ─────────────────────────────────────────────────────────────────────────────
// Panel top-bar system
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Renders a small icon-only button in the panel top bar.
 *
 * Applies a transparent button style, renders @p label (typically a Font Awesome
 * icon string), and restores styles.  Used internally by RenderEditorPanelTopBar
 * but available for custom panel headers.
 * @param theme Active editor theme.
 * @param label Button label / icon string.
 * @param size  Explicit pixel size.
 * @return True on click.
 */
bool EditorHeaderIconButton(const EditorTheme &theme, const char *label,
                            const ImVec2 &size);

/**
 * @brief Renders an error-coloured single-line text message.
 *
 * Uses palette.destructive as the text colour.  Intended for inline validation
 * error messages inside panels and modals.
 * @param theme Active editor theme.
 * @param text  Null-terminated error string.
 */
void ErrorText(const EditorTheme &theme, const char *text);

/**
 * @brief Identifies a named tab in an editor panel top bar.
 */
enum class EditorPanelTab {
  Scene,     /**< Hierarchy / scene graph view. */
  Project,   /**< Project file browser. */
  Viewport,  /**< 3D viewport. */
  Assets,    /**< Asset registry grid. */
  Console,   /**< Log / console output. */
  Animation, /**< Animation timeline. */
  MCP,       /**< Model Context Protocol inspector. */
};

/**
 * @brief One tab entry passed to RenderEditorPanelTopBar().
 */
struct EditorPanelTabItem {
  EditorPanelTab tab;             /**< Which tab this entry represents. */
  bool           selected = false; /**< Draws the accent underline when true. */
};

/**
 * @brief One item in a dropdown menu opened by a panel action button.
 *
 * Items are rendered recursively: items with @p childCount > 0 become
 * sub-menus; items with @p childCount == 0 become menu items that invoke
 * @p action on selection.
 *
 * @note The @p children pointer must remain valid for the entire duration of
 *       the ImGui frame in which the dropdown is rendered (stack arrays are fine).
 */
struct EditorPanelDropdownItem {
  const char                   *icon      = nullptr; /**< Optional Font Awesome icon prefix. */
  const char                   *label     = nullptr; /**< Menu item label text. */
  std::function<void()>          action;             /**< Callback invoked on selection (leaf items only). */
  const EditorPanelDropdownItem *children  = nullptr; /**< Pointer to child items (sub-menu). */
  size_t                         childCount = 0;     /**< Number of child items; 0 = leaf item. */
};

/**
 * @brief One action button entry in the panel top-bar action area.
 *
 * Action buttons sit in the top-right corner of a panel header.  Each button
 * either fires a callback via its dropdown or reports a click through
 * EditorPanelTopBarResult::clickedActionIndex.
 */
struct EditorPanelActionItem {
  const char                              *icon;           /**< Required Font Awesome icon displayed on the button. */
  const char                              *text    = nullptr; /**< Optional text rendered after the icon. */
  std::span<const EditorPanelDropdownItem>  dropdown = {};  /**< Non-empty → clicking opens a recursive popup menu.
                                                              *   Empty    → clickedActionIndex is set on click. */
  bool                                     enabled  = true; /**< When false the button is hidden and its space is reclaimed. */
};

/**
 * @brief Return value from RenderEditorPanelTopBar().
 */
struct EditorPanelTopBarResult {
  int clickedTabIndex    = -1; /**< Index into the @p tabs span of the tab clicked this frame, or -1. */
  int clickedActionIndex = -1; /**< Index into the @p actions span of the button clicked this frame
                                *   (only set when EditorPanelActionItem::dropdown is empty), or -1. */
};

/**
 * @brief Returns the display string for the given tab identifier.
 * @param tab Tab to look up.
 * @return Null-terminated label string (e.g. "Scene", "Assets").
 */
const char *EditorPanelTabLabel(EditorPanelTab tab);

/**
 * @brief Renders the full top bar of an editor panel: tabs + action buttons.
 *
 * Draws an underline-style tab bar on the left and icon action buttons on the
 * right, all within the current window's header area.  A full-width muted
 * divider is drawn at the tab baseline; the selected tab shows an accent-colour
 * underline segment extending to the same baseline.
 *
 * @param theme   Active editor theme.
 * @param id      Unique ImGui id string for this top bar (used for popup ids).
 * @param tabs    Ordered list of tabs to render.
 * @param actions Ordered list of action buttons to render (right-aligned).
 * @return Result indicating which tab or action was clicked, if any.
 */
EditorPanelTopBarResult RenderEditorPanelTopBar(
    const EditorTheme &theme, const char *id,
    std::span<const EditorPanelTabItem> tabs,
    std::span<const EditorPanelActionItem> actions);

// ─────────────────────────────────────────────────────────────────────────────
// Group A — Modal and picker shell
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Configuration for BeginEditorModal().
 */
struct EditorModalConfig {
  const char *id         = nullptr; /**< ImGui popup id (must match the id used for OpenPopup). */
  float       width      = 480.0f;  /**< Initial window width in pixels; 0 = skip SetNextWindowSize. */
  bool        autoResize = true;    /**< When true passes ImGuiWindowFlags_AlwaysAutoResize. */
};

/**
 * @brief Opens and begins an editor-themed modal popup window.
 *
 * If @p openThisFrame is true, calls ImGui::OpenPopup(@p cfg.id) before
 * BeginPopupModal so the caller can trigger the modal with a boolean flag
 * without a separate OpenPopup call.
 *
 * If @p cfg.width > 0, calls SetNextWindowSize to set the initial width.
 *
 * @par Usage pattern
 * @code
 * if (m_openModal) {
 *   Horo::Ui::EditorModalConfig cfg{"My Dialog##id"};
 *   if (Horo::Ui::BeginEditorModal(cfg, m_openModal)) {
 *     m_openModal = false;
 *     // ... body widgets ...
 *     auto r = Horo::Ui::RenderEditorModalFooter(theme, "OK");
 *     if (r.confirmed || r.cancelled) { }
 *     Horo::Ui::EndEditorModal();
 *   }
 * }
 * @endcode
 *
 * @param cfg           Modal configuration.
 * @param openThisFrame Pass true the frame the modal should open.
 * @return True while the modal is open; caller must render body and call EndEditorModal().
 */
bool BeginEditorModal(const EditorModalConfig &cfg, bool openThisFrame);

/**
 * @brief Closes and ends the current editor modal popup.
 *
 * Must be called after BeginEditorModal() returns true.
 */
void EndEditorModal();

/**
 * @brief Visual style for the modal footer produced by RenderEditorModalFooter().
 */
enum class EditorModalFooterStyle {
  OkCancel,          /**< Cancel + primary-action button (default). */
  DestructiveCancel, /**< Cancel + primary-action button tinted with palette.destructive. */
  ThreeWay,          /**< Alternate + Cancel + primary-action (e.g. Discard / Cancel / Save). */
};

/**
 * @brief Result returned by RenderEditorModalFooter().
 */
struct EditorModalFooterResult {
  bool confirmed = false; /**< True when the primary-action button was clicked. */
  bool cancelled = false; /**< True when Cancel was clicked (popup already closed). */
  bool alternate = false; /**< True when the ThreeWay alternate button was clicked (popup already closed). */
};

/**
 * @brief Renders the standard footer button row inside an editor modal.
 *
 * Always renders a Cancel button.  The primary button is labelled with
 * @p confirmLabel.  For ThreeWay style an additional @p alternateLabel button
 * is prepended.  DestructiveCancel tints the confirm button with
 * palette.destructive.  Calling CloseCurrentPopup() on cancel is handled
 * automatically.
 *
 * @param theme          Active editor theme.
 * @param confirmLabel   Label for the primary action button (e.g. "Save", "Delete").
 * @param style          Visual variant; defaults to OkCancel.
 * @param alternateLabel Label for the ThreeWay middle button; ignored for other styles.
 * @param buttonWidth    Width of each button in pixels.
 * @return Struct with booleans for each button outcome.
 */
EditorModalFooterResult RenderEditorModalFooter(
    const EditorTheme     &theme,
    const char            *confirmLabel,
    EditorModalFooterStyle style          = EditorModalFooterStyle::OkCancel,
    const char            *alternateLabel = nullptr,
    float                  buttonWidth    = 120.0f);

/**
 * @brief Configuration for BeginEditorPickerModal().
 */
struct EditorPickerConfig {
  const char *id        = nullptr; /**< ImGui popup id string. */
  const char *prompt    = nullptr; /**< Optional TextDisabled line rendered above the search field. */
  float       width     = 520.0f;  /**< Initial popup width in pixels. */
  const char *fieldHint = nullptr; /**< Placeholder text for the search input; defaults to "Search...". */
};

/**
 * @brief Opens and begins an editor-themed picker modal (search + list).
 *
 * A picker modal combines a search field with a scrollable list of rows.
 * It is appropriate for Command Palette, Quick Open, and Asset Search dialogs.
 *
 * The caller must:
 * -# Call BeginEditorPickerModal; return early if it returns false.
 * -# For each visible row, call EditorPickerModalRow().
 * -# Call EndEditorPickerModal() to close the scroll region and render the
 *    Close button / handle Escape.
 *
 * @param cfg            Picker configuration.
 * @param openThisFrame  True the frame the picker should open.
 * @param queryBuf       Writable buffer that receives search query input.
 * @param queryBufSize   Size of @p queryBuf in bytes.
 * @return True while the modal is open.
 */
bool BeginEditorPickerModal(const EditorPickerConfig &cfg,
                            bool   openThisFrame,
                            char  *queryBuf, size_t queryBufSize);

/**
 * @brief Renders one selectable row inside a picker modal.
 *
 * Must be called between BeginEditorPickerModal() and EndEditorPickerModal().
 * @param label    Row display string.
 * @param selected True to render the row in the selection colour.
 * @return True when this row is clicked.
 */
bool EditorPickerModalRow(const char *label, bool selected);

/**
 * @brief Ends the picker scroll region and renders the Close button.
 *
 * If the user clicks Close or presses Escape, sets @p openFlag to false,
 * clears @p query (if non-null), and calls CloseCurrentPopup.
 * @param openFlag In/out flag that tracks whether the picker is open.
 * @param query    Optional std::string to clear on close; pass nullptr to skip.
 */
void EndEditorPickerModal(bool &openFlag, std::string *query);

// ─────────────────────────────────────────────────────────────────────────────
// Group B — Input field primitives
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Renders a muted label above a themed text input field.
 *
 * Draws a TextDisabled label string, then a full-width (or @p width wide)
 * InputText or InputTextWithHint depending on whether @p hint is supplied.
 * @param label    Label string shown above the input; may be nullptr to skip.
 * @param id       ImGui widget id (use "##…" to hide from display).
 * @param buf      Writable text buffer.
 * @param bufSize  Size of @p buf in bytes.
 * @param width    Field width; 0 = full available width.
 * @param hint     Placeholder hint text; nullptr → plain InputText.
 * @return True when the buffer content changes this frame.
 */
bool RenderEditorLabeledInput(
    const char *label,
    const char *id,
    char       *buf,  size_t bufSize,
    float       width = 0.0f,
    const char *hint  = nullptr);

/**
 * @brief Renders a themed checkbox with accent CheckMark tint.
 *
 * Pushes palette.accent as ImGuiCol_CheckMark, calls ImGui::Checkbox, pops,
 * and optionally shows a tooltip when the item is hovered.
 * @param theme   Active editor theme.
 * @param label   Checkbox label rendered to the right of the box.
 * @param value   In/out boolean toggled by the checkbox.
 * @param tooltip Optional tooltip string; nullptr = no tooltip.
 * @return True when @p value changes this frame.
 */
bool RenderEditorCheckbox(
    const EditorTheme &theme,
    const char        *label,
    bool              &value,
    const char        *tooltip = nullptr);

/**
 * @brief Renders a custom pill-shaped toggle switch.
 *
 * Uses InvisibleButton for hit detection and AddRectFilled/AddCircleFilled on
 * the window draw-list to paint a rounded pill with a sliding handle, similar
 * to a mobile-style toggle.  The label is rendered inline to the right of the
 * pill.
 * @param theme  Active editor theme.
 * @param id     ImGui widget id for the invisible hit area.
 * @param label  Text rendered to the right of the pill; may be nullptr.
 * @param value  In/out boolean representing the on/off state.
 * @return True when @p value changes this frame.
 */
bool RenderEditorToggle(
    const EditorTheme &theme,
    const char        *id,
    const char        *label,
    bool              &value);

/**
 * @brief Optional parameters for RenderEditorDragFloat / RenderEditorDragFloat3.
 */
struct DragFloatOptions {
    float vmin = 0.0f;         /**< Minimum clamp value (0 = unclamped). */
    float vmax = 0.0f;         /**< Maximum clamp value (0 = unclamped). */
    const char *fmt = "%.3f"; /**< printf-style format for the displayed number. */
    float width = 0.0f;        /**< Field width; 0 = full available width. */
};

/**
 * @brief Renders a muted label above a DragFloat widget.
 * @param label   Label shown above the drag field; may be nullptr.
 * @param id      ImGui widget id for the DragFloat.
 * @param value   In/out float value.
 * @param speed   Drag sensitivity.
 * @param options Optional clamp range, format, and width.
 * @return True when @p value changes this frame.
 */
bool RenderEditorDragFloat(
    const char *label, const char *id,
    float      &value,
    float       speed = 0.1f,
    const DragFloatOptions &options = {});

/**
 * @brief Renders a muted label above a DragFloat3 widget (XYZ triple).
 * @param label   Label shown above the drag field; may be nullptr.
 * @param id      ImGui widget id.
 * @param value   In/out float[3] array (X, Y, Z).
 * @param speed   Drag sensitivity per component.
 * @param options Optional clamp range, format, and width.
 * @return True when any component changes this frame.
 */
bool RenderEditorDragFloat3(
    const char *label, const char *id,
    float       value[3],
    float       speed = 0.1f,
    const DragFloatOptions &options = {});

/**
 * @brief Renders a muted label above a SliderFloat widget.
 * @param label  Label shown above the slider; may be nullptr.
 * @param id     ImGui widget id.
 * @param value  In/out float value.
 * @param vmin   Slider minimum.
 * @param vmax   Slider maximum.
 * @param fmt    printf-style format for the displayed number.
 * @param width  Field width; 0 = full available width.
 * @return True when @p value changes this frame.
 */
bool RenderEditorSliderFloat(
    const char *label, const char *id,
    float      &value, float vmin, float vmax,
    const char *fmt   = "%.2f",
    float       width = 0.0f);

/**
 * @brief Renders a muted label above a ColorEdit3 widget.
 * @param label  Label shown above the colour picker; may be nullptr.
 * @param id     ImGui widget id.
 * @param color  In/out float[3] array (R, G, B in 0..1 range).
 * @param width  Field width; 0 = full available width.
 * @return True when @p color changes this frame.
 */
bool RenderEditorColorEdit3(
    const char *label, const char *id,
    float       color[3],
    float       width = 0.0f);

/**
 * @brief Begins a two-column property row (label | widget).
 *
 * Renders @p label at the current cursor X, then advances the cursor to
 * @p labelWidth so the caller can render any widget inline to the right.
 * Call EndEditorPropertyRow() after the widget.
 *
 * @par Example
 * @code
 * Horo::Ui::BeginEditorPropertyRow("Mesh", 120.0f);
 *   ImGui::TextUnformatted(obj.meshName.c_str());
 * Horo::Ui::EndEditorPropertyRow();
 * @endcode
 *
 * @param label      Left-column text.
 * @param labelWidth Pixel offset at which the widget column begins.
 * @return Always true; exists for RAII symmetry with EndEditorPropertyRow.
 */
bool BeginEditorPropertyRow(const char *label, float labelWidth = 120.0f);

/**
 * @brief Ends a property row begun with BeginEditorPropertyRow().
 *
 * Currently a no-op; exists so future versions can add row padding or
 * separators without changing call sites.
 */
void EndEditorPropertyRow();

// ─────────────────────────────────────────────────────────────────────────────
// Group C — Card and status primitives
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Configuration for BeginEditorCard().
 */
struct EditorCardConfig {
  const char *id       = nullptr; /**< ImGui child window id. */
  float       width    = 0.0f;    /**< Child window width; 0 = full available width. */
  float       height   = 0.0f;    /**< Child window height; 0 = auto. */
  bool        selected = false;   /**< When true applies a selection-tint to the child background. */
  bool        hovered  = false;   /**< Written by BeginEditorCard — true if the cursor is inside the card. */
};

/**
 * @brief Begins a bordered card child window with optional selection tint.
 *
 * If @p cfg.selected is true, pushes a translucent version of
 * palette.selection as ImGuiCol_ChildBg before calling BeginChild.
 * After BeginChild the pushed colour is popped so body widgets inherit
 * the normal style.  Sets @p cfg.hovered based on IsWindowHovered().
 *
 * @par Example
 * @code
 * Horo::Ui::EditorCardConfig cfg{"##my_card", tileW, tileH, isSelected};
 * if (Horo::Ui::BeginEditorCard(theme, cfg)) {
 *   // render thumbnail, label, etc.
 *   Horo::Ui::EndEditorCard();
 * }
 * @endcode
 *
 * @param theme Active editor theme.
 * @param cfg   Card configuration (hovered is written as an output).
 * @return True if the card is visible; caller must call EndEditorCard().
 */
bool BeginEditorCard(const EditorTheme &theme, EditorCardConfig &cfg);

/**
 * @brief Ends a card child window begun with BeginEditorCard().
 */
void EndEditorCard();

/**
 * @brief Severity level for RenderEditorStatusText().
 */
enum class EditorStatusLevel {
  Info,    /**< Informational — uses palette.textMuted. */
  Warning, /**< Caution — amber/yellow fixed colour. */
  Error,   /**< Error — uses palette.destructive. */
  Success, /**< Positive outcome — soft green fixed colour. */
};

/**
 * @brief Renders a coloured status message derived from the active theme palette.
 *
 * Selects the appropriate colour for @p level (Info → textMuted, Warning →
 * amber, Error → destructive, Success → soft green) and renders the text
 * with ImGui::TextColored.
 *
 * @param theme  Active editor theme.
 * @param level  Severity level that determines the text colour.
 * @param text   Message to display.
 */
void RenderEditorStatusText(const EditorTheme &theme,
                            EditorStatusLevel  level,
                            const char        *text);

// ─────────────────────────────────────────────────────────────────────────────
// Group D — Settings modal primitives
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief One entry in an editor vertical tab strip (used by Settings modal).
 *
 * Rows are rendered as selectable cards with an optional leading icon, a
 * bold label, and a muted description string. The @p selected flag drives
 * accent / selection tinting; @p id doubles as the ImGui id suffix.
 */
struct EditorVerticalTabItem {
  const char *id = nullptr;          /**< Stable ImGui id suffix (e.g. "mcp"). */
  const char *icon = nullptr;        /**< Optional Font Awesome icon string; may be nullptr. */
  const char *label = nullptr;       /**< Required primary label (e.g. "MCP"). */
  const char *description = nullptr; /**< Optional muted description under the label. */
  bool        selected = false;      /**< Drawn in accent/selection tint when true. */
};

/**
 * @brief Return value from RenderEditorVerticalTabs().
 */
struct EditorVerticalTabResult {
  int clickedIndex = -1; /**< Index of the tab clicked this frame, or -1. */
};

/**
 * @brief Renders a left-aligned vertical tab strip inside a fixed-width column.
 *
 * Each tab is a full-width selectable card that paints its own hover/selection
 * background via the draw list (so the visual stays consistent even when
 * tooltip popups or other style overrides are active). The function must be
 * called inside an existing ImGui window or child region; it creates its own
 * child window of the given @p width to host the tab stack.
 *
 * @param theme Active editor theme.
 * @param id    Stable ImGui id for the child window (e.g. "##settings_tabs").
 * @param tabs  Ordered list of tab items.
 * @param width Pixel width of the tab column.
 * @return Result containing the clicked tab index (or -1 if none).
 */
EditorVerticalTabResult RenderEditorVerticalTabs(
    const EditorTheme &theme,
    const char *id,
    std::span<const EditorVerticalTabItem> tabs,
    float width);

/**
 * @brief Begins a titled settings card inside a modal or panel.
 *
 * Pushes card colours, opens a child window with a border, renders the title
 * header, and positions the cursor for body content. Must be paired with
 * EndEditorSettingsCard() even when Begin returns false (mirrors the ImGui
 * child-window pattern used by BeginEditorCard()).
 *
 * @param theme Active editor theme.
 * @param id    ImGui id for the card child window.
 * @param title Header string displayed at the top of the card.
 * @return Always true while the card is visible; the caller must always call
 *         EndEditorSettingsCard().
 */
bool BeginEditorSettingsCard(const EditorTheme &theme,
                             const char *id,
                             const char *title);

/** @brief Ends a settings card begun with BeginEditorSettingsCard(). */
void EndEditorSettingsCard();

/**
 * @brief Renders a single settings row with a label and optional muted description.
 *
 * Intended for read-only informational rows such as the MCP Endpoint display.
 * Interactive fields (toggles, checkboxes, number inputs) should use their
 * existing primitives (RenderEditorToggle / RenderEditorCheckbox / ImGui::InputInt)
 * inline instead.
 * @param theme       Active editor theme.
 * @param label       Primary label; may be nullptr to skip.
 * @param description Muted secondary description; may be nullptr to skip.
 */
void RenderEditorSettingText(const EditorTheme &theme,
                             const char *label,
                             const char *description);

/**
 * @brief Return value from RenderEditorSettingsFooter().
 *
 * Unlike EditorModalFooterResult, this footer intentionally does NOT call
 * ImGui::CloseCurrentPopup(). The caller is responsible for closing the
 * popup only when save succeeded (OK) or the user chose to discard (Cancel).
 */
struct EditorSettingsFooterResult {
  bool cancelled = false; /**< True when Cancel was clicked this frame. */
  bool applied   = false; /**< True when Apply was clicked this frame. */
  bool accepted  = false; /**< True when OK was clicked this frame. */
};

/**
 * @brief Renders the Cancel / Apply / OK footer for a settings modal.
 *
 * Buttons are right-aligned in the order Cancel, Apply, OK. Apply is disabled
 * when @p canApply is false; OK is always enabled (it can close a clean modal).
 * The caller decides when to call ImGui::CloseCurrentPopup().
 *
 * @param theme       Active editor theme.
 * @param canApply    Enables the Apply button when true; disables it when false.
 * @param buttonWidth Pixel width of each button.
 * @return Which button (if any) was clicked this frame.
 */
EditorSettingsFooterResult RenderEditorSettingsFooter(
    const EditorTheme &theme,
    bool canApply,
    float buttonWidth = 104.0f);

} // namespace Horo::Ui
