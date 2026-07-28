#pragma once

#include "Horo/Editor/EditorTheme.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace Horo::Editor::Ui
{

// ── Semantic button variant ──────────────────────────────────────────

/** @brief Semantic button variant for shared editor buttons. */
enum class ButtonVariant
{
    Primary,
    Secondary,
};

/** @brief Predefined size presets for shared buttons. */
enum class ButtonSize
{
    Small,
    Medium,
    Large,
};

// ── Button props & primitive ─────────────────────────────────────────

/** @brief Input contract for the shared editor button primitive. */
struct ButtonProps
{
    const char *label = "";
    ImVec2 size = {0.0F, 0.0F};
    ButtonVariant variant = ButtonVariant::Primary;
    bool enabled = true;
    float fontSize = 14.0F;
    ImFont *font = nullptr;
    float baseFontSize = Theme::FontPx::Sans;
    ButtonSize componentSize = ButtonSize::Medium;
    bool fillAvailableWidth = false;
};

/** @brief Draws a shared editor button primitive. */
[[nodiscard]] bool Button(const ButtonProps &props);

// ── Card / surface primitives ────────────────────────────────────────

/** @brief RAII child surface matching the shared card visual contract. */
class ScopedCard
{
  public:
    explicit ScopedCard(const char *id, ImVec2 size, float padX = Theme::Layout::CardPad,
                        float padY = Theme::Layout::CardPad, ImVec4 bg = Theme::Bg2(), bool autoResizeY = false);
    ~ScopedCard();

    ScopedCard(const ScopedCard &) = delete;
    ScopedCard &operator=(const ScopedCard &) = delete;
};

// ── Icon helpers ─────────────────────────────────────────────────────

/** @brief Draws an icon-only close button using vector strokes, not glyph text. */
[[nodiscard]] bool IconCloseButton(const char *id, ImVec2 size);

/** @brief Direction rendered by the compact navigation icon button. */
enum class NavigationIcon : std::uint8_t
{
    Back,
    Forward,
    Up,
};

/**
 * @brief Draws a borderless navigation button with a compact hover surface.
 * @param id Stable UI identity.
 * @param icon Directional icon to render.
 * @param size Exact interaction bounds.
 * @param enabled Whether the action may be invoked.
 * @return True when an enabled button was activated.
 */
[[nodiscard]] bool NavigationIconButton(
    const char *id, NavigationIcon icon, ImVec2 size, bool enabled = true);

/** @brief Vector glyph supported by the shared compact icon button. */
enum class IconButtonGlyph : std::uint8_t
{
    Plus,
    Folder,
};

/** @brief Input contract for a shared compact icon-only action. */
struct IconButtonProps
{
    const char* id = "";
    IconButtonGlyph glyph = IconButtonGlyph::Plus;
    ImVec2 size = {32.0F, 32.0F};
    const char* tooltip = "";
    bool enabled = true;
};

/**
 * @brief Draws a themed icon-only action with hover, focus, and tooltip states.
 * @param props Stable identity, glyph, geometry, tooltip, and enabled state.
 * @return True when the enabled action was activated.
 */
[[nodiscard]] bool IconButton(const IconButtonProps& props);

// ── Typography primitives ────────────────────────────────────────────

/** @brief Draws an uppercase section label. */
void SectionTitle(const char *upperCaseLabel, const Theme::Fonts &fonts);

/** @brief Draws an uppercase field label. */
void FieldLabel(const char *upperCaseLabel, const Theme::Fonts &fonts);

/** @brief Renders a hint string wrapped under a field. */
void Hint(const char *text, const Theme::Fonts &fonts);

/** @brief Renders an error string wrapped under a field. */
void ErrorText(const char *text, const Theme::Fonts &fonts);

/**
 * @brief Draws compact clickable text without a button surface.
 * @param id Stable UI identity.
 * @param label Visible link text.
 * @param font Font used for measurement and rendering.
 * @param fontSize Visible text size.
 * @param current Whether the link represents the current destination.
 * @return True when activated.
 */
[[nodiscard]] bool TextLink(const char* id, const char* label, ImFont* font, float fontSize, bool current = false);

// ── Badge / tag primitives ───────────────────────────────────────────

/** @brief Semantic visual tone for reusable badges and tags. */
enum class BadgeTone : std::uint8_t
{
    Neutral,
    Accent,
    Success,
    Warning,
    Error,
};

/** @brief Shared geometry presets for badges and status pills. */
enum class BadgeSize : std::uint8_t
{
    Small,
    Medium,
};

/** @brief Input contract for the shared compact badge/tag primitive. */
struct BadgeProps
{
    const char* label = ""; /**< Visible badge text. */
    BadgeTone tone = BadgeTone::Neutral; /**< Semantic color treatment. */
    BadgeSize size = BadgeSize::Small; /**< Shared geometry preset. */
    bool leadingIndicator = false; /**< Whether to draw a status dot before the label. */
};

/**
 * @brief Draws a compact, vertically centred badge and advances the inline cursor.
 * @param props Visible content and semantic presentation.
 * @param fonts Editor typography handles.
 */
void Badge(const BadgeProps& props, const Theme::Fonts& fonts);

/**
 * @brief Returns the exact horizontal space consumed by @ref Badge.
 * @param props Visible content and semantic presentation.
 * @param fonts Editor typography handles.
 * @return Width in logical UI pixels.
 */
[[nodiscard]] float BadgeWidth(const BadgeProps& props, const Theme::Fonts& fonts);

/**
 * @brief Resolves the semantic foreground color used by a badge tone.
 * @param tone Semantic badge state.
 * @return Theme-derived foreground color.
 */
[[nodiscard]] ImVec4 BadgeToneColor(BadgeTone tone);

// ── Separator ────────────────────────────────────────────────────────

/** @brief Draws a dashed horizontal separator across available width. */
void DashedSeparator(float dash = 4.0F, float gap = 3.0F);

/**
 * @brief Draws a compact label followed by a horizontal separator.
 * @param label Visible separator label.
 * @param fonts Editor typography handles.
 */
void LabeledSeparator(const char* label, const Theme::Fonts& fonts);

// ── Settings / form row primitives ───────────────────────────────────

/**
 * @brief Draws a settings section group heading with a horizontal rule.
 *
 * @param first Pass true for the first group in a section to suppress
 *              the top spacing.
 */
void SettingGroup(const char *label, const Theme::Fonts &fonts, bool first = false);

/**
 * @brief Generic two-column settings row.
 *
 * Column 0 holds the label + optional description; column 1
 * invokes the control callback and has a fixed width of
 * `Theme::Layout::ControlW`.
 */
template <typename ControlFn>
void SettingRow(const char *label, const char *description, const Theme::Fonts &f, ControlFn &&control)
{
    ImGui::PushID(label);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{0.0F, 0.0F});
    if (ImGui::BeginTable("row", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("info", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthFixed, Theme::Layout::ControlW);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::BeginGroup();
        {
            Theme::ScopedTextStyle ts(f.sans, 16.0F, Theme::FontPx::Sans);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
        }
        if (description != nullptr && description[0] != '\0')
        {
            Theme::ScopedTextStyle ts(f.sans, 14.0F, Theme::FontPx::Sans);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 16.0F);
            ImGui::TextWrapped("%s", description);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::EndGroup();

        ImGui::TableSetColumnIndex(1);
        control();

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    ImGui::Dummy({0.0F, 10.0F});
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine({p.x, p.y}, {p.x + w, p.y}, Theme::U32(Theme::Border()), 1.0F);
    ImGui::Dummy({0.0F, 10.0F});
    ImGui::PopID();
}

// ── Form controls ────────────────────────────────────────────────────

/** @brief Non-owning callbacks used by a combo whose entries come from a typed model. */
struct ComboItemSource
{
    const void *context = nullptr; /**< Model passed unchanged to each callback. */
    const char *(*label)(const void *context, int index) = nullptr; /**< Required display-label callback. */
    bool (*enabled)(const void *context, int index) = nullptr; /**< Optional selection predicate; null enables all. */
    const char *(*disabledTooltip)(const void *context, int index) =
        nullptr; /**< Optional diagnostic for disabled rows. */
};

/** @brief Renders a styled dropdown with optional error styling. Returns true if the selection changed. */
[[nodiscard]] bool ComboControl(const char *id, int *value, const char *const items[], int itemCount,
                                const Theme::Fonts &fonts, bool error = false, float height = 0.0F);

/**
 * @brief Renders the shared dropdown design for entries projected from a typed model.
 * @param id Stable UI identity.
 * @param value Selected entry index.
 * @param itemCount Number of entries exposed by @p source.
 * @param source Non-owning entry callbacks valid for the duration of this call.
 * @param fonts Editor typography handles.
 * @param error Whether to render the field in its error state.
 * @param height Explicit field height, or zero to use the shared default.
 * @return True when an enabled entry changed the selection.
 */
[[nodiscard]] bool ComboControl(const char *id, int *value, int itemCount, const ComboItemSource &source,
                                const Theme::Fonts &fonts, bool error = false, float height = 0.0F);

/**
 * @brief Renders an input text field with shared frame styling and optional error state.
 * @param id Stable UI identity.
 * @param buffer Mutable null-terminated text buffer.
 * @param bufferSize Capacity of @p buffer, including the null terminator.
 * @param fonts Editor typography handles.
 * @param error Whether to render the field in its error state.
 * @param width Requested control width; negative values fill the remaining content width.
 * @return True when the text changed.
 */
[[nodiscard]] bool InputTextControl(const char *id, char *buffer, size_t bufferSize, const Theme::Fonts &fonts,
                                    bool error = false, float width = -1.0F);

/**
 * @brief Renders the string-backed overload of the shared input text field.
 * @param id Stable UI identity.
 * @param value Mutable text value.
 * @param maxSize Maximum storage size, including the null terminator.
 * @param fonts Editor typography handles.
 * @param error Whether to render the field in its error state.
 * @param width Requested control width; negative values fill the remaining content width.
 * @return True when the text changed.
 */
[[nodiscard]] bool InputTextControl(const char *id, std::string &value, size_t maxSize, const Theme::Fonts &fonts,
                                    bool error = false, float width = -1.0F);

/**
 * @brief Draws a hex color input paired with a clickable swatch and anchored picker popup.
 *
 * @param id Stable UI identity.
 * @param buffer Mutable `#RRGGBB` draft buffer. Invalid intermediate text leaves the last valid swatch visible.
 * @param bufferSize Buffer capacity.
 * @param fonts Editor typography handles.
 * @return True when a valid color value was committed into @p buffer by typing or the picker.
 */
[[nodiscard]] bool ColorHexControl(const char *id, char *buffer, size_t bufferSize, const Theme::Fonts &fonts);
[[nodiscard]] bool ColorHexControl(const char *id, std::string &value, size_t maxSize, const Theme::Fonts &fonts);

enum class SliderValueFormat : std::uint8_t
{
    Integer,
    Minutes,
    Percent,
    Milliseconds,
};

/** @brief Integer input with shared frame styling. */
void InputIntControl(const char *id, int *value, const Theme::Fonts &fonts);

/** @brief Float input with shared frame styling. */
void InputFloatControl(const char *id, float *value, const Theme::Fonts &fonts);

/**
 * @brief Custom slider imitating an HTML <input type="range">.
 *
 * @param format Typed format for the value label.
 * @param step   Quantisation step; a value of 25 for a 75–200 scale snaps to 75/100/125/….
 */
void SliderIntControl(const char *id, int *value, int minValue, int maxValue, SliderValueFormat format,
                      const Theme::Fonts &fonts, int step = 1);

/**
 * @brief iOS-style toggle switch.
 *
 * @param showLabel If true, draws an "Enabled" / "Disabled" label to the right.
 * @return True when the toggle was clicked.
 */
[[nodiscard]] bool ToggleControl(const char *id, bool *value, const Theme::Fonts &fonts, bool showLabel = true);

/**
 * @brief A standard styled checkbox.
 *
 * @param label The label to show next to the checkbox.
 * @param value Pointer to the boolean state.
 * @param fonts The application font set.
 * @return True when the checkbox was clicked.
 */
[[nodiscard]] bool CheckboxControl(const char *label, bool *value, const Theme::Fonts &fonts);

// ── Higher-order helpers ─────────────────────────────────────────────

/**
 * @brief A plugin row: version + description + toggle, rendered inside
 *        a SettingRow for the plugin name.
 */
void PluginRow(const char *name, const char *version, const char *description, bool *enabled,
               const Theme::Fonts &fonts);

/** @brief Draws a keyboard shortcut display (keycap chips). */
void ShortcutDisplay(const char *a, const char *b, const char *c, const Theme::Fonts &fonts);

/**
 * @brief Interactive shortcut key recorder.
 *
 * When @p listening is false, renders the current key binding as kbd chips
 * inside a dashed-border clickable area. Sets @p listening = true on click.
 *
 * When @p listening is true, shows "Press keys..." with a pulse effect
 * and polls ImGui key state. On next non-modifier key press, writes the
 * combo string into @p keysOut (truncated to @p keysOutSize) and sets
 * @p listening = false. Escape cancels without writing.
 *
 * @return true if a new key combo was just recorded this frame.
 */
[[nodiscard]] bool ShortcutRecorder(const char *id, const char *keysLabel, bool *listening, std::string &keysOut,
                                    const Theme::Fonts &fonts, const char *placeholderText = "Click to record",
                                    const char *listeningText = "Press keys...");

/** @brief Draws a colour-theme chip (swatch dot + label). Returns true when clicked. */
[[nodiscard]] bool ThemeChip(const char *label, ImVec4 swatch, bool active, const Theme::Fonts &fonts);

// ── Modal layout primitives ──────────────────────────────────────────

/** @brief Shared geometry and chrome configuration for an editor workflow modal. */
struct ModalShellProps
{
    const char* id = "EditorModal"; /**< Stable ImGui window identity. */
    const char* title = ""; /**< Visible title-bar text. */
    ImVec2 requestedSize = {Theme::Layout::ModalW, Theme::Layout::ModalH};
    float viewportPadding = 48.0F;
    float minimumWidth = 360.0F;
    float minimumHeight = 360.0F;
    float headerHeight = Theme::Layout::HeaderH;
    float footerHeight = Theme::Layout::FooterH;
    ImTextureID logo = 0;
    bool showBrandMark = false;
    bool showClose = true;
    float titleFontSize = 14.0F;
};

/**
 * @brief RAII modal window with shared positioning, title bar, body geometry,
 *        footer surface, and close action.
 *
 * Exclusive focus and close policy remain owned by EditorModalHost. This
 * component owns presentation only.
 */
class ScopedModalShell
{
  public:
    /**
     * @brief Begins a modal window and draws its shared title-bar chrome.
     * @param props Modal identity, geometry, title, and optional brand treatment.
     * @param fonts Editor typography handles.
     */
    ScopedModalShell(const ModalShellProps& props, const Theme::Fonts& fonts);

    /** @brief Ends any open footer and releases the modal ImGui style stack. */
    ~ScopedModalShell();

    ScopedModalShell(const ScopedModalShell&) = delete;
    ScopedModalShell& operator=(const ScopedModalShell&) = delete;

    /** @brief Returns the vertical space between title bar and footer. */
    [[nodiscard]] float BodyHeight() const noexcept;

    /** @brief Returns the local Y coordinate where the footer begins. */
    [[nodiscard]] float FooterStartY() const noexcept;

    /** @brief Returns true when the title-bar close action was activated. */
    [[nodiscard]] bool CloseRequested() const noexcept;

    /**
     * @brief Begins the shared fixed footer surface.
     * @param padding Inner footer padding.
     * @param border Whether the footer child owns a border.
     */
    void BeginFooter(ImVec2 padding, bool border = false);

    /** @brief Ends the footer surface opened by @ref BeginFooter. */
    void EndFooter();

  private:
    float bodyHeight_{};
    float footerStartY_{};
    float footerHeight_{};
    bool closeRequested_{};
    bool footerOpen_{};
};

/** @brief Presentation configuration for a modal sidebar/content split. */
struct ModalSplitPaneProps
{
    const char* id = "ModalSplitPane";
    ImVec2 size = {0.0F, 0.0F};
    float leadingWidth = Theme::Layout::SidebarW;
    float gap = 0.0F;
    ImVec2 leadingPadding = {
        Theme::Layout::SidebarPadX, Theme::Layout::SidebarPadY};
    ImVec2 contentPadding = {
        Theme::Layout::BodyPadX, Theme::Layout::BodyPadY};
    ImVec4 leadingBackground = Theme::Bg0();
    ImVec4 contentBackground = Theme::Bg1();
    bool drawDivider = true;
    bool leadingScrollable = false;
    bool contentScrollable = true;
};

/**
 * @brief Draws an optional-navigation modal layout without retaining callbacks.
 * @param props Split geometry and semantic surfaces.
 * @param drawLeading Draws sidebar/navigation content.
 * @param drawContent Draws the primary pane content.
 */
template <typename LeadingFn, typename ContentFn>
void ModalSplitPane(
    const ModalSplitPaneProps& props, LeadingFn&& drawLeading,
    ContentFn&& drawContent)
{
    ImGui::PushID(props.id);
    const ImGuiWindowFlags leadingFlags =
        props.leadingScrollable
            ? ImGuiWindowFlags_AlwaysUseWindowPadding
            : ImGuiWindowFlags_AlwaysUseWindowPadding |
                  ImGuiWindowFlags_NoScrollbar |
                  ImGuiWindowFlags_NoScrollWithMouse;
    const ImGuiWindowFlags contentFlags =
        props.contentScrollable
            ? ImGuiWindowFlags_AlwaysUseWindowPadding
            : ImGuiWindowFlags_AlwaysUseWindowPadding |
                  ImGuiWindowFlags_NoScrollbar |
                  ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, props.leadingPadding);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, props.leadingBackground);
    ImGui::BeginChild(
        "##leading", {props.leadingWidth, props.size.y}, false, leadingFlags);
    drawLeading();
    const ImVec2 leadingPosition = ImGui::GetWindowPos();
    const ImVec2 leadingSize = ImGui::GetWindowSize();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (props.drawDivider)
    {
        ImGui::GetWindowDrawList()->AddLine(
            {leadingPosition.x + leadingSize.x - 1.0F, leadingPosition.y},
            {leadingPosition.x + leadingSize.x - 1.0F,
             leadingPosition.y + leadingSize.y},
            Theme::U32(Theme::Border()), 1.0F);
    }

    ImGui::SameLine(0.0F, props.gap);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, props.contentPadding);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, props.contentBackground);
    ImGui::BeginChild("##content", {0.0F, props.size.y}, false, contentFlags);
    drawContent();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

// ── Dock UI ───────────────────────────────────────────────────────────

int DrawDockTabs(std::span<const char *const> tabs, int activeTab, const Theme::Fonts &fonts);

void DrawObjTitle(const char *title, const char *badgeText, ImVec4 badgeBg, ImVec4 badgeFg, const Theme::Fonts &fonts);
void DrawPropSection(const char *label, const Theme::Fonts &fonts);
void DrawPropRow(const char *label, const char *value, const Theme::Fonts &fonts);

/** @brief Semantic text tone for shared context-menu actions. */
enum class ContextMenuItemTone
{
    Normal,
    Danger,
};

/**
 * @brief Opens a styled context popup for the preceding ImGui item when requested.
 * @param id Stable popup identity scoped by the caller.
 * @return True while the popup is open; pair with @ref EndContextMenu.
 */
[[nodiscard]] bool BeginContextMenu(const char *id);

/** @brief Opens a styled context popup for empty space in the current window. */
[[nodiscard]] bool BeginContextWindowMenu(const char *id);

/** @brief Ends a context popup opened by @ref BeginContextMenu. */
void EndContextMenu();

/**
 * @brief Draws one shared context-menu action row.
 * @param label Localized action label.
 * @param shortcut Optional platform shortcut label.
 * @param fonts Editor typography handles.
 * @param tone Semantic action tone.
 * @return True when the action was activated.
 */
[[nodiscard]] bool ContextMenuItem(const char *label, const char *shortcut, const Theme::Fonts &fonts,
                                   ContextMenuItemTone tone = ContextMenuItemTone::Normal,
                                   std::string_view iconToken = {});

/** @brief Opens one shared nested context-menu category row. */
[[nodiscard]] bool BeginContextSubmenu(const char *label, const Theme::Fonts &fonts,
                                       std::string_view iconToken = {});

/** @brief Ends a nested context-menu category opened by @ref BeginContextSubmenu. */
void EndContextSubmenu();

/** @brief Draws one centralized editor icon token into the supplied bounds. */
void DrawEditorIcon(ImDrawList *drawList, std::string_view iconToken, ImVec2 position, ImVec2 size, ImU32 color);

/** @brief Draws a shared inset context-menu separator. */
void ContextMenuSeparator();

/** @brief Interaction result returned by an editable inspector property row. */
struct Float3PropertyEditResult
{
    bool changed{false}; /**< Value changed during the current frame. */
    bool committed{false}; /**< The current edit interaction completed with a changed value. */
};

/**
 * @brief Draws a shared inspector row for editing three floating-point axis values.
 * @param label Localized property label.
 * @param id Stable UI identity scoped by the caller.
 * @param value Mutable X, Y, and Z values.
 * @param fonts Editor typography handles.
 * @param speed Mouse-drag increment.
 * @return Per-frame change and interaction-commit state.
 */
[[nodiscard]] Float3PropertyEditResult DrawFloat3PropRow(const char *label, const char *id,
                                                         std::array<float, 3> &value, const Theme::Fonts &fonts,
                                                         float speed = 0.05F);

} // namespace Horo::Editor::Ui
