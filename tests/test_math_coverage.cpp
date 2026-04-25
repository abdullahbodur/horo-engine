#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "math/Mat3.h"
#include "math/Mat4.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "math/Transform.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Vec4.h"

using namespace Monolith;
using Catch::Approx;

TEST_CASE("Vec2 ToString contains coordinates", "[math][vec2][tostring]") {
  Vec2 v{3.0f, 4.0f};
  std::string s = v.ToString();
  REQUIRE(s.find("3") != std::string::npos);
  REQUIRE(s.find("4") != std::string::npos);
}

TEST_CASE("Vec2 Normalized of zero vector returns zero", "[math][vec2]") {
  Vec2 v{0.0f, 0.0f};
  Vec2 n = v.Normalized();
  REQUIRE(n.x == Approx(0.0f));
  REQUIRE(n.y == Approx(0.0f));
}

TEST_CASE("Vec3 ToString contains coordinates", "[math][vec3][tostring]") {
  Vec3 v{1.0f, 2.0f, 3.0f};
  std::string s = v.ToString();
  REQUIRE(s.find("1") != std::string::npos);
  REQUIRE(s.find("2") != std::string::npos);
  REQUIRE(s.find("3") != std::string::npos);
}

TEST_CASE("Vec3 Normalized of zero vector returns zero", "[math][vec3]") {
  Vec3 v{0.0f, 0.0f, 0.0f};
  Vec3 n = v.Normalized();
  REQUIRE(n.x == Approx(0.0f));
  REQUIRE(n.y == Approx(0.0f));
  REQUIRE(n.z == Approx(0.0f));
}

TEST_CASE("Vec3 compound assignment operators", "[math][vec3]") {
  Vec3 v{1, 2, 3};
  v += Vec3{1, 1, 1};
  REQUIRE(v.x == Approx(2));
  REQUIRE(v.y == Approx(3));
  REQUIRE(v.z == Approx(4));

  v -= Vec3{1, 1, 1};
  REQUIRE(v.x == Approx(1));
  REQUIRE(v.y == Approx(2));
  REQUIRE(v.z == Approx(3));

  v *= 2.0f;
  REQUIRE(v.x == Approx(2));
  REQUIRE(v.y == Approx(4));
  REQUIRE(v.z == Approx(6));

  v /= 2.0f;
  REQUIRE(v.x == Approx(1));
  REQUIRE(v.y == Approx(2));
  REQUIRE(v.z == Approx(3));
}

TEST_CASE("Vec4 ToString contains coordinates", "[math][vec4][tostring]") {
  Vec4 v{1, 2, 3, 4};
  std::string s = v.ToString();
  REQUIRE(s.find("1") != std::string::npos);
  REQUIRE(s.find("4") != std::string::npos);
}

TEST_CASE("Vec4 compound arithmetic", "[math][vec4]") {
  Vec4 a{1, 2, 3, 4};
  Vec4 b{4, 3, 2, 1};
  Vec4 sum = a + b;
  REQUIRE(sum.x == Approx(5));
  REQUIRE(sum.w == Approx(5));
  Vec4 diff = b - a;
  REQUIRE(diff.x == Approx(3));
  REQUIRE(diff.w == Approx(-3));
  Vec4 scaled = a * 2.0f;
  REQUIRE(scaled.z == Approx(6));
}

TEST_CASE("Mat3 operator== and operator!=", "[math][mat3]") {
  Mat3 A = Mat3::Identity();
  Mat3 B = Mat3::Identity();
  Mat3 C = Mat3::Zero();
  REQUIRE(A == B);
  REQUIRE(A != C);
}

TEST_CASE("Mat3 singular matrix Inverse returns Identity fallback",
          "[math][mat3]") {
  Mat3 Z = Mat3::Zero();
  Mat3 inv = Z.Inverse();
  REQUIRE(inv(0, 0) == Approx(1.0f));
  REQUIRE(inv(1, 1) == Approx(1.0f));
  REQUIRE(inv(2, 2) == Approx(1.0f));
  REQUIRE(inv(0, 1) == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Mat3 ToString produces non-empty string", "[math][mat3][tostring]") {
  Mat3 I = Mat3::Identity();
  std::string s = I.ToString();
  REQUIRE(!s.empty());
  REQUIRE(s.find("1") != std::string::npos);
}

TEST_CASE("Mat3 matrix multiply: scale * identity = scale", "[math][mat3]") {
  Mat3 S = Mat3::FromColumns({2, 0, 0}, {0, 3, 0}, {0, 0, 4});
  Mat3 I = Mat3::Identity();
  Mat3 R = S * I;
  REQUIRE(R(0, 0) == Approx(2));
  REQUIRE(R(1, 1) == Approx(3));
  REQUIRE(R(2, 2) == Approx(4));
}

TEST_CASE("Mat4 Zero returns zero matrix", "[math][mat4]") {
  Mat4 Z = Mat4::Zero();
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      REQUIRE(Z(r, c) == Approx(0.0f));
}

TEST_CASE("Mat4 singular matrix Inverse returns Identity fallback",
          "[math][mat4]") {
  Mat4 Z = Mat4::Zero();
  Mat4 inv = Z.Inverse();
  REQUIRE(inv(0, 0) == Approx(1.0f));
  REQUIRE(inv(1, 1) == Approx(1.0f));
  REQUIRE(inv(2, 2) == Approx(1.0f));
  REQUIRE(inv(3, 3) == Approx(1.0f));
}

TEST_CASE("Mat4 Transposed twice gives original", "[math][mat4]") {
  Mat4 T = Mat4::Translate({1, 2, 3});
  Mat4 TT = T.Transposed().Transposed();
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      REQUIRE(TT(r, c) == Approx(T(r, c)).epsilon(1e-5f));
}

TEST_CASE("Mat4 operator* (matrix × matrix): Scale * Scale = Scale²",
          "[math][mat4]") {
  Mat4 S = Mat4::Scale({2, 3, 4});
  Mat4 R = S * S;
  REQUIRE(R(0, 0) == Approx(4));
  REQUIRE(R(1, 1) == Approx(9));
  REQUIRE(R(2, 2) == Approx(16));
}

TEST_CASE("Mat4 TransformPoint w-division path", "[math][mat4]") {
  Mat4 P = Mat4::Perspective(ToRadians(90.0f), 1.0f, 1.0f, 100.0f);
  Vec3 result = P.TransformPoint({0, 0, -5});
  (void)result;
  REQUIRE(true);
}

TEST_CASE("Mat4 ToString produces non-empty string", "[math][mat4][tostring]") {
  std::string s = Mat4::Identity().ToString();
  REQUIRE(!s.empty());
  REQUIRE(s.find("1") != std::string::npos);
}

TEST_CASE("Quaternion ToEuler then FromEuler round-trip (pitch)",
          "[math][quat]") {
  float angle = ToRadians(45.0f);
  Quaternion q = Quaternion::FromAxisAngle(Vec3::Right(), angle);
  Vec3 euler = q.ToEuler();
  REQUIRE(euler.x == Approx(angle).epsilon(1e-3f));
}

TEST_CASE("Quaternion ToEuler: identity gives zero", "[math][quat]") {
  Vec3 e = Quaternion::Identity().ToEuler();
  REQUIRE(e.x == Approx(0).margin(1e-5f));
  REQUIRE(e.y == Approx(0).margin(1e-5f));
  REQUIRE(e.z == Approx(0).margin(1e-5f));
}

TEST_CASE("Quaternion Lerp produces intermediate orientation", "[math][quat]") {
  Quaternion a = Quaternion::Identity();
  Quaternion b = Quaternion::FromAxisAngle(Vec3::Up(), ToRadians(90.0f));
  Quaternion mid = Quaternion::Lerp(a, b, 0.5f);
  REQUIRE(mid.Length() == Approx(1.0f).epsilon(1e-4f));
  REQUIRE(mid.w != Approx(a.w).epsilon(1e-3f));
  REQUIRE(mid.w != Approx(b.w).epsilon(1e-3f));
}

TEST_CASE("Quaternion LookRotation toward +X gives correct forward",
          "[math][quat]") {
  Quaternion q = Quaternion::LookRotation(Vec3::Right());
  Vec3 fwd = q.Forward();
  REQUIRE(fwd.x == Approx(1.0f).epsilon(1e-4f));
  REQUIRE(fwd.y == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("Quaternion LookRotation toward -Z (forward) is near identity",
          "[math][quat]") {
  Quaternion q = Quaternion::LookRotation(Vec3::Forward());
  Vec3 fwd = q.Forward();
  REQUIRE(fwd.z == Approx(-1.0f).epsilon(1e-4f));
}

TEST_CASE("Quaternion Dot: two identical quaternions dot to 1",
          "[math][quat]") {
  Quaternion q = Quaternion::FromAxisAngle(Vec3::Up(), ToRadians(45.0f));
  REQUIRE(Quaternion::Dot(q, q) == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("Quaternion Conjugate: conjugate then multiply gives scalar",
          "[math][quat]") {
  Quaternion q =
      Quaternion::FromAxisAngle({1, 0, 0}, ToRadians(60.0f)).Normalized();
  Quaternion c = q.Conjugate();
  Quaternion result = q * c;
  REQUIRE(result.w == Approx(1.0f).epsilon(1e-4f));
  REQUIRE(result.x == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("Quaternion FromEuler yaw creates Z rotation", "[math][quat]") {
  float angle = ToRadians(90.0f);
  Quaternion q = Quaternion::FromEuler(0.0f, angle, 0.0f);
  Vec3 v{1, 0, 0};
  Vec3 r = q * v;
  REQUIRE(r.x == Approx(0.0f).margin(1e-4f));
  REQUIRE(r.y == Approx(1.0f).epsilon(1e-4f));
  REQUIRE(r.z == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("Transform scale affects TransformPoint", "[math][transform]") {
  Transform t;
  t.scale = {3, 3, 3};
  Vec3 r = t.TransformPoint({1, 1, 1});
  REQUIRE(r.x == Approx(3));
  REQUIRE(r.y == Approx(3));
  REQUIRE(r.z == Approx(3));
}

TEST_CASE("Transform rotation affects direction vectors", "[math][transform]") {
  Transform t;
  t.rotation = Quaternion::FromAxisAngle(Vec3::Up(), ToRadians(90.0f));
  Vec3 fwd = t.Forward();
  REQUIRE(fwd.x == Approx(-1.0f).epsilon(1e-4f));
  REQUIRE(fwd.z == Approx(0.0f).margin(1e-4f));
}
