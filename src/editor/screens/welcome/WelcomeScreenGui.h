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
    OpenRecentProject, ///< @see WelcomeScreenGuiResult::openRecentIndex
    OpenProject,
};

/**
 * @brief Result of a single welcome screen render frame.
 *
 * Carries the command and, for commands that carry a payload (e.g.
 * `OpenRecentProject`), the associated index into
 * `WelcomeViewModel::recentProjects`.
 */
struct WelcomeScreenGuiResult
{
    WelcomeScreenGuiCommand command = WelcomeScreenGuiCommand::None;
    /// Zero-based index into WelcomeViewModel::recentProjects.
    /// Valid only when command == OpenRecentProject.
    int openRecentIndex = -1;
};

/**
 * @brief Draws the HoroEditor welcome screen using design-system components.
 * @param viewModel Immutable welcome screen data.
 * @param fonts Loaded editor font handles.
 * @param assets Texture handles used by the screen.
 * @return Result describing the user action that occurred this frame.
 */
[[nodiscard]] WelcomeScreenGuiResult DrawWelcomeScreenGui(const WelcomeViewModel &viewModel,
                                                          const Theme::Fonts &fonts,
                                                          const WelcomeScreenGuiAssets &assets);

} // namespace Horo::Editor
