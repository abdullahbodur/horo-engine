#include "math/Vec4.h"

#include <sstream>

#include "math/Vec3.h"

namespace Horo {

Vec4::Vec4(const Vec3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}

Vec3 Vec4::XYZ() const {
  return {x, y, z};
}

std::string Vec4::ToString() const {
  std::ostringstream ss;
  ss << "Vec4(" << x << ", " << y << ", " << z << ", " << w << ")";
  return ss.str();
}

}  // namespace Horo
