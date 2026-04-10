#pragma once

#include <memory>
#include <string>

#include "renderer/IRenderBackend.h"
#include "renderer/RenderBackend.h"

namespace Monolith {

struct RenderBackendCreateResult {
  std::unique_ptr<IRenderBackend> backend;
  RenderBackendId selected = RenderBackendId::Auto;
  std::string error;
};

RenderBackendCreateResult CreateRenderBackend(const RenderBackendSelection& selection);

}  // namespace Monolith
