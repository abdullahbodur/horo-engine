#pragma once
#include <algorithm>
#include <cmath>

namespace Monolith {

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;
constexpr float HALF_PI = 0.5f * PI;
constexpr float DEG2RAD = PI / 180.0f;
constexpr float RAD2DEG = 180.0f / PI;
constexpr float EPSILON = 1e-6f;

inline float ToRadians(float degrees) {
  return degrees * DEG2RAD;
}
inline float ToDegrees(float radians) {
  return radians * RAD2DEG;
}

inline float Clamp(float v, float lo, float hi) {
  return std::clamp(v, lo, hi);
}
inline float Clamp01(float v) {
  return Clamp(v, 0.0f, 1.0f);
}
inline float Lerp(float a, float b, float t) {
  return a + t * (b - a);
}
inline float Abs(float v) {
  return std::fabs(v);
}
inline float Sqrt(float v) {
  return std::sqrt(v);
}
inline float Sin(float v) {
  return std::sin(v);
}
inline float Cos(float v) {
  return std::cos(v);
}
inline float Tan(float v) {
  return std::tan(v);
}
inline float Acos(float v) {
  return std::acos(Clamp(v, -1.0f, 1.0f));
}
inline float Atan2(float y, float x) {
  return std::atan2(y, x);
}
inline float Pow(float base, float exp) {
  return std::pow(base, exp);
}
inline float Floor(float v) {
  return std::floor(v);
}
inline float Ceil(float v) {
  return std::ceil(v);
}
inline float Round(float v) {
  return std::round(v);
}
inline float Min(float a, float b) {
  return std::min(a, b);
}
inline float Max(float a, float b) {
  return std::max(a, b);
}

inline bool NearlyEqual(float a, float b, float eps = EPSILON) {
  return Abs(a - b) <= eps;
}
inline bool NearlyZero(float v, float eps = EPSILON) {
  return Abs(v) <= eps;
}

}  // namespace Monolith
