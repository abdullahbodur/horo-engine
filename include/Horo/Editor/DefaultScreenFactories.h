#pragma once

namespace Horo::Editor
{

class ScreenRegistry;

/**
 * @file DefaultScreenFactories.h
 * @brief Registration helpers for default application GUI screens.
 */
void RegisterWelcomeScreen(ScreenRegistry &registry);
void RegisterProjectCreationScreen(ScreenRegistry &registry);
void RegisterProjectLoadingScreen(ScreenRegistry &registry);
void RegisterEditorWorkspaceScreen(ScreenRegistry &registry);

} // namespace Horo::Editor
