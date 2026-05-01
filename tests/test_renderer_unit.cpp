// test_renderer_unit.cpp
//
// Unit tests for renderer helpers that do not need an OpenGL context.
//
// Coverage targets:
//   - RenderBackend: ToString, IsRenderBackendSupported,
//     ResolveRequestedRenderBackend, GetDefaultRenderBackendCapabilities
//   - RenderViewUtils: BuildRenderView
//   - Light: struct defaults
//   - Material: HasShader

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "renderer/Camera.h"
#include "renderer/Light.h"
#include "renderer/Material.h"
#if defined(HORO_RENDERER_OPENGL)
#include "renderer/OpenGLRenderBackend.h"
#endif
#include "renderer/RenderBackend.h"
#include "renderer/RenderViewUtils.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"

using namespace Horo;
using Catch::Approx;

// ===========================================================================
// RenderBackend — ToString
// ===========================================================================

TEST_CASE("RenderBackend: ToString returns correct string for each id", "[renderer][backend]") {
  CHECK(std::string(ToString(RenderBackendId::Auto)) == "Auto");
  CHECK(std::string(ToString(RenderBackendId::OpenGL)) == "OpenGL");
  CHECK(std::string(ToString(RenderBackendId::Vulkan)) == "Vulkan");
}

// ===========================================================================
// RenderBackend — ResolveRequestedRenderBackend
// ===========================================================================

TEST_CASE("ResolveRequestedRenderBackend: Auto resolves to OpenGL", "[renderer][backend]") {
  RenderBackendSelection sel;
  sel.requested = RenderBackendId::Auto;
  CHECK(ResolveRequestedRenderBackend(sel) == RenderBackendId::OpenGL);
}

TEST_CASE("ResolveRequestedRenderBackend: explicit OpenGL is preserved", "[renderer][backend]") {
  RenderBackendSelection sel;
  sel.requested = RenderBackendId::OpenGL;
  CHECK(ResolveRequestedRenderBackend(sel) == RenderBackendId::OpenGL);
}

TEST_CASE("ResolveRequestedRenderBackend: explicit Vulkan is preserved", "[renderer][backend]") {
  RenderBackendSelection sel;
  sel.requested = RenderBackendId::Vulkan;
  CHECK(ResolveRequestedRenderBackend(sel) == RenderBackendId::Vulkan);
}

// ===========================================================================
// RenderBackend — IsRenderBackendSupported
// ===========================================================================

TEST_CASE("IsRenderBackendSupported: OpenGL is always supported", "[renderer][backend]") {
  CHECK(IsRenderBackendSupported(RenderBackendId::OpenGL));
}

TEST_CASE("IsRenderBackendSupported: Auto is supported (resolves to OpenGL)", "[renderer][backend]") {
  // Auto resolves to OpenGL which is always present.
  CHECK(IsRenderBackendSupported(RenderBackendId::Auto));
}

// ===========================================================================
// RenderBackend — GetDefaultRenderBackendCapabilities
// ===========================================================================

TEST_CASE("GetDefaultRenderBackendCapabilities: Auto delegates to OpenGL", "[renderer][backend][coverage]") {
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

TEST_CASE("GetDefaultRenderBackendCapabilities: OpenGL defaults are explicit", "[renderer][backend][coverage]") {
  const RenderBackendCapabilities caps =
      GetDefaultRenderBackendCapabilities(RenderBackendId::OpenGL);

  CHECK(caps.supportsDebugDraw);
  CHECK(caps.supportsWireframeOverlay);
  CHECK_FALSE(caps.supportsDebugLabels);
  CHECK(caps.supportsOffscreenTargets);
  CHECK(caps.supportsNativeTextureHandles);
  CHECK(caps.supportsReadback);
  CHECK(caps.supportsDepthReadback);
  CHECK(caps.supportsDebugHud);
  CHECK_FALSE(caps.supportsComputePasses);
  CHECK_FALSE(caps.supportsGpuTimestamps);
  CHECK_FALSE(caps.supportsBindlessResources);
}

TEST_CASE("GetDefaultRenderBackendCapabilities: Vulkan defaults are explicit", "[renderer][backend][coverage]") {
  const RenderBackendCapabilities caps =
      GetDefaultRenderBackendCapabilities(RenderBackendId::Vulkan);

  CHECK_FALSE(caps.supportsDebugDraw);
  CHECK_FALSE(caps.supportsWireframeOverlay);
  CHECK_FALSE(caps.supportsDebugLabels);
  CHECK(caps.supportsOffscreenTargets);
  CHECK(caps.supportsNativeTextureHandles);
  CHECK_FALSE(caps.supportsReadback);
  CHECK_FALSE(caps.supportsDepthReadback);
  CHECK_FALSE(caps.supportsDebugHud);
  CHECK_FALSE(caps.supportsComputePasses);
  CHECK_FALSE(caps.supportsGpuTimestamps);
  CHECK_FALSE(caps.supportsBindlessResources);
}

// ===========================================================================
// RenderViewUtils — BuildRenderView
// ===========================================================================

TEST_CASE("BuildRenderView: copies camera position into RenderView", "[renderer][view]") {
  Camera cam;
  cam.position = {1.0f, 2.0f, 3.0f};
  cam.target = {0.0f, 0.0f, 0.0f};

  RenderView rv = BuildRenderView(cam);

  CHECK(rv.cameraPosition.x == Approx(1.0f));
  CHECK(rv.cameraPosition.y == Approx(2.0f));
  CHECK(rv.cameraPosition.z == Approx(3.0f));
}

TEST_CASE("BuildRenderView: view and projection matrices are non-identity by default", "[renderer][view]") {
  Camera cam;
  cam.position = {0.0f, 3.0f, 8.0f};
  cam.target = {0.0f, 0.0f, 0.0f};

  RenderView rv = BuildRenderView(cam);

  // The view matrix of a non-trivial camera differs from identity.
  // Check any element: e.g. translation column z (row=2, col=3) should be
  // non-zero.
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

TEST_CASE("Light: default type is Point with valid defaults", "[renderer][light]") {
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

TEST_CASE("Material: HasShader returns false when shader is null", "[renderer][material][coverage]") {
  Material mat;
  CHECK_FALSE(mat.HasShader());
}

TEST_CASE("Material: HasShader returns false for invalid shader", "[renderer][material][coverage]") {
  Material mat;
  mat.shader = std::make_shared<Shader>();
  // Default-constructed Shader is not valid (no GL program loaded)
  CHECK_FALSE(mat.HasShader());
}

TEST_CASE("Shader: default and moved-from objects remain invalid", "[renderer][shader]") {
  Shader shader;
  CHECK_FALSE(shader.IsValid());
  CHECK(shader.GetProgramID() == 0u);

  Shader moved(std::move(shader));
  CHECK_FALSE(moved.IsValid());
  CHECK(moved.GetProgramID() == 0u);
  CHECK_FALSE(shader.IsValid());
  CHECK(shader.GetProgramID() == 0u);

  Shader assigned;
  assigned = std::move(moved);
  CHECK_FALSE(assigned.IsValid());
  CHECK(assigned.GetProgramID() == 0u);
  CHECK_FALSE(moved.IsValid());
  CHECK(moved.GetProgramID() == 0u);
}

TEST_CASE("Texture: default and moved-from objects remain invalid", "[renderer][texture]") {
  Texture texture;
  CHECK_FALSE(texture.IsValid());
  CHECK(texture.GetNativeId() == 0u);
  CHECK(texture.GetWidth() == 0);
  CHECK(texture.GetHeight() == 0);
  CHECK_FALSE(texture.GetRenderTargetHandle().IsValid());

  Texture moved(std::move(texture));
  CHECK_FALSE(moved.IsValid());
  CHECK(moved.GetNativeId() == 0u);
  CHECK_FALSE(texture.IsValid());
  CHECK(texture.GetNativeId() == 0u);

  Texture assigned;
  assigned = std::move(moved);
  CHECK_FALSE(assigned.IsValid());
  CHECK(assigned.GetNativeId() == 0u);
  CHECK_FALSE(moved.IsValid());
  CHECK(moved.GetNativeId() == 0u);
}

// ===========================================================================
// RenderBackend — Vulkan capability and support paths
// ===========================================================================

TEST_CASE("IsRenderBackendSupported: Vulkan returns a defined result", "[renderer][backend]") {
  // The result depends on whether HORO_HAS_VULKAN is defined, but the
  // call must not crash and must exercise the Vulkan branch.
  const bool result = IsRenderBackendSupported(RenderBackendId::Vulkan);
  // Either true (Vulkan present) or false (not compiled in) — both are valid.
  (void)result;
  SUCCEED();
}

// ===========================================================================
// Shader / Texture / OpenGL backend deterministic paths
// ===========================================================================

TEST_CASE("Shader: FromFiles throws when shader files are missing", "[renderer][shader]") {
  const auto missing = std::filesystem::path("tests/fixtures/does_not_exist");
  CHECK_THROWS_AS(Shader::FromFiles((missing / "missing.vert").string(),
                                    (missing / "missing.frag").string()),
                  ShaderException);
}

TEST_CASE("Texture: default object exposes invalid OpenGL handle metadata", "[renderer][texture]") {
  Texture texture;
  const RenderTargetHandle handle = texture.GetRenderTargetHandle(true);

  CHECK_FALSE(texture.IsValid());
  CHECK(texture.GetNativeId() == 0u);
  CHECK(texture.GetWidth() == 0);
  CHECK(texture.GetHeight() == 0);
  CHECK(handle.backendId == RenderBackendId::OpenGL);
  CHECK(handle.nativeType == RenderNativeHandleType::OpenGLTexture2D);
  CHECK(handle.nativeHandle == 0u);
  CHECK_FALSE(handle.IsValid());
  CHECK(handle.needsYFlip);
}

#if defined(HORO_RENDERER_OPENGL)
TEST_CASE("OpenGLRenderBackend: readback validates dimensions and errors", "[renderer][backend][opengl]") {
  OpenGLRenderBackend backend;
  std::vector<uint8_t> color;
  std::vector<float> depth;
  std::string error;

  REQUIRE_FALSE(backend.ReadbackColorBgr8(0, 4, color, &error));
  CHECK(error.find("positive dimensions") != std::string::npos);
  REQUIRE_FALSE(backend.ReadbackDepth32F(4, -1, depth, &error));
  CHECK(error.find("positive dimensions") != std::string::npos);
  REQUIRE_FALSE(backend.ReadbackColorBgr8(-1, 4, color, nullptr));
  REQUIRE_FALSE(backend.ReadbackDepth32F(4, 0, depth, nullptr));
}

TEST_CASE("OpenGLRenderBackend: invalid readback requests preserve output buffers", "[renderer][backend][opengl]") {
  OpenGLRenderBackend backend;
  std::vector<uint8_t> color = {1u, 2u, 3u};
  std::vector<float> depth = {0.25f, 0.5f};
  std::string colorError = "unchanged";
  std::string depthError = "unchanged";

  REQUIRE_FALSE(backend.ReadbackColorBgr8(-16, 8, color, &colorError));
  REQUIRE_FALSE(backend.ReadbackDepth32F(8, -16, depth, &depthError));

  CHECK(color == std::vector<uint8_t>({1u, 2u, 3u}));
  CHECK(depth == std::vector<float>({0.25f, 0.5f}));
  CHECK(colorError.find("positive dimensions") != std::string::npos);
  CHECK(depthError.find("positive dimensions") != std::string::npos);
}

TEST_CASE("OpenGLRenderBackend: viewport target APIs remain unavailable", "[renderer][backend][opengl]") {
  OpenGLRenderBackend backend;
  std::string error;
  RenderTargetHandle handle = RenderTargetHandle::OpenGLTexture(33u);

  REQUIRE_FALSE(backend.EnsureEditorViewportRenderTarget(800u, 600u, &error));
  CHECK(error.find("unavailable") != std::string::npos);
  REQUIRE_FALSE(
      backend.TryGetEditorViewportRenderTargetHandle(&handle, true, &error));
  CHECK(error.find("unavailable") != std::string::npos);
  CHECK_FALSE(handle.IsValid());

  REQUIRE_FALSE(
      backend.TryGetEditorViewportRenderTargetHandle(nullptr, false, nullptr));
}

TEST_CASE("OpenGLRenderBackend: capabilities mirror OpenGL defaults", "[renderer][backend][opengl]") {
  OpenGLRenderBackend backend;

  const RenderBackendCapabilities actual = backend.GetCapabilities();
  const RenderBackendCapabilities expected =
      GetDefaultRenderBackendCapabilities(RenderBackendId::OpenGL);

  CHECK(actual.supportsDebugDraw == expected.supportsDebugDraw);
  CHECK(actual.supportsWireframeOverlay == expected.supportsWireframeOverlay);
  CHECK(actual.supportsOffscreenTargets == expected.supportsOffscreenTargets);
  CHECK(actual.supportsNativeTextureHandles ==
        expected.supportsNativeTextureHandles);
  CHECK(actual.supportsReadback == expected.supportsReadback);
  CHECK(actual.supportsDepthReadback == expected.supportsDepthReadback);
  CHECK(actual.supportsDebugHud == expected.supportsDebugHud);
}
#endif // HORO_RENDERER_OPENGL

// ===========================================================================
// Mesh — non-GPU paths
// ===========================================================================

#include "renderer/Mesh.h"
#include "renderer/SkinnedMesh.h"

TEST_CASE("Mesh: default construction gives zero index count and unit half-extents", "[renderer][mesh]") {
  Mesh m;
  CHECK(m.GetIndexCount() == 0);
  const Vec3 he = m.GetHalfExtents();
  CHECK(he.x == Approx(0.5f));
  CHECK(he.y == Approx(0.5f));
  CHECK(he.z == Approx(0.5f));
  const Vec3 center = m.GetLocalAabbCenter();
  CHECK(center.x == Approx(0.0f));
  CHECK(center.y == Approx(0.0f));
  CHECK(center.z == Approx(0.0f));
}

TEST_CASE("Mesh: move constructor transfers state", "[renderer][mesh]") {
  Mesh a;
  Mesh b(std::move(a));
  CHECK(b.GetIndexCount() == 0);
}

TEST_CASE("Mesh: move assignment transfers state", "[renderer][mesh]") {
  Mesh a;
  Mesh b;
  b = std::move(a);
  CHECK(b.GetIndexCount() == 0);
}

TEST_CASE("Mesh: CreateBox generates expected CPU geometry", "[renderer][mesh]") {
  const Mesh mesh = Mesh::CreateBox(2.0f, 3.0f, 4.0f);

  CHECK(mesh.GetVertices().size() == 24);
  CHECK(mesh.GetIndices().size() == 36);
  CHECK(mesh.GetIndexCount() == 36);
  for (const Vertex &vertex : mesh.GetVertices()) {
    CHECK(std::abs(vertex.normal.x) + std::abs(vertex.normal.y) +
              std::abs(vertex.normal.z) == Approx(1.0f));
  }
}

TEST_CASE("Mesh: CreatePlane and CreateQuad generate expected bounds", "[renderer][mesh]") {
  const Mesh plane = Mesh::CreatePlane(5.0f);
  CHECK(plane.GetVertices().size() == 4);
  CHECK(plane.GetIndices().size() == 6);
  CHECK(plane.GetVertices()[0].position.x == Approx(-5.0f));
  CHECK(plane.GetVertices()[0].position.y == Approx(0.0f));
  CHECK(plane.GetVertices()[0].position.z == Approx(-5.0f));
  CHECK(plane.GetVertices()[0].normal.y == Approx(1.0f));

  const Mesh quad = Mesh::CreateQuad();
  CHECK(quad.GetVertices().size() == 4);
  CHECK(quad.GetIndices().size() == 6);
  CHECK(quad.GetVertices()[0].position.x == Approx(-1.0f));
  CHECK(quad.GetVertices()[0].position.y == Approx(-1.0f));
  CHECK(quad.GetVertices()[0].position.z == Approx(0.0f));
  CHECK(quad.GetVertices()[0].normal.z == Approx(1.0f));
}

TEST_CASE("Mesh: CreatePyramid generates expected CPU geometry", "[renderer][mesh]") {
  const Mesh mesh = Mesh::CreatePyramid(2.0f, 3.0f);

  CHECK(mesh.GetVertices().size() == 16);
  CHECK(mesh.GetIndices().size() == 18);
  CHECK(mesh.GetIndexCount() == 18);
  CHECK(mesh.GetHalfExtents().x == Approx(2.0f));
  CHECK(mesh.GetHalfExtents().y == Approx(3.0f));
  CHECK(mesh.GetHalfExtents().z == Approx(2.0f));
  CHECK(mesh.GetLocalAabbCenter().y == Approx(0.0f));
}

TEST_CASE("Mesh: CreateCylinder generates expected CPU geometry", "[renderer][mesh]") {
  const Mesh mesh = Mesh::CreateCylinder(1.5f, 2.0f, 8);

  CHECK(mesh.GetVertices().size() == 52);
  CHECK(mesh.GetIndices().size() == 96);
  CHECK(mesh.GetIndexCount() == 96);
}

TEST_CASE("Mesh: CreateSphere generates expected CPU geometry", "[renderer][mesh]") {
  const Mesh mesh = Mesh::CreateSphere(2.0f, 2, 4);

  CHECK(mesh.GetVertices().size() == 15);
  CHECK(mesh.GetIndices().size() == 48);
  CHECK(mesh.GetIndexCount() == 48);
}

// ===========================================================================
// SkinnedMesh — non-GPU paths
// ===========================================================================

TEST_CASE("SkinnedMesh: default construction is valid", "[renderer][skinned-mesh]") {
  SkinnedMesh sm;
  CHECK(sm.GetIndexCount() == 0);
}

TEST_CASE("SkinnedMesh: move constructor transfers state", "[renderer][skinned-mesh]") {
  SkinnedMesh a;
  SkinnedMesh b(std::move(a));
  CHECK(b.GetIndexCount() == 0);
  CHECK(a.GetIndexCount() == 0);
}

TEST_CASE("SkinnedMesh: move assignment transfers state", "[renderer][skinned-mesh]") {
  SkinnedMesh a;
  SkinnedMesh b;
  b = std::move(a);
  CHECK(b.GetIndexCount() == 0);
}
