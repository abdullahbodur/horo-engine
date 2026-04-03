#pragma once
#include <string>

#include "math/Vec3.h"

namespace Horo {

// Column-major 3x3 matrix.
// m[col][row]
struct Mat3 {
  float m[3][3];

  Mat3();
  explicit Mat3(float diagonal);

  static Mat3 Identity();
  static Mat3 Zero();

  // Construct from columns
  static Mat3 FromColumns(const Vec3& c0, const Vec3& c1, const Vec3& c2);

  Vec3 GetColumn(int col) const;
  Vec3 GetRow(int row) const;
  void SetColumn(int col, const Vec3& v);

  Mat3 Transposed() const;
  float Determinant() const;
  Mat3 Inverse() const;

  Mat3 operator+(const Mat3& o) const;
  Mat3 operator-(const Mat3& o) const;
  Mat3 operator*(const Mat3& o) const;
  Vec3 operator*(const Vec3& v) const;
  Mat3 operator*(float s) const;

  bool operator==(const Mat3& o) const;
  bool operator!=(const Mat3& o) const { return !(*this == o); }

  float& operator()(int row, int col) { return m[col][row]; }
  const float& operator()(int row, int col) const { return m[col][row]; }

  std::string ToString() const;
};

}  // namespace Horo
