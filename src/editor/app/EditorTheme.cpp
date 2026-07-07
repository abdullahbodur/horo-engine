#include "Horo/Editor/EditorTheme.h"

namespace Horo::Editor::Theme
{

    void Apply(ImGuiStyle &style)
    {
        ImGui::StyleColorsDark();

        style.WindowRounding = 0;
        style.FrameRounding = Layout::Radius;
        style.ChildRounding = Layout::Radius;
        style.PopupRounding = Layout::Radius;
        style.ScrollbarRounding = Layout::Radius;
        style.GrabRounding = Layout::Radius;

        style.WindowBorderSize = 0;
        style.FrameBorderSize = 1; // CSS input/select/button border: 1px solid var(--bd)
        style.ChildBorderSize = 1;

        style.WindowPadding = ImVec2{0, 0};
        style.FramePadding = ImVec2{10, 7}; // input { padding: 7px 10px }
        style.ItemSpacing = ImVec2{8, 8};
        style.ItemInnerSpacing = ImVec2{8, 4};
        style.ScrollbarSize = 10.0F;

        auto *c = style.Colors;
        c[ImGuiCol_WindowBg] = Bg0();
        c[ImGuiCol_ChildBg] = Bg1();
        c[ImGuiCol_PopupBg] = Bg1();
        c[ImGuiCol_Button] = Bg3();
        c[ImGuiCol_ButtonHovered] = Hover();
        c[ImGuiCol_ButtonActive] = Bg3();
        c[ImGuiCol_FrameBg] = Bg3();
        c[ImGuiCol_FrameBgHovered] = Hover();
        c[ImGuiCol_FrameBgActive] = Hover();
        c[ImGuiCol_Text] = Text();
        c[ImGuiCol_TextDisabled] = Muted();
        c[ImGuiCol_Border] = Border();
        c[ImGuiCol_Separator] = Border();
        c[ImGuiCol_ScrollbarBg] = Bg2();
        c[ImGuiCol_ScrollbarGrab] = ImVec4{0x3a / 255.0F, 0x40 / 255.0F, 0x49 / 255.0F, 1.0F};
        c[ImGuiCol_ScrollbarGrabHovered] = Accent();
        c[ImGuiCol_ScrollbarGrabActive] = Accent();
        c[ImGuiCol_CheckMark] = Accent();
        c[ImGuiCol_Tab] = Bg2();
        c[ImGuiCol_TabHovered] = Hover();
        c[ImGuiCol_TabActive] = Bg3();
    }

} // namespace Horo::Editor::Theme