#pragma once

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/WelcomeController.h"
#include "Horo/Editor/EditorGuiContext.h"
#include <imgui.h>

namespace Horo::Editor
{

/** @brief Texture handles required by the welcome view renderer. */
struct WelcomeViewAssets
{
    ImTextureID logo = 0;
};

/** @brief Commands emitted by the welcome view renderer. */
enum class WelcomeViewCommand
{
    None,
    NewProject,
    OpenSettings,
    OpenRecentProject, ///< @see WelcomeViewResult::openRecentIndex
    OpenProject,
};

/**
 * @brief Result of a single welcome view render frame.
 *
 * Carries the command and, for commands that carry a payload (e.g.
 * `OpenRecentProject`), the associated index into
 * `WelcomeViewModel::recentProjects`.
 */
struct WelcomeViewResult
{
    WelcomeViewCommand command = WelcomeViewCommand::None;
    /// Zero-based index into WelcomeViewModel::recentProjects.
    /// Valid only when command == OpenRecentProject.
    int openRecentIndex = -1;
};

/**
 * @brief Draws the HoroEditor welcome view using design-system components.
 * @param viewModel Immutable welcome screen data.
 * @param ctx Editor GUI context and fonts.
 * @param assets Texture handles used by the view.
 * @return Result describing the user action that occurred this frame.
 */
[[nodiscard]] WelcomeViewResult DrawWelcomeView(const WelcomeViewModel &viewModel,
                                                const EditorGuiContext &ctx,
                                                const WelcomeViewAssets &assets);

} // namespace Horo::Editor
