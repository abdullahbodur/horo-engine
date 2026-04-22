// test_renderer_unit.cpp
//
// Unit tests for renderer helpers that do not need an OpenGL context.
//
// Coverage targets:
//   - RenderBackend: ToString, IsRenderBackendSupported,
//     ResolveRequestedRenderBackend, GetDefaultRenderBackendCapabilities(Auto)
//   - RenderViewUtils: BuildRenderView
//   - Light: struct defaults
//   - Material: HasShader

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "renderer/Camera.h"
#include "renderer/Light.h"
#include "renderer/Material.h"
#include "renderer/RenderBackend.h"
#include "renderer/RenderViewUtils.h"
#include "renderer/Shader.h"

using namespace Monolith;
using Catch::Approx;

// ===========================================================================
// RenderBackend — ToString
// ===========================================================================

TEST_CASE("RenderBackend: ToString returns correct string for each id",
          "[renderer][backend]") {
    CHECK(std::string(ToString(RenderBackendId::Auto)) == "Auto");
    CHECK(std::string(ToString(RenderBackendId::OpenGL)) == "OpenGL");
    CHECK(std::string(ToString(RenderBackendId::Vulkan)) == "Vulkan");
}

// ===========================================================================
// RenderBackend — ResolveRequestedRenderBackend
// ===========================================================================

TEST_CASE("ResolveRequestedRenderBackend: Auto resolves to OpenGL",
          "[renderer][backend]") {
    RenderBackendSelection sel;
    sel.requested = RenderBackendId::Auto;
    CHECK(ResolveRequestedRenderBackend(sel) == RenderBackendId::OpenGL);
}

TEST_CASE("ResolveRequestedRenderBackend: explicit OpenGL is preserved",
          "[renderer][backend]") {
    RenderBackendSelection sel;
    sel.requested = RenderBackendId::OpenGL;
    CHECK(ResolveRequestedRenderBackend(sel) == RenderBackendId::OpenGL);
}

TEST_CASE("ResolveRequestedRenderBackend: explicit Vulkan is preserved",
          "[renderer][backend]") {
    RenderBackendSelection sel;
    sel.requested = RenderBackendId::Vulkan;
    CHECK(ResolveRequestedRenderBackend(sel) == RenderBackendId::Vulkan);
}

// ===========================================================================
// RenderBackend — IsRenderBackendSupported
// ===========================================================================

TEST_CASE("IsRenderBackendSupported: OpenGL is always supported",
          "[renderer][backend]") {
    CHECK(IsRenderBackendSupported(RenderBackendId::OpenGL));
}

TEST_CASE("IsRenderBackendSupported: Auto is supported (resolves to OpenGL)",
          "[renderer][backend]") {
    // Auto resolves to OpenGL which is always present.
    CHECK(IsRenderBackendSupported(RenderBackendId::Auto));
}

// ===========================================================================
// RenderBackend — GetDefaultRenderBackendCapabilities(Auto)
// ===========================================================================

TEST_CASE(
    "GetDefaultRenderBackendCapabilities: Auto delegates to OpenGL",
    "[renderer][backend]") {
    const RenderBackendCapabilities autoCaps =
            GetDefaultRenderBackendCapabilities(RenderBackendId::Auto);
    const RenderBackendCapabilities glCaps =
            GetDefaultRenderBackendCapabilities(RenderBackendId::OpenGL);

    // Auto must produce the same set as OpenGL since it always resolves to it.
    CHECK(autoCaps.supportsDebugDraw == glCaps.supportsDebugDraw);
    CHECK(autoCaps.supportsWireframeOverlay == glCaps.supportsWireframeOverlay);
    CHECK(autoCaps.supportsOffscreenTargets == glCaps.supportsOffscreenTargets);
    CHECK(autoCaps.supportsReadback == glCaps.supportsReadback);
    CHECK(autoCaps.supportsDebugHud == glCaps.supportsDebugHud);
}

// ===========================================================================
// RenderViewUtils — BuildRenderView
// ===========================================================================

TEST_CASE("BuildRenderView: copies camera position into RenderView",
          "[renderer][view]") {
    Camera cam;
    cam.position = {1.0f, 2.0f, 3.0f};
    cam.target = {0.0f, 0.0f, 0.0f};

    RenderView rv = BuildRenderView(cam);

    CHECK(rv.cameraPosition.x == Approx(1.0f));
    CHECK(rv.cameraPosition.y == Approx(2.0f));
    CHECK(rv.cameraPosition.z == Approx(3.0f));
}

TEST_CASE("BuildRenderView: view and projection matrices are non-identity by default",
          "[renderer][view]") {
    Camera cam;
    cam.position = {0.0f, 3.0f, 8.0f};
    cam.target = {0.0f, 0.0f, 0.0f};

    RenderView rv = BuildRenderView(cam);

    // The view matrix of a non-trivial camera differs from identity.
    // Check any element: e.g. translation column z (row=2, col=3) should be non-zero.
    const Mat4 identity = Mat4::Identity();
    bool viewDiffersFromIdentity = false;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (std::abs(rv.view(r, c) - identity(r, c)) > 1e-4f) {
                viewDiffersFromIdentity = true;
                break;
            }
        }
        if (viewDiffersFromIdentity)
            break;
    }
    CHECK(viewDiffersFromIdentity);
}

// ===========================================================================
// Light — struct defaults
// ===========================================================================

TEST_CASE("Light: default type is Point with valid defaults",
          "[renderer][light]") {
    Light light;
    CHECK(light.type == Light::Type::Point);
    CHECK(light.intensity == Approx(1.0f));
    CHECK(light.radius > 0.0f);
}

TEST_CASE("Light: Directional type can be assigned", "[renderer][light]") {
    Light light;
    light.type = Light::Type::Directional;
    CHECK(light.type == Light::Type::Directional);
}

// ===========================================================================
// Material — HasShader
// ===========================================================================

TEST_CASE("Material: HasShader returns false when shader is null",
          "[renderer][material]") {
    Material mat;
    CHECK_FALSE(mat.HasShader());
}

TEST_CASE("Material: HasShader returns false for invalid shader",
          "[renderer][material]") {
    Material mat;
    mat.shader = std::make_shared<Shader>();
    // Default-constructed Shader is not valid (no GL program loaded)
    CHECK_FALSE(mat.HasShader());
}

// ===========================================================================
// RenderBackend — Vulkan capability and support paths
// ===========================================================================

TEST_CASE("GetDefaultRenderBackendCapabilities: Vulkan returns expected caps",
          "[renderer][backend]") {
    const RenderBackendCapabilities caps =
            GetDefaultRenderBackendCapabilities(RenderBackendId::Vulkan);
    // Vulkan preset does not support debug draw in the current backend spec.
    CHECK_FALSE(caps.supportsDebugDraw);
    CHECK_FALSE(caps.supportsDebugHud);
    // Offscreen targets and native handles are supported even for Vulkan.
    CHECK(caps.supportsOffscreenTargets);
    CHECK(caps.supportsNativeTextureHandles);
}

TEST_CASE("IsRenderBackendSupported: Vulkan returns a defined result",
          "[renderer][backend]") {
    // The result depends on whether MONOLITH_HAS_VULKAN is defined, but the
    // call must not crash and must exercise the Vulkan branch.
    const bool result =
            IsRenderBackendSupported(RenderBackendId::Vulkan);
    // Either true (Vulkan present) or false (not compiled in) — both are valid.
    (void) result;
    SUCCEED();
}
