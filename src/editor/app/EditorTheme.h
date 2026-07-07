#pragma once

#include <imgui.h>

namespace Horo::Editor::Theme
{

    // ─────────────────────────────────────────────────────────────────────────
    // Palette — exact match for the CSS custom properties in the HTML mockups:
    //   --bg0 #0a0c0f   --bg1 #12151a   --bg2 #181c21   --bg3 #1f242b
    //   --hover #232830 --bd #2a2f37    --bd2 #3a4049
    //   --txt #e8e4d9   --mut #9a958a   --dim #5e5b54
    //   --a #04A5FC     --ok #5fb88a    --warn #e8a33d  --err #d4524a
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] constexpr ImVec4 Bg0() { return {0.039F, 0.047F, 0.059F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 Bg1() { return {0.071F, 0.082F, 0.102F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 Bg2() { return {0.094F, 0.110F, 0.129F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 Bg3() { return {0.122F, 0.141F, 0.169F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 Hover() { return {0.137F, 0.157F, 0.188F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 Border() { return {0.165F, 0.184F, 0.216F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 BorderStrong() { return {0.227F, 0.251F, 0.286F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 Text() { return {0.910F, 0.894F, 0.851F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 Muted() { return {0.604F, 0.584F, 0.541F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 Dim() { return {0.369F, 0.357F, 0.329F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 Accent() { return {0.016F, 0.647F, 0.988F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 AccentHover() { return {0.180F, 0.706F, 0.992F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 AccentActive() { return {0.000F, 0.500F, 0.820F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 AccentSoft() { return {0.016F, 0.647F, 0.988F, 0.15F}; }
    [[nodiscard]] constexpr ImVec4 Ok() { return {0.373F, 0.722F, 0.541F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 Warn() { return {0.910F, 0.639F, 0.239F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 Err() { return {0.831F, 0.322F, 0.290F, 1.0F}; }
    [[nodiscard]] constexpr ImVec4 DarkText() { return {0.020F, 0.075F, 0.110F, 1.0F}; }

    [[nodiscard]] inline ImU32 U32(const ImVec4 &c) { return ImGui::GetColorU32(c); }

    // ─────────────────────────────────────────────────────────────────────────
    // Fonts — the three font atlas entries loaded by the application
    // ─────────────────────────────────────────────────────────────────────────
    struct Fonts
    {
        ImFont *sans = nullptr;         // Inter,        loaded size: 15px (--font-sans)
        ImFont *mono = nullptr;         // IBM Plex Mono loaded size: 13px (--font-mono, regular)
        ImFont *monoSemiBold = nullptr; // IBM Plex Mono loaded size: 15px (--font-mono, 600/700)
    };

    namespace FontPx
    {
        constexpr float Sans = 15.0F;
        constexpr float Mono = 13.0F;
        constexpr float MonoSemiBold = 15.0F;
    }

    // Converts arbitrary HTML/CSS font sizes to the fixed atlas sizes we have
    // using the required SetWindowFontScale() multiplier.
    [[nodiscard]] constexpr float Scale(float targetPx, float basePx) { return targetPx / basePx; }

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
        explicit ScopedFont(ImFont *f) : font(f) { PushFont(font); }
        ~ScopedFont() { PopFont(font); }
        ScopedFont(const ScopedFont &) = delete;
        ScopedFont &operator=(const ScopedFont &) = delete;
    };

    // RAII: applies a window-local font scale and always restores 1.0 at scope exit.
    struct ScopedFontScale
    {
        explicit ScopedFontScale(float scale) { ImGui::SetWindowFontScale(scale); }
        ~ScopedFontScale() { ImGui::SetWindowFontScale(1.0F); }
        ScopedFontScale(const ScopedFontScale &) = delete;
        ScopedFontScale &operator=(const ScopedFontScale &) = delete;
    };

    // Shortcut: pushes `font` and scales it to the target HTML pixel size,
    // then automatically restores both at scope exit.
    //   Example: ScopedTextStyle ts(f.mono, /*targetPx=*/11.0f, Theme::FontPx::Mono);
    struct ScopedTextStyle
    {
        ScopedFont font;
        ScopedFontScale scale;
        ScopedTextStyle(ImFont *f, float targetPx, float basePx)
            : font(f), scale(Scale(targetPx, basePx)) {}
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
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Applies the global ImGui style. Call once after ImGui::CreateContext().
    // All visual defaults such as colors, rounding, and padding come from here;
    // widget code must not depend on this function's implementation details.
    // ─────────────────────────────────────────────────────────────────────────
    void Apply(ImGuiStyle &style);

} // namespace Horo::Editor::Theme