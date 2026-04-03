#pragma once
#include <memory>

#include "math/Vec4.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"

namespace Horo {

class Material {
 public:
  Vec4 color = {1, 1, 1, 1};
  float roughness = 0.5f;
  float metallic = 0.0f;

  std::shared_ptr<Texture> albedoMap;  // optional; falls back to u_color if null
  float uvScale = 1.0f;                // texture tiling multiplier (>1 = more tiles = zoomed out)

  Shader* shader = nullptr;  // non-owning

  void Apply() const;
};

}  // namespace Horo
