#pragma once

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/WelcomeScreen.h"

#include <imgui.h>

namespace Horo::Editor
{

    /** @brief Texture handles required by the welcome screen renderer. */
    struct WelcomeScreenGuiAssets
    {
        ImTextureID logo = 0;
    };

    /** @brief Commands emitted by the welcome screen renderer. */
    enum class WelcomeScreenGuiCommand
    {
        None,
        NewProject,
        OpenSettings,
    };

    /**
     * @brief Draws the HoroEditor welcome screen using design-system components.
     * @param viewModel Immutable welcome screen data.
     * @param fonts Loaded editor font handles.
     * @param assets Texture handles used by the screen.
     * @return The command requested by user interaction this frame.
     */
    [[nodiscard]] WelcomeScreenGuiCommand DrawWelcomeScreenGui(const WelcomeViewModel &viewModel,
                                                               const Theme::Fonts &fonts,
                                                               const WelcomeScreenGuiAssets &assets);

} // namespace Horo::Editor
