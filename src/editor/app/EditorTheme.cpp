#include "Horo/Editor/EditorTheme.h"

#include <imgui.h>

namespace Horo::Editor::Theme
{

    namespace
    {
        Preset g_activePreset = Preset::HoroDark;

        /** @brief Applies the complete Horo Dark colour palette to the ImGui style. */
        void ApplyHoroDark(ImGuiStyle &style)
        {
            auto *c = style.Colors;
            // clang-format off
            c[ImGuiCol_WindowBg]        = {0.039F, 0.047F, 0.059F, 1.0F};   // #0a0c0f
            c[ImGuiCol_ChildBg]         = {0.071F, 0.082F, 0.102F, 1.0F};   // #12151a
            c[ImGuiCol_PopupBg]         = {0.071F, 0.082F, 0.102F, 1.0F};
            c[ImGuiCol_FrameBg]         = {0.122F, 0.141F, 0.169F, 1.0F};   // #1f242b
            c[ImGuiCol_FrameBgHovered]  = {0.137F, 0.157F, 0.188F, 1.0F};  // #232830
            c[ImGuiCol_FrameBgActive]   = {0.137F, 0.157F, 0.188F, 1.0F};
            c[ImGuiCol_Button]          = {0.122F, 0.141F, 0.169F, 1.0F};
            c[ImGuiCol_ButtonHovered]   = {0.016F, 0.647F, 0.988F, 0.14F};  // accent-glow
            c[ImGuiCol_ButtonActive]    = {0.016F, 0.647F, 0.988F, 0.24F};
            c[ImGuiCol_Text]            = {0.910F, 0.894F, 0.851F, 1.0F};   // #e8e4d9
            c[ImGuiCol_TextDisabled]    = {0.369F, 0.357F, 0.329F, 1.0F};   // #5e5b54
            c[ImGuiCol_Border]          = {0.165F, 0.184F, 0.216F, 1.0F};   // #2a2f37
            c[ImGuiCol_BorderShadow]    = {0.0F, 0.0F, 0.0F, 0.0F};
            c[ImGuiCol_ScrollbarBg]     = {0.0F, 0.0F, 0.0F, 0.0F};
            c[ImGuiCol_ScrollbarGrab]   = {0.227F, 0.251F, 0.286F, 1.0F};   // #3a4049
            c[ImGuiCol_ScrollbarGrabHovered] = {0.369F, 0.357F, 0.329F, 1.0F};
            c[ImGuiCol_ScrollbarGrabActive]  = {0.910F, 0.894F, 0.851F, 1.0F};
            c[ImGuiCol_CheckMark]       = {0.016F, 0.647F, 0.988F, 1.0F};   // accent
            c[ImGuiCol_SliderGrab]      = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_SliderGrabActive]= {0.149F, 0.714F, 0.992F, 1.0F};   // accent-hover
            c[ImGuiCol_Header]          = {0.016F, 0.647F, 0.988F, 0.14F};
            c[ImGuiCol_HeaderHovered]   = {0.137F, 0.157F, 0.188F, 1.0F};
            c[ImGuiCol_HeaderActive]    = {0.137F, 0.157F, 0.188F, 1.0F};
            c[ImGuiCol_ResizeGrip]      = {0.016F, 0.647F, 0.988F, 0.25F};
            c[ImGuiCol_ResizeGripHovered]= {0.016F, 0.647F, 0.988F, 0.67F};
            c[ImGuiCol_ResizeGripActive]= {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_PlotLines]       = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_PlotHistogram]   = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_TableBorderStrong]= {0.165F, 0.184F, 0.216F, 1.0F};
            c[ImGuiCol_TableBorderLight] = {0.165F, 0.184F, 0.216F, 0.5F};
            // clang-format on
        }

        /** @brief Applies the Midnight colour palette. */
        void ApplyMidnight(ImGuiStyle &style)
        {
            auto *c = style.Colors;
            // clang-format off
            c[ImGuiCol_WindowBg]        = {0.027F, 0.039F, 0.063F, 1.0F};   // #070a10
            c[ImGuiCol_ChildBg]         = {0.047F, 0.063F, 0.094F, 1.0F};   // #0c1018
            c[ImGuiCol_PopupBg]         = {0.047F, 0.063F, 0.094F, 1.0F};
            c[ImGuiCol_FrameBg]         = {0.086F, 0.106F, 0.149F, 1.0F};   // #161b26
            c[ImGuiCol_FrameBgHovered]  = {0.106F, 0.125F, 0.173F, 1.0F};
            c[ImGuiCol_FrameBgActive]   = {0.106F, 0.125F, 0.173F, 1.0F};
            c[ImGuiCol_Button]          = {0.086F, 0.106F, 0.149F, 1.0F};
            c[ImGuiCol_ButtonHovered]   = {0.447F, 0.282F, 0.847F, 0.18F};  // violet glow
            c[ImGuiCol_ButtonActive]    = {0.447F, 0.282F, 0.847F, 0.28F};
            c[ImGuiCol_Text]            = {0.867F, 0.855F, 0.898F, 1.0F};   // #dddaee
            c[ImGuiCol_TextDisabled]    = {0.361F, 0.349F, 0.400F, 1.0F};   // #5c5966
            c[ImGuiCol_Border]          = {0.149F, 0.169F, 0.216F, 1.0F};   // #262b37
            c[ImGuiCol_BorderShadow]    = {0.0F, 0.0F, 0.0F, 0.0F};
            c[ImGuiCol_ScrollbarBg]     = {0.0F, 0.0F, 0.0F, 0.0F};
            c[ImGuiCol_ScrollbarGrab]   = {0.212F, 0.231F, 0.278F, 1.0F};
            c[ImGuiCol_ScrollbarGrabHovered] = {0.361F, 0.349F, 0.400F, 1.0F};
            c[ImGuiCol_ScrollbarGrabActive]  = {0.867F, 0.855F, 0.898F, 1.0F};
            c[ImGuiCol_CheckMark]       = {0.447F, 0.282F, 0.847F, 1.0F};   // violet
            c[ImGuiCol_SliderGrab]      = {0.447F, 0.282F, 0.847F, 1.0F};
            c[ImGuiCol_SliderGrabActive]= {0.545F, 0.400F, 0.902F, 1.0F};
            c[ImGuiCol_Header]          = {0.447F, 0.282F, 0.847F, 0.18F};
            c[ImGuiCol_HeaderHovered]   = {0.106F, 0.125F, 0.173F, 1.0F};
            c[ImGuiCol_HeaderActive]    = {0.106F, 0.125F, 0.173F, 1.0F};
            c[ImGuiCol_ResizeGrip]      = {0.447F, 0.282F, 0.847F, 0.25F};
            c[ImGuiCol_ResizeGripHovered]= {0.447F, 0.282F, 0.847F, 0.67F};
            c[ImGuiCol_ResizeGripActive]= {0.447F, 0.282F, 0.847F, 1.0F};
            c[ImGuiCol_PlotLines]       = {0.447F, 0.282F, 0.847F, 1.0F};
            c[ImGuiCol_PlotHistogram]   = {0.447F, 0.282F, 0.847F, 1.0F};
            c[ImGuiCol_TableBorderStrong]= {0.149F, 0.169F, 0.216F, 1.0F};
            c[ImGuiCol_TableBorderLight] = {0.149F, 0.169F, 0.216F, 0.5F};
            // clang-format on
        }

        /** @brief Applies the Light colour palette. */
        void ApplyLight(ImGuiStyle &style)
        {
            auto *c = style.Colors;
            // clang-format off
            c[ImGuiCol_WindowBg]        = {0.941F, 0.937F, 0.929F, 1.0F};   // #f0efed
            c[ImGuiCol_ChildBg]         = {0.961F, 0.957F, 0.953F, 1.0F};   // #f5f4f3
            c[ImGuiCol_PopupBg]         = {0.961F, 0.957F, 0.953F, 1.0F};
            c[ImGuiCol_FrameBg]         = {0.902F, 0.898F, 0.890F, 1.0F};   // #e6e5e3
            c[ImGuiCol_FrameBgHovered]  = {0.867F, 0.863F, 0.855F, 1.0F};
            c[ImGuiCol_FrameBgActive]   = {0.867F, 0.863F, 0.855F, 1.0F};
            c[ImGuiCol_Button]          = {0.902F, 0.898F, 0.890F, 1.0F};
            c[ImGuiCol_ButtonHovered]   = {0.016F, 0.647F, 0.988F, 0.12F};
            c[ImGuiCol_ButtonActive]    = {0.016F, 0.647F, 0.988F, 0.22F};
            c[ImGuiCol_Text]            = {0.125F, 0.129F, 0.137F, 1.0F};   // #202123
            c[ImGuiCol_TextDisabled]    = {0.529F, 0.525F, 0.518F, 1.0F};   // #878684
            c[ImGuiCol_Border]          = {0.784F, 0.780F, 0.773F, 1.0F};   // #c8c7c5
            c[ImGuiCol_BorderShadow]    = {0.0F, 0.0F, 0.0F, 0.0F};
            c[ImGuiCol_ScrollbarBg]     = {0.0F, 0.0F, 0.0F, 0.0F};
            c[ImGuiCol_ScrollbarGrab]   = {0.659F, 0.655F, 0.647F, 1.0F};
            c[ImGuiCol_ScrollbarGrabHovered] = {0.529F, 0.525F, 0.518F, 1.0F};
            c[ImGuiCol_ScrollbarGrabActive]  = {0.125F, 0.129F, 0.137F, 1.0F};
            c[ImGuiCol_CheckMark]       = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_SliderGrab]      = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_SliderGrabActive]= {0.149F, 0.714F, 0.992F, 1.0F};
            c[ImGuiCol_Header]          = {0.016F, 0.647F, 0.988F, 0.12F};
            c[ImGuiCol_HeaderHovered]   = {0.867F, 0.863F, 0.855F, 1.0F};
            c[ImGuiCol_HeaderActive]    = {0.867F, 0.863F, 0.855F, 1.0F};
            c[ImGuiCol_ResizeGrip]      = {0.016F, 0.647F, 0.988F, 0.25F};
            c[ImGuiCol_ResizeGripHovered]= {0.016F, 0.647F, 0.988F, 0.67F};
            c[ImGuiCol_ResizeGripActive]= {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_PlotLines]       = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_PlotHistogram]   = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_TableBorderStrong]= {0.784F, 0.780F, 0.773F, 1.0F};
            c[ImGuiCol_TableBorderLight] = {0.784F, 0.780F, 0.773F, 0.5F};
            // clang-format on
        }
    } // namespace

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
        style.FrameBorderSize = 1;
        style.ChildBorderSize = 1;

        style.WindowPadding = ImVec2{0, 0};
        style.FramePadding = ImVec2{10, 7};
        style.ItemSpacing = ImVec2{8, 8};
        style.ItemInnerSpacing = ImVec2{8, 4};
        style.ScrollbarSize = 10.0F;

        ApplyHoroDark(style);
    }

    void SetThemePreset(const Preset preset)
    {
        g_activePreset = preset;
    }

    Preset GetThemePreset()
    {
        return g_activePreset;
    }

    void ApplyCurrentTheme()
    {
        ImGuiStyle &style = ImGui::GetStyle();
        switch (g_activePreset)
        {
        case Preset::Midnight:
            ApplyMidnight(style);
            break;
        case Preset::Light:
            ApplyLight(style);
            break;
        case Preset::HoroDark:
        default:
            ApplyHoroDark(style);
            break;
        }
    }

} // namespace Horo::Editor::Theme
