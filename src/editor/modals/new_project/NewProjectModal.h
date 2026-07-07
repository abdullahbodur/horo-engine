#pragma once

#include "Horo/Editor/EditorTheme.h"

#include <imgui.h>

namespace Horo::Editor
{

/** @brief Mutable UI state for the New Project modal. */
struct NewProjectState
{
    bool open = false;
    int step = 1;
    int selectedTemplate = 1;
    char name[128] = "DesertRun";
    char path[512] = "/Users/bodur/projects/games/DesertRun";
    char version[32] = "0.1.0";
    char defaultScene[128] = "assets/scenes/main.horo";
    int renderBackend = 0;
    char targetFps[8] = "60";
    int physics = 0;
    int buildProfile = 0;
    int assetCompression = 0;
    int textureCompression = 0;
    int targetPlatform = 0;
    int compilerFamily = 0;
    int cppStandard = 0;
    bool initGit = true;
    bool restorePackages = true;
    bool includeStarter = true;
    bool generateCMake = false;
};

/**
 * @brief Draws the New Project modal when its state is open.
 * @param state Mutable modal state.
 * @param fonts Editor font handles.
 * @param logo Optional logo texture shown in the modal header.
 */
void DrawNewProjectModal(NewProjectState& state, const Theme::Fonts& fonts, ::ImTextureID logo);

} // namespace Horo::Editor
