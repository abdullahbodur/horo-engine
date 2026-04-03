#pragma once
#include <cmath>
#include <string>

namespace Horo {

struct Vec3 {
  float x, y, z;

  Vec3() : x(0), y(0), z(0) {}
  Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
  explicit Vec3(float s) : x(s), y(s), z(s) {}

  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
  Vec3 operator-() const { return {-x, -y, -z}; }
  Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }  // component-wise

  Vec3& operator+=(const Vec3& o) {
    x += o.x;
    y += o.y;
    z += o.z;
    return *this;
  }
  Vec3& operator-=(const Vec3& o) {
    x -= o.x;
    y -= o.y;
    z -= o.z;
    return *this;
  }
  Vec3& operator*=(float s) {
    x *= s;
    y *= s;
    z *= s;
    return *this;
  }
  Vec3& operator/=(float s) {
    x /= s;
    y /= s;
    z /= s;
    return *this;
  }

  bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
  bool operator!=(const Vec3& o) const { return !(*this == o); }

  float LengthSq() const { return x * x + y * y + z * z; }
  float Length() const { return std::sqrt(LengthSq()); }
  Vec3 Normalized() const;
  float& operator[](int i) { return (&x)[i]; }
  float operator[](int i) const { return (&x)[i]; }

  static float Dot(const Vec3& a, const Vec3& b);
  static Vec3 Cross(const Vec3& a, const Vec3& b);
  static Vec3 Lerp(const Vec3& a, const Vec3& b, float t);
  static float Distance(const Vec3& a, const Vec3& b);
  static Vec3 Reflect(const Vec3& v, const Vec3& n);

  static Vec3 Zero() { return {0, 0, 0}; }
  static Vec3 One() { return {1, 1, 1}; }
  static Vec3 Up() { return {0, 1, 0}; }
  static Vec3 Down() { return {0, -1, 0}; }
  static Vec3 Right() { return {1, 0, 0}; }
  static Vec3 Left() { return {-1, 0, 0}; }
  static Vec3 Forward() { return {0, 0, -1}; }
  static Vec3 Back() { return {0, 0, 1}; }

  std::string ToString() const;
};

inline Vec3 operator*(float s, const Vec3& v) {
  return v * s;
}

}  // namespace Horo
