/** @file LauncherProjectTemplate.h
 *  @brief Provides the scaffold creation function for new launcher projects. */
#pragma once

#include <filesystem>
#include <string>

#include "ui/launcher/LauncherProject.h"

namespace Horo::Launcher {

/** @brief Parameters required to create a new project from a template. */
struct LauncherProjectTemplateRequest {
    std::filesystem::path projectRoot;          /**< Absolute path where the project directory will be created. */
    std::string projectName;                    /**< Human-readable display name for the new project. */
    std::filesystem::path sdkRoot;              /**< Absolute path to the engine SDK used for code generation. */
    std::string rendererBackend = "opengl";     /**< Renderer backend identifier (e.g. "opengl", "vulkan"). */
};

/** @brief Generates the directory structure and manifest for a new project from a template.
 *  @param request            Parameters controlling what project to create.
 *  @param outProjectDocument Receives the newly created project document on success; must not be nullptr.
 *  @param outError           Receives a human-readable error message on failure.
 *  @return True if the project was created and the document populated successfully. */
bool CreateLauncherProjectTemplate(
    const LauncherProjectTemplateRequest &request,
    LauncherProjectDocument *outProjectDocument, std::string *outError);

} // namespace Horo::Launcher
