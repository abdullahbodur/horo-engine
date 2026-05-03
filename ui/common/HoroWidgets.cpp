#include "ui/common/HoroWidgets.h"
#include "ui/common/HoroTheme.h"

namespace Horo::UI {

bool PrimaryButton(const char* label, const ImVec2& size) {
    const HoroTheme& t = GetHoroTheme();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 9.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,        t.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  t.accentActive);
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.96f, 0.97f, 1.0f, 1.0f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    return pressed;
}

bool SecondaryButton(const char* label, const ImVec2& size) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 9.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.12f, 0.15f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.20f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.14f, 0.18f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.92f, 0.94f, 0.98f, 1.0f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    return pressed;
}

void LabeledInput(const char* title, const char* id,
                  char* buffer, size_t bufferSize,
                  float inputWidth) {
    const HoroTheme& t = GetHoroTheme();
    ImGui::TextDisabled("%s", title);
    ImGui::SetNextItemWidth(inputWidth);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(12.0f, 9.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        t.surfaceDark);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.06f, 0.11f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.08f, 0.15f, 0.25f, 1.0f));
    ImGui::InputText(id, buffer, bufferSize);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
}

} // namespace Horo::UI
