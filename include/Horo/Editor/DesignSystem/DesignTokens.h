/**
 * @file DesignTokens.h
 * @brief Typed, theme-resolved design metrics shared by editor UI primitives.
 */
#pragma once

#include <array>
#include <cstddef>
#include <imgui.h>

namespace Horo::Editor::DesignSystem {

    /** @brief Shared t-shirt size used by every editor primitive. */
    enum class ComponentSize : std::size_t {
        XS,
        Small,
        Medium,
        Large,
        XL,
    };

    /** @brief Theme-backed spacing step used by component style properties. */
    enum class SpacingSize : std::size_t {
        None,
        XS,
        Small,
        Medium,
        Large,
        XL,
    };

    /** @brief Geometry and typography resolved for one component size. */
    struct ComponentSizeMetrics {
        float fontSize;      /**< Text size in logical UI pixels. */
        float paddingX;      /**< Default horizontal frame padding. */
        float paddingY;      /**< Default vertical frame padding. */
        float minimumHeight; /**< Minimum interaction height. */
        float iconSize;      /**< Square icon drawing extent. */
    };

    /** @brief Shared primitive sizing and spacing scale. */
    struct ComponentTokens {
        std::array<ComponentSizeMetrics, 5> sizes;
        std::array<float, 6> spacing;
    };

    /**
     * @brief Semantic color tokens consumed by editor GUI components.
     *
     * These values are the packaged default dark theme for the bootstrap GUI. They
     * intentionally live outside feature screens so screens compose tokens instead
     * of embedding visual literals in screen code.
     */
    struct ColorTokens {
        ImVec4 surfaceRoot;
        ImVec4 surfaceWindow;
        ImVec4 surfacePanel;
        ImVec4 surfaceRaised;
        ImVec4 surfaceHover;
        ImVec4 border;
        ImVec4 borderStrong;
        ImVec4 textPrimary;
        ImVec4 textMuted;
        ImVec4 textDim;
        ImVec4 actionPrimary;
        ImVec4 actionPrimaryHover;
        ImVec4 actionPrimaryActive;
        ImVec4 actionPrimarySoft;
        ImVec4 statusOk;
        ImVec4 statusWarn;
        ImVec4 statusError;
        ImVec4 textOnActionPrimary;
    };

    /** @brief Semantic typography sizes for the bootstrap GUI. */
    struct TypographyTokens {
        float sansBase;
        float sansCompactBase;
        float sansEmphasisBase;
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
        float uiScale;
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
        ComponentTokens components;
    };

    /**
     * @brief Returns the resolved metrics for one shared component size.
     * @param tokens Active resolved design tokens.
     * @param size Requested t-shirt size.
     * @return Theme and display-scale resolved component metrics.
     */
    [[nodiscard]] inline const ComponentSizeMetrics &MetricsFor(const DesignTokens &tokens, const ComponentSize size) noexcept {
        return tokens.components.sizes[static_cast<std::size_t>(size)];
    }

    /**
     * @brief Returns one resolved style-spacing value.
     * @param tokens Active resolved design tokens.
     * @param size Requested semantic spacing step.
     * @return Theme and display-scale resolved spacing value.
     */
    [[nodiscard]] inline float SpacingFor(const DesignTokens &tokens, const SpacingSize size) noexcept {
        return tokens.components.spacing[static_cast<std::size_t>(size)];
    }

    /** @brief Applies one global UI scale to resolved component metrics. */
    inline void ApplyGlobalUiScale(DesignTokens &tokens, const float scale) noexcept {
        const float clamped = scale < 0.75F ? 0.75F : scale > 2.0F ? 2.0F : scale;
        tokens.sizes.uiScale = clamped;

        // Resolve all numeric presentation metrics from one scale so component size,
        // typography, spacing and layout grow together.
        tokens.typography.sansBase *= clamped;
        tokens.typography.sansCompactBase *= clamped;
        tokens.typography.sansEmphasisBase *= clamped;

        tokens.radii.control *= clamped;
        tokens.radii.card *= clamped;
        tokens.radii.modal *= clamped;

        tokens.sizes.welcomeSideWidth *= clamped;
        tokens.sizes.welcomePadding *= clamped;
        tokens.sizes.modalWidth *= clamped;
        tokens.sizes.modalHeight *= clamped;
        tokens.sizes.modalHeaderHeight *= clamped;
        tokens.sizes.modalFooterHeight *= clamped;
        tokens.sizes.modalSidebarWidth *= clamped;
        tokens.sizes.settingsWidth *= clamped;
        tokens.sizes.settingsHeight *= clamped;
        tokens.spacing.cardPadding *= clamped;
        tokens.spacing.gridGap *= clamped;
        tokens.spacing.bodyPaddingX *= clamped;
        tokens.spacing.bodyPaddingY *= clamped;
        tokens.spacing.sidebarPaddingX *= clamped;
        tokens.spacing.sidebarPaddingY *= clamped;

        for (ComponentSizeMetrics &metrics : tokens.components.sizes) {
            metrics.fontSize *= clamped;
            metrics.paddingX *= clamped;
            metrics.paddingY *= clamped;
            metrics.minimumHeight *= clamped;
            metrics.iconSize *= clamped;
        }
        for (float &spacing : tokens.components.spacing)
            spacing *= clamped;
    }

    /**
     * @brief Returns the packaged default editor design tokens.
     * @return Unscaled packaged baseline used before custom theme overrides are applied.
     */
    [[nodiscard]] constexpr DesignTokens DefaultDesignTokens() noexcept {
        return DesignTokens{
            ColorTokens{
                ImVec4{0.039F, 0.047F, 0.059F, 1.0F},
                ImVec4{0.071F, 0.082F, 0.102F, 1.0F},
                ImVec4{0.094F, 0.110F, 0.129F, 1.0F},
                ImVec4{0.122F, 0.141F, 0.169F, 1.0F},
                ImVec4{0.137F, 0.157F, 0.188F, 1.0F},
                ImVec4{0.165F, 0.184F, 0.216F, 1.0F},
                ImVec4{0.227F, 0.251F, 0.286F, 1.0F},
                ImVec4{0.910F, 0.894F, 0.851F, 1.0F},
                ImVec4{0.604F, 0.584F, 0.541F, 1.0F},
                ImVec4{0.369F, 0.357F, 0.329F, 1.0F},
                ImVec4{0.016F, 0.647F, 0.988F, 1.0F},
                ImVec4{0.180F, 0.706F, 0.992F, 1.0F},
                ImVec4{0.000F, 0.500F, 0.820F, 1.0F},
                ImVec4{0.016F, 0.647F, 0.988F, 0.15F},
                ImVec4{0.373F, 0.722F, 0.541F, 1.0F},
                ImVec4{0.910F, 0.639F, 0.239F, 1.0F},
                ImVec4{0.831F, 0.322F, 0.290F, 1.0F},
                ImVec4{0.020F, 0.075F, 0.110F, 1.0F},
            },
            TypographyTokens{16.0F, 14.0F, 16.0F},
            RadiusTokens{4.0F, 6.0F, 8.0F},
            SizeTokens{280.0F, 32.0F, 900.0F, 680.0F, 58.0F, 52.0F, 220.0F, 620.0F, 440.0F, 1.0F},
            SpacingTokens{18.0F, 14.0F, 28.0F, 24.0F, 14.0F, 18.0F},
            ComponentTokens{
                std::array{
                    ComponentSizeMetrics{12.0F, 8.0F, 3.0F, 24.0F, 12.0F},
                    ComponentSizeMetrics{13.0F, 10.0F, 5.0F, 28.0F, 14.0F},
                    ComponentSizeMetrics{14.0F, 14.0F, 7.0F, 32.0F, 16.0F},
                    ComponentSizeMetrics{16.0F, 18.0F, 10.0F, 40.0F, 20.0F},
                    ComponentSizeMetrics{18.0F, 22.0F, 13.0F, 48.0F, 24.0F},
                },
                std::array{0.0F, 4.0F, 8.0F, 12.0F, 16.0F, 24.0F},
            },
        };
    }

}  // namespace Horo::Editor::DesignSystem
