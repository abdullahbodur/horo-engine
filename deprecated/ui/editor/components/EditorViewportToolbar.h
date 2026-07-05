/** @file EditorViewportToolbar.h
 *  @brief Viewport title-bar tool controls and state contracts. */
#pragma once

#include <functional>

#include "ui/HoroTheme.h"

namespace Horo::Editor {
    /** @brief Read-only state consumed by the viewport title-bar tool strip. */
    struct EditorViewportToolbarState {
        bool preciseTransformEnabled = false; /**< True when transform precision snapping is active. */
        float preciseTranslateStepMeters = 0.1f; /**< Translation snap interval in engine units/metres. */
    };

    /** @brief Callback table used by viewport title-bar tools. */
    struct EditorViewportToolbarCallbacks {
        std::function<void(bool)> setPreciseTransformEnabled; /**< Toggles precision snapping. */
        std::function<void(float)> setPreciseTranslateStepMeters; /**< Updates translation snap interval. */
    };

    /** @brief Returns a stable label for the nearest supported translate precision step. */
    const char *ViewportTranslatePrecisionLabel(float stepMeters);

    /** @brief Resolves the active translate snap size from precision state. */
    float ResolveViewportTranslateSnapStep(bool preciseEnabled,
                                           float preciseStepMeters,
                                           float fallbackStepMeters);

    /** @brief Resolves the active rotate snap size in degrees from precision state. */
    float ResolveViewportRotateSnapStepDegrees(bool preciseEnabled,
                                               float preciseStepMeters,
                                               float fallbackStepDegrees);

    /** @brief Resolves the active scale snap size from precision state. */
    float ResolveViewportScaleSnapStep(bool preciseEnabled,
                                       float preciseStepMeters,
                                       float fallbackStep);

    /** @brief Draws the viewport title-bar tools. */
    class EditorViewportToolbar {
    public:
        /** @brief Draws the viewport section bar with suffix tool controls.
         *  @param theme     Active editor theme.
         *  @param state     Current viewport tool state.
         *  @param callbacks Mutating callbacks invoked by tool menu items.
         */
        void Draw(const Ui::EditorTheme &theme,
                  const EditorViewportToolbarState &state,
                  const EditorViewportToolbarCallbacks &callbacks) const;
    };
} // namespace Horo::Editor
