#include "math/Mat4.h"

#include <cstring>
#include <sstream>

#include "math/MathUtils.h"
#include "math/Quaternion.h"

namespace Monolith {

Mat4::Mat4() {
  std::memset(m, 0, sizeof(m));
}

Mat4::Mat4(float diag) {
  std::memset(m, 0, sizeof(m));
  m[0][0] = diag;
  m[1][1] = diag;
  m[2][2] = diag;
  m[3][3] = diag;
}

Mat4 Mat4::Identity() {
  return Mat4(1.0f);
}
Mat4 Mat4::Zero() {
  return Mat4(0.0f);
}

Mat4 Mat4::Translate(const Vec3& t) {
  Mat4 r = Identity();
  r.m[3][0] = t.x;
  r.m[3][1] = t.y;
  r.m[3][2] = t.z;
  return r;
}

Mat4 Mat4::Scale(const Vec3& s) {
  Mat4 r = Identity();
  r.m[0][0] = s.x;
  r.m[1][1] = s.y;
  r.m[2][2] = s.z;
  return r;
}

Mat4 Mat4::RotateX(float rad) {
  Mat4 r = Identity();
  r.m[1][1] = Cos(rad);
  r.m[2][1] = -Sin(rad);
  r.m[1][2] = Sin(rad);
  r.m[2][2] = Cos(rad);
  return r;
}

Mat4 Mat4::RotateY(float rad) {
  Mat4 r = Identity();
  r.m[0][0] = Cos(rad);
  r.m[2][0] = Sin(rad);
  r.m[0][2] = -Sin(rad);
  r.m[2][2] = Cos(rad);
  return r;
}

Mat4 Mat4::RotateZ(float rad) {
  Mat4 r = Identity();
  r.m[0][0] = Cos(rad);
  r.m[1][0] = -Sin(rad);
  r.m[0][1] = Sin(rad);
  r.m[1][1] = Cos(rad);
  return r;
}

Mat4 Mat4::Rotate(const Quaternion& q) {
  // Convert quaternion to 4x4 rotation matrix
  float x = q.x, y = q.y, z = q.z, w = q.w;
  float x2 = x + x, y2 = y + y, z2 = z + z;
  float xx = x * x2, xy = x * y2, xz = x * z2;
  float yy = y * y2, yz = y * z2, zz = z * z2;
  float wx = w * x2, wy = w * y2, wz = w * z2;

  Mat4 r = Identity();
  r.m[0][0] = 1.0f - (yy + zz);
  r.m[0][1] = xy + wz;
  r.m[0][2] = xz - wy;

  r.m[1][0] = xy - wz;
  r.m[1][1] = 1.0f - (xx + zz);
  r.m[1][2] = yz + wx;

  r.m[2][0] = xz + wy;
  r.m[2][1] = yz - wx;
  r.m[2][2] = 1.0f - (xx + yy);
  return r;
}

Mat4 Mat4::Perspective(float fovY, float aspect, float zNear, float zFar) {
  float tanHalf = Tan(fovY * 0.5f);
  Mat4 r;
  r.m[0][0] = 1.0f / (aspect * tanHalf);
  r.m[1][1] = 1.0f / tanHalf;
  r.m[2][2] = -(zFar + zNear) / (zFar - zNear);
  r.m[2][3] = -1.0f;
  r.m[3][2] = -(2.0f * zFar * zNear) / (zFar - zNear);
  return r;
}

Mat4 Mat4::Orthographic(float left, float right, float bottom, float top, float zNear, float zFar) {
  Mat4 r = Identity();
  r.m[0][0] = 2.0f / (right - left);
  r.m[1][1] = 2.0f / (top - bottom);
  r.m[2][2] = -2.0f / (zFar - zNear);
  r.m[3][0] = -(right + left) / (right - left);
  r.m[3][1] = -(top + bottom) / (top - bottom);
  r.m[3][2] = -(zFar + zNear) / (zFar - zNear);
  return r;
}

Mat4 Mat4::LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
  Vec3 f = (center - eye).Normalized();
  Vec3 r = Vec3::Cross(f, up).Normalized();
  Vec3 u = Vec3::Cross(r, f);

  Mat4 res = Identity();
  res.m[0][0] = r.x;
  res.m[1][0] = r.y;
  res.m[2][0] = r.z;
  res.m[0][1] = u.x;
  res.m[1][1] = u.y;
  res.m[2][1] = u.z;
  res.m[0][2] = -f.x;
  res.m[1][2] = -f.y;
  res.m[2][2] = -f.z;
  res.m[3][0] = -Vec3::Dot(r, eye);
  res.m[3][1] = -Vec3::Dot(u, eye);
  res.m[3][2] = Vec3::Dot(f, eye);
  return res;
}

Mat4 Mat4::operator*(const Mat4& o) const {
  Mat4 result;
  for (int col = 0; col < 4; col++)
    for (int row = 0; row < 4; row++) {
      float sum = 0;
      for (int k = 0; k < 4; k++)
        sum += m[k][row] * o.m[col][k];
      result.m[col][row] = sum;
    }
  return result;
}

Vec4 Mat4::operator*(const Vec4& v) const {
  return {m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0] * v.w,
          m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1] * v.w,
          m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2] * v.w,
          m[0][3] * v.x + m[1][3] * v.y + m[2][3] * v.z + m[3][3] * v.w};
}

Vec3 Mat4::TransformPoint(const Vec3& p) const {
  Vec4 v = *this * Vec4(p, 1.0f);
  if (!NearlyZero(v.w))
    return {v.x / v.w, v.y / v.w, v.z / v.w};
  return {v.x, v.y, v.z};
}

Vec3 Mat4::TransformVector(const Vec3& v) const {
  Vec4 r = *this * Vec4(v, 0.0f);
  return {r.x, r.y, r.z};
}

Mat4 Mat4::Transposed() const {
  Mat4 t;
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      t.m[r][c] = m[c][r];
  return t;
}

float Mat4::Determinant() const {
  // Laplace expansion along first row
  float a = m[0][0], b = m[1][0], c = m[2][0], d = m[3][0];

  auto minor = [&](int c0, int c1, int c2, int r0, int r1, int r2) -> float {
    return m[c0][r0] * (m[c1][r1] * m[c2][r2] - m[c2][r1] * m[c1][r2]) -
           m[c1][r0] * (m[c0][r1] * m[c2][r2] - m[c2][r1] * m[c0][r2]) +
           m[c2][r0] * (m[c0][r1] * m[c1][r2] - m[c1][r1] * m[c0][r2]);
  };

  return a * minor(1, 2, 3, 1, 2, 3) - b * minor(0, 2, 3, 1, 2, 3) + c * minor(0, 1, 3, 1, 2, 3) -
         d * minor(0, 1, 2, 1, 2, 3);
}

Mat4 Mat4::Inverse() const {
  // Cofactor / adjugate method
  float inv[16];
  const float* src = &m[0][0];

  inv[0] = src[5] * src[10] * src[15] - src[5] * src[11] * src[14] - src[9] * src[6] * src[15] +
           src[9] * src[7] * src[14] + src[13] * src[6] * src[11] - src[13] * src[7] * src[10];
  inv[4] = -src[4] * src[10] * src[15] + src[4] * src[11] * src[14] + src[8] * src[6] * src[15] -
           src[8] * src[7] * src[14] - src[12] * src[6] * src[11] + src[12] * src[7] * src[10];
  inv[8] = src[4] * src[9] * src[15] - src[4] * src[11] * src[13] - src[8] * src[5] * src[15] +
           src[8] * src[7] * src[13] + src[12] * src[5] * src[11] - src[12] * src[7] * src[9];
  inv[12] = -src[4] * src[9] * src[14] + src[4] * src[10] * src[13] + src[8] * src[5] * src[14] -
            src[8] * src[6] * src[13] - src[12] * src[5] * src[10] + src[12] * src[6] * src[9];

  inv[1] = -src[1] * src[10] * src[15] + src[1] * src[11] * src[14] + src[9] * src[2] * src[15] -
           src[9] * src[3] * src[14] - src[13] * src[2] * src[11] + src[13] * src[3] * src[10];
  inv[5] = src[0] * src[10] * src[15] - src[0] * src[11] * src[14] - src[8] * src[2] * src[15] +
           src[8] * src[3] * src[14] + src[12] * src[2] * src[11] - src[12] * src[3] * src[10];
  inv[9] = -src[0] * src[9] * src[15] + src[0] * src[11] * src[13] + src[8] * src[1] * src[15] -
           src[8] * src[3] * src[13] - src[12] * src[1] * src[11] + src[12] * src[3] * src[9];
  inv[13] = src[0] * src[9] * src[14] - src[0] * src[10] * src[13] - src[8] * src[1] * src[14] +
            src[8] * src[2] * src[13] + src[12] * src[1] * src[10] - src[12] * src[2] * src[9];

  inv[2] = src[1] * src[6] * src[15] - src[1] * src[7] * src[14] - src[5] * src[2] * src[15] +
           src[5] * src[3] * src[14] + src[13] * src[2] * src[7] - src[13] * src[3] * src[6];
  inv[6] = -src[0] * src[6] * src[15] + src[0] * src[7] * src[14] + src[4] * src[2] * src[15] -
           src[4] * src[3] * src[14] - src[12] * src[2] * src[7] + src[12] * src[3] * src[6];
  inv[10] = src[0] * src[5] * src[15] - src[0] * src[7] * src[13] - src[4] * src[1] * src[15] +
            src[4] * src[3] * src[13] + src[12] * src[1] * src[7] - src[12] * src[3] * src[5];
  inv[14] = -src[0] * src[5] * src[14] + src[0] * src[6] * src[13] + src[4] * src[1] * src[14] -
            src[4] * src[2] * src[13] - src[12] * src[1] * src[6] + src[12] * src[2] * src[5];

  inv[3] = -src[1] * src[6] * src[11] + src[1] * src[7] * src[10] + src[5] * src[2] * src[11] -
           src[5] * src[3] * src[10] - src[9] * src[2] * src[7] + src[9] * src[3] * src[6];
  inv[7] = src[0] * src[6] * src[11] - src[0] * src[7] * src[10] - src[4] * src[2] * src[11] +
           src[4] * src[3] * src[10] + src[8] * src[2] * src[7] - src[8] * src[3] * src[6];
  inv[11] = -src[0] * src[5] * src[11] + src[0] * src[7] * src[9] + src[4] * src[1] * src[11] -
            src[4] * src[3] * src[9] - src[8] * src[1] * src[7] + src[8] * src[3] * src[5];
  inv[15] = src[0] * src[5] * src[10] - src[0] * src[6] * src[9] - src[4] * src[1] * src[10] +
            src[4] * src[2] * src[9] + src[8] * src[1] * src[6] - src[8] * src[2] * src[5];

  float det = src[0] * inv[0] + src[1] * inv[4] + src[2] * inv[8] + src[3] * inv[12];
  if (NearlyZero(det))
    return Identity();

  float invDet = 1.0f / det;
  Mat4 result;
  float* dst = &result.m[0][0];
  for (int i = 0; i < 16; i++)
    dst[i] = inv[i] * invDet;
  return result;
}

Vec4 Mat4::GetColumn(int col) const {
  return {m[col][0], m[col][1], m[col][2], m[col][3]};
}

std::string Mat4::ToString() const {
  std::ostringstream ss;
  ss << "Mat4[\n";
  for (int r = 0; r < 4; r++)
    ss << "  [" << m[0][r] << ", " << m[1][r] << ", " << m[2][r] << ", " << m[3][r] << "]\n";
  ss << "]";
  return ss.str();
}

}  // namespace Monolith
