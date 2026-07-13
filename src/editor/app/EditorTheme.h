#pragma once

#include <imgui.h>

#include <Horo/Editor/DesignSystem/DesignTokens.h>

namespace Horo::Editor::Theme
{
// ─────────────────────────────────────────────────────────────────────────
// Active Design Tokens (runtime-switchable)
// ─────────────────────────────────────────────────────────────────────────
[[nodiscard]] const DesignSystem::DesignTokens &GetActiveTokens();

// ─────────────────────────────────────────────────────────────────────────
// Palette — exact match for the CSS custom properties in the HTML mockups:
// ─────────────────────────────────────────────────────────────────────────
[[nodiscard]] inline ImVec4 Bg0()
{
    return GetActiveTokens().colors.surfaceRoot;
}
[[nodiscard]] inline ImVec4 Bg1()
{
    return GetActiveTokens().colors.surfaceWindow;
}
[[nodiscard]] inline ImVec4 Bg2()
{
    return GetActiveTokens().colors.surfacePanel;
}
[[nodiscard]] inline ImVec4 Bg3()
{
    return GetActiveTokens().colors.surfaceRaised;
}
[[nodiscard]] inline ImVec4 Hover()
{
    return GetActiveTokens().colors.surfaceHover;
}
[[nodiscard]] inline ImVec4 Border()
{
    return GetActiveTokens().colors.border;
}
[[nodiscard]] inline ImVec4 BorderStrong()
{
    return GetActiveTokens().colors.borderStrong;
}
[[nodiscard]] inline ImVec4 Text()
{
    return GetActiveTokens().colors.textPrimary;
}
[[nodiscard]] inline ImVec4 Muted()
{
    return GetActiveTokens().colors.textMuted;
}
[[nodiscard]] inline ImVec4 Dim()
{
    return GetActiveTokens().colors.textDim;
}
[[nodiscard]] inline ImVec4 Accent()
{
    return GetActiveTokens().colors.actionPrimary;
}
[[nodiscard]] inline ImVec4 AccentHover()
{
    return GetActiveTokens().colors.actionPrimaryHover;
}
[[nodiscard]] inline ImVec4 AccentActive()
{
    return GetActiveTokens().colors.actionPrimaryActive;
}
[[nodiscard]] inline ImVec4 AccentSoft()
{
    return GetActiveTokens().colors.actionPrimarySoft;
}
[[nodiscard]] inline ImVec4 Ok()
{
    return GetActiveTokens().colors.statusOk;
}
[[nodiscard]] inline ImVec4 Warn()
{
    return GetActiveTokens().colors.statusWarn;
}
[[nodiscard]] inline ImVec4 Err()
{
    return GetActiveTokens().colors.statusError;
}

[[nodiscard]] inline ImVec4 ErrSoft()
{
    return {GetActiveTokens().colors.statusError.x, GetActiveTokens().colors.statusError.y,
            GetActiveTokens().colors.statusError.z, 0.12F};
}

[[nodiscard]] inline ImVec4 DarkText()
{
    return GetActiveTokens().colors.textOnActionPrimary;
}
[[nodiscard]] inline ImVec4 Shadow()
{
    return {0.000F, 0.000F, 0.000F, 0.550F};
}

[[nodiscard]] inline ImU32 U32(const ImVec4 &c)
{
    return ImGui::GetColorU32(c);
}

// ─────────────────────────────────────────────────────────────────────────
// Fonts — the three font atlas entries loaded by the application
// ─────────────────────────────────────────────────────────────────────────
struct Fonts
{
    ImFont *sans = nullptr;         // InterVariable, standard body size.
    ImFont *sansCompact = nullptr;  // InterVariable, compact metadata size.
    ImFont *sansEmphasis = nullptr; // InterVariable, emphasis size.
};

namespace FontPx
{
constexpr float Sans = 15.0F;
constexpr float SansCompact = 13.0F;
constexpr float SansEmphasis = 15.0F;
} // namespace FontPx

// Converts arbitrary HTML/CSS font sizes to the fixed atlas sizes we have
// using the required SetWindowFontScale() multiplier.
[[nodiscard]] constexpr float Scale(float targetPx, float basePx)
{
    return targetPx / basePx;
}

inline void PushFont(ImFont *f)
{
    if (f)
        ImGui::PushFont(f);
}

inline void PopFont(ImFont *f)
{
    if (f)
        ImGui::PopFont();
}

// RAII: pushes a font when non-null and guarantees the matching pop.
struct ScopedFont
{
    ImFont *font;
    explicit ScopedFont(ImFont *f) : font(f)
    {
        PushFont(font);
    }
    ~ScopedFont()
    {
        PopFont(font);
    }
    ScopedFont(const ScopedFont &) = delete;
    ScopedFont &operator=(const ScopedFont &) = delete;
};

// RAII: applies a window-local font scale and always restores 1.0 at scope exit.
struct ScopedFontScale
{
    explicit ScopedFontScale(float scale)
    {
        ImGui::SetWindowFontScale(scale);
    }
    ~ScopedFontScale()
    {
        ImGui::SetWindowFontScale(1.0F);
    }
    ScopedFontScale(const ScopedFontScale &) = delete;
    ScopedFontScale &operator=(const ScopedFontScale &) = delete;
};

// Shortcut: pushes `font` and scales it to the target HTML pixel size,
// then automatically restores both at scope exit.
//   Example: ScopedTextStyle ts(f.sansCompact, /*targetPx=*/11.0f, Theme::FontPx::SansCompact);
struct ScopedTextStyle
{
    ScopedFont font;
    ScopedFontScale scale;

    ScopedTextStyle(ImFont *f, float targetPx, float basePx) : font(f), scale(Scale(targetPx, basePx))
    {
    }
};

// ─────────────────────────────────────────────────────────────────────────
// Layout metrics — pixel values matching the HTML mockups
// ─────────────────────────────────────────────────────────────────────────
namespace Layout
{
constexpr float Radius = 4.0F;      // --radius
constexpr float RadiusCard = 6.0F;  // .template { border-radius: 6px }
constexpr float RadiusModal = 8.0F; // .modal / .welcome-card { border-radius: 8px }

// Welcome screen (welcome-screen.html)
constexpr float WelcomeOuterPad = 40.0F; // .welcome { padding: 40px }
constexpr float WelcomeCardW = 900.0F;   // .welcome-card { width: min(900px, 100%) }
constexpr float WelcomeSideW = 280.0F;   // grid-template-columns: 280px 1fr
constexpr float WelcomePad = 32.0F;      // .side, .main { padding: 32px }

// New Project wizard (new-project-wizard.html)
constexpr float ModalW = 900.0F;
constexpr float ModalH = 680.0F;
constexpr float HeaderH = 58.0F;
constexpr float FooterH = 52.0F;
constexpr float SidebarW = 220.0F;   // grid-template-columns: 220px 1fr
constexpr float SidebarPadX = 14.0F; // .steps { padding: 18px 14px }
constexpr float SidebarPadY = 18.0F;
constexpr float BodyPadX = 28.0F; // .main { padding: 24px 28px }
constexpr float BodyPadY = 24.0F;
constexpr float CardPad = 18.0F; // .card { padding: 18px }
constexpr float GridGap = 14.0F; // .grid { gap: 14px }

// Settings modal
constexpr float SettingsW = 620.0F;
constexpr float SettingsH = 440.0F;
constexpr float ControlW = 260.0F; // form control column width
} // namespace Layout

// ─────────────────────────────────────────────────────────────────────────
// Applies the global ImGui style. Call once after ImGui::CreateContext().
// All visual defaults such as colors, rounding, and padding come from here;
// widget code must not depend on this function's implementation details.
// ─────────────────────────────────────────────────────────────────────────
void Apply(ImGuiStyle &style);
} // namespace Horo::Editor::Theme
