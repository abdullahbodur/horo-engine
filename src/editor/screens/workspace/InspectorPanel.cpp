#include "InspectorPanel.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"

#include <vector>

namespace Horo::Editor {
    namespace {
        const std::vector<const char *> kTabNames{"Inspector"};
    }

    void InspectorPanel::DrawIcon(ImDrawList *dl, const ImVec2 &pos, const ImVec2 &size, const ImU32 color) {
        const float ox = pos.x + (size.x - 14.0f) * 0.5f;
        const float oy = pos.y + (size.y - 14.0f) * 0.5f;

        // Simple inspector icon (list with details)
        dl->AddRect(ImVec2(ox + 2, oy + 2), ImVec2(ox + 12, oy + 12), color, 0.0f, 0, 1.5f);
        dl->AddLine(ImVec2(ox + 4, oy + 5), ImVec2(ox + 10, oy + 5), color, 1.5f);
        dl->AddLine(ImVec2(ox + 4, oy + 8), ImVec2(ox + 10, oy + 8), color, 1.5f);
    }

    void InspectorPanel::DrawPanel(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &vm,
                                   EditorWorkspaceViewCommandData &cmd, const EditorGuiContext &ctx) {
        Ui::DrawDockTabs(kTabNames, 0, ctx.theme.fonts);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
        ImGui::BeginChild("##Content", ImVec2(size.x, size.y - 28.0f), false, ImGuiWindowFlags_NoSavedSettings);

        if (vm.selectedIndex >= 0 && vm.selectedIndex < static_cast<int>(vm.objects.size())) {
            // Placeholder properties matching HTML design
            constexpr auto badgeBg = ImVec4(95.0f / 255.0f, 184.0f / 255.0f, 138.0f / 255.0f, 0.15f);

            const auto &selectedObject = vm.objects[static_cast<std::size_t>(vm.selectedIndex)];
            Ui::DrawObjTitle(selectedObject.name.c_str(), "Mesh", badgeBg, Theme::Ok(), ctx.theme.fonts);

            Ui::DrawPropSection("Transform", ctx.theme.fonts);
            Ui::DrawPropRow("Position", "0.00, 0.00, 0.00", ctx.theme.fonts);
            Ui::DrawPropRow("Rotation", "0.00, 0.00, 0.00", ctx.theme.fonts);
            Ui::DrawPropRow("Scale", "1.00, 1.00, 1.00", ctx.theme.fonts);

            Ui::DrawPropSection("Mesh Renderer", ctx.theme.fonts);
            Ui::DrawPropRow("Mesh", "SM_Floor_000", ctx.theme.fonts);
            Ui::DrawPropRow("Material", "M_Floor_Tile", ctx.theme.fonts);
            Ui::DrawPropRow("Shadows", "On", ctx.theme.fonts);

            Ui::DrawPropSection("Static Flags", ctx.theme.fonts);
            Ui::DrawPropRow("Batching", "✓ Static", ctx.theme.fonts);
            Ui::DrawPropRow("Lightmap", "✓ Static", ctx.theme.fonts);
        } else {
            ImGui::SetCursorPosX(14.0F);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14.0F);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
            ImGui::TextWrapped("Select an object in the Hierarchy to inspect its properties.");
            ImGui::PopStyleColor();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
} // namespace Horo::Editor
