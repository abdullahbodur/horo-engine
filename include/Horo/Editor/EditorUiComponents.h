#pragma once

#include "Horo/Editor/EditorTheme.h"

#include <imgui.h>

#include <array>
#include <functional>
#include <utility>

namespace Horo::Editor::Ui
{

    // ── Semantic button variant ──────────────────────────────────────────

    /** @brief Semantic button variant for shared editor buttons. */
    enum class ButtonVariant
    {
        Primary,
        Secondary,
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
    };

    /** @brief Draws a shared editor button primitive. */
    [[nodiscard]] bool Button(const ButtonProps &props);

    // ── Card / surface primitives ────────────────────────────────────────

    /** @brief RAII child surface matching the shared card visual contract. */
    class ScopedCard
    {
    public:
        explicit ScopedCard(const char *id,
                            ImVec2 size,
                            float padX = Theme::Layout::CardPad,
                            float padY = Theme::Layout::CardPad,
                            ImVec4 bg = Theme::Bg2());
        ~ScopedCard();

        ScopedCard(const ScopedCard &) = delete;
        ScopedCard &operator=(const ScopedCard &) = delete;
    };

    // ── Icon helpers ─────────────────────────────────────────────────────

    /** @brief Draws an icon-only close button using vector strokes, not glyph text. */
    [[nodiscard]] bool IconCloseButton(const char *id, ImVec2 size);

    // ── Typography primitives ────────────────────────────────────────────

    /** @brief Draws an uppercase section label. */
    void SectionTitle(const char *upperCaseLabel, const Theme::Fonts &fonts);

    /** @brief Draws an uppercase field label. */
    void FieldLabel(const char *upperCaseLabel, const Theme::Fonts &fonts);

    /** @brief Draws muted wrapped helper text. */
    void Hint(const char *text, const Theme::Fonts &fonts);

    // ── Separator ────────────────────────────────────────────────────────

    /** @brief Draws a dashed horizontal separator across available width. */
    void DashedSeparator(float dash = 4.0F, float gap = 3.0F);

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

    /** @brief Dropdown / combo box with shared frame styling. */
    void ComboControl(const char *id, int *value, const char *const items[], int itemCount, const Theme::Fonts &fonts);

    /** @brief Text input with shared frame styling. */
    void InputTextControl(const char *id, char *buffer, size_t bufferSize, const Theme::Fonts &fonts);

    /** @brief Integer input with shared frame styling. */
    void InputIntControl(const char *id, int *value, const Theme::Fonts &fonts);

    /** @brief Float input with shared frame styling. */
    void InputFloatControl(const char *id, float *value, const Theme::Fonts &fonts);

    /**
     * @brief Custom slider imitating an HTML <input type="range">.
     *
     * @param suffix A printf-style format string for the value label (e.g. "%d%%").
     * @param step   Quantisation step; a value of 25 for a 75–200 scale snaps to 75/100/125/….
     */
    void SliderIntControl(const char *id,
                          int *value,
                          int minValue,
                          int maxValue,
                          const char *suffix,
                          const Theme::Fonts &fonts,
                          int step = 1);

    /**
     * @brief iOS-style toggle switch.
     *
     * @param showLabel If true, draws an "Enabled" / "Disabled" label to the right.
     * @return True when the toggle was clicked.
     */
    [[nodiscard]] bool ToggleControl(const char *id, bool *value, const Theme::Fonts &fonts, bool showLabel = true);

    // ── Higher-order helpers ─────────────────────────────────────────────

    /**
     * @brief A plugin row: version + description + toggle, rendered inside
     *        a SettingRow for the plugin name.
     */
    void PluginRow(const char *name,
                   const char *version,
                   const char *description,
                   bool *enabled,
                   const Theme::Fonts &fonts);

    /** @brief Draws a keyboard shortcut display (keycap chips). */
    void ShortcutDisplay(const char *a, const char *b, const char *c, const Theme::Fonts &fonts);

    /** @brief Draws a colour-theme chip (swatch dot + label). Returns true when clicked. */
    [[nodiscard]] bool ThemeChip(const char *label, ImVec4 swatch, bool active, const Theme::Fonts &fonts);

} // namespace Horo::Editor::Ui
