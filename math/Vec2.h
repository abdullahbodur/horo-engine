#pragma once
#include <cmath>
#include <string>

namespace Monolith {
struct Vec2 {
  float x, y;

  Vec2() : x(0), y(0) {}

  Vec2(float x, float y) : x(x), y(y) {}

  explicit Vec2(float s) : x(s), y(s) {}

  Vec2 operator-() const { return {-x, -y}; }

  friend Vec2 operator+(const Vec2 &lhs, const Vec2 &rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
  }

  friend Vec2 operator-(const Vec2 &lhs, const Vec2 &rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y};
  }

  friend Vec2 operator*(const Vec2 &lhs, float scalar) {
    return {lhs.x * scalar, lhs.y * scalar};
  }

  friend Vec2 operator*(float scalar, const Vec2 &rhs) { return rhs * scalar; }

  friend Vec2 operator/(const Vec2 &lhs, float scalar) {
    return {lhs.x / scalar, lhs.y / scalar};
  }

  Vec2 &operator+=(const Vec2 &o) {
    x += o.x;
    y += o.y;
    return *this;
  }

  Vec2 &operator-=(const Vec2 &o) {
    x -= o.x;
    y -= o.y;
    return *this;
  }

  Vec2 &operator*=(float s) {
    x *= s;
    y *= s;
    return *this;
  }

  Vec2 &operator/=(float s) {
    x /= s;
    y /= s;
    return *this;
  }

  bool operator==(const Vec2 &o) const = default;

  float LengthSq() const { return x * x + y * y; }
  float Length() const { return std::sqrt(LengthSq()); }

  Vec2 Normalized() const;

  float &operator[](int i) { return (&x)[i]; }
  float operator[](int i) const { return (&x)[i]; }

  static float Dot(const Vec2 &a, const Vec2 &b) {
    return a.x * b.x + a.y * b.y;
  }

  static Vec2 Lerp(const Vec2 &a, const Vec2 &b, float t) {
    return a + (b - a) * t;
  }

  static Vec2 Zero() { return {0, 0}; }
  static Vec2 One() { return {1, 1}; }
  static Vec2 UnitX() { return {1, 0}; }
  static Vec2 UnitY() { return {0, 1}; }

  std::string ToString() const;
};
} // namespace Monolith
