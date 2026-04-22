#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numeric>

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


TEST_CASE("Vec2 default constructor is zero", "[math][vec2]") {
    Vec2 v;
    REQUIRE(v.x == Approx(0.0f));
    REQUIRE(v.y == Approx(0.0f));
}

TEST_CASE("Vec2 arithmetic operators", "[math][vec2]") {
    Vec2 a{3, 4};
    Vec2 b{1, 2};

    Vec2 sum = a + b;
    REQUIRE(sum.x == Approx(4));
    REQUIRE(sum.y == Approx(6));

    Vec2 diff = a - b;
    REQUIRE(diff.x == Approx(2));
    REQUIRE(diff.y == Approx(2));

    Vec2 scaled = a * 2.0f;
    REQUIRE(scaled.x == Approx(6));
    REQUIRE(scaled.y == Approx(8));

    Vec2 divided = a / 2.0f;
    REQUIRE(divided.x == Approx(1.5f));
    REQUIRE(divided.y == Approx(2.0f));

    Vec2 neg = -a;
    REQUIRE(neg.x == Approx(-3));
    REQUIRE(neg.y == Approx(-4));
}

TEST_CASE("Vec2 compound assignment operators", "[math][vec2]") {
    Vec2 v{1, 2};
    v += Vec2{3, 4};
    REQUIRE(v.x == Approx(4));
    REQUIRE(v.y == Approx(6));

    v -= Vec2{1, 1};
    REQUIRE(v.x == Approx(3));
    REQUIRE(v.y == Approx(5));

    v *= 2.0f;
    REQUIRE(v.x == Approx(6));
    REQUIRE(v.y == Approx(10));

    v /= 2.0f;
    REQUIRE(v.x == Approx(3));
    REQUIRE(v.y == Approx(5));
}

TEST_CASE("Vec2 scalar multiply from left", "[math][vec2]") {
    Vec2 v{2, 3};
    Vec2 r = 4.0f * v;
    REQUIRE(r.x == Approx(8));
    REQUIRE(r.y == Approx(12));
}

TEST_CASE("Vec2 equality operators", "[math][vec2]") {
    Vec2 a{1, 2};
    Vec2 b{1, 2};
    Vec2 c{1, 3};
    REQUIRE(a == b);
    REQUIRE(a != c);
}

TEST_CASE("Vec2 subscript operator", "[math][vec2]") {
    Vec2 v{5, 7};
    REQUIRE(v[0] == Approx(5));
    REQUIRE(v[1] == Approx(7));

    v[0] = 10.0f;
    REQUIRE(v.x == Approx(10));
}

TEST_CASE("Vec2 length and normalize", "[math][vec2]") {
    Vec2 v{3, 4};
    REQUIRE(v.LengthSq() == Approx(25.0f));
    REQUIRE(v.Length() == Approx(5.0f));

    Vec2 n = v.Normalized();
    REQUIRE(n.Length() == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(n.x == Approx(0.6f));
    REQUIRE(n.y == Approx(0.8f));
}

TEST_CASE("Vec2 dot product", "[math][vec2]") {
    Vec2 a{1, 0};
    Vec2 b{0, 1};
    REQUIRE(Vec2::Dot(a, b) == Approx(0));
    REQUIRE(Vec2::Dot(a, a) == Approx(1));

    Vec2 c{3, 4};
    Vec2 d{1, 2};
    REQUIRE(Vec2::Dot(c, d) == Approx(11));
}

TEST_CASE("Vec2 lerp", "[math][vec2]") {
    Vec2 a{0, 0};
    Vec2 b{10, 20};
    Vec2 m = Vec2::Lerp(a, b, 0.5f);
    REQUIRE(m.x == Approx(5));
    REQUIRE(m.y == Approx(10));
}

TEST_CASE("Vec2 static factories", "[math][vec2]") {
    REQUIRE(Vec2::Zero().x == Approx(0));
    REQUIRE(Vec2::Zero().y == Approx(0));
    REQUIRE(Vec2::One().x == Approx(1));
    REQUIRE(Vec2::One().y == Approx(1));
    REQUIRE(Vec2::UnitX().x == Approx(1));
    REQUIRE(Vec2::UnitX().y == Approx(0));
    REQUIRE(Vec2::UnitY().x == Approx(0));
    REQUIRE(Vec2::UnitY().y == Approx(1));
}


TEST_CASE("Vec3 component-wise multiply", "[math][vec3]") {
    Vec3 a{2, 3, 4};
    Vec3 b{5, 6, 7};
    Vec3 r = a * b;
    REQUIRE(r.x == Approx(10));
    REQUIRE(r.y == Approx(18));
    REQUIRE(r.z == Approx(28));
}

TEST_CASE("Vec3 scalar multiply from left", "[math][vec3]") {
    Vec3 v{1, 2, 3};
    Vec3 r = 3.0f * v;
    REQUIRE(r.x == Approx(3));
    REQUIRE(r.y == Approx(6));
    REQUIRE(r.z == Approx(9));
}

TEST_CASE("Vec3 subscript operator", "[math][vec3]") {
    Vec3 v{4, 5, 6};
    REQUIRE(v[0] == Approx(4));
    REQUIRE(v[1] == Approx(5));
    REQUIRE(v[2] == Approx(6));

    v[1] = 99.0f;
    REQUIRE(v.y == Approx(99));
}

TEST_CASE("Vec3 equality", "[math][vec3]") {
    Vec3 a{1, 2, 3};
    Vec3 b{1, 2, 3};
    Vec3 c{1, 2, 4};
    REQUIRE(a == b);
    REQUIRE(a != c);
}

TEST_CASE("Vec3 distance", "[math][vec3]") {
    Vec3 a{0, 0, 0};
    Vec3 b{3, 4, 0};
    REQUIRE(Vec3::Distance(a, b) == Approx(5.0f));
    REQUIRE(Vec3::Distance(b, a) == Approx(5.0f));
}

TEST_CASE("Vec3 reflect", "[math][vec3]") {
    const Vec3 incident{1, -1, 0};
    const Vec3 reflected = Vec3::Reflect(incident, Vec3::Up());
    const Vec3 expected{1, 1, 0};
    REQUIRE(reflected == expected);
}

TEST_CASE("Vec3 reflect: perpendicular to normal is unchanged",
          "[math][vec3]") {
    Vec3 incident{1, 0, 0};
    Vec3 normal{0, 1, 0};
    Vec3 r = Vec3::Reflect(incident, normal);
    REQUIRE(r.x == Approx(1));
    REQUIRE(r.y == Approx(0));
    REQUIRE(r.z == Approx(0));
}

TEST_CASE("Vec3 directional constants", "[math][vec3]") {
    REQUIRE(Vec3::Up() == Vec3(0, 1, 0));
    REQUIRE(Vec3::Down() == Vec3(0, -1, 0));
    REQUIRE(Vec3::Right() == Vec3(1, 0, 0));
    REQUIRE(Vec3::Left() == Vec3(-1, 0, 0));
    REQUIRE(Vec3::Forward() == Vec3(0, 0, -1));
    REQUIRE(Vec3::Back() == Vec3(0, 0, 1));
}


TEST_CASE("Vec4 default constructor is zero", "[math][vec4]") {
    Vec4 v;
    REQUIRE(v.x == Approx(0));
    REQUIRE(v.y == Approx(0));
    REQUIRE(v.z == Approx(0));
    REQUIRE(v.w == Approx(0));
}

TEST_CASE("Vec4 construct from Vec3", "[math][vec4]") {
    Vec3 xyz{1, 2, 3};
    Vec4 v{xyz, 0.5f};
    REQUIRE(v.x == Approx(1));
    REQUIRE(v.y == Approx(2));
    REQUIRE(v.z == Approx(3));
    REQUIRE(v.w == Approx(0.5f));
}

TEST_CASE("Vec4 XYZ extraction", "[math][vec4]") {
    Vec4 v{7, 8, 9, 1};
    Vec3 xyz = v.XYZ();
    REQUIRE(xyz.x == Approx(7));
    REQUIRE(xyz.y == Approx(8));
    REQUIRE(xyz.z == Approx(9));
}

TEST_CASE("Vec4 arithmetic operators", "[math][vec4]") {
    Vec4 a{1, 2, 3, 4};
    Vec4 b{5, 6, 7, 8};

    Vec4 sum = a + b;
    REQUIRE(sum.x == Approx(6));
    REQUIRE(sum.w == Approx(12));

    Vec4 diff = b - a;
    REQUIRE(diff.x == Approx(4));
    REQUIRE(diff.w == Approx(4));

    Vec4 scaled = a * 2.0f;
    REQUIRE(scaled.z == Approx(6));

    Vec4 neg = -a;
    REQUIRE(neg.x == Approx(-1));
    REQUIRE(neg.w == Approx(-4));
}

TEST_CASE("Vec4 length", "[math][vec4]") {
    Vec4 v{1, 0, 0, 0};
    REQUIRE(v.Length() == Approx(1.0f));
    REQUIRE(v.LengthSq() == Approx(1.0f));

    Vec4 v2{1, 1, 1, 1};
    REQUIRE(v2.LengthSq() == Approx(4.0f));
    REQUIRE(v2.Length() == Approx(2.0f));
}

TEST_CASE("Vec4 dot product", "[math][vec4]") {
    Vec4 a{1, 0, 0, 0};
    Vec4 b{0, 1, 0, 0};
    REQUIRE(Vec4::Dot(a, b) == Approx(0));

    Vec4 c{1, 2, 3, 4};
    Vec4 d{5, 6, 7, 8};
    REQUIRE(Vec4::Dot(c, d) == Approx(70));
}

TEST_CASE("Vec4 subscript operator", "[math][vec4]") {
    Vec4 v{10, 20, 30, 40};
    REQUIRE(v[0] == Approx(10));
    REQUIRE(v[3] == Approx(40));
}

TEST_CASE("Vec4 equality", "[math][vec4]") {
    Vec4 a{1, 2, 3, 4};
    Vec4 b{1, 2, 3, 4};
    Vec4 c{1, 2, 3, 5};
    REQUIRE(a == b);
    REQUIRE(a != c);
}


TEST_CASE("Mat3 zero constructor", "[math][mat3]") {
    Mat3 Z = Mat3::Zero();
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            REQUIRE(Z(r, c) == Approx(0));
}

TEST_CASE("Mat3 FromColumns stores columns correctly", "[math][mat3]") {
    Vec3 c0{1, 2, 3};
    Vec3 c1{4, 5, 6};
    Vec3 c2{7, 8, 9};
    Mat3 M = Mat3::FromColumns(c0, c1, c2);

    Vec3 gc0 = M.GetColumn(0);
    REQUIRE(gc0.x == Approx(1));
    REQUIRE(gc0.y == Approx(2));
    REQUIRE(gc0.z == Approx(3));

    Vec3 gc1 = M.GetColumn(1);
    REQUIRE(gc1.x == Approx(4));
    REQUIRE(gc1.y == Approx(5));
    REQUIRE(gc1.z == Approx(6));
}

TEST_CASE("Mat3 GetRow", "[math][mat3]") {
    Mat3 M = Mat3::FromColumns({1, 2, 3}, {4, 5, 6}, {7, 8, 9});
    Vec3 row0 = M.GetRow(0);
    REQUIRE(row0.x == Approx(1));
    REQUIRE(row0.y == Approx(4));
    REQUIRE(row0.z == Approx(7));

    Vec3 row1 = M.GetRow(1);
    REQUIRE(row1.x == Approx(2));
    REQUIRE(row1.y == Approx(5));
    REQUIRE(row1.z == Approx(8));
}

TEST_CASE("Mat3 SetColumn", "[math][mat3]") {
    Mat3 M = Mat3::Identity();
    M.SetColumn(2, {10, 20, 30});
    Vec3 c2 = M.GetColumn(2);
    REQUIRE(c2.x == Approx(10));
    REQUIRE(c2.y == Approx(20));
    REQUIRE(c2.z == Approx(30));
}

TEST_CASE("Mat3 transpose", "[math][mat3]") {
    Mat3 M = Mat3::FromColumns({1, 2, 3}, {4, 5, 6}, {7, 8, 9});
    Mat3 T = M.Transposed();
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            REQUIRE(T(r, c) == Approx(M(c, r)));
}

TEST_CASE("Mat3 transpose of transpose is identity", "[math][mat3]") {
    Mat3 M = Mat3::FromColumns({1, 4, 7}, {2, 5, 8}, {3, 6, 9});
    Mat3 TT = M.Transposed().Transposed();
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            REQUIRE(TT(r, c) == Approx(M(r, c)));
}

TEST_CASE("Mat3 determinant of diagonal matrix", "[math][mat3]") {
    Mat3 M = Mat3::FromColumns({1, 0, 0}, {0, 2, 0}, {0, 0, 3});
    REQUIRE(M.Determinant() == Approx(6.0f));
}

TEST_CASE("Mat3 determinant of identity is 1", "[math][mat3]") {
    REQUIRE(Mat3::Identity().Determinant() == Approx(1.0f));
}

TEST_CASE("Mat3 general inverse: M * Minv = I", "[math][mat3]") {
    Mat3 M = Mat3::FromColumns({1, 0, 0}, {0, 2, 0}, {0, 0, 4});
    Mat3 Minv = M.Inverse();
    Mat3 I = M * Minv;
    REQUIRE(I(0, 0) == Approx(1).epsilon(1e-5f));
    REQUIRE(I(1, 1) == Approx(1).epsilon(1e-5f));
    REQUIRE(I(2, 2) == Approx(1).epsilon(1e-5f));
    REQUIRE(I(0, 1) == Approx(0).margin(1e-5f));
    REQUIRE(I(1, 0) == Approx(0).margin(1e-5f));
}

TEST_CASE("Mat3 matrix-vector multiply", "[math][mat3]") {
    Mat3 M = Mat3::FromColumns({2, 0, 0}, {0, 3, 0}, {0, 0, 4});
    Vec3 v{1, 1, 1};
    Vec3 r = M * v;
    REQUIRE(r.x == Approx(2));
    REQUIRE(r.y == Approx(3));
    REQUIRE(r.z == Approx(4));
}

TEST_CASE("Mat3 matrix-matrix multiply: I*I = I", "[math][mat3]") {
    Mat3 I = Mat3::Identity();
    Mat3 R = I * I;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            REQUIRE(R(r, c) == Approx(I(r, c)));
}

TEST_CASE("Mat3 scalar multiply", "[math][mat3]") {
    Mat3 I = Mat3::Identity();
    Mat3 R = I * 3.0f;
    REQUIRE(R(0, 0) == Approx(3));
    REQUIRE(R(1, 1) == Approx(3));
    REQUIRE(R(2, 2) == Approx(3));
    REQUIRE(R(0, 1) == Approx(0));
}

TEST_CASE("Mat3 add and subtract", "[math][mat3]") {
    Mat3 A = Mat3::Identity();
    Mat3 B = Mat3::Identity();
    Mat3 Sum = A + B;
    REQUIRE(Sum(0, 0) == Approx(2));
    REQUIRE(Sum(1, 1) == Approx(2));

    Mat3 Diff = A - B;
    REQUIRE(Diff(0, 0) == Approx(0));
}


TEST_CASE("Mat4 RotateX 90 degrees transforms Y to Z", "[math][mat4]") {
    Mat4 R = Mat4::RotateX(ToRadians(90.0f));
    Vec4 y{0, 1, 0, 0};
    Vec4 r = R * y;
    REQUIRE(r.x == Approx(0).margin(1e-5f));
    REQUIRE(r.y == Approx(0).margin(1e-5f));
    REQUIRE(r.z == Approx(1).epsilon(1e-5f));
}

TEST_CASE("Mat4 RotateY 90 degrees transforms X to -Z", "[math][mat4]") {
    Mat4 R = Mat4::RotateY(ToRadians(90.0f));
    Vec4 x{1, 0, 0, 0};
    Vec4 r = R * x;
    REQUIRE(r.x == Approx(0).margin(1e-5f));
    REQUIRE(r.y == Approx(0).margin(1e-5f));
    REQUIRE(r.z == Approx(-1).epsilon(1e-5f));
}

TEST_CASE("Mat4 RotateZ 90 degrees transforms X to Y", "[math][mat4]") {
    Mat4 R = Mat4::RotateZ(ToRadians(90.0f));
    Vec4 x{1, 0, 0, 0};
    Vec4 r = R * x;
    REQUIRE(r.x == Approx(0).margin(1e-5f));
    REQUIRE(r.y == Approx(1).epsilon(1e-5f));
    REQUIRE(r.z == Approx(0).margin(1e-5f));
}

TEST_CASE("Mat4 Rotate from Quaternion matches RotateY", "[math][mat4]") {
    float angle = ToRadians(45.0f);
    Quaternion q = Quaternion::FromAxisAngle({0, 1, 0}, angle);
    Mat4 Mq = Mat4::Rotate(q);
    Mat4 My = Mat4::RotateY(angle);

    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            REQUIRE(Mq(r, c) == Approx(My(r, c)).epsilon(1e-5f));
}

TEST_CASE("Mat4 TransformPoint applies translation", "[math][mat4]") {
    Mat4 T = Mat4::Translate({5, -3, 2});
    Vec3 p = T.TransformPoint({0, 0, 0});
    REQUIRE(p.x == Approx(5));
    REQUIRE(p.y == Approx(-3));
    REQUIRE(p.z == Approx(2));
}

TEST_CASE("Mat4 TransformVector ignores translation", "[math][mat4]") {
    Mat4 T = Mat4::Translate({100, 200, 300});
    Vec3 v = T.TransformVector({1, 0, 0});
    REQUIRE(v.x == Approx(1));
    REQUIRE(v.y == Approx(0));
    REQUIRE(v.z == Approx(0));
}

TEST_CASE("Mat4 GetTranslation", "[math][mat4]") {
    Mat4 T = Mat4::Translate({3, -1, 7});
    Vec3 t = T.GetTranslation();
    REQUIRE(t.x == Approx(3));
    REQUIRE(t.y == Approx(-1));
    REQUIRE(t.z == Approx(7));
}

TEST_CASE("Mat4 GetColumn of identity", "[math][mat4]") {
    Mat4 I = Mat4::Identity();
    Vec4 c0 = I.GetColumn(0);
    REQUIRE(c0.x == Approx(1));
    REQUIRE(c0.y == Approx(0));
    REQUIRE(c0.z == Approx(0));
    REQUIRE(c0.w == Approx(0));

    Vec4 c3 = I.GetColumn(3);
    REQUIRE(c3.x == Approx(0));
    REQUIRE(c3.w == Approx(1));
}

TEST_CASE("Mat4 Data returns column-major layout", "[math][mat4]") {
    Mat4 T = Mat4::Translate({1, 2, 3});
    const float *d = T.Data();
    REQUIRE(d != nullptr);
    REQUIRE(d[12] == Approx(1));
    REQUIRE(d[13] == Approx(2));
    REQUIRE(d[14] == Approx(3));
}

TEST_CASE("Mat4 LookAt: eye maps to view-space origin", "[math][mat4]") {
    Vec3 eye{0, 0, 5};
    Vec3 center{0, 0, 0};
    Vec3 up = Vec3::Up();
    Mat4 view = Mat4::LookAt(eye, center, up);
    Vec3 eyeInView = view.TransformPoint(eye);
    REQUIRE(eyeInView.x == Approx(0).margin(1e-4f));
    REQUIRE(eyeInView.y == Approx(0).margin(1e-4f));
    REQUIRE(eyeInView.z == Approx(0).margin(1e-4f));
}

TEST_CASE("Mat4 LookAt: forward vector points toward center", "[math][mat4]") {
    Vec3 eye{5, 0, 0};
    Vec3 center{0, 0, 0};
    Mat4 view = Mat4::LookAt(eye, center, Vec3::Up());
    Vec3 centerInView = view.TransformPoint(center);
    REQUIRE(centerInView.z < 0.0f);
}

TEST_CASE("Mat4 Perspective produces non-singular matrix", "[math][mat4]") {
    Mat4 P = Mat4::Perspective(ToRadians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
    REQUIRE(P.Determinant() != Approx(0).margin(1e-6f));
}

TEST_CASE("Mat4 Orthographic produces non-singular matrix", "[math][mat4]") {
    Mat4 O = Mat4::Orthographic(-10, 10, -10, 10, 0.1f, 100.0f);
    REQUIRE(O.Determinant() != Approx(0).margin(1e-6f));
}

TEST_CASE("Mat4 Orthographic maps center to zero", "[math][mat4]") {
    const float l = -10.0f;
    const float r = 10.0f;
    const float b = -10.0f;
    const float t = 10.0f;
    const float n = 0.0f;
    const float f = 100.0f;
    Mat4 O = Mat4::Orthographic(l, r, b, t, n, f);
    const float cx = std::midpoint(l, r);
    const float cy = std::midpoint(b, t);
    Vec4 center{cx, cy, -n, 1};
    Vec4 result = O * center;
    REQUIRE(result.x == Approx(0).margin(1e-5f));
    REQUIRE(result.y == Approx(0).margin(1e-5f));
}


TEST_CASE("Quaternion conjugate negates xyz", "[math][quat]") {
    Quaternion q{1, 2, 3, 4};
    Quaternion c = q.Conjugate();
    REQUIRE(c.x == Approx(-1));
    REQUIRE(c.y == Approx(-2));
    REQUIRE(c.z == Approx(-3));
    REQUIRE(c.w == Approx(4));
}

TEST_CASE("Quaternion inverse: q * q.Inverse() = identity", "[math][quat]") {
    Quaternion q = Quaternion::FromAxisAngle({1, 1, 0}, ToRadians(60.0f));
    q = q.Normalized();
    Quaternion inv = q.Inverse();
    Quaternion result = q * inv;
    REQUIRE(result.x == Approx(0).margin(1e-5f));
    REQUIRE(result.y == Approx(0).margin(1e-5f));
    REQUIRE(result.z == Approx(0).margin(1e-5f));
    REQUIRE(result.w == Approx(1).epsilon(1e-5f));
}

TEST_CASE("Quaternion dot product", "[math][quat]") {
    Quaternion id = Quaternion::Identity();
    REQUIRE(Quaternion::Dot(id, id) == Approx(1.0f));

    Quaternion q = Quaternion::FromAxisAngle({0, 1, 0}, ToRadians(90.0f));
    float dot = Quaternion::Dot(q, id);
    REQUIRE(dot == Approx(std::cos(ToRadians(45.0f))).epsilon(1e-4f));
}

TEST_CASE("Quaternion lerp at endpoints", "[math][quat]") {
    Quaternion a = Quaternion::Identity();
    Quaternion b = Quaternion::FromAxisAngle({0, 0, 1}, ToRadians(90.0f));

    Quaternion l0 = Quaternion::Lerp(a, b, 0.0f);
    REQUIRE(l0.w == Approx(a.w).epsilon(1e-4f));
    REQUIRE(l0.x == Approx(a.x).margin(1e-4f));

    Quaternion l1 = Quaternion::Lerp(a, b, 1.0f);
    REQUIRE(l1.w == Approx(b.w).epsilon(1e-4f));
    REQUIRE(l1.z == Approx(b.z).epsilon(1e-4f));
}

TEST_CASE("Quaternion ToMat3 from identity is identity matrix",
          "[math][quat]") {
    Quaternion id = Quaternion::Identity();
    Mat3 M = id.ToMat3();
    REQUIRE(M(0, 0) == Approx(1).epsilon(1e-5f));
    REQUIRE(M(1, 1) == Approx(1).epsilon(1e-5f));
    REQUIRE(M(2, 2) == Approx(1).epsilon(1e-5f));
    REQUIRE(M(0, 1) == Approx(0).margin(1e-5f));
    REQUIRE(M(1, 0) == Approx(0).margin(1e-5f));
}

TEST_CASE("Quaternion ToMat3 produces same rotation as operator*",
          "[math][quat]") {
    Quaternion q = Quaternion::FromAxisAngle({0, 1, 0}, ToRadians(45.0f));
    Vec3 v{1, 0, 0};
    Vec3 byOp = q * v;
    Vec3 byMat = q.ToMat3() * v;
    REQUIRE(byOp.x == Approx(byMat.x).epsilon(1e-5f));
    REQUIRE(byOp.y == Approx(byMat.y).margin(1e-5f));
    REQUIRE(byOp.z == Approx(byMat.z).epsilon(1e-5f));
}

TEST_CASE("Quaternion direction vectors from identity", "[math][quat]") {
    Quaternion id = Quaternion::Identity();
    Vec3 fwd = id.Forward();
    Vec3 up = id.Up();
    Vec3 rt = id.Right();

    REQUIRE(fwd.x == Approx(Vec3::Forward().x).margin(1e-5f));
    REQUIRE(fwd.z == Approx(Vec3::Forward().z).epsilon(1e-5f));
    REQUIRE(up.y == Approx(1).epsilon(1e-5f));
    REQUIRE(rt.x == Approx(1).epsilon(1e-5f));
}

TEST_CASE("Quaternion FromEuler pitch=90deg matches AxisAngle Y rotation",
          "[math][quat]") {
    float angle = ToRadians(90.0f);
    Quaternion qEuler = Quaternion::FromEuler(angle, 0.0f, 0.0f);
    Quaternion qAxis = Quaternion::FromAxisAngle(Vec3::Up(), angle);

    Vec3 v{1, 0, 0};
    Vec3 r1 = qEuler * v;
    Vec3 r2 = qAxis * v;
    REQUIRE(r1.x == Approx(r2.x).epsilon(1e-4f));
    REQUIRE(r1.y == Approx(r2.y).margin(1e-4f));
    REQUIRE(r1.z == Approx(r2.z).epsilon(1e-4f));
}

TEST_CASE("Quaternion FromEuler roll=90deg matches AxisAngle X rotation",
          "[math][quat]") {
    float angle = ToRadians(90.0f);
    Quaternion qEuler = Quaternion::FromEuler(0.0f, 0.0f, angle);
    Quaternion qAxis = Quaternion::FromAxisAngle(Vec3::Right(), angle);

    Vec3 v{0, 1, 0};
    Vec3 r1 = qEuler * v;
    Vec3 r2 = qAxis * v;
    REQUIRE(r1.x == Approx(r2.x).margin(1e-4f));
    REQUIRE(r1.y == Approx(r2.y).epsilon(1e-4f));
    REQUIRE(r1.z == Approx(r2.z).epsilon(1e-4f));
}

TEST_CASE("Quaternion ToEuler: identity gives zero pitch and roll",
          "[math][quat]") {
    Vec3 euler = Quaternion::Identity().ToEuler();
    REQUIRE(euler.x == Approx(0).margin(1e-5f));
    REQUIRE(euler.z == Approx(0).margin(1e-5f));
}

TEST_CASE("Quaternion LookRotation toward forward is near-identity",
          "[math][quat]") {
    Quaternion q = Quaternion::LookRotation(Vec3::Forward());
    Vec3 fwd = q.Forward();
    REQUIRE(fwd.x == Approx(Vec3::Forward().x).margin(1e-4f));
    REQUIRE(fwd.z == Approx(Vec3::Forward().z).epsilon(1e-4f));
}


TEST_CASE("Transform identity: ToMatrix is identity", "[math][transform]") {
    Transform t = Transform::Identity();
    Mat4 M = t.ToMatrix();
    Vec3 origin = M.TransformPoint({0, 0, 0});
    REQUIRE(origin.x == Approx(0).margin(1e-5f));
    REQUIRE(origin.y == Approx(0).margin(1e-5f));
    REQUIRE(origin.z == Approx(0).margin(1e-5f));
}

TEST_CASE("Transform ToMatrix carries translation", "[math][transform]") {
    Transform t;
    t.position = {5, -2, 3};
    Mat4 M = t.ToMatrix();
    Vec3 translated = M.TransformPoint({0, 0, 0});
    REQUIRE(translated.x == Approx(5));
    REQUIRE(translated.y == Approx(-2));
    REQUIRE(translated.z == Approx(3));
}

TEST_CASE("Transform ToMatrix applies scale", "[math][transform]") {
    Transform t;
    t.scale = {2, 3, 4};
    Mat4 M = t.ToMatrix();
    Vec3 r = M.TransformPoint({1, 1, 1});
    REQUIRE(r.x == Approx(2));
    REQUIRE(r.y == Approx(3));
    REQUIRE(r.z == Approx(4));
}

TEST_CASE("Transform TransformPoint: translation + identity rotation",
          "[math][transform]") {
    Transform t;
    t.position = {10, 0, 0};
    Vec3 result = t.TransformPoint({1, 0, 0});
    REQUIRE(result.x == Approx(11));
    REQUIRE(result.y == Approx(0));
    REQUIRE(result.z == Approx(0));
}

TEST_CASE("Transform direction vectors are correct for identity",
          "[math][transform]") {
    Transform t = Transform::Identity();
    REQUIRE(t.Forward().z == Approx(Vec3::Forward().z).epsilon(1e-5f));
    REQUIRE(t.Up().y == Approx(1).epsilon(1e-5f));
    REQUIRE(t.Right().x == Approx(1).epsilon(1e-5f));
}


TEST_CASE("MathUtils Clamp01", "[math][utils]") {
    REQUIRE(Clamp01(0.5f) == Approx(0.5f));
    REQUIRE(Clamp01(-1.0f) == Approx(0.0f));
    REQUIRE(Clamp01(2.0f) == Approx(1.0f));
}

TEST_CASE("MathUtils Min and Max", "[math][utils]") {
    REQUIRE(Min(3.0f, 5.0f) == Approx(3.0f));
    REQUIRE(Max(3.0f, 5.0f) == Approx(5.0f));
    REQUIRE(Min(-1.0f, 1.0f) == Approx(-1.0f));
}

TEST_CASE("MathUtils Abs", "[math][utils]") {
    REQUIRE(Abs(-5.5f) == Approx(5.5f));
    REQUIRE(Abs(3.2f) == Approx(3.2f));
    REQUIRE(Abs(0.0f) == Approx(0.0f));
}

TEST_CASE("MathUtils Sqrt", "[math][utils]") {
    REQUIRE(Sqrt(4.0f) == Approx(2.0f));
    REQUIRE(Sqrt(9.0f) == Approx(3.0f));
    REQUIRE(Sqrt(0.0f) == Approx(0.0f));
}

TEST_CASE("MathUtils NearlyEqual", "[math][utils]") {
    REQUIRE(NearlyEqual(1.0f, 1.0f));
    REQUIRE(NearlyEqual(1.0f, 1.0f + 5e-7f));
    REQUIRE_FALSE(NearlyEqual(1.0f, 2.0f));
}

TEST_CASE("MathUtils NearlyZero", "[math][utils]") {
    REQUIRE(NearlyZero(0.0f));
    REQUIRE(NearlyZero(5e-7f));
    REQUIRE_FALSE(NearlyZero(0.1f));
}

TEST_CASE("MathUtils trig functions", "[math][utils]") {
    REQUIRE(Sin(0.0f) == Approx(0.0f));
    REQUIRE(Sin(HALF_PI) == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(Cos(0.0f) == Approx(1.0f));
    REQUIRE(Cos(PI) == Approx(-1.0f).epsilon(1e-5f));
    REQUIRE(Tan(0.0f) == Approx(0.0f));
}

TEST_CASE("MathUtils Floor, Ceil, Round", "[math][utils]") {
    REQUIRE(Floor(2.9f) == Approx(2.0f));
    REQUIRE(Ceil(2.1f) == Approx(3.0f));
    REQUIRE(Round(2.5f) == Approx(3.0f));
    REQUIRE(Round(2.4f) == Approx(2.0f));
}

TEST_CASE("MathUtils Pow", "[math][utils]") {
    REQUIRE(Pow(2.0f, 10.0f) == Approx(1024.0f));
    REQUIRE(Pow(3.0f, 2.0f) == Approx(9.0f));
}

TEST_CASE("MathUtils Acos is inverse of Cos", "[math][utils]") {
    float angle = ToRadians(60.0f);
    REQUIRE(Acos(Cos(angle)) == Approx(angle).epsilon(1e-5f));
}

TEST_CASE("MathUtils Atan2", "[math][utils]") {
    REQUIRE(Atan2(0.0f, 1.0f) == Approx(0.0f));
    REQUIRE(Atan2(1.0f, 0.0f) == Approx(HALF_PI).epsilon(1e-5f));
    REQUIRE(Atan2(0.0f, -1.0f) == Approx(PI).epsilon(1e-5f));
}
