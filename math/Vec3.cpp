#include "math/Vec3.h"

#include <sstream>

#include "math/MathUtils.h"

namespace Monolith {

float Vec3::Dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Vec3::Cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 Vec3::Normalized() const {
  float len = Length();
  if (NearlyZero(len))
    return Vec3::Zero();
  return *this / len;
}

Vec3 Vec3::Lerp(const Vec3& a, const Vec3& b, float t) {
  return a + (b - a) * t;
}

float Vec3::Distance(const Vec3& a, const Vec3& b) {
  return (b - a).Length();
}

Vec3 Vec3::Reflect(const Vec3& v, const Vec3& n) {
  return v - n * (2.0f * Dot(v, n));
}

std::string Vec3::ToString() const {
  std::ostringstream ss;
  ss << "Vec3(" << x << ", " << y << ", " << z << ")";
  return ss.str();
}

}  // namespace Monolith
