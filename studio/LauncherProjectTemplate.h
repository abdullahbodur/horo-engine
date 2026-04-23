#pragma once

#include <filesystem>
#include <string>

#include "LauncherProject.h"

namespace Monolith::Launcher {

struct LauncherProjectTemplateRequest {
  std::filesystem::path projectRoot;
  std::string projectName;
  std::filesystem::path sdkRoot;
};

bool CreateLauncherProjectTemplate(const LauncherProjectTemplateRequest& request,
                                     LauncherProjectDocument* outProjectDocument,
                                     std::string* outError);

}  // namespace Monolith::Launcher
