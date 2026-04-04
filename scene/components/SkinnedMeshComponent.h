#pragma once

#include <memory>
#include <string>

#include "renderer/Material.h"
#include "renderer/SkinnedMesh.h"

namespace Monolith {

// Mirrors MeshComponent but for skinned geometry.
struct SkinnedMeshComponent {
  std::shared_ptr<SkinnedMesh> mesh;
  std::shared_ptr<Material>    material;
  bool visible = true;
  std::string meshTag;  // source identifier (e.g. asset path), informational
};

}  // namespace Monolith
