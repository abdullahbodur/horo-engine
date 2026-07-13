#include "ViewportPanel.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"

#include <vector>

namespace Horo::Editor {
    namespace {
        const std::vector<const char *> kTabNames{"Viewport"};
    }

    void ViewportPanel::DrawIcon(ImDrawList *dl, const ImVec2 &pos, const ImVec2 &size, const ImU32 color) {
        const float ox = pos.x + (size.x - 14.0f) * 0.5f;
        const float oy = pos.y + (size.y - 14.0f) * 0.5f;

        // Simple viewport icon (screen/camera)
        dl->AddRect(ImVec2(ox + 2, oy + 3), ImVec2(ox + 12, oy + 11), color, 1.0f, 0, 1.5f);
        dl->AddCircle(ImVec2(ox + 7, oy + 7), 2.0f, color, 0, 1.5f);
    }

    auto ViewportPanel::DrawPanel(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &vm,
                                  EditorWorkspaceViewCommandData &cmd, const EditorGuiContext &ctx) -> void {
        Ui::DrawDockTabs(kTabNames, 0, ctx.theme.fonts);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
        ImGui::BeginChild("##Content", ImVec2(size.x, size.y - 28.0f), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 orig = ImGui::GetCursorScreenPos();
        const float w = size.x;
        const float h = size.y - 28.0f; // Adjust for tabs

        // Background gradient
        dl->AddRectFilledMultiColor(
            orig, ImVec2(orig.x + w, orig.y + h), ImGui::GetColorU32(ImVec4(0.05F, 0.06F, 0.09F, 1.0F)),
            ImGui::GetColorU32(ImVec4(0.05F, 0.06F, 0.09F, 1.0F)),
            ImGui::GetColorU32(ImVec4(0.09F, 0.11F, 0.15F, 1.0F)),
            ImGui::GetColorU32(ImVec4(0.09F, 0.11F, 0.15F, 1.0F)));

        // Perspective grid
        const ImU32 gridCol = ImGui::GetColorU32(ImVec4(0.16F, 0.20F, 0.27F, 1.0F));
        const float cx = orig.x + w * 0.5F;
        const float horizon = orig.y + h * 0.38F;
        const float ground = orig.y + h;
        constexpr int kLines = 14;
        for (int g = 0; g <= kLines; ++g) {
            const float t = static_cast<float>(g) / kLines;
            const float xOff = (t - 0.5F) * w;
            dl->AddLine(ImVec2(cx + xOff, ground), ImVec2(cx, horizon), gridCol, 0.7F);

            const float gt = static_cast<float>(g) / kLines;
            const float yPos = ground - gt * (ground - horizon);
            const float hw = w * (1.0F - gt * 0.90F) * 0.5F;
            dl->AddLine(ImVec2(cx - hw, yPos), ImVec2(cx + hw, yPos), gridCol, 0.7F);
        }

        // Horizon glow
        dl->AddRectFilledMultiColor(
            ImVec2(orig.x, horizon - 12.0F), ImVec2(orig.x + w, horizon + 22.0F),
            ImGui::GetColorU32(ImVec4(0.01F, 0.22F, 0.44F, 0.0F)),
            ImGui::GetColorU32(ImVec4(0.01F, 0.22F, 0.44F, 0.0F)),
            ImGui::GetColorU32(ImVec4(0.03F, 0.38F, 0.60F, 0.35F)),
            ImGui::GetColorU32(ImVec4(0.03F, 0.38F, 0.60F, 0.35F)));

        // Object count overlay (top-left)
        ImGui::SetCursorScreenPos(ImVec2(orig.x + 10.0F, orig.y + 8.0F));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.32F, 0.38F, 0.48F, 1.0F));
        ImGui::Text("%d object(s)", static_cast<int>(vm.objects.size()));
        ImGui::PopStyleColor();

        // Center placeholder
        const auto msg = "Renderer not attached";
        const float msgW = ImGui::CalcTextSize(msg).x;
        ImGui::SetCursorScreenPos(ImVec2(cx - msgW * 0.5F, orig.y + h * 0.52F));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.28F, 0.32F, 0.40F, 1.0F));
        ImGui::TextUnformatted(msg);
        ImGui::PopStyleColor();

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
} // namespace Horo::Editor
