#pragma once
#include <cmath>
#include <string>

namespace Horo {

struct Vec3;

struct Vec4 {
  float x, y, z, w;

  Vec4() : x(0), y(0), z(0), w(0) {}
  Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
  Vec4(const Vec3& v, float w);
  explicit Vec4(float s) : x(s), y(s), z(s), w(s) {}

  Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
  Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
  Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
  Vec4 operator/(float s) const { return {x / s, y / s, z / s, w / s}; }
  Vec4 operator-() const { return {-x, -y, -z, -w}; }

  Vec4& operator+=(const Vec4& o) {
    x += o.x;
    y += o.y;
    z += o.z;
    w += o.w;
    return *this;
  }
  Vec4& operator-=(const Vec4& o) {
    x -= o.x;
    y -= o.y;
    z -= o.z;
    w -= o.w;
    return *this;
  }
  Vec4& operator*=(float s) {
    x *= s;
    y *= s;
    z *= s;
    w *= s;
    return *this;
  }

  bool operator==(const Vec4& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
  bool operator!=(const Vec4& o) const { return !(*this == o); }

  float LengthSq() const { return x * x + y * y + z * z + w * w; }
  float Length() const { return std::sqrt(LengthSq()); }
  float& operator[](int i) { return (&x)[i]; }
  float operator[](int i) const { return (&x)[i]; }

  Vec3 XYZ() const;

  static float Dot(const Vec4& a, const Vec4& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  }
  static Vec4 Zero() { return {0, 0, 0, 0}; }

  std::string ToString() const;
};

inline Vec4 operator*(float s, const Vec4& v) {
  return v * s;
}

}  // namespace Horo
