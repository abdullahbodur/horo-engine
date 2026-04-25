#pragma once
#include <string>

#include "math/Vec3.h"
#include "math/Vec4.h"

namespace Monolith {
struct Quaternion;

// Column-major 4x4 matrix — matches OpenGL convention.
// m[col][row]
struct Mat4 {
  float m[4][4];

  Mat4();

  explicit Mat4(float diagonal);

  static Mat4 Identity();

  static Mat4 Zero();

  // Standard transforms
  static Mat4 Translate(const Vec3 &t);

  static Mat4 Scale(const Vec3 &s);

  static Mat4 RotateX(float radians);

  static Mat4 RotateY(float radians);

  static Mat4 RotateZ(float radians);

  static Mat4 Rotate(const Quaternion &q);

  // Camera / projection
  static Mat4 Perspective(float fovY, float aspect, float zNear, float zFar);

  static Mat4 Orthographic(float left, float right, float bottom, float top,
                           float zNear, float zFar);

  static Mat4 LookAt(const Vec3 &eye, const Vec3 &center, const Vec3 &up);

  Mat4 operator*(const Mat4 &o) const;

  Vec4 operator*(const Vec4 &v) const;

  Vec3 TransformPoint(const Vec3 &p) const;  // applies w division
  Vec3 TransformVector(const Vec3 &v) const; // w = 0

  Mat4 Transposed() const;

  Mat4 Inverse() const;

  float Determinant() const;

  Vec4 GetColumn(int col) const;

  Vec3 GetTranslation() const { return {m[3][0], m[3][1], m[3][2]}; }

  float &operator()(int row, int col) { return m[col][row]; }
  const float &operator()(int row, int col) const { return m[col][row]; }

  // Returns pointer to float[16] column-major data suitable for
  // glUniformMatrix4fv
  const float *Data() const { return &m[0][0]; }

  std::string ToString() const;
};
} // namespace Monolith
