#include "ui/common/HoroTheme.h"

namespace Horo::UI {

const HoroTheme& GetHoroTheme() {
    static const HoroTheme theme{};
    return theme;
}

void ApplyHoroEditorTheme() {
    ImGui::StyleColorsDark();
    const HoroTheme& t = GetHoroTheme();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = t.panelRounding;
    style.ChildRounding    = t.cardRounding;
    style.FrameRounding    = 6.0f;
    style.PopupRounding    = 8.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding     = 4.0f;
    style.TabRounding      = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize  = 0.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]            = t.panelSoft;
    colors[ImGuiCol_ChildBg]             = t.panel;
    colors[ImGuiCol_PopupBg]             = t.panel;
    colors[ImGuiCol_FrameBg]             = t.surfaceDark;
    colors[ImGuiCol_FrameBgHovered]      = ImVec4(0.06f, 0.11f, 0.18f, 1.0f);
    colors[ImGuiCol_FrameBgActive]       = ImVec4(0.08f, 0.15f, 0.25f, 1.0f);
    colors[ImGuiCol_TitleBg]             = t.panel;
    colors[ImGuiCol_TitleBgActive]       = ImVec4(0.06f, 0.12f, 0.20f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed]    = t.surfaceDark;
    colors[ImGuiCol_MenuBarBg]           = t.panel;
    colors[ImGuiCol_ScrollbarBg]         = t.surfaceDark;
    colors[ImGuiCol_ScrollbarGrab]       = ImVec4(0.18f, 0.28f, 0.42f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered]= t.accent;
    colors[ImGuiCol_ScrollbarGrabActive] = t.accentActive;
    colors[ImGuiCol_CheckMark]           = t.accent;
    colors[ImGuiCol_SliderGrab]          = t.accent;
    colors[ImGuiCol_SliderGrabActive]    = t.accentHover;
    colors[ImGuiCol_Button]              = ImVec4(0.12f, 0.15f, 0.20f, 1.0f);
    colors[ImGuiCol_ButtonHovered]       = ImVec4(0.16f, 0.20f, 0.28f, 1.0f);
    colors[ImGuiCol_ButtonActive]        = ImVec4(0.14f, 0.18f, 0.24f, 1.0f);
    colors[ImGuiCol_Header]              = ImVec4(0.18f, 0.33f, 0.52f, 1.0f);
    colors[ImGuiCol_HeaderHovered]       = ImVec4(0.22f, 0.38f, 0.58f, 1.0f);
    colors[ImGuiCol_HeaderActive]        = ImVec4(0.20f, 0.35f, 0.55f, 1.0f);
    colors[ImGuiCol_Separator]           = t.border;
    colors[ImGuiCol_SeparatorHovered]    = t.accent;
    colors[ImGuiCol_SeparatorActive]     = t.accentActive;
    colors[ImGuiCol_ResizeGrip]          = ImVec4(0.16f, 0.27f, 0.42f, 0.40f);
    colors[ImGuiCol_ResizeGripHovered]   = t.accent;
    colors[ImGuiCol_ResizeGripActive]    = t.accentActive;
    colors[ImGuiCol_Tab]                 = t.panel;
    colors[ImGuiCol_TabHovered]          = ImVec4(0.16f, 0.30f, 0.50f, 1.0f);
    colors[ImGuiCol_TabSelected]         = ImVec4(0.18f, 0.36f, 0.58f, 1.0f);
    colors[ImGuiCol_TabSelectedOverline] = t.accent;
    colors[ImGuiCol_TabDimmed]           = t.surfaceDark;
    colors[ImGuiCol_TabDimmedSelected]   = t.panel;
    colors[ImGuiCol_Border]              = t.border;
    colors[ImGuiCol_BorderShadow]        = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_Text]                = t.textPrimary;
    colors[ImGuiCol_TextDisabled]        = t.textMuted;
}

} // namespace Horo::UI
