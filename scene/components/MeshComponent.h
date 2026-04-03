#pragma once
#include <memory>
#include <string>

#include "renderer/Material.h"
#include "renderer/Mesh.h"

namespace Horo {

struct MeshComponent {
  std::shared_ptr<Mesh> mesh;
  std::shared_ptr<Material> material;
  bool visible = true;
  std::string meshTag;  // source tag for scene export ("stone.obj", etc.)
};

}  // namespace Horo
