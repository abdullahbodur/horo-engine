#include "InspectorPanel.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"

#include <array>

namespace Horo::Editor
{

void InspectorPanel::DrawIcon(ImDrawList *dl, const ImVec2 &pos, const ImVec2 &size, const ImU32 color)
{
    const float ox = pos.x + (size.x - 14.0f) * 0.5f;
    const float oy = pos.y + (size.y - 14.0f) * 0.5f;

    // Simple inspector icon (list with details)
    dl->AddRect(ImVec2(ox + 2, oy + 2), ImVec2(ox + 12, oy + 12), color, 0.0f, 0, 1.5f);
    dl->AddLine(ImVec2(ox + 4, oy + 5), ImVec2(ox + 10, oy + 5), color, 1.5f);
    dl->AddLine(ImVec2(ox + 4, oy + 8), ImVec2(ox + 10, oy + 8), color, 1.5f);
}

void InspectorPanel::DrawPanel(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &vm,
                               EditorWorkspaceViewCommandData &cmd, const EditorGuiContext &ctx)
{
    const std::array tabNames{ctx.localization.Get("editor", "workspace.panel.inspector").c_str()};
    Ui::DrawDockTabs(tabNames, 0, ctx.theme.fonts);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::BeginChild("##Content", ImVec2(size.x, size.y - 28.0f), false, ImGuiWindowFlags_NoSavedSettings);

    if (vm.selectedIndex >= 0 && vm.selectedIndex < static_cast<int>(vm.objects.size()))
    {
        // Placeholder properties matching HTML design
        constexpr auto badgeBg = ImVec4(95.0f / 255.0f, 184.0f / 255.0f, 138.0f / 255.0f, 0.15f);

        const auto &selectedObject = vm.objects[static_cast<std::size_t>(vm.selectedIndex)];
        const std::string &meshType = ctx.localization.Get("editor", "workspace.inspector.mesh_type");
        Ui::DrawObjTitle(selectedObject.name.c_str(), meshType.c_str(), badgeBg, Theme::Ok(), ctx.theme.fonts);

        Ui::DrawPropSection(ctx.localization.Get("editor", "workspace.inspector.transform").c_str(), ctx.theme.fonts);
        Ui::DrawPropRow(ctx.localization.Get("editor", "workspace.inspector.position").c_str(), "0.00, 0.00, 0.00",
                        ctx.theme.fonts);
        Ui::DrawPropRow(ctx.localization.Get("editor", "workspace.inspector.rotation").c_str(), "0.00, 0.00, 0.00",
                        ctx.theme.fonts);
        Ui::DrawPropRow(ctx.localization.Get("editor", "workspace.inspector.scale").c_str(), "1.00, 1.00, 1.00",
                        ctx.theme.fonts);

        Ui::DrawPropSection(ctx.localization.Get("editor", "workspace.inspector.mesh_renderer").c_str(),
                            ctx.theme.fonts);
        Ui::DrawPropRow(ctx.localization.Get("editor", "workspace.inspector.mesh").c_str(), "SM_Floor_000",
                        ctx.theme.fonts);
        Ui::DrawPropRow(ctx.localization.Get("editor", "workspace.inspector.material").c_str(), "M_Floor_Tile",
                        ctx.theme.fonts);
        Ui::DrawPropRow(ctx.localization.Get("editor", "workspace.inspector.shadows").c_str(),
                        ctx.localization.Get("editor", "workspace.value.on").c_str(), ctx.theme.fonts);

        Ui::DrawPropSection(ctx.localization.Get("editor", "workspace.inspector.static_flags").c_str(),
                            ctx.theme.fonts);
        Ui::DrawPropRow(ctx.localization.Get("editor", "workspace.inspector.batching").c_str(),
                        ctx.localization.Get("editor", "workspace.value.static").c_str(), ctx.theme.fonts);
        Ui::DrawPropRow(ctx.localization.Get("editor", "workspace.inspector.lightmap").c_str(),
                        ctx.localization.Get("editor", "workspace.value.static").c_str(), ctx.theme.fonts);
    }
    else
    {
        ImGui::SetCursorPosX(14.0F);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14.0F);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
        ImGui::TextWrapped("%s", ctx.localization.Get("editor", "workspace.inspector.empty").c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace Horo::Editor
