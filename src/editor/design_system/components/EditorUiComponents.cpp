#include "editor/design_system/components/EditorUiComponents.h"

#include <algorithm>
#include <cmath>

namespace Horo::Editor::Ui
{

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
            clicked = false;
        }
        return clicked;
    }

    [[nodiscard]] bool IconCloseButton(const char *id, const ImVec2 size)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();

        ImGui::PushID(id);
        ImGui::InvisibleButton("##close", size);
        const bool clicked = ImGui::IsItemClicked();
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        auto *dl = ImGui::GetWindowDrawList();
        const ImVec2 center{pos.x + size.x * 0.5F, pos.y + size.y * 0.5F};
        const float arm = std::min(size.x, size.y) * 0.28F;

        if (hovered)
        {
            dl->AddRectFilled(pos,
                              {pos.x + size.x, pos.y + size.y},
                              Theme::U32(Theme::Bg3()),
                              Theme::Layout::Radius);
            dl->AddRect(pos,
                        {pos.x + size.x, pos.y + size.y},
                        Theme::U32(Theme::BorderStrong()),
                        Theme::Layout::Radius,
                        0,
                        1.0F);
        }

        const ImU32 color = Theme::U32(hovered ? Theme::Text() : Theme::Dim());
        constexpr float kThickness = 1.5F;
        dl->AddLine({center.x - arm, center.y - arm}, {center.x + arm, center.y + arm}, color, kThickness);
        dl->AddLine({center.x + arm, center.y - arm}, {center.x - arm, center.y + arm}, color, kThickness);

        return clicked;
    }

    void SectionTitle(const char *upperCaseLabel, const Theme::Fonts &fonts)
    {
        Theme::ScopedTextStyle textStyle(fonts.monoSemiBold, 13.0F, Theme::FontPx::MonoSemiBold);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
        ImGui::TextUnformatted(upperCaseLabel);
        ImGui::PopStyleColor();
    }

    void FieldLabel(const char *upperCaseLabel, const Theme::Fonts &fonts)
    {
        Theme::ScopedTextStyle textStyle(fonts.mono, 12.0F, Theme::FontPx::Mono);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
        ImGui::TextUnformatted(upperCaseLabel);
        ImGui::PopStyleColor();
    }

    void Hint(const char *text, const Theme::Fonts &fonts)
    {
        Theme::ScopedTextStyle textStyle(fonts.mono, 11.5F, Theme::FontPx::Mono);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    void DashedSeparator(const float dash, const float gap)
    {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        auto *dl = ImGui::GetWindowDrawList();
        const ImU32 color = Theme::U32(Theme::Border());
        const float step = dash + gap;
        if (step <= 0.0F)
        {
            return;
        }
        const auto steps = static_cast<int>(std::ceil(width / step));
        for (int i = 0; i < steps; ++i)
        {
            const float x = static_cast<float>(i) * step;
            const float xEnd = std::min(x + dash, width);
            dl->AddLine({p0.x + x, p0.y}, {p0.x + xEnd, p0.y}, color, 1.0F);
        }
        ImGui::Dummy({width, 1.0F});
    }

} // namespace Horo::Editor::Ui
