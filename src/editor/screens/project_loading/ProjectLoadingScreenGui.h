#pragma once

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorGuiContext.h"
#include <imgui.h>
#include <string>

namespace Horo::Editor
{

    enum class ProjectLoadingScreenGuiCommand
    {
        None,
        Cancel
    };

    /**
     * @brief State for the project loading simulation/UI.
     */
    struct ProjectLoadingScreenGuiState
    {
        std::string projectName;
        std::string projectRoot;
        float progress = 0.0f;
        std::string statusText = "Preparing workspace...";
        bool isCancelled = false;
    };

    /**
     * @brief Draws the project loading screen overlay/modal.
     * @param state The current loading state to visualize.
     * @param fonts The loaded editor fonts.
     * @return Command if the user interacts (e.g. Cancel).
     */
    [[nodiscard]] ProjectLoadingScreenGuiCommand DrawProjectLoadingScreenGui(
        ProjectLoadingScreenGuiState &state,
        const EditorGuiContext &ctx);

} // namespace Horo::Editor
