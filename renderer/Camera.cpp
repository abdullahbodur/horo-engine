#include "renderer/Camera.h"

#include "math/MathUtils.h"

namespace Horo {

Mat4 Camera::GetView() const {
  return Mat4::LookAt(position, target, up);
}

Mat4 Camera::GetProjection() const {
  return Mat4::Perspective(ToRadians(fovY), aspect, zNear, zFar);
}

}  // namespace Horo
