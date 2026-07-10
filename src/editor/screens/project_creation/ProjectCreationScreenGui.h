#pragma once

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/ProjectCreationScreen.h"

namespace Horo::Editor {

/** @brief Transient ImGui state retained by the ProjectCreation route presentation. */
struct ProjectCreationScreenGuiState {
    bool initialized = false;
    int step = 1;
    bool confirmingDiscard = false;
    char projectName[128]{};
    char projectPath[512]{};
    char projectVersion[32]{};
    char defaultScene[128]{};
    char targetFps[16]{};
    int renderBackendIndex = 0;
    int physicsIndex = 0;
    int buildProfileIndex = 0;
    int assetCompressionIndex = 0;
    int textureCompressionIndex = 0;
    int targetPlatformIndex = 0;
    int compilerFamilyIndex = 0;
    int cppStandardIndex = 0;
    char packageRegistryUrl[256]{"registry://horo/starter-package"};
    char packageVersion[64]{"1.0.0"};
    int firstPersonInputMapIndex = 0;
    bool demoObservabilityOverlays = true;
    bool demoBenchmarkScene = true;
    bool customSubsystems[5]{true, true, true, false, false};
};

/** @brief Command emitted by the ProjectCreation route presentation. */
enum class ProjectCreationScreenGuiCommand {
    None,
    ReturnToWelcome,
    CreateProject,
};

/**
 * @brief Draws the ProjectCreation route as full application content.
 * @param controller Headless project-creation workflow state.
 * @param state Route-local presentation state.
 * @param fonts Loaded editor font handles.
 * @param logo Optional logo texture shown in the modal header.
 * @return Typed navigation command requested during this frame.
 */
[[nodiscard]] ProjectCreationScreenGuiCommand DrawProjectCreationScreenGui(
    ProjectCreationController& controller,
    ProjectCreationScreenGuiState& state,
    const Theme::Fonts& fonts,
    ImTextureID logo = 0);

} // namespace Horo::Editor
