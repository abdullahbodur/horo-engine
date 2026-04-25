#include "math/Mat3.h"

#include <cstring>
#include <sstream>

#include "math/MathUtils.h"

namespace Monolith {
Mat3::Mat3() {
  for (auto &col : m)
    col.fill(0.0f);
}

Mat3::Mat3(float diagonal) {
  for (auto &col : m)
    col.fill(0.0f);
  m[0][0] = diagonal;
  m[1][1] = diagonal;
  m[2][2] = diagonal;
}

Mat3 Mat3::Identity() { return Mat3(1.0f); }
Mat3 Mat3::Zero() { return Mat3(0.0f); }

Mat3 Mat3::FromColumns(const Vec3 &c0, const Vec3 &c1, const Vec3 &c2) {
  Mat3 r;
  r.m[0][0] = c0.x;
  r.m[0][1] = c0.y;
  r.m[0][2] = c0.z;
  r.m[1][0] = c1.x;
  r.m[1][1] = c1.y;
  r.m[1][2] = c1.z;
  r.m[2][0] = c2.x;
  r.m[2][1] = c2.y;
  r.m[2][2] = c2.z;
  return r;
}

Vec3 Mat3::GetColumn(int col) const {
  return {m[col][0], m[col][1], m[col][2]};
}

Vec3 Mat3::GetRow(int row) const { return {m[0][row], m[1][row], m[2][row]}; }

void Mat3::SetColumn(int col, const Vec3 &v) {
  m[col][0] = v.x;
  m[col][1] = v.y;
  m[col][2] = v.z;
}

Mat3 Mat3::Transposed() const {
  Mat3 t;
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++)
      t.m[r][c] = m[c][r];
  return t;
}

float Mat3::Determinant() const {
  return m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) -
         m[1][0] * (m[0][1] * m[2][2] - m[2][1] * m[0][2]) +
         m[2][0] * (m[0][1] * m[1][2] - m[1][1] * m[0][2]);
}

Mat3 Mat3::Inverse() const {
  float det = Determinant();
  if (NearlyZero(det))
    return Mat3::Identity();

  float invDet = 1.0f / det;
  Mat3 inv;
  inv.m[0][0] = (m[1][1] * m[2][2] - m[2][1] * m[1][2]) * invDet;
  inv.m[1][0] = -(m[1][0] * m[2][2] - m[2][0] * m[1][2]) * invDet;
  inv.m[2][0] = (m[1][0] * m[2][1] - m[2][0] * m[1][1]) * invDet;
  inv.m[0][1] = -(m[0][1] * m[2][2] - m[2][1] * m[0][2]) * invDet;
  inv.m[1][1] = (m[0][0] * m[2][2] - m[2][0] * m[0][2]) * invDet;
  inv.m[2][1] = -(m[0][0] * m[2][1] - m[2][0] * m[0][1]) * invDet;
  inv.m[0][2] = (m[0][1] * m[1][2] - m[1][1] * m[0][2]) * invDet;
  inv.m[1][2] = -(m[0][0] * m[1][2] - m[1][0] * m[0][2]) * invDet;
  inv.m[2][2] = (m[0][0] * m[1][1] - m[1][0] * m[0][1]) * invDet;
  return inv;
}

bool Mat3::operator==(const Mat3 &o) const {
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      if (!NearlyEqual(m[i][j], o.m[i][j]))
        return false;
  return true;
}

std::string Mat3::ToString() const {
  std::ostringstream ss;
  ss << "Mat3[\n";
  for (int r = 0; r < 3; r++) {
    ss << "  [" << m[0][r] << ", " << m[1][r] << ", " << m[2][r] << "]\n";
  }
  ss << "]";
  return ss.str();
}
} // namespace Monolith
