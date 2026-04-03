#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "math/Vec3.h"
#include "math/Vec4.h"
#include "math/Mat3.h"
#include "math/Mat4.h"
#include "math/Quaternion.h"
#include "math/MathUtils.h"

using namespace Horo;
using Catch::Approx;

// ---- Vec3 ----

TEST_CASE("Vec3 arithmetic", "[math][vec3]")
{
    Vec3 a{1, 2, 3};
    Vec3 b{4, 5, 6};

    REQUIRE((a + b).x == Approx(5));
    REQUIRE((a - b).y == Approx(-3));
    REQUIRE((a * 2.0f).z == Approx(6));
    REQUIRE((-a).x == Approx(-1));
}

TEST_CASE("Vec3 length and normalize", "[math][vec3]")
{
    Vec3 v{3, 4, 0};
    REQUIRE(v.Length() == Approx(5.0f));
    Vec3 n = v.Normalized();
    REQUIRE(n.x == Approx(0.6f));
    REQUIRE(n.y == Approx(0.8f));
    REQUIRE(n.LengthSq() == Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("Vec3 dot product", "[math][vec3]")
{
    Vec3 a{1, 0, 0};
    Vec3 b{0, 1, 0};
    REQUIRE(Vec3::Dot(a, b) == Approx(0));
    REQUIRE(Vec3::Dot(a, a) == Approx(1));

    Vec3 c{1, 2, 3};
    Vec3 d{4, 5, 6};
    REQUIRE(Vec3::Dot(c, d) == Approx(32));
}

TEST_CASE("Vec3 cross product", "[math][vec3]")
{
    Vec3 x = Vec3::Right(); // (1,0,0)
    Vec3 y = Vec3::Up();    // (0,1,0)
    Vec3 z = Vec3::Cross(x, y);
    // Right × Up = +Z = (0,0,1) in a right-hand coord system
    REQUIRE(z.x == Approx(0));
    REQUIRE(z.y == Approx(0));
    REQUIRE(z.z == Approx(1));

    // Anti-commutativity: Up × Right = -Z = (0,0,-1)
    Vec3 zz = Vec3::Cross(y, x);
    REQUIRE(zz.z == Approx(-1));
}

TEST_CASE("Vec3 lerp", "[math][vec3]")
{
    Vec3 a{0,0,0};
    Vec3 b{10,20,30};
    Vec3 m = Vec3::Lerp(a, b, 0.5f);
    REQUIRE(m.x == Approx(5));
    REQUIRE(m.y == Approx(10));
    REQUIRE(m.z == Approx(15));
}

// ---- Mat4 ----

TEST_CASE("Mat4 identity multiply", "[math][mat4]")
{
    Mat4 I = Mat4::Identity();
    Vec4 v{1, 2, 3, 1};
    Vec4 r = I * v;
    REQUIRE(r.x == Approx(1));
    REQUIRE(r.y == Approx(2));
    REQUIRE(r.z == Approx(3));
    REQUIRE(r.w == Approx(1));
}

TEST_CASE("Mat4 translate", "[math][mat4]")
{
    Mat4 T = Mat4::Translate({5, -3, 2});
    Vec4 p{0, 0, 0, 1};
    Vec4 r = T * p;
    REQUIRE(r.x == Approx(5));
    REQUIRE(r.y == Approx(-3));
    REQUIRE(r.z == Approx(2));
}

TEST_CASE("Mat4 scale", "[math][mat4]")
{
    Mat4 S = Mat4::Scale({2, 3, 4});
    Vec4 p{1, 1, 1, 1};
    Vec4 r = S * p;
    REQUIRE(r.x == Approx(2));
    REQUIRE(r.y == Approx(3));
    REQUIRE(r.z == Approx(4));
}

TEST_CASE("Mat4 inverse of identity", "[math][mat4]")
{
    Mat4 I = Mat4::Identity();
    Mat4 inv = I.Inverse();
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            REQUIRE(inv(r, c) == Approx(I(r, c)).epsilon(1e-5));
}

TEST_CASE("Mat4 inverse general", "[math][mat4]")
{
    Mat4 M = Mat4::Translate({3, -1, 2}) * Mat4::Scale({2, 2, 2});
    Mat4 Minv = M.Inverse();
    Mat4 I = M * Minv;

    // Product should be identity
    REQUIRE(I(0,0) == Approx(1).epsilon(1e-4));
    REQUIRE(I(1,1) == Approx(1).epsilon(1e-4));
    REQUIRE(I(2,2) == Approx(1).epsilon(1e-4));
    REQUIRE(I(3,3) == Approx(1).epsilon(1e-4));
    REQUIRE(I(0,1) == Approx(0).margin(1e-4));
    REQUIRE(I(1,0) == Approx(0).margin(1e-4));
}

TEST_CASE("Mat4 transpose", "[math][mat4]")
{
    Mat4 M = Mat4::Translate({1, 2, 3});
    Mat4 T = M.Transposed();
    REQUIRE(T(0, 3) == Approx(M(3, 0)));
    REQUIRE(T(1, 3) == Approx(M(3, 1)));
    REQUIRE(T(2, 3) == Approx(M(3, 2)));
}

// ---- Mat3 ----

TEST_CASE("Mat3 identity", "[math][mat3]")
{
    Mat3 I = Mat3::Identity();
    Vec3 v{2, 3, 4};
    Vec3 r = I * v;
    REQUIRE(r.x == Approx(v.x));
    REQUIRE(r.y == Approx(v.y));
    REQUIRE(r.z == Approx(v.z));
}

TEST_CASE("Mat3 inverse", "[math][mat3]")
{
    Mat3 I = Mat3::Identity();
    Mat3 Iinv = I.Inverse();
    // Identity inverse is identity
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            REQUIRE(Iinv(r, c) == Approx(I(r, c)).epsilon(1e-5));
}

// ---- Quaternion ----

TEST_CASE("Quaternion identity", "[math][quat]")
{
    Quaternion q = Quaternion::Identity();
    Vec3 v{1, 2, 3};
    Vec3 r = q * v;
    REQUIRE(r.x == Approx(v.x));
    REQUIRE(r.y == Approx(v.y));
    REQUIRE(r.z == Approx(v.z));
}

TEST_CASE("Quaternion 90 deg rotation around Y", "[math][quat]")
{
    Quaternion q = Quaternion::FromAxisAngle({0,1,0}, ToRadians(90.0f));
    Vec3 forward{0, 0, -1};
    Vec3 r = q * forward;
    // 90° CCW around +Y (from above): -Z → -X  (forward turns to left)
    // Y-rotation matrix: x'= x*cos + z*sin, z'= -x*sin + z*cos
    // For (0,0,-1): x' = -1*sin(90) = -1, z' = -1*cos(90) = 0
    REQUIRE(r.x == Approx(-1).epsilon(1e-4));
    REQUIRE(r.y == Approx(0).margin(1e-4));
    REQUIRE(r.z == Approx(0).margin(1e-4));
}

TEST_CASE("Quaternion normalize", "[math][quat]")
{
    Quaternion q{0.1f, 0.2f, 0.3f, 0.4f};
    Quaternion n = q.Normalized();
    float len = n.Length();
    REQUIRE(len == Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("Quaternion slerp at t=0 and t=1", "[math][quat]")
{
    Quaternion a = Quaternion::Identity();
    Quaternion b = Quaternion::FromAxisAngle({0,1,0}, ToRadians(90.0f));

    Quaternion s0 = Quaternion::Slerp(a, b, 0.0f);
    Quaternion s1 = Quaternion::Slerp(a, b, 1.0f);

    REQUIRE(s0.w == Approx(a.w).epsilon(1e-4));
    REQUIRE(s1.x == Approx(b.x).epsilon(1e-4));
    REQUIRE(s1.y == Approx(b.y).epsilon(1e-4));
    REQUIRE(s1.w == Approx(b.w).epsilon(1e-4));
}

TEST_CASE("Quaternion composition", "[math][quat]")
{
    // Two 90-degree Y rotations should equal one 180-degree Y rotation
    Quaternion q90 = Quaternion::FromAxisAngle({0,1,0}, ToRadians(90.0f));
    Quaternion q180 = q90 * q90;
    Vec3 v{1, 0, 0};
    Vec3 r = q180 * v;
    // 180 around Y: (1,0,0) -> (-1,0,0)
    REQUIRE(r.x == Approx(-1).epsilon(1e-4));
    REQUIRE(r.y == Approx(0).margin(1e-4));
    REQUIRE(r.z == Approx(0).margin(1e-4));
}

// ---- MathUtils ----

TEST_CASE("MathUtils clamp and lerp", "[math][utils]")
{
    REQUIRE(Clamp(5.0f, 0.0f, 3.0f) == Approx(3.0f));
    REQUIRE(Clamp(-1.0f, 0.0f, 1.0f) == Approx(0.0f));
    REQUIRE(Lerp(0.0f, 10.0f, 0.3f) == Approx(3.0f));
}

TEST_CASE("MathUtils radians and degrees", "[math][utils]")
{
    REQUIRE(ToRadians(180.0f) == Approx(PI));
    REQUIRE(ToDegrees(PI) == Approx(180.0f));
}
