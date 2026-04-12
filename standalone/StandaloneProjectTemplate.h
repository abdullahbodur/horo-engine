#pragma once

#include <filesystem>
#include <string>

#include "standalone/StandaloneProject.h"

namespace Monolith::Standalone {

struct StandaloneProjectTemplateRequest {
  std::filesystem::path projectRoot;
  std::string projectName;
  std::filesystem::path sdkRoot;
};

bool CreateStandaloneProjectTemplate(const StandaloneProjectTemplateRequest& request,
                                     StandaloneProjectDocument* outProjectDocument,
                                     std::string* outError);

}  // namespace Monolith::Standalone
