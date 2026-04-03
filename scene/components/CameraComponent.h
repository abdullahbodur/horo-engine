#pragma once
#include "renderer/Camera.h"

namespace Horo {

struct CameraComponent {
  Camera camera;
  bool isActive = false;
};

}  // namespace Horo
