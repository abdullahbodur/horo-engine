#pragma once
#include <memory>

#include "math/Vec4.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"

namespace Monolith {
class Material {
public:
  Vec4 color = {1, 1, 1, 1};
  float roughness = 0.5f;
  float metallic = 0.0f;

  std::shared_ptr<Texture> albedoMap; // optional; falls back to u_color if null
  float uvScale =
      1.0f; // texture tiling multiplier (>1 = more tiles = zoomed out)

  std::shared_ptr<Shader>
      shader; // shared resource handle used by renderer backends

  bool HasShader() const { return shader && shader->IsValid(); }
};
} // namespace Monolith
