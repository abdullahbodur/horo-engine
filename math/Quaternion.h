#pragma once
#include <string>

#include "math/Vec3.h"

namespace Monolith {

struct Mat3;

struct Quaternion {
  float x, y, z, w;

  Quaternion() : x(0), y(0), z(0), w(1) {}
  Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

  static Quaternion Identity() { return {0, 0, 0, 1}; }
  static Quaternion FromAxisAngle(const Vec3& axis, float radians);
  static Quaternion FromEuler(float pitch, float yaw, float roll);  // radians, XYZ order
  static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t);
  static Quaternion Lerp(const Quaternion& a, const Quaternion& b, float t);
  static float Dot(const Quaternion& a, const Quaternion& b);
  static Quaternion LookRotation(const Vec3& forward, const Vec3& up = Vec3::Up());

  Quaternion operator*(const Quaternion& o) const;
  Vec3 operator*(const Vec3& v) const;  // rotate vector
  Quaternion operator*(float s) const;
  Quaternion operator+(const Quaternion& o) const;
  Quaternion Conjugate() const { return {-x, -y, -z, w}; }
  Quaternion Inverse() const;
  Quaternion Normalized() const;
  float Length() const;
  Vec3 ToEuler() const;  // returns pitch/yaw/roll in radians
  Mat3 ToMat3() const;
  Vec3 Forward() const { return *this * Vec3::Forward(); }
  Vec3 Up() const { return *this * Vec3::Up(); }
  Vec3 Right() const { return *this * Vec3::Right(); }

  std::string ToString() const;
};

}  // namespace Monolith
