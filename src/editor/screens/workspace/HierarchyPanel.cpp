#include "HierarchyPanel.h"
#include "Horo/Editor/EditorTheme.h"

#include <string>

#include "Horo/Editor/EditorUiComponents.h"

namespace Horo::Editor
{
void HierarchyPanel::DrawIcon(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, const ImU32 color)
{
    const float ox = pos.x + (size.x - 14.0f) * 0.5f;
    const float oy = pos.y + (size.y - 14.0f) * 0.5f;
    
    // Simple hierarchy icon (nodes and branches)
    dl->AddLine(ImVec2(ox + 2, oy + 2), ImVec2(ox + 12, oy + 2), color, 1.5f);
    dl->AddLine(ImVec2(ox + 4, oy + 2), ImVec2(ox + 4, oy + 7), color, 1.5f);
    dl->AddLine(ImVec2(ox + 4, oy + 7), ImVec2(ox + 12, oy + 7), color, 1.5f);
    dl->AddLine(ImVec2(ox + 4, oy + 7), ImVec2(ox + 4, oy + 12), color, 1.5f);
    dl->AddLine(ImVec2(ox + 4, oy + 12), ImVec2(ox + 12, oy + 12), color, 1.5f);
}

void HierarchyPanel::DrawPanel(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &vm,
                               EditorWorkspaceViewCommandData &cmd, const EditorGuiContext &ctx)
{
    const std::vector tabNames = {"Hierarchy"};
    Ui::DrawDockTabs(tabNames, 0, ctx.theme.fonts);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::BeginChild("##Content", ImVec2(size.x, size.y - 28.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    // Toolbar
    ImGui::Dummy(ImVec2(0.0F, 4.0F));
    ImGui::SetCursorPosX(8.0F);
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::Bg3());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Hover());
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Accent());
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
    {
        const Theme::ScopedFont f(ctx.theme.fonts.sans);
        if (ImGui::SmallButton("+ Add Object"))
        {
            cmd.command = EditorWorkspaceViewCommand::AddObject;
        }
    }
    ImGui::PopStyleColor(4);
    ImGui::Dummy(ImVec2(0.0F, 4.0F));

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 1.0F));
    const float listW = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < static_cast<int>(vm.objects.size()); ++i)
    {
        auto &obj = vm.objects[static_cast<std::size_t>(i)];
        const bool isSel = (vm.selectedIndex == i);

        if (isSel)
        {
            ImGui::PushStyleColor(ImGuiCol_Header, Theme::AccentSoft());
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::AccentSoft());
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::AccentSoft());
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::Hover());
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::Hover());
        }

        ImGui::PushID(i);
        ImGui::SetCursorPosX(0.0F);
        if (ImGui::Selectable(("  " + obj.name).c_str(), isSel, ImGuiSelectableFlags_SpanAllColumns,
                              ImVec2(listW, 22.0F)))
        {
            cmd.command = EditorWorkspaceViewCommand::SelectObject;
            cmd.targetIndex = isSel ? -1 : i;
        }
        ImGui::PopID();
        ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleVar();

    if (vm.objects.empty())
    {
        ImGui::Dummy(ImVec2(0.0F, 8.0F));
        ImGui::SetCursorPosX(16.0F);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
        ImGui::TextUnformatted("Scene is empty");
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace Horo::Editor
