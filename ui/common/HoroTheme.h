#pragma once

#include <imgui.h>

namespace Horo::UI {

// Shared dark-navy color palette used by both the launcher and editor.
// All ImVec4 fields are normalized [0,1] RGBA.
struct HoroTheme final {
    // Background gradient (deep navy)
    ImVec4 backgroundTop{1.0f / 255.0f, 7.0f / 255.0f, 17.0f / 255.0f, 1.0f};
    ImVec4 backgroundBottom{1.0f / 255.0f, 7.0f / 255.0f, 17.0f / 255.0f, 1.0f};

    // Surface layers
    ImVec4 panel{0.05f, 0.09f, 0.15f, 0.94f};
    ImVec4 panelSoft{0.07f, 0.11f, 0.18f, 0.90f};
    ImVec4 surfaceDark{0.04f, 0.08f, 0.13f, 1.0f};

    // Border
    ImVec4 border{0.16f, 0.27f, 0.42f, 0.68f};

    // Text
    ImVec4 textPrimary{0.92f, 0.94f, 0.98f, 1.0f};
    ImVec4 textMuted{0.68f, 0.74f, 0.84f, 1.0f};

    // Semantic status colors
    ImVec4 success{0.75f, 0.95f, 0.75f, 1.0f};
    ImVec4 warning{1.0f, 0.85f, 0.4f, 1.0f};
    ImVec4 error{1.0f, 0.45f, 0.4f, 1.0f};
    ImVec4 info{0.4f, 0.8f, 1.0f, 1.0f};
    ImVec4 infoBright{0.65f, 0.85f, 1.0f, 1.0f};

    // Accent (brand blue)
    ImVec4 accent{0.23f, 0.54f, 0.93f, 1.0f};
    ImVec4 accentHover{0.28f, 0.60f, 0.99f, 1.0f};
    ImVec4 accentActive{0.18f, 0.46f, 0.82f, 1.0f};

    // Rounding radii
    float windowRounding = 16.0f;
    float panelRounding = 12.0f;
    float cardRounding = 10.0f;
};

// Returns the global singleton theme instance.
const HoroTheme& GetHoroTheme();

// Applies the Horo dark theme to ImGui's style.
// Call once after ImGui::CreateContext() and before the first frame.
void ApplyHoroEditorTheme();

} // namespace Horo::UI
