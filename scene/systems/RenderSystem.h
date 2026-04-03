#pragma once
#include "scene/System.h"

namespace Horo {

class Camera;

class RenderSystem : public System {
 public:
  explicit RenderSystem(Camera& camera, float& alpha) : m_camera(camera), m_alpha(alpha) {}
  void OnUpdate(Registry& registry, float dt) override;

 private:
  Camera& m_camera;
  float& m_alpha;
};

}  // namespace Horo
