#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "math/Mat3.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "math/Vec3.h"

using namespace Monolith;
using Catch::Approx;

// ===========================================================================
// Quaternion — LookRotation (hits multiple branches in the Mat3→Quat code)
// ===========================================================================

TEST_CASE("Quaternion::LookRotation: looking along -Z", "[quaternion]") {
    Quaternion q =
            Quaternion::LookRotation({0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
    REQUIRE(q.Length() == Approx(1.0f).epsilon(1e-5f));
    // Looking along -Z with default orientation should give near-identity
    Vec3 fwd = q * Vec3::Forward();
    REQUIRE(fwd.z == Approx(-1.0f).epsilon(1e-4f));
}

TEST_CASE("Quaternion::LookRotation: looking along +X", "[quaternion]") {
    Quaternion q =
            Quaternion::LookRotation({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    REQUIRE(q.Length() == Approx(1.0f).epsilon(1e-5f));
    // Result should be a valid rotation
    Vec3 fwd = q * Vec3::Forward(); // should now point along +X
    REQUIRE(fwd.x == Approx(1.0f).epsilon(1e-4f));
}

TEST_CASE("Quaternion::LookRotation: looking along +Y (up)", "[quaternion]") {
    Quaternion q =
            Quaternion::LookRotation({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
    REQUIRE(q.Length() == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("Quaternion::LookRotation: looking along +Z", "[quaternion]") {
    Quaternion q =
            Quaternion::LookRotation({0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
    REQUIRE(q.Length() == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("Quaternion::LookRotation: diagonal direction", "[quaternion]") {
    Vec3 dir = Vec3{1.0f, 0.0f, -1.0f}.Normalized();
    Quaternion q = Quaternion::LookRotation(dir, {0.0f, 1.0f, 0.0f});
    REQUIRE(q.Length() == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("Quaternion::LookRotation: rotates forward to match direction",
          "[quaternion]") {
    auto dir = Vec3{1.0f, 0.0f, 0.0f};
    Quaternion q = Quaternion::LookRotation(dir, {0.0f, 1.0f, 0.0f});
    Vec3 result = q * Vec3::Forward();
    // q*forward should give dir
    REQUIRE(result.x == Approx(dir.x).epsilon(1e-4f));
    REQUIRE(result.y == Approx(dir.y).margin(1e-4f));
    REQUIRE(result.z == Approx(dir.z).margin(1e-4f));
}

// ===========================================================================
// Quaternion — Slerp edge cases
// ===========================================================================

TEST_CASE("Quaternion::Slerp: t=0 returns a", "[quaternion]") {
    Quaternion a = Quaternion::Identity();
    Quaternion b = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI);

    Quaternion r = Quaternion::Slerp(a, b, 0.0f);
    REQUIRE(r.w == Approx(a.w).epsilon(1e-5f));
    REQUIRE(r.x == Approx(a.x).margin(1e-5f));
}

TEST_CASE("Quaternion::Slerp: t=1 returns b", "[quaternion]") {
    Quaternion a = Quaternion::Identity();
    Quaternion b = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI * 0.5f);

    Quaternion r = Quaternion::Slerp(a, b, 1.0f);
    REQUIRE(r.w == Approx(b.w).epsilon(1e-4f));
    REQUIRE(r.y == Approx(b.y).epsilon(1e-4f));
}

TEST_CASE("Quaternion::Slerp: t=0.5 gives midpoint rotation", "[quaternion]") {
    Quaternion a = Quaternion::Identity();
    Quaternion b = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI * 0.5f);

    Quaternion r = Quaternion::Slerp(a, b, 0.5f);
    REQUIRE(r.Length() == Approx(1.0f).epsilon(1e-5f));
    // Should be 45° rotation
    Vec3 v = r * Vec3::Forward();
    REQUIRE(v.y == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("Quaternion::Slerp: result is normalized", "[quaternion]") {
    Quaternion a = Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, 0.3f);
    Quaternion b = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, 0.7f);

    for (int step = 0; step <= 10; ++step) {
        const float t = static_cast<float>(step) / 10.0f;
        Quaternion r = Quaternion::Slerp(a, b, t);
        REQUIRE(r.Length() == Approx(1.0f).epsilon(1e-5f));
    }
}

TEST_CASE(
    "Quaternion::Slerp: handles nearly identical quaternions (Lerp fallback)",
    "[quaternion]") {
    Quaternion a = Quaternion::Identity();
    Quaternion b = Quaternion::Identity();

    Quaternion r = Quaternion::Slerp(a, b, 0.5f);
    REQUIRE(r.Length() == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(r.w == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("Quaternion::Slerp: shortest path (handles antipodal quaternions)",
          "[quaternion]") {
    Quaternion a = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, 0.1f);
    Quaternion b = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, -PI + 0.1f);

    // Both valid quaternions, should slerp along shortest path
    Quaternion r = Quaternion::Slerp(a, b, 0.5f);
    REQUIRE(r.Length() == Approx(1.0f).epsilon(1e-5f));
}

// ===========================================================================
// Quaternion — Lerp
// ===========================================================================

TEST_CASE("Quaternion::Lerp: result is normalized", "[quaternion]") {
    Quaternion a = Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, 0.5f);
    Quaternion b = Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, 1.0f);

    Quaternion r = Quaternion::Lerp(a, b, 0.5f);
    REQUIRE(r.Length() == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("Quaternion::Lerp: t=0 returns a (normalized)", "[quaternion]") {
    Quaternion a = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, 0.5f);
    Quaternion b = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI);

    Quaternion r = Quaternion::Lerp(a, b, 0.0f);
    REQUIRE(r.Length() == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(r.w == Approx(a.w).epsilon(1e-4f));
}

// ===========================================================================
// Quaternion — ToEuler
// ===========================================================================

TEST_CASE("Quaternion::ToEuler: identity gives zero angles", "[quaternion]") {
    Quaternion q = Quaternion::Identity();
    Vec3 e = q.ToEuler();
    REQUIRE(e.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(e.z == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Quaternion::ToEuler: 90 degree rotation around X gives pitch=~PI/2",
          "[quaternion]") {
    Quaternion q = Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, PI * 0.5f);
    Vec3 e = q.ToEuler();
    REQUIRE(e.x == Approx(PI * 0.5f).epsilon(1e-3f));
}

TEST_CASE("Quaternion::ToEuler: 90 degree rotation around Z gives roll=~PI/2",
          "[quaternion]") {
    Quaternion q = Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, PI * 0.5f);
    Vec3 e = q.ToEuler();
    REQUIRE(e.z == Approx(PI * 0.5f).epsilon(1e-3f));
}

TEST_CASE("Quaternion::ToEuler: 90 degree rotation around Y stays finite",
          "[quaternion]") {
    Quaternion q = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI * 0.5f);
    Vec3 e = q.ToEuler();
    REQUIRE(std::isfinite(e.x));
    REQUIRE(std::isfinite(e.y));
    REQUIRE(std::isfinite(e.z));
}

TEST_CASE("Quaternion::ToEuler: -90 degree rotation around Y stays finite",
          "[quaternion]") {
    Quaternion q = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, -PI * 0.5f);
    Vec3 e = q.ToEuler();
    REQUIRE(std::isfinite(e.x));
    REQUIRE(std::isfinite(e.y));
    REQUIRE(std::isfinite(e.z));
}

// ===========================================================================
// Quaternion — ToMat3
// ===========================================================================

TEST_CASE("Quaternion::ToMat3: identity gives identity matrix",
          "[quaternion]") {
    Quaternion q = Quaternion::Identity();
    Mat3 m = q.ToMat3();
    REQUIRE(m.m[0][0] == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(m.m[1][1] == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(m.m[2][2] == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(m.m[0][1] == Approx(0.0f).margin(1e-5f));
    REQUIRE(m.m[1][0] == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Quaternion::ToMat3: 180 degree Y rotation flips X", "[quaternion]") {
    Quaternion q = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI);
    Mat3 m = q.ToMat3();
    Vec3 x = m.GetColumn(0);
    // After 180° around Y, X axis should become approximately -X
    REQUIRE(x.x == Approx(-1.0f).epsilon(1e-4f));
}

// ===========================================================================
// Quaternion — Conjugate and Inverse
// ===========================================================================

TEST_CASE("Quaternion::Conjugate negates xyz", "[quaternion]") {
    Quaternion q{0.1f, 0.2f, 0.3f, 0.9f};
    Quaternion c = q.Conjugate();
    REQUIRE(c.x == Approx(-0.1f));
    REQUIRE(c.y == Approx(-0.2f));
    REQUIRE(c.z == Approx(-0.3f));
    REQUIRE(c.w == Approx(0.9f));
}

TEST_CASE("Quaternion::Inverse of unit quaternion equals conjugate",
          "[quaternion]") {
    Quaternion q = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, 0.5f);
    Quaternion inv = q.Inverse();
    Quaternion conj = q.Conjugate();

    // For unit quaternion, inverse == conjugate
    REQUIRE(inv.x == Approx(conj.x).epsilon(1e-5f));
    REQUIRE(inv.y == Approx(conj.y).epsilon(1e-5f));
    REQUIRE(inv.z == Approx(conj.z).epsilon(1e-5f));
    REQUIRE(inv.w == Approx(conj.w).epsilon(1e-5f));
}

TEST_CASE("Quaternion: q * q.Inverse == Identity", "[quaternion]") {
    Quaternion q =
            Quaternion::FromAxisAngle({1.0f, 1.0f, 0.0f}, 1.3f).Normalized();
    Quaternion inv = q.Inverse();
    Quaternion result = q * inv;
    REQUIRE(result.w == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(result.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(result.y == Approx(0.0f).margin(1e-5f));
    REQUIRE(result.z == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Quaternion::Inverse of zero quaternion returns identity",
          "[quaternion]") {
    Quaternion q{0.0f, 0.0f, 0.0f, 0.0f};
    Quaternion inv = q.Inverse();
    REQUIRE(inv.w == Approx(1.0f));
}

// ===========================================================================
// Quaternion — Dot
// ===========================================================================

TEST_CASE("Quaternion::Dot: identity with itself is 1", "[quaternion]") {
    Quaternion a = Quaternion::Identity();
    REQUIRE(Quaternion::Dot(a, a) == Approx(1.0f));
}

TEST_CASE("Quaternion::Dot: opposite quaternions have dot = -1",
          "[quaternion]") {
    Quaternion a = Quaternion::Identity();
    Quaternion b{0.0f, 0.0f, 0.0f, -1.0f};
    REQUIRE(Quaternion::Dot(a, b) == Approx(-1.0f));
}

// ===========================================================================
// Quaternion — composition
// ===========================================================================

TEST_CASE("Quaternion: composing with inverse gives identity", "[quaternion]") {
    Quaternion q = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI * 0.5f);
    Quaternion r = q * q.Inverse();
    Vec3 v{1.0f, 2.0f, 3.0f};
    Vec3 rotated = r * v;
    REQUIRE(rotated.x == Approx(v.x).epsilon(1e-4f));
    REQUIRE(rotated.y == Approx(v.y).epsilon(1e-4f));
    REQUIRE(rotated.z == Approx(v.z).epsilon(1e-4f));
}

TEST_CASE("Quaternion: chained 90-degree rotations around Y give 180 degrees",
          "[quaternion]") {
    Quaternion q90 = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI * 0.5f);
    Quaternion q180 = q90 * q90;

    Vec3 fwd = q180 * Vec3::Forward();
    // Forward (-Z) rotated 180° around Y → +Z
    REQUIRE(fwd.z == Approx(1.0f).epsilon(1e-4f));
    REQUIRE(std::abs(fwd.x) < 1e-4f);
}

TEST_CASE("Quaternion: Normalized of non-unit returns unit", "[quaternion]") {
    Quaternion q{2.0f, 0.0f, 0.0f, 0.0f};
    Quaternion n = q.Normalized();
    REQUIRE(n.Length() == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("Quaternion: Normalized of zero returns identity", "[quaternion]") {
    Quaternion q{0.0f, 0.0f, 0.0f, 0.0f};
    Quaternion n = q.Normalized();
    REQUIRE(n.w == Approx(1.0f));
}

TEST_CASE("Quaternion::ToString returns non-empty string", "[quaternion]") {
    Quaternion q = Quaternion::Identity();
    REQUIRE_FALSE(q.ToString().empty());
}

// ===========================================================================
// Quaternion — FromEuler
// ===========================================================================

TEST_CASE("Quaternion::FromEuler: zero angles gives identity", "[quaternion]") {
    Quaternion q = Quaternion::FromEuler(0.0f, 0.0f, 0.0f);
    REQUIRE(q.w == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(q.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(q.y == Approx(0.0f).margin(1e-5f));
    REQUIRE(q.z == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Quaternion::FromEuler: result is normalized", "[quaternion]") {
    Quaternion q = Quaternion::FromEuler(0.3f, 0.5f, 0.7f);
    REQUIRE(q.Length() == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("Quaternion::FromAxisAngle: zero angle gives identity-like result",
          "[quaternion]") {
    Quaternion q = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, 0.0f);
    REQUIRE(q.w == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(std::abs(q.y) < 1e-5f);
}
