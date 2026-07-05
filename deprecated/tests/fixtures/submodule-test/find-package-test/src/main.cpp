/** @file main.cpp
 *  @brief Find-package smoke test — exercises Horo Engine public API
 *         surfaces when the engine is consumed as an installed SDK
 *         via find_package(HoroEngine).
 *
 *  This is a build-and-link validation plus runtime API exercise.
 *  It verifies that the installed SDK exposes a complete set of
 *  public headers and that the exported CMake targets resolve
 *  correctly at both compile time and link time.
 *
 *  Every API call in this file corresponds to a real, existing
 *  public symbol.  Do not add aspirational or forward-looking API
 *  calls — they will break the smoke test overnight.
 */

#include "math/Mat4.h"
#include "math/Quaternion.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Vec4.h"
#include "physics/PhysicsWorld.h"
#include "renderer/Camera.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#define CHECK(expr, label)                            \
    do {                                              \
        if (!(expr)) {                                \
            std::fprintf(stderr, "FAIL: %s\n", label); \
            return EXIT_FAILURE;                       \
        }                                              \
    } while (0)

int main() {
    // ---- Math layer: vectors ---------------------------------------
    {
        const Horo::Vec2 v2{3.0f, 4.0f};
        CHECK(v2.Length() > 0.0f, "Vec2::Length()");

        const Horo::Vec3 v3{1.0f, 2.0f, 3.0f};
        CHECK(v3.Length() > 0.0f, "Vec3::Length()");

        const Horo::Vec4 v4{1.0f, 0.0f, 0.0f, 1.0f};
        CHECK(v4.Length() > 0.0f, "Vec4::Length()");

        const Horo::Vec3 u = Horo::Vec3::Up();
        CHECK(Horo::Vec3::Dot(v3, u) == 2.0f, "Vec3::Dot()");
    }

    // ---- Math layer: matrix & quaternion ---------------------------
    {
        const Horo::Mat4 identity = Horo::Mat4::Identity();
        const Horo::Vec4 col0 = identity.GetColumn(0);
        CHECK(col0.x == 1.0f, "Mat4::GetColumn(0).x");
        CHECK(col0.y == 0.0f, "Mat4::GetColumn(0).y");

        // Identity quaternion rotated Forward = Forward (unchanged).
        const Horo::Quaternion q = Horo::Quaternion::Identity();
        const Horo::Vec3 rotated = q * Horo::Vec3::Forward();
        CHECK(std::abs(rotated.x -  0.0f) < 0.001f, "Quat rot x");
        CHECK(std::abs(rotated.y -  0.0f) < 0.001f, "Quat rot y");
        CHECK(std::abs(rotated.z - (-1.0f)) < 0.001f, "Quat rot z");
    }

    // ---- Renderer layer: Camera ------------------------------------
    {
        Horo::Camera camera;
        camera.position = Horo::Vec3{0.0f, 2.0f, 5.0f};
        camera.target = Horo::Vec3::Zero();
        camera.fovY = 60.0f;
        const Horo::Mat4 view = camera.GetView();
        const Horo::Vec4 col0 = view.GetColumn(0);
        CHECK(col0.x != 0.0f || col0.y != 0.0f || col0.z != 0.0f || col0.w != 0.0f,
              "Camera::GetView() non-zero");
    }

    // ---- Renderer layer: Mesh --------------------------------------
    {
        Horo::Mesh mesh;
        CHECK(mesh.GetIndexCount() == 0, "Mesh default index count");
        CHECK(mesh.GetVertices().size() == 0, "Mesh default vertex count");

        // CreateQuad uploads GPU buffers — may require a context.
        // Skip IsValid() check; just verify construction succeeds.
        Horo::Mesh quad = Horo::Mesh::CreateQuad();
        CHECK(quad.GetIndexCount() > 0, "CreateQuad has indices");
    }

    // ---- Physics layer: PhysicsWorld -------------------------------
    {
        Horo::PhysicsWorld world;
        world.SetGravity(Horo::Vec3{0.0f, -9.81f, 0.0f});
        const Horo::Vec3 gravity = world.GetGravity();
        CHECK(std::abs(gravity.y + 9.81f) < 0.01f, "PhysicsWorld gravity");
    }

    // ---- Scene layer -----------------------------------------------
    {
        Horo::Scene scene;
        scene.CreateEntity(Horo::Vec3::Zero());
        scene.CreateEntity(Horo::Vec3{10.0f, 0.0f, 0.0f});
        scene.Clear();
    }

    return EXIT_SUCCESS;
}
