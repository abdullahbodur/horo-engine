#pragma once
#include "math/Transform.h"

namespace Horo {

struct TransformComponent {
  Transform current;
  Transform previous;  // for interpolated rendering
};

}  // namespace Horo
