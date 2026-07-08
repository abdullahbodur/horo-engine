#pragma once

#include <imgui.h>

namespace Horo::Editor::DesignSystem {

/**
 * @brief Semantic color tokens consumed by editor GUI components.
 *
 * These values are the packaged default dark theme for the bootstrap GUI. They
 * intentionally live outside feature screens so screens compose tokens instead
 * of embedding visual literals in screen code.
 */
struct ColorTokens {
    ::ImVec4 surfaceRoot;
    ::ImVec4 surfaceWindow;
    ::ImVec4 surfacePanel;
    ::ImVec4 surfaceRaised;
    ::ImVec4 surfaceHover;
    ::ImVec4 border;
    ::ImVec4 borderStrong;
    ::ImVec4 textPrimary;
    ::ImVec4 textMuted;
    ::ImVec4 textDim;
    ::ImVec4 actionPrimary;
    ::ImVec4 actionPrimaryHover;
    ::ImVec4 actionPrimaryActive;
    ::ImVec4 actionPrimarySoft;
    ::ImVec4 statusOk;
    ::ImVec4 statusWarn;
    ::ImVec4 statusError;
    ::ImVec4 textOnActionPrimary;
};

/** @brief Semantic typography sizes for the bootstrap GUI. */
struct TypographyTokens {
    float sansBase;
    float monoBase;
    float monoSemiBoldBase;
};

/** @brief Shared shape tokens for editor GUI components. */
struct RadiusTokens {
    float control;
    float card;
    float modal;
};

/** @brief Shared layout and control dimensions for editor GUI components. */
struct SizeTokens {
    float welcomeSideWidth;
    float welcomePadding;
    float modalWidth;
    float modalHeight;
    float modalHeaderHeight;
    float modalFooterHeight;
    float modalSidebarWidth;
    float settingsWidth;
    float settingsHeight;
};

/** @brief Shared spacing tokens for editor GUI components. */
struct SpacingTokens {
    float cardPadding;
    float gridGap;
    float bodyPaddingX;
    float bodyPaddingY;
    float sidebarPaddingX;
    float sidebarPaddingY;
};

/** @brief Resolved immutable editor design tokens for one rendered frame. */
struct DesignTokens {
    ColorTokens colors;
    TypographyTokens typography;
    RadiusTokens radii;
    SizeTokens sizes;
    SpacingTokens spacing;
};

/**
 * @brief Returns the packaged default editor design tokens.
 * @return Default immutable token set used until data-driven theme loading lands.
 */
[[nodiscard]] constexpr DesignTokens DefaultDesignTokens() noexcept
{
    return DesignTokens{
        ColorTokens{
            ::ImVec4{0.039F, 0.047F, 0.059F, 1.0F},
            ::ImVec4{0.071F, 0.082F, 0.102F, 1.0F},
            ::ImVec4{0.094F, 0.110F, 0.129F, 1.0F},
            ::ImVec4{0.122F, 0.141F, 0.169F, 1.0F},
            ::ImVec4{0.137F, 0.157F, 0.188F, 1.0F},
            ::ImVec4{0.165F, 0.184F, 0.216F, 1.0F},
            ::ImVec4{0.227F, 0.251F, 0.286F, 1.0F},
            ::ImVec4{0.910F, 0.894F, 0.851F, 1.0F},
            ::ImVec4{0.604F, 0.584F, 0.541F, 1.0F},
            ::ImVec4{0.369F, 0.357F, 0.329F, 1.0F},
            ::ImVec4{0.016F, 0.647F, 0.988F, 1.0F},
            ::ImVec4{0.180F, 0.706F, 0.992F, 1.0F},
            ::ImVec4{0.000F, 0.500F, 0.820F, 1.0F},
            ::ImVec4{0.016F, 0.647F, 0.988F, 0.15F},
            ::ImVec4{0.373F, 0.722F, 0.541F, 1.0F},
            ::ImVec4{0.910F, 0.639F, 0.239F, 1.0F},
            ::ImVec4{0.831F, 0.322F, 0.290F, 1.0F},
            ::ImVec4{0.020F, 0.075F, 0.110F, 1.0F},
        },
        TypographyTokens{16.0F, 14.0F, 16.0F},
        RadiusTokens{4.0F, 6.0F, 8.0F},
        SizeTokens{280.0F, 32.0F, 900.0F, 680.0F, 58.0F, 52.0F, 220.0F, 620.0F, 440.0F},
        SpacingTokens{18.0F, 14.0F, 28.0F, 24.0F, 14.0F, 18.0F},
    };
}

} // namespace Horo::Editor::DesignSystem
