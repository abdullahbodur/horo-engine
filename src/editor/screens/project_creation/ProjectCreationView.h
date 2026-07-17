#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/ProjectCreationController.h"
#include "Horo/Runtime/Input.h"
#include "editor/project_model/RendererAvailability.h"

#include <string>

namespace Horo::Editor
{
    struct GuiContentRegion;

    /** @brief Transient ImGui state retained by the ProjectCreation route view presentation. */
    struct ProjectCreationViewState
    {
        bool initialized = false;
        int step = 1;
        bool confirmingDiscard = false;
        std::string projectName;
        std::string projectPath;
        std::string projectVersion;
        std::string defaultScene;
        std::string targetFps;
        int renderBackendIndex = 0;
        int physicsIndex = 0;
        int buildProfileIndex = 0;
        int assetCompressionIndex = 0;
        int textureCompressionIndex = 0;
        int targetPlatformIndex = 0;
        int compilerFamilyIndex = 0;
        int cppStandardIndex = 0;
        std::string packageRegistryUrl = "registry://horo/starter-package";
        std::string packageVersion = "1.0.0";
        int firstPersonInputMapIndex = 0;
        bool demoObservabilityOverlays = true;
        bool demoBenchmarkScene = true;
        bool customSubsystems[5]{true, true, true, false, false};
    };

    /** @brief Command emitted by the ProjectCreation route view presentation. */
    enum class ProjectCreationViewCommand
    {
        None,
        ReturnToWelcome,
        CreateProject,
    };

    /**
     * @brief Draws the ProjectCreation route as full application content.
     * @param controller Headless project-creation workflow state.
     * @param state Route-local presentation state.
     * @param ctx Editor GUI context and fonts.
     * @param rendererAvailability Machine-local renderer choices and disabled-state diagnostics.
     * @param logo Optional logo texture shown in the header.
     * @return Typed navigation command requested during this frame.
     */
    [[nodiscard]] ProjectCreationViewCommand DrawProjectCreationView(
        ProjectCreationController& controller, ProjectCreationViewState& state, const EditorGuiContext& ctx,
        Input::InputRouter& inputRouter, const RendererAvailabilitySnapshot& rendererAvailability,
        const GuiContentRegion& contentRegion,
        ImTextureID logo = 0);
} // namespace Horo::Editor
