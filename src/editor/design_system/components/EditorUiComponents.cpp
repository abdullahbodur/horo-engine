/** @copydoc EditorUiComponents.h */

#include "Horo/Editor/EditorUiComponents.h"

#include "Horo/Editor/EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Horo::Editor::Ui
{

    namespace
    {

        void PushControlStyle()
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 5.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Layout::Radius);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::Bg3());
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::Hover());
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::Hover());
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
        }

        void PopControlStyle()
        {
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(3);
        }

    } // namespace

    // ── Button ───────────────────────────────────────────────────────────

    [[nodiscard]] bool Button(const ButtonProps &props)
    {
        using namespace Theme;

        if (!props.enabled)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45F);
        }

        if (props.variant == ButtonVariant::Primary)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Accent());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentHover());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentActive());
            ImGui::PushStyleColor(ImGuiCol_Text, DarkText());
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Bg3());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentSoft());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{Accent().x, Accent().y, Accent().z, 0.24F});
            ImGui::PushStyleColor(ImGuiCol_Text, Text());
        }

        bool clicked = false;
        {
            ScopedTextStyle textStyle(props.font, props.fontSize, props.baseFontSize);
            clicked = ImGui::Button(props.label, props.size);
        }

        ImGui::PopStyleColor(4);
        if (!props.enabled)
        {
            ImGui::PopStyleVar();
        }
        return clicked;
    }

    // ── ScopedCard ───────────────────────────────────────────────────────

    ScopedCard::ScopedCard(const char *id, const ImVec2 size, const float padX, const float padY, const ImVec4 bg)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{padX, padY});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
        ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar);
    }

    ScopedCard::~ScopedCard()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ── IconCloseButton ──────────────────────────────────────────────────

    [[nodiscard]] bool IconCloseButton(const char *id, const ImVec2 size)
    {
        using namespace Theme;

        ImGui::PushID(id);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##close", size);
        const bool hovered = ImGui::IsItemHovered();

        auto *dl = ImGui::GetWindowDrawList();
        const float pad = 4.0F;
        const ImVec2 a{pos.x + pad, pos.y + pad};
        const ImVec2 b{pos.x + size.x - pad, pos.y + size.y - pad};
        const ImU32 col = U32(hovered ? Text() : Dim());
        dl->AddLine(a, b, col, 1.5F);
        dl->AddLine({b.x, a.y}, {a.x, b.y}, col, 1.5F);

        ImGui::PopID();
        return clicked;
    }

    // ── SectionTitle ─────────────────────────────────────────────────────

    void SectionTitle(const char *upperCaseLabel, const Theme::Fonts &fonts)
    {
        Theme::ScopedTextStyle ts(fonts.monoSemiBold, 18.0F, Theme::FontPx::MonoSemiBold);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
        ImGui::TextUnformatted(upperCaseLabel);
        ImGui::PopStyleColor();
    }

    // ── FieldLabel ───────────────────────────────────────────────────────

    void FieldLabel(const char *upperCaseLabel, const Theme::Fonts &fonts)
    {
        Theme::ScopedTextStyle ts(fonts.monoSemiBold, 12.0F, Theme::FontPx::MonoSemiBold);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
        ImGui::TextUnformatted(upperCaseLabel);
        ImGui::PopStyleColor();
    }

    // ── Hint ─────────────────────────────────────────────────────────────

    void Hint(const char *text, const Theme::Fonts &fonts)
    {
        Theme::ScopedTextStyle ts(fonts.sans, 12.0F, Theme::FontPx::Sans);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s", text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    // ── DashedSeparator ──────────────────────────────────────────────────

    void DashedSeparator(const float dash, const float gap)
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        auto *dl = ImGui::GetWindowDrawList();
        float x = p.x;
        while (x < p.x + w)
        {
            const float end = std::min(x + dash, p.x + w);
            dl->AddLine({x, p.y}, {end, p.y}, Theme::U32(Theme::Border()), 1.0F);
            x = end + gap;
        }
        ImGui::Dummy({0.0F, 4.0F});
    }

    // ── SettingGroup ─────────────────────────────────────────────────────

    void SettingGroup(const char *label, const Theme::Fonts &fonts, const bool first)
    {
        if (!first)
        {
            ImGui::Dummy({0.0F, 18.0F});
        }

        Theme::ScopedTextStyle ts(fonts.monoSemiBold, 13.0F, Theme::FontPx::MonoSemiBold);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();

        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddLine(p, {p.x + w, p.y}, Theme::U32(Theme::Border()), 1.0F);
        ImGui::Dummy({0.0F, 8.0F});
    }

    // ── ComboControl ─────────────────────────────────────────────────────

    void ComboControl(const char *id, int *value, const char *const items[], const int itemCount, const Theme::Fonts &fonts)
    {
        PushControlStyle();
        ImGui::PushItemWidth(-1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.0F, 4.0F});
        {
            Theme::ScopedTextStyle ts(fonts.mono, 15.0F, Theme::FontPx::Mono);
            ImGui::Combo(id, value, items, itemCount);
        }
        ImGui::PopStyleVar();
        ImGui::PopItemWidth();
        PopControlStyle();
    }

    // ── InputTextControl ─────────────────────────────────────────────────

    void InputTextControl(const char *id, char *buffer, const size_t bufferSize, const Theme::Fonts &fonts)
    {
        PushControlStyle();
        ImGui::PushItemWidth(-1.0F);
        {
            Theme::ScopedTextStyle ts(fonts.mono, 14.0F, Theme::FontPx::Mono);
            ImGui::InputText(id, buffer, bufferSize);
        }
        ImGui::PopItemWidth();
        PopControlStyle();
    }

    // ── InputIntControl ──────────────────────────────────────────────────

    void InputIntControl(const char *id, int *value, const Theme::Fonts &fonts)
    {
        PushControlStyle();
        ImGui::PushItemWidth(-1.0F);
        {
            Theme::ScopedTextStyle ts(fonts.mono, 14.0F, Theme::FontPx::Mono);
            ImGui::InputInt(id, value, 1, 4);
        }
        ImGui::PopItemWidth();
        PopControlStyle();
    }

    // ── InputFloatControl ────────────────────────────────────────────────

    void InputFloatControl(const char *id, float *value, const Theme::Fonts &fonts)
    {
        PushControlStyle();
        ImGui::PushItemWidth(-1.0F);
        {
            Theme::ScopedTextStyle ts(fonts.mono, 14.0F, Theme::FontPx::Mono);
            ImGui::InputFloat(id, value, 0.1F, 1.0F, "%.1f");
        }
        ImGui::PopItemWidth();
        PopControlStyle();
    }

    // ── SliderIntControl ─────────────────────────────────────────────────

    void SliderIntControl(const char *id,
                          int *value,
                          const int minValue,
                          const int maxValue,
                          const char *suffix,
                          const Theme::Fonts &fonts,
                          const int step)
    {
        ImGui::PushID(id);
        constexpr float TrackW = Theme::Layout::ControlW - 54.0F;
        constexpr float TrackH = 4.0F;
        constexpr float HitH = 22.0F;
        constexpr float KnobR = 7.0F;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("slider", {TrackW, HitH});
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();

        if (active && maxValue > minValue)
        {
            const float mouseT = (ImGui::GetIO().MousePos.x - pos.x) / TrackW;
            const float clampedT = std::clamp(mouseT, 0.0F, 1.0F);
            const float rawValue = static_cast<float>(minValue) +
                                   clampedT * static_cast<float>(maxValue - minValue);
            const int snapped = minValue +
                                static_cast<int>(std::round((rawValue - static_cast<float>(minValue)) /
                                                            static_cast<float>(step))) *
                                    step;
            *value = std::clamp(snapped, minValue, maxValue);
        }

        const float t = maxValue > minValue
                            ? (static_cast<float>(*value - minValue) / static_cast<float>(maxValue - minValue))
                            : 0.0F;
        const float trackY = pos.y + (HitH - TrackH) * 0.5F;
        auto *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled({pos.x, trackY}, {pos.x + TrackW, trackY + TrackH}, Theme::U32(Theme::BorderStrong()), 2.0F);
        dl->AddRectFilled({pos.x, trackY}, {pos.x + TrackW * t, trackY + TrackH}, Theme::U32(Theme::Accent()), 2.0F);

        const ImVec2 knob{pos.x + TrackW * t, pos.y + HitH * 0.5F};
        dl->AddCircleFilled(knob, KnobR + (hovered || active ? 1.0F : 0.0F), Theme::U32(Theme::AccentHover()), 20);
        dl->AddCircleFilled(knob, KnobR, Theme::U32(Theme::Accent()), 20);
        dl->AddCircle(knob, KnobR + 1.0F, Theme::U32(Theme::Bg1()), 20, 2.0F);

        ImGui::SameLine(0.0F, 10.0F);

        char text[32]{};
        // cppcheck-suppress wrongPrintfScanfArgNum — suffix is a format string
        std::snprintf(text, sizeof(text), suffix, *value);
        {
            Theme::ScopedTextStyle ts(fonts.mono, 14.0F, Theme::FontPx::Mono);
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (HitH - textSize.y) * 0.5F);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }

    // ── ToggleControl ────────────────────────────────────────────────────

    [[nodiscard]] bool ToggleControl(const char *id, bool *value, const Theme::Fonts &fonts, const bool showLabel)
    {
        ImGui::PushID(id);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size{36.0F, 20.0F};
        ImGui::InvisibleButton("toggle", size);
        const bool clicked = ImGui::IsItemClicked();
        if (clicked)
        {
            *value = !*value;
        }

        const bool hovered = ImGui::IsItemHovered();
        auto *dl = ImGui::GetWindowDrawList();
        ImVec4 bg = Theme::Bg3();
        if (*value)
            bg = Theme::Accent();
        else if (hovered)
            bg = Theme::Hover();
        dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, Theme::U32(bg), 10.0F);
        dl->AddRect(pos, {pos.x + size.x, pos.y + size.y}, Theme::U32(*value ? Theme::Accent() : Theme::Border()), 10.0F);
        const float knobX = *value ? pos.x + 21.0F : pos.x + 3.0F;
        dl->AddCircleFilled({knobX + 6.0F, pos.y + 10.0F}, 6.0F, Theme::U32(*value ? ImVec4{1, 1, 1, 1} : Theme::Dim()), 16);

        if (showLabel)
        {
            ImGui::SameLine(0.0F, 10.0F);
            Theme::ScopedTextStyle ts(fonts.sans, 13.0F, Theme::FontPx::Sans);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
            ImGui::TextUnformatted(*value ? "Enabled" : "Disabled");
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
        return clicked;
    }

    // ── PluginRow ────────────────────────────────────────────────────────

    void PluginRow(const char *name,
                   const char *version,
                   const char *description,
                   bool *enabled,
                   const Theme::Fonts &fonts)
    {
        SettingRow(name, nullptr, fonts, [&]() {
            const float cursorY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(cursorY + 4.0F);
            {
                Theme::ScopedTextStyle ts(fonts.mono, 10.5F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                ImGui::TextUnformatted(version);
                ImGui::PopStyleColor();
            }
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1.0F);
            {
                Theme::ScopedTextStyle ts(fonts.sans, 12.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Theme::Layout::ControlW - 52.0F);
                ImGui::TextWrapped("%s", description);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::SetCursorPos({Theme::Layout::ControlW - 42.0F, cursorY + 6.0F});
            (void)ToggleControl("plugin-toggle", enabled, fonts, false);
        });
    }

    // ── ShortcutDisplay ──────────────────────────────────────────────────

    void ShortcutDisplay(const char *a, const char *b, const char *c, const Theme::Fonts &fonts)
    {
        const std::array<const char *, 3> keys = {a, b, c};
        for (int i = 0; i < 3; ++i)
        {
            if (keys[i] == nullptr || keys[i][0] == '\0')
                continue;
            if (i > 0)
            {
                ImGui::SameLine(0.0F, 4.0F);
                {
                    Theme::ScopedTextStyle plus(fonts.mono, 12.0F, Theme::FontPx::Mono);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                    ImGui::TextUnformatted("+");
                    ImGui::PopStyleColor();
                }
                ImGui::SameLine(0.0F, 4.0F);
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{7.0F, 3.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Layout::Radius);
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Bg3());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Bg3());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Bg3());
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
            {
                Theme::ScopedTextStyle keyText(fonts.mono, 12.0F, Theme::FontPx::Mono);
                ImGui::Button(keys[i], ImVec2{0.0F, 24.0F});
            }
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);
        }
    }

    // ── ThemeChip ────────────────────────────────────────────────────────

    [[nodiscard]] bool ThemeChip(const char *label, const ImVec4 swatch, const bool active, const Theme::Fonts &fonts)
    {
        ImGui::PushID(label);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{12.0F, 7.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Layout::Radius);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);

        const auto accentGlow = ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.14F};
        ImGui::PushStyleColor(ImGuiCol_Button, active ? accentGlow : Theme::Bg3());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Hover());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Hover());
        ImGui::PushStyleColor(ImGuiCol_Text, active ? Theme::Text() : Theme::Muted());
        ImGui::PushStyleColor(ImGuiCol_Border, active ? Theme::Accent() : Theme::Border());

        bool clicked = false;
        {
            Theme::ScopedTextStyle ts(fonts.mono, 12.0F, Theme::FontPx::Mono);
            clicked = ImGui::Button(label, ImVec2{82.0F, 32.0F});
        }
        const ImVec2 min = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddCircleFilled({min.x + 12.0F, min.y + 16.0F}, 5.0F, Theme::U32(swatch), 16);
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(3);
        ImGui::PopID();
        return clicked;
    }

} // namespace Horo::Editor::Ui
