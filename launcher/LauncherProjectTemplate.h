#pragma once

#include <filesystem>
#include <string>

#include "launcher/LauncherProject.h"

namespace Horo::Launcher {
    struct LauncherProjectTemplateRequest {
        std::filesystem::path projectRoot;
        std::string projectName;
        std::filesystem::path sdkRoot;
        std::string rendererBackend = "opengl";
    };

    bool CreateLauncherProjectTemplate(
        const LauncherProjectTemplateRequest &request,
        LauncherProjectDocument *outProjectDocument, std::string *outError);
} // namespace Horo::Launcher
