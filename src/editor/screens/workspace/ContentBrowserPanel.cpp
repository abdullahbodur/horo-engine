#include "ContentBrowserPanel.h"
#include "Horo/Editor/EditorTheme.h"

#include <algorithm>
#include <array>

#include "Horo/Editor/EditorUiComponents.h"

namespace Horo::Editor
{

void ContentBrowserPanel::DrawIcon(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, const ImU32 color)
{
    const float ox = pos.x + (size.x - 14.0f) * 0.5f;
    const float oy = pos.y + (size.y - 14.0f) * 0.5f;
    
    // Simple folder icon
    dl->AddLine(ImVec2(ox + 2, oy + 4), ImVec2(ox + 5, oy + 4), color, 1.5f);
    dl->AddLine(ImVec2(ox + 5, oy + 4), ImVec2(ox + 6, oy + 6), color, 1.5f);
    dl->AddLine(ImVec2(ox + 6, oy + 6), ImVec2(ox + 12, oy + 6), color, 1.5f);
    dl->AddLine(ImVec2(ox + 12, oy + 6), ImVec2(ox + 12, oy + 11), color, 1.5f);
    dl->AddLine(ImVec2(ox + 12, oy + 11), ImVec2(ox + 2, oy + 11), color, 1.5f);
    dl->AddLine(ImVec2(ox + 2, oy + 11), ImVec2(ox + 2, oy + 4), color, 1.5f);
}

void ContentBrowserPanel::DrawPanel(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &vm,
                                     EditorWorkspaceViewCommandData &cmd, const EditorGuiContext &ctx)
{
    const std::vector tabNames = {"Content Browser"};
    Ui::DrawDockTabs(tabNames, 0, ctx.theme.fonts);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::BeginChild("##Content", ImVec2(size.x, size.y - 28.0f), false, ImGuiWindowFlags_NoSavedSettings);

    // Breadcrumb
    ImGui::SetCursorPosX(10.0F);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
    ImGui::TextUnformatted("assets/");
    ImGui::PopStyleColor();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0F, 6.0F));
    ImGui::Dummy(ImVec2(0.0F, 4.0F));

    constexpr std::array<const char *, 6> kDirs{"scenes", "models", "textures", "materials", "shaders", "audio"};
    constexpr float cellW = 70.0F;
    constexpr float cellH = 58.0F;
    constexpr float pad = 6.0F;
    int col = 0;
    const int cols = (std::max)(1, static_cast<int>((size.x - 10.0F) / (cellW + pad)));

    ImGui::PushStyleColor(ImGuiCol_Button, Theme::Bg3());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Hover());
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::AccentSoft());
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
    ImGui::SetCursorPosX(10.0F);
    for (const char *d : kDirs)
    {
        if (col > 0 && col < cols)
        {
            ImGui::SameLine(0.0F, pad);
        }
        else if (col == 0)
        {
            ImGui::SetCursorPosX(10.0F);
        }
        ImGui::PushID(d);
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
        ImGui::Button("##dir", ImVec2(cellW, cellH * 0.7F));
        ImGui::PopStyleColor();
        const float textW = ImGui::CalcTextSize(d).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cellW - textW) * 0.5F);
        ImGui::TextUnformatted(d);
        ImGui::EndGroup();
        ImGui::PopID();
        col = (col + 1) % cols;
    }
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace Horo::Editor
