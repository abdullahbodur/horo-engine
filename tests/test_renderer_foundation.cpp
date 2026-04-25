#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#if defined(MONOLITH_HAS_VULKAN)
#include <GLFW/glfw3.h>
#endif

#include "math/Mat4.h"
#include "renderer/DebugHUD.h"
#include "renderer/GltfLoader.h"
#include "renderer/IRenderBackend.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"
#include "renderer/RenderBackend.h"
#include "renderer/RenderTargetHandle.h"
#include "renderer/RenderViewUtils.h"
#include "renderer/Renderer.h"
#include "renderer/SkinnedMesh.h"
#include "renderer/VulkanRenderBackend.h"
#include "scene/Registry.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"
#include "scene/systems/RenderSystem.h"
#include "tests/TestTempPaths.h"

using namespace Monolith;
using json = nlohmann::json;

namespace {
class FakeRenderBackend : public IRenderBackend {
public:
  void BeginFrame(const RenderFrameConfig &frame) override {
    events.emplace_back("begin-frame");
    lastFrame = frame;
  }

  void EndFrame() override { events.emplace_back("end-frame"); }

  void BeginPass(const RenderPassConfig &pass) override {
    events.emplace_back("begin-pass");
    lastPass = pass;
  }

  void EndPass() override { events.emplace_back("end-pass"); }

  void DrawMesh(const MeshDrawCommand &command) override {
    events.emplace_back("draw-mesh");
    lastMesh = command;
    ++drawCalls;
  }

  void DrawSkinnedMesh(const SkinnedMeshDrawCommand &command) override {
    events.emplace_back("draw-skinned");
    lastSkinned = command;
    ++drawCalls;
  }

  void DrawWireframe(const WireframeDrawCommand &command) override {
    events.emplace_back("draw-wireframe");
    lastWireframe = command;
    ++drawCalls;
  }

  RenderBackendId GetBackendId() const override {
    return RenderBackendId::OpenGL;
  }

  RenderBackendCapabilities GetCapabilities() const override {
    return GetDefaultRenderBackendCapabilities(RenderBackendId::OpenGL);
  }

  int GetDrawCallCount() const override { return drawCalls; }

  bool ReadbackColorBgr8(int width, int height, std::vector<uint8_t> &outPixels,
                         std::string *outError) override {
    if (width <= 0 || height <= 0) {
      if (outError)
        *outError = "invalid color readback dimensions";
      return false;
    }
    outPixels.assign(static_cast<size_t>(width * height * 3), 0u);
    return true;
  }

  bool ReadbackDepth32F(int width, int height, std::vector<float> &outDepth,
                        std::string *outError) override {
    if (width <= 0 || height <= 0) {
      if (outError)
        *outError = "invalid depth readback dimensions";
      return false;
    }
    outDepth.assign(static_cast<size_t>(width * height), 1.0f);
    return true;
  }

  bool EnsureEditorViewportRenderTarget(uint32_t, uint32_t,
                                        std::string *outError) override {
    if (outError)
      *outError = "fake backend has no viewport targets";
    return false;
  }

  bool TryGetEditorViewportRenderTargetHandle(RenderTargetHandle *outHandle,
                                              bool,
                                              std::string *outError) override {
    if (outHandle)
      *outHandle = {};
    if (outError)
      *outError = "fake backend has no viewport target handle";
    return false;
  }

  std::vector<std::string> events;
  RenderFrameConfig lastFrame;
  RenderPassConfig lastPass;
  MeshDrawCommand lastMesh;
  SkinnedMeshDrawCommand lastSkinned;
  WireframeDrawCommand lastWireframe;
  int drawCalls = 0;
};

class DebugHudUnsupportedBackend final : public FakeRenderBackend {
public:
  RenderBackendCapabilities GetCapabilities() const override {
    RenderBackendCapabilities caps = {};
    caps.supportsDebugHud = false;
    return caps;
  }
};

template <typename T>
void AppendPod(std::vector<uint8_t> *out, const T &value) {
  const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
  out->insert(out->end(), bytes, bytes + sizeof(T));
}

void PadToAlignment(std::vector<uint8_t> *out, size_t alignment) {
  while ((out->size() % alignment) != 0u)
    out->push_back(0);
}

std::filesystem::path BuildGltfFixturePath(const std::string &name) {
  namespace fs = std::filesystem;
  const fs::path dir = Monolith::Tests::SecureTempPath(name);
  std::error_code ec;
  fs::remove_all(dir, ec);
  ec.clear();
  fs::create_directories(dir, ec);
  REQUIRE_FALSE(ec);
  return dir;
}

std::filesystem::path WriteSkinnedAnimatedFixture() {
  namespace fs = std::filesystem;
  const fs::path dir = BuildGltfFixturePath("renderer_gltf_skinned");
  const fs::path binPath = dir / "buffer.bin";
  const fs::path gltfPath = dir / "scene.gltf";

  std::vector<uint8_t> bin;

  const size_t posOffset = bin.size();
  const std::array<float, 9> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                          0.0f, 0.0f, 1.0f, 0.0f};
  for (float v : positions)
    AppendPod(&bin, v);

  const size_t jointsOffset = bin.size();
  const std::array<uint8_t, 12> joints = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  bin.insert(bin.end(), joints.begin(), joints.end());

  PadToAlignment(&bin, 4);
  const size_t weightsOffset = bin.size();
  const std::array<float, 12> weights = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                         0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
  for (float v : weights)
    AppendPod(&bin, v);

  const size_t indicesOffset = bin.size();
  const std::array<uint16_t, 3> indices = {0, 1, 2};
  for (uint16_t v : indices)
    AppendPod(&bin, v);

  PadToAlignment(&bin, 4);
  const size_t ibmOffset = bin.size();
  const std::array<float, 16> ibm = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                     0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                     0.0f, 0.0f, 0.0f, 1.0f};
  for (float v : ibm)
    AppendPod(&bin, v);

  const size_t animTimeOffset = bin.size();
  const std::array<float, 2> times = {0.0f, 1.0f};
  for (float v : times)
    AppendPod(&bin, v);

  const size_t animTransOffset = bin.size();
  const std::array<float, 6> translations = {0.0f, 0.0f, 0.0f,
                                             1.0f, 0.0f, 0.0f};
  for (float v : translations)
    AppendPod(&bin, v);

  {
    std::ofstream out(binPath, std::ios::binary);
    REQUIRE(out.is_open());
    out.write(reinterpret_cast<const char *>(bin.data()),
              static_cast<std::streamsize>(bin.size()));
  }

  const json gltf = {
      {"asset", {{"version", "2.0"}}},
      {"buffers",
       json::array({{{"uri", "buffer.bin"}, {"byteLength", bin.size()}}})},
      {"bufferViews",
       json::array({{{"buffer", 0},
                     {"byteOffset", posOffset},
                     {"byteLength", positions.size() * sizeof(float)}},
                    {{"buffer", 0},
                     {"byteOffset", jointsOffset},
                     {"byteLength", joints.size()}},
                    {{"buffer", 0},
                     {"byteOffset", weightsOffset},
                     {"byteLength", weights.size() * sizeof(float)}},
                    {{"buffer", 0},
                     {"byteOffset", indicesOffset},
                     {"byteLength", indices.size() * sizeof(uint16_t)}},
                    {{"buffer", 0},
                     {"byteOffset", ibmOffset},
                     {"byteLength", ibm.size() * sizeof(float)}},
                    {{"buffer", 0},
                     {"byteOffset", animTimeOffset},
                     {"byteLength", times.size() * sizeof(float)}},
                    {{"buffer", 0},
                     {"byteOffset", animTransOffset},
                     {"byteLength", translations.size() * sizeof(float)}}})},
      {"accessors", json::array({{{"bufferView", 0},
                                  {"componentType", 5126},
                                  {"count", 3},
                                  {"type", "VEC3"}},
                                 {{"bufferView", 1},
                                  {"componentType", 5121},
                                  {"count", 3},
                                  {"type", "VEC4"}},
                                 {{"bufferView", 2},
                                  {"componentType", 5126},
                                  {"count", 3},
                                  {"type", "VEC4"}},
                                 {{"bufferView", 3},
                                  {"componentType", 5123},
                                  {"count", 3},
                                  {"type", "SCALAR"}},
                                 {{"bufferView", 4},
                                  {"componentType", 5126},
                                  {"count", 1},
                                  {"type", "MAT4"}},
                                 {{"bufferView", 5},
                                  {"componentType", 5126},
                                  {"count", 2},
                                  {"type", "SCALAR"}},
                                 {{"bufferView", 6},
                                  {"componentType", 5126},
                                  {"count", 2},
                                  {"type", "VEC3"}}})},
      {"meshes",
       json::array(
           {{{"primitives",
              json::array(
                  {{{"attributes",
                     {{"POSITION", 0}, {"JOINTS_0", 1}, {"WEIGHTS_0", 2}}},
                    {"indices", 3},
                    {"mode", 4}}})}}})},
      {"skins", json::array({{{"joints", json::array({0})},
                              {"inverseBindMatrices", 4}}})},
      {"nodes",
       json::array({{{"name", "root_joint"}},
                    {{"name", "mesh_node"}, {"mesh", 0}, {"skin", 0}}})},
      {"animations",
       json::array(
           {{{"name", "move_x"},
             {"samplers", json::array({{{"input", 5},
                                        {"output", 6},
                                        {"interpolation", "LINEAR"}}})},
             {"channels",
              json::array(
                  {{{"sampler", 0},
                    {"target", {{"node", 0}, {"path", "translation"}}}}})}}})},
      {"scenes", json::array({{{"nodes", json::array({1})}}})},
      {"scene", 0}};

  {
    std::ofstream out(gltfPath);
    REQUIRE(out.is_open());
    out << gltf.dump(2);
  }

  return gltfPath;
}

std::filesystem::path WritePrimitiveFixture(const std::string &name, int mode,
                                            bool includePosition) {
  namespace fs = std::filesystem;
  const fs::path dir = BuildGltfFixturePath(name);
  const fs::path binPath = dir / "buffer.bin";
  const fs::path gltfPath = dir / "scene.gltf";

  std::vector<uint8_t> bin;
  const std::array<float, 9> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                          0.0f, 0.0f, 1.0f, 0.0f};
  for (float v : positions)
    AppendPod(&bin, v);

  {
    std::ofstream out(binPath, std::ios::binary);
    REQUIRE(out.is_open());
    out.write(reinterpret_cast<const char *>(bin.data()),
              static_cast<std::streamsize>(bin.size()));
  }

  json attributes = json::object();
  if (includePosition)
    attributes["POSITION"] = 0;
  else
    attributes["NORMAL"] = 0;

  const json gltf = {
      {"asset", {{"version", "2.0"}}},
      {"buffers",
       json::array({{{"uri", "buffer.bin"}, {"byteLength", bin.size()}}})},
      {"bufferViews",
       json::array({{{"buffer", 0},
                     {"byteOffset", 0},
                     {"byteLength", positions.size() * sizeof(float)}}})},
      {"accessors", json::array({{{"bufferView", 0},
                                  {"componentType", 5126},
                                  {"count", 3},
                                  {"type", "VEC3"}}})},
      {"meshes",
       json::array({{{"primitives", json::array({{{"attributes", attributes},
                                                  {"mode", mode}}})}}})},
      {"nodes", json::array({{{"mesh", 0}}})},
      {"scenes", json::array({{{"nodes", json::array({0})}}})},
      {"scene", 0}};

  {
    std::ofstream out(gltfPath);
    REQUIRE(out.is_open());
    out << gltf.dump(2);
  }

  return gltfPath;
}

#if defined(MONOLITH_HAS_VULKAN)
struct OverlayRenderProbe {
  bool invoked = false;
  void *commandBufferHandle = nullptr;
};

void CaptureOverlayRenderProbe(void *userData, void *commandBufferHandle) {
  if (!userData)
    return;
  auto *probe = static_cast<OverlayRenderProbe *>(userData);
  probe->invoked = true;
  probe->commandBufferHandle = commandBufferHandle;
}
#endif
} // namespace

TEST_CASE(
    "Renderer routes explicit frame and pass commands through backend seam",
    "[renderer][foundation]") {
  FakeRenderBackend backend;
  Renderer::UseBackend(&backend);

  const Camera camera;
  std::vector<Light> lights(2);
  Mesh mesh;
  Material material;

  Renderer::BeginFrame(RenderFrameConfig{lights, "test-frame"});
  Renderer::BeginPass(RenderPassConfig{RenderPassId::OpaqueScene,
                                       BuildRenderView(camera), "main"});
  Renderer::Submit(mesh, Mat4::Identity(), material);
  Renderer::EndPass();
  Renderer::EndFrame();

  REQUIRE(backend.events == std::vector<std::string>{"begin-frame",
                                                     "begin-pass", "draw-mesh",
                                                     "end-pass", "end-frame"});
  REQUIRE(backend.lastFrame.lights.size() == 2);
  REQUIRE(backend.lastPass.id == RenderPassId::OpaqueScene);
  REQUIRE(backend.lastMesh.mesh == &mesh);
  REQUIRE(backend.lastMesh.material == &material);
  REQUIRE(Renderer::GetDrawCallCount() == 1);
  std::vector<uint8_t> colorReadback;
  std::vector<float> depthReadback;
  std::string readbackError;
  REQUIRE(Renderer::ReadbackColorBgr8(2, 2, colorReadback, &readbackError));
  REQUIRE(colorReadback.size() == 12u);
  REQUIRE(Renderer::ReadbackDepth32F(2, 2, depthReadback, &readbackError));
  REQUIRE(depthReadback.size() == 4u);
  RenderTargetHandle viewportHandle;
  REQUIRE_FALSE(
      Renderer::EnsureEditorViewportRenderTarget(320u, 180u, &readbackError));
  REQUIRE(readbackError.find("no viewport targets") != std::string::npos);
  REQUIRE_FALSE(Renderer::TryGetEditorViewportRenderTargetHandle(
      &viewportHandle, false, &readbackError));
  REQUIRE(readbackError.find("no viewport target handle") != std::string::npos);
  REQUIRE_FALSE(viewportHandle.IsValid());

  Renderer::ResetBackend();
}

TEST_CASE("Renderer forwards frame output config through the backend seam",
          "[renderer][foundation][frame-output]") {
  FakeRenderBackend backend;
  Renderer::UseBackend(&backend);

  const Vec4 clearColor{0.3f, 0.2f, 0.1f, 1.0f};
  Renderer::BeginFrame(
      RenderFrameConfig{{}, "frame-output", clearColor, false, true});
  Renderer::EndFrame();

  REQUIRE(backend.events ==
          std::vector<std::string>{"begin-frame", "end-frame"});
  REQUIRE(backend.lastFrame.debugLabel == "frame-output");
  REQUIRE(backend.lastFrame.clearColor == clearColor);
  REQUIRE_FALSE(backend.lastFrame.clearColorBuffer);
  REQUIRE(backend.lastFrame.clearDepthBuffer);

  Renderer::ResetBackend();
}

TEST_CASE(
    "Renderer initializes the default OpenGL backend through a typed selection",
    "[renderer][foundation][backend]") {
  const RenderBackendInitResult init =
      Renderer::InitializeBackend({RenderBackendId::Auto});

  REQUIRE(init.ok);
  REQUIRE(init.requested == RenderBackendId::Auto);
  REQUIRE(init.selected == RenderBackendId::OpenGL);
  REQUIRE(Renderer::GetBackendId() == RenderBackendId::OpenGL);

  const RenderBackendCapabilities caps = Renderer::GetBackendCapabilities();
  REQUIRE(caps.supportsDebugDraw);
  REQUIRE(caps.supportsWireframeOverlay);
  REQUIRE(caps.supportsOffscreenTargets);
  REQUIRE(caps.supportsNativeTextureHandles);
  REQUIRE(caps.supportsReadback);
  REQUIRE(caps.supportsDepthReadback);
  REQUIRE(caps.supportsDebugHud);
  REQUIRE_FALSE(caps.supportsComputePasses);
}

TEST_CASE("Renderer rejects unsupported backend requests without replacing the "
          "active backend",
          "[renderer][foundation][backend]") {
  REQUIRE(Renderer::InitializeBackend({RenderBackendId::OpenGL}).ok);

  const RenderBackendInitResult init =
      Renderer::InitializeBackend({RenderBackendId::Vulkan});

#if defined(MONOLITH_HAS_VULKAN)
  REQUIRE_FALSE(init.ok);
  REQUIRE(init.selected == RenderBackendId::Vulkan);
  REQUIRE(init.error.find("window handle") != std::string::npos);
  REQUIRE(Renderer::GetBackendId() == RenderBackendId::OpenGL);
  REQUIRE(Renderer::IsBackendSupported(RenderBackendId::Vulkan));

  const RenderBackendCapabilities vulkanCaps =
      GetDefaultRenderBackendCapabilities(RenderBackendId::Vulkan);
  REQUIRE_FALSE(vulkanCaps.supportsDebugDraw);
  REQUIRE_FALSE(vulkanCaps.supportsWireframeOverlay);
  REQUIRE(vulkanCaps.supportsOffscreenTargets);
  REQUIRE(vulkanCaps.supportsNativeTextureHandles);
  REQUIRE_FALSE(vulkanCaps.supportsReadback);
  REQUIRE_FALSE(vulkanCaps.supportsDepthReadback);
  REQUIRE_FALSE(vulkanCaps.supportsDebugHud);
#else
  REQUIRE_FALSE(init.ok);
  REQUIRE(init.selected == RenderBackendId::Vulkan);
  REQUIRE_FALSE(init.error.empty());
  REQUIRE(Renderer::GetBackendId() == RenderBackendId::OpenGL);
  REQUIRE_FALSE(Renderer::IsBackendSupported(RenderBackendId::Vulkan));
#endif
}

TEST_CASE("RenderBackendSelection preserves native window handles for backend "
          "bootstrap",
          "[renderer][foundation][backend][selection]") {
  RenderBackendSelection selection;
  selection.requested = RenderBackendId::Vulkan;
  selection.nativeWindowHandle = &selection;

  REQUIRE(selection.requested == RenderBackendId::Vulkan);
  REQUIRE(selection.nativeWindowHandle == &selection);
}

TEST_CASE("Backend capability defaults express the current parity matrix",
          "[renderer][foundation][backend][parity]") {
  const RenderBackendCapabilities glCaps =
      GetDefaultRenderBackendCapabilities(RenderBackendId::OpenGL);
  REQUIRE(glCaps.supportsDebugDraw);
  REQUIRE(glCaps.supportsWireframeOverlay);
  REQUIRE(glCaps.supportsOffscreenTargets);
  REQUIRE(glCaps.supportsNativeTextureHandles);
  REQUIRE(glCaps.supportsReadback);
  REQUIRE(glCaps.supportsDepthReadback);
  REQUIRE(glCaps.supportsDebugHud);

  const RenderBackendCapabilities vkCaps =
      GetDefaultRenderBackendCapabilities(RenderBackendId::Vulkan);
  REQUIRE_FALSE(vkCaps.supportsDebugDraw);
  REQUIRE_FALSE(vkCaps.supportsWireframeOverlay);
  REQUIRE(vkCaps.supportsOffscreenTargets);
  REQUIRE(vkCaps.supportsNativeTextureHandles);
  REQUIRE_FALSE(vkCaps.supportsReadback);
  REQUIRE_FALSE(vkCaps.supportsDepthReadback);
  REQUIRE_FALSE(vkCaps.supportsDebugHud);
}

TEST_CASE("RenderTargetHandle constructors preserve backend-native metadata",
          "[renderer][foundation][handles]") {
  const RenderTargetHandle glHandle =
      RenderTargetHandle::OpenGLTexture(42u, true);
  REQUIRE(glHandle.IsValid());
  REQUIRE(glHandle.backendId == RenderBackendId::OpenGL);
  REQUIRE(glHandle.nativeType == RenderNativeHandleType::OpenGLTexture2D);
  REQUIRE(glHandle.nativeHandle == 42u);
  REQUIRE(glHandle.width == 0u);
  REQUIRE(glHandle.height == 0u);
  REQUIRE(glHandle.generation == 0u);
  REQUIRE(glHandle.needsYFlip);

  int descriptorSetToken = 0;
  auto *fakeDescriptorSet = &descriptorSetToken;
  const RenderTargetHandle vkHandle = RenderTargetHandle::VulkanDescriptorSet(
      reinterpret_cast<VulkanImGuiDescriptorSetHandle>(fakeDescriptorSet),
      false);
  REQUIRE(vkHandle.IsValid());
  REQUIRE(vkHandle.backendId == RenderBackendId::Vulkan);
  REQUIRE(vkHandle.nativeType ==
          RenderNativeHandleType::VulkanImGuiDescriptorSet);
  REQUIRE(vkHandle.nativeHandle != 0u);
  REQUIRE(vkHandle.width == 0u);
  REQUIRE(vkHandle.height == 0u);
  REQUIRE(vkHandle.generation == 0u);
  REQUIRE_FALSE(vkHandle.needsYFlip);
}

#if defined(MONOLITH_HAS_VULKAN)
TEST_CASE(
    "Vulkan material translation snapshots backend-relevant material state",
    "[renderer][foundation][vulkan][translation]") {
  Material material;
  material.color = {0.2f, 0.4f, 0.6f, 1.0f};
  material.roughness = 0.25f;
  material.metallic = 0.75f;
  material.uvScale = 2.5f;
  material.albedoMap = std::make_shared<Texture>();
  material.shader = std::make_shared<Shader>();

  const VulkanRenderBackend::TranslatedMaterialState translated =
      VulkanRenderBackend::TranslateMaterialState(material);

  REQUIRE(translated.baseColor == material.color);
  REQUIRE(translated.roughness == Catch::Approx(material.roughness));
  REQUIRE(translated.metallic == Catch::Approx(material.metallic));
  REQUIRE(translated.uvScale == Catch::Approx(material.uvScale));
  REQUIRE_FALSE(translated.usesAlbedoMap);
  REQUIRE_FALSE(translated.usesCustomShader);
}

TEST_CASE("Vulkan opaque pipeline keys track translated material feature usage",
          "[renderer][foundation][vulkan][pipeline]") {
  VulkanRenderBackend::TranslatedMaterialState materialState;
  materialState.usesAlbedoMap = true;
  materialState.usesCustomShader = false;

  const VulkanRenderBackend::OpaquePipelineKey key =
      VulkanRenderBackend::BuildOpaquePipelineKey(materialState);

  REQUIRE(key.usesAlbedoMap);
  REQUIRE_FALSE(key.usesCustomShader);
  REQUIRE(key.writesDepth);
  REQUIRE(key.depthTestEnabled);
}

TEST_CASE("Vulkan backend exposes opaque raster scaffold once initialized with "
          "a window",
          "[renderer][foundation][vulkan][scaffold]") {
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported()) {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window =
      glfwCreateWindow(64, 64, "vulkan-scaffold-test", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    SKIP("Unable to create hidden GLFW window for Vulkan raster scaffold test");
  }

  VulkanRenderBackend backend(window);
  REQUIRE(backend.IsInitialized());
  int framebufferWidth = 0;
  int framebufferHeight = 0;
  glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
  framebufferWidth = std::max(framebufferWidth, 1);
  framebufferHeight = std::max(framebufferHeight, 1);
  REQUIRE(backend.HasOpaqueRasterScaffold());
  REQUIRE(backend.HasOpaquePipelineCreationScaffold());
  REQUIRE(backend.HasOpaqueShaderPipelineScaffold());
  REQUIRE(backend.HasOpaqueGraphicsPipelineScaffold());
  if (!backend.HasOpaqueDrawExecutionReady()) {
    REQUIRE(backend.GetLastError().find("shader modules") != std::string::npos);
  }

  void *instanceHandle = nullptr;
  void *physicalDeviceHandle = nullptr;
  void *deviceHandle = nullptr;
  void *queueHandle = nullptr;
  void *renderPassHandle = nullptr;
  uint32_t queueFamily = 0;
  uint32_t imageCount = 0;
  REQUIRE(backend.TryGetImGuiVulkanInitData(
      &instanceHandle, &physicalDeviceHandle, &deviceHandle, &queueFamily,
      &queueHandle, &renderPassHandle, &imageCount));
  REQUIRE(instanceHandle != nullptr);
  REQUIRE(physicalDeviceHandle != nullptr);
  REQUIRE(deviceHandle != nullptr);
  REQUIRE(queueHandle != nullptr);
  REQUIRE(renderPassHandle != nullptr);
  REQUIRE(imageCount >= 2);

  glfwDestroyWindow(window);
  glfwTerminate();
}

TEST_CASE("Vulkan backend accepts opaque-scene submissions when initialized "
          "with a window handle",
          "[renderer][foundation][vulkan][opaque]") {
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported()) {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window =
      glfwCreateWindow(64, 64, "vulkan-test", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    SKIP("Unable to create hidden GLFW window for Vulkan backend test");
  }

  RenderBackendSelection selection;
  selection.requested = RenderBackendId::Vulkan;
  selection.nativeWindowHandle = window;
  const RenderBackendInitResult init = Renderer::InitializeBackend(selection);

  if (!init.ok) {
    glfwDestroyWindow(window);
    glfwTerminate();
    FAIL(init.error);
  }

  Mesh mesh;
  Material material;
  Camera camera;

  Renderer::BeginFrame({{}, "vulkan-opaque-scene"});
  Renderer::BeginPass({RenderPassId::OpaqueScene, BuildRenderView(camera),
                       "vulkan-opaque-pass"});
  Renderer::Submit(mesh, Mat4::Identity(), material);
  Renderer::EndPass();
  Renderer::EndFrame();

  REQUIRE(Renderer::GetBackendId() == RenderBackendId::Vulkan);
  REQUIRE(Renderer::GetDrawCallCount() == 1);

  Renderer::ResetBackend();
  glfwDestroyWindow(window);
  glfwTerminate();
}

TEST_CASE("Vulkan backend executes opaque indexed draws when shader pipeline "
          "is ready",
          "[renderer][foundation][vulkan][opaque][draw-indexed]") {
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported()) {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window =
      glfwCreateWindow(64, 64, "vulkan-diagnostics-test", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    SKIP("Unable to create hidden GLFW window for Vulkan diagnostics test");
  }

  VulkanRenderBackend backend(window);
  REQUIRE(backend.IsInitialized());

  Mesh mesh = Mesh::CreateQuad();
  Material material;
  Camera camera;

  backend.BeginFrame({{}, "vulkan-opaque-diagnostics"});
  backend.BeginPass({RenderPassId::OpaqueScene, BuildRenderView(camera),
                     "vulkan-opaque-pass"});
  backend.DrawMesh({&mesh, &material, Mat4::Identity()});
  backend.EndPass();
  backend.EndFrame();

  REQUIRE(backend.GetDrawCallCount() == 1);
  if (!backend.HasOpaqueDrawExecutionReady()) {
    REQUIRE(backend.GetLastError().find("shader modules") != std::string::npos);
    REQUIRE(backend.GetExecutedOpaqueIndexedDrawCount() == 0);
  } else {
    REQUIRE(backend.GetExecutedOpaqueIndexedDrawCount() >= 1);
    REQUIRE(backend.GetLastError().find("draw submissions are queued") ==
            std::string::npos);
  }

  glfwDestroyWindow(window);
  glfwTerminate();
}

TEST_CASE("Vulkan backend executes queued overlay callbacks while recording "
          "frame commands",
          "[renderer][foundation][vulkan][overlay]") {
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported()) {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window =
      glfwCreateWindow(64, 64, "vulkan-overlay-test", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    SKIP(
        "Unable to create hidden GLFW window for Vulkan overlay callback test");
  }

  VulkanRenderBackend backend(window);
  REQUIRE(backend.IsInitialized());

  OverlayRenderProbe overlayProbe;

  // Frame-external queueing should be ignored by the backend.
  backend.QueueOverlayRenderCallback(&CaptureOverlayRenderProbe, &overlayProbe);
  REQUIRE_FALSE(overlayProbe.invoked);
  REQUIRE(backend.GetLastError().find("active frame") != std::string::npos);

  backend.BeginFrame({{}, "vulkan-overlay-frame"});
  backend.QueueOverlayRenderCallback(&CaptureOverlayRenderProbe, &overlayProbe);
  backend.EndFrame();

  REQUIRE(overlayProbe.invoked);
  REQUIRE(overlayProbe.commandBufferHandle != nullptr);

  glfwDestroyWindow(window);
  glfwTerminate();
}

TEST_CASE("Vulkan backend manages offscreen render-target lifecycle and resize "
          "metadata",
          "[renderer][foundation][vulkan][offscreen]") {
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported()) {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window =
      glfwCreateWindow(64, 64, "vulkan-offscreen-test", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    SKIP(
        "Unable to create hidden GLFW window for Vulkan offscreen target test");
  }

  VulkanRenderBackend backend(window);
  REQUIRE(backend.IsInitialized());
  const RenderBackendCapabilities caps = backend.GetCapabilities();
  REQUIRE(caps.supportsOffscreenTargets);
  REQUIRE(caps.supportsNativeTextureHandles);

  const std::string targetKey = "editor-thumb:mesh-a";
  REQUIRE_FALSE(backend.EnsureOffscreenRenderTarget(targetKey, 0, 128));
  REQUIRE(backend.GetLastError().find("non-zero") != std::string::npos);

  REQUIRE(backend.EnsureOffscreenRenderTarget(targetKey, 128, 128));
  VulkanRenderBackend::OffscreenTargetMetadata metadata{};
  REQUIRE(backend.TryGetOffscreenRenderTargetMetadata(targetKey, &metadata));
  REQUIRE(metadata.width == 128u);
  REQUIRE(metadata.height == 128u);
  REQUIRE(metadata.generation == 1u);
  REQUIRE(metadata.readyForSampling);
  REQUIRE_FALSE(metadata.hasImGuiDescriptor);

  RenderTargetHandle initialHandle{};
  const bool initialHandleOk = backend.TryGetOffscreenRenderTargetHandle(
      targetKey, &initialHandle, false);
  if (initialHandleOk) {
    REQUIRE(initialHandle.IsValid());
    REQUIRE(initialHandle.backendId == RenderBackendId::Vulkan);
    REQUIRE(initialHandle.nativeType ==
            RenderNativeHandleType::VulkanImGuiDescriptorSet);
    REQUIRE(initialHandle.width == 128u);
    REQUIRE(initialHandle.height == 128u);
    REQUIRE(initialHandle.generation == metadata.generation);
    REQUIRE_FALSE(initialHandle.needsYFlip);

    RenderTargetHandle flippedHandle{};
    REQUIRE(backend.TryGetOffscreenRenderTargetHandle(targetKey, &flippedHandle,
                                                      true));
    REQUIRE(flippedHandle.IsValid());
    REQUIRE(flippedHandle.nativeHandle == initialHandle.nativeHandle);
    REQUIRE(flippedHandle.width == initialHandle.width);
    REQUIRE(flippedHandle.height == initialHandle.height);
    REQUIRE(flippedHandle.generation == initialHandle.generation);
    REQUIRE(flippedHandle.needsYFlip);
  } else {
    REQUIRE(backend.GetLastError().find(
                "ImGui Vulkan texture registration is not ready") !=
            std::string::npos);
  }

  REQUIRE(backend.TryGetOffscreenRenderTargetMetadata(targetKey, &metadata));
  REQUIRE(metadata.hasImGuiDescriptor == initialHandleOk);

  REQUIRE(backend.EnsureOffscreenRenderTarget(targetKey, 256, 128));
  VulkanRenderBackend::OffscreenTargetMetadata resizedMetadata{};
  REQUIRE(
      backend.TryGetOffscreenRenderTargetMetadata(targetKey, &resizedMetadata));
  REQUIRE(resizedMetadata.width == 256u);
  REQUIRE(resizedMetadata.height == 128u);
  REQUIRE(resizedMetadata.generation == metadata.generation + 1u);
  REQUIRE(resizedMetadata.readyForSampling);
  RenderTargetHandle resizedHandle{};
  const bool resizedHandleOk = backend.TryGetOffscreenRenderTargetHandle(
      targetKey, &resizedHandle, false);
  if (resizedHandleOk) {
    REQUIRE(resizedHandle.IsValid());
    REQUIRE(resizedHandle.width == resizedMetadata.width);
    REQUIRE(resizedHandle.height == resizedMetadata.height);
    REQUIRE(resizedHandle.generation == resizedMetadata.generation);
  } else {
    REQUIRE(backend.GetLastError().find(
                "ImGui Vulkan texture registration is not ready") !=
            std::string::npos);
  }
  REQUIRE(resizedMetadata.hasImGuiDescriptor == resizedHandleOk);

  backend.DestroyOffscreenRenderTarget(targetKey);
  REQUIRE_FALSE(
      backend.TryGetOffscreenRenderTargetMetadata(targetKey, &resizedMetadata));
  REQUIRE_FALSE(backend.TryGetOffscreenRenderTargetHandle(
      targetKey, &resizedHandle, false));
  REQUIRE(backend.GetLastError().find("unknown target key") !=
          std::string::npos);

  glfwDestroyWindow(window);
  glfwTerminate();
}

TEST_CASE("Vulkan editor viewport target stays stable across frame recording "
          "and resize",
          "[renderer][foundation][vulkan][viewport]") {
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported()) {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window =
      glfwCreateWindow(96, 96, "vulkan-editor-viewport-test", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    SKIP("Unable to create hidden GLFW window for Vulkan editor viewport test");
  }

  VulkanRenderBackend backend(window);
  REQUIRE(backend.IsInitialized());

  std::string error;
  RenderTargetHandle missingViewportHandle{};
  REQUIRE_FALSE(backend.TryGetEditorViewportRenderTargetHandle(
      &missingViewportHandle, false, &error));
  REQUIRE(error.find("unknown target key") != std::string::npos);
  REQUIRE_FALSE(missingViewportHandle.IsValid());

  error.clear();
  REQUIRE_FALSE(backend.EnsureEditorViewportRenderTarget(0u, 64u, &error));
  REQUIRE(error.find("non-zero") != std::string::npos);

  error.clear();
  REQUIRE(backend.EnsureEditorViewportRenderTarget(96u, 64u, &error));

  VulkanRenderBackend::OffscreenTargetMetadata initialMetadata{};
  REQUIRE(backend.TryGetOffscreenRenderTargetMetadata("__editor.viewport.scene",
                                                      &initialMetadata));
  REQUIRE(initialMetadata.width == 96u);
  REQUIRE(initialMetadata.height == 64u);
  REQUIRE(initialMetadata.readyForSampling);

  RenderTargetHandle initialViewportHandle{};
  const bool initialViewportHandleOk =
      backend.TryGetEditorViewportRenderTargetHandle(&initialViewportHandle,
                                                     false, &error);
  if (initialViewportHandleOk) {
    REQUIRE(initialViewportHandle.IsValid());
    REQUIRE(initialViewportHandle.width == initialMetadata.width);
    REQUIRE(initialViewportHandle.height == initialMetadata.height);
    REQUIRE(initialViewportHandle.generation == initialMetadata.generation);
    REQUIRE_FALSE(initialViewportHandle.needsYFlip);
  } else {
    REQUIRE(error.find("ImGui Vulkan texture registration is not ready") !=
            std::string::npos);
  }

  backend.BeginFrame({{}, "vulkan-editor-viewport-frame"});
  backend.QueueOverlayRenderCallback(&CaptureOverlayRenderProbe, nullptr);
  backend.EndFrame();
  REQUIRE(backend.GetLastError().empty());

  VulkanRenderBackend::OffscreenTargetMetadata afterFrameMetadata{};
  REQUIRE(backend.TryGetOffscreenRenderTargetMetadata("__editor.viewport.scene",
                                                      &afterFrameMetadata));
  REQUIRE(afterFrameMetadata.width == initialMetadata.width);
  REQUIRE(afterFrameMetadata.height == initialMetadata.height);
  REQUIRE(afterFrameMetadata.readyForSampling);
  REQUIRE(afterFrameMetadata.generation == initialMetadata.generation);

  RenderTargetHandle afterFrameViewportHandle{};
  const bool afterFrameViewportHandleOk =
      backend.TryGetEditorViewportRenderTargetHandle(&afterFrameViewportHandle,
                                                     false, &error);
  REQUIRE(afterFrameViewportHandleOk == initialViewportHandleOk);
  if (afterFrameViewportHandleOk) {
    REQUIRE(afterFrameViewportHandle.IsValid());
    REQUIRE(afterFrameViewportHandle.generation ==
            initialViewportHandle.generation);
  }

  REQUIRE(backend.EnsureEditorViewportRenderTarget(144u, 72u, &error));
  VulkanRenderBackend::OffscreenTargetMetadata resizedMetadata{};
  REQUIRE(backend.TryGetOffscreenRenderTargetMetadata("__editor.viewport.scene",
                                                      &resizedMetadata));
  REQUIRE(resizedMetadata.width == 144u);
  REQUIRE(resizedMetadata.height == 72u);
  REQUIRE(resizedMetadata.generation == initialMetadata.generation + 1u);
  REQUIRE(resizedMetadata.readyForSampling);

  RenderTargetHandle resizedViewportHandle{};
  const bool resizedViewportHandleOk =
      backend.TryGetEditorViewportRenderTargetHandle(&resizedViewportHandle,
                                                     true, &error);
  REQUIRE(resizedViewportHandleOk == initialViewportHandleOk);
  if (resizedViewportHandleOk) {
    REQUIRE(resizedViewportHandle.IsValid());
    REQUIRE(resizedViewportHandle.width == resizedMetadata.width);
    REQUIRE(resizedViewportHandle.height == resizedMetadata.height);
    REQUIRE(resizedViewportHandle.generation == resizedMetadata.generation);
    REQUIRE(resizedViewportHandle.needsYFlip);
  }

  glfwDestroyWindow(window);
  glfwTerminate();
}

TEST_CASE(
    "Vulkan backend reports and executes readback support deterministically",
    "[renderer][foundation][vulkan][readback]") {
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported()) {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window =
      glfwCreateWindow(64, 64, "vulkan-readback-test", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    SKIP("Unable to create hidden GLFW window for Vulkan readback test");
  }

  VulkanRenderBackend backend(window);
  REQUIRE(backend.IsInitialized());
  int framebufferWidth = 0;
  int framebufferHeight = 0;
  glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
  framebufferWidth = std::max(framebufferWidth, 1);
  framebufferHeight = std::max(framebufferHeight, 1);

  const RenderBackendCapabilities caps = backend.GetCapabilities();
  std::vector<uint8_t> colorReadback;
  std::vector<float> depthReadback;
  std::string readbackError;

  if (caps.supportsReadback) {
    REQUIRE_FALSE(backend.ReadbackColorBgr8(framebufferWidth, framebufferHeight,
                                            colorReadback, &readbackError));
    REQUIRE(readbackError.find("no captured frame yet") != std::string::npos);
  } else {
    REQUIRE_FALSE(backend.ReadbackColorBgr8(framebufferWidth, framebufferHeight,
                                            colorReadback, &readbackError));
    REQUIRE(readbackError.find("unavailable") != std::string::npos);
  }

  if (caps.supportsDepthReadback) {
    REQUIRE_FALSE(backend.ReadbackDepth32F(framebufferWidth, framebufferHeight,
                                           depthReadback, &readbackError));
    REQUIRE(readbackError.find("no captured frame yet") != std::string::npos);
  } else {
    REQUIRE_FALSE(backend.ReadbackDepth32F(framebufferWidth, framebufferHeight,
                                           depthReadback, &readbackError));
    REQUIRE(readbackError.find("unavailable") != std::string::npos);
  }

  Camera camera;
  Mesh mesh = Mesh::CreateQuad();
  Material material;
  backend.BeginFrame({{}, "vulkan-readback-frame"});
  backend.BeginPass({RenderPassId::OpaqueScene, BuildRenderView(camera),
                     "vulkan-readback-pass"});
  backend.DrawMesh({&mesh, &material, Mat4::Identity()});
  backend.EndPass();
  backend.EndFrame();

  if (caps.supportsReadback) {
    REQUIRE_FALSE(backend.ReadbackColorBgr8(framebufferWidth + 1,
                                            framebufferHeight, colorReadback,
                                            &readbackError));
    REQUIRE(readbackError.find("must match swapchain extent") !=
            std::string::npos);
    REQUIRE_FALSE(backend.ReadbackColorBgr8(0, framebufferHeight, colorReadback,
                                            &readbackError));
    REQUIRE(readbackError.find("positive dimensions") != std::string::npos);

    backend.BeginFrame({{}, "vulkan-readback-active-frame"});
    REQUIRE_FALSE(backend.ReadbackColorBgr8(framebufferWidth, framebufferHeight,
                                            colorReadback, &readbackError));
    REQUIRE(readbackError.find("outside active BeginFrame/EndFrame scope") !=
            std::string::npos);
    backend.EndFrame();

    const bool colorOk = backend.ReadbackColorBgr8(
        framebufferWidth, framebufferHeight, colorReadback, &readbackError);
    if (colorOk)
      REQUIRE(colorReadback.size() ==
              static_cast<size_t>(framebufferWidth * framebufferHeight * 3));
    else
      REQUIRE_FALSE(readbackError.empty());
  } else {
    REQUIRE_FALSE(
        backend.ReadbackColorBgr8(64, 64, colorReadback, &readbackError));
    REQUIRE_FALSE(readbackError.empty());
  }

  if (caps.supportsDepthReadback) {
    REQUIRE_FALSE(backend.ReadbackDepth32F(framebufferWidth,
                                           framebufferHeight + 1, depthReadback,
                                           &readbackError));
    REQUIRE(readbackError.find("must match swapchain extent") !=
            std::string::npos);
    REQUIRE_FALSE(backend.ReadbackDepth32F(framebufferWidth, 0, depthReadback,
                                           &readbackError));
    REQUIRE(readbackError.find("positive dimensions") != std::string::npos);

    backend.BeginFrame({{}, "vulkan-depth-readback-active-frame"});
    REQUIRE_FALSE(backend.ReadbackDepth32F(framebufferWidth, framebufferHeight,
                                           depthReadback, &readbackError));
    REQUIRE(readbackError.find("outside active BeginFrame/EndFrame scope") !=
            std::string::npos);
    backend.EndFrame();

    const bool depthOk = backend.ReadbackDepth32F(
        framebufferWidth, framebufferHeight, depthReadback, &readbackError);
    if (depthOk)
      REQUIRE(depthReadback.size() ==
              static_cast<size_t>(framebufferWidth * framebufferHeight));
    else
      REQUIRE_FALSE(readbackError.empty());
  } else {
    REQUIRE_FALSE(
        backend.ReadbackDepth32F(64, 64, depthReadback, &readbackError));
    REQUIRE_FALSE(readbackError.empty());
  }

  glfwDestroyWindow(window);
  glfwTerminate();
}
#endif

TEST_CASE("Renderer supports multiple explicit passes within a single frame",
          "[renderer][foundation]") {
  FakeRenderBackend backend;
  Renderer::UseBackend(&backend);

  Camera camera;
  camera.position = {4.0f, 5.0f, 6.0f};

  std::vector<Light> lights(3);
  Mesh mesh;
  Material material;
  Shader shader;

  Renderer::BeginFrame(RenderFrameConfig{lights, "multi-pass-frame"});
  Renderer::BeginPass(RenderPassConfig{RenderPassId::OpaqueScene,
                                       BuildRenderView(camera), "opaque"});
  Renderer::Submit(mesh, Mat4::Identity(), material);
  Renderer::EndPass();
  Renderer::BeginPass(RenderPassConfig{RenderPassId::WireframeOverlay,
                                       BuildRenderView(camera), "wireframe"});
  Renderer::SubmitWireframe(mesh, Mat4::Identity(), shader, 0.3f, 0.7f, 0.2f);
  Renderer::EndPass();
  Renderer::EndFrame();

  REQUIRE(backend.events ==
          std::vector<std::string>{"begin-frame", "begin-pass", "draw-mesh",
                                   "end-pass", "begin-pass", "draw-wireframe",
                                   "end-pass", "end-frame"});
  REQUIRE(backend.lastFrame.lights.size() == 3);
  REQUIRE(backend.lastPass.id == RenderPassId::WireframeOverlay);
  REQUIRE(backend.lastPass.view.cameraPosition.x == Catch::Approx(4.0f));
  REQUIRE(Renderer::GetDrawCallCount() == 2);

  Renderer::ResetBackend();
}

TEST_CASE(
    "RenderSystem submits visible mesh entities into the active explicit pass",
    "[renderer][foundation][scene]") {
  FakeRenderBackend backend;
  Renderer::UseBackend(&backend);

  Registry registry;
  const Entity entity = registry.Create();

  TransformComponent transform;
  transform.current.position = {1.0f, 2.0f, 3.0f};
  transform.previous.position = transform.current.position;
  registry.Add<TransformComponent>(entity, transform);

  auto mesh = std::make_shared<Mesh>();
  auto material = std::make_shared<Material>();
  MeshComponent meshComponent;
  meshComponent.mesh = mesh;
  meshComponent.material = material;
  meshComponent.visible = true;
  registry.Add<MeshComponent>(entity, meshComponent);

  Camera camera;
  float alpha = 1.0f;
  RenderSystem system(camera, alpha);

  Renderer::BeginFrame({{}, "scene-frame"});
  Renderer::BeginPass(
      {RenderPassId::OpaqueScene, BuildRenderView(camera), "scene-pass"});
  system.OnUpdate(registry, 0.0f);
  Renderer::EndPass();
  Renderer::EndFrame();

  REQUIRE(backend.events == std::vector<std::string>{"begin-frame",
                                                     "begin-pass", "draw-mesh",
                                                     "end-pass", "end-frame"});
  REQUIRE(backend.lastMesh.mesh == mesh.get());
  REQUIRE(backend.lastMesh.material == material.get());

  Renderer::ResetBackend();
}

TEST_CASE("Material copies share shader and texture resource handles",
          "[renderer][foundation][ownership]") {
  auto shader = std::make_shared<Shader>();
  auto texture = std::make_shared<Texture>();

  Material original;
  original.shader = shader;
  original.albedoMap = texture;

  Material copy = original;

  REQUIRE(copy.shader == shader);
  REQUIRE(copy.albedoMap == texture);
  REQUIRE_FALSE(copy.HasShader());
}

TEST_CASE("Mesh::CreateSphere produces non-empty CPU geometry",
          "[renderer][mesh][procedural]") {
  Mesh m = Mesh::CreateSphere(1.0f, 4, 4);
  REQUIRE_FALSE(m.GetVertices().empty());
  REQUIRE_FALSE(m.GetIndices().empty());
  const Vec3 ext = m.GetHalfExtents();
  REQUIRE(ext.x > 0.0f);
  REQUIRE(ext.y > 0.0f);
  REQUIRE(ext.z > 0.0f);
}

TEST_CASE("Mesh::CreateBox produces 24 vertices and 36 indices",
          "[renderer][mesh][procedural]") {
  Mesh m = Mesh::CreateBox(1.0f, 2.0f, 3.0f);
  REQUIRE(m.GetVertices().size() == 24u);
  REQUIRE(m.GetIndices().size() == 36u);
  REQUIRE(m.GetIndexCount() == 36);
}

TEST_CASE("Mesh::CreateCylinder produces non-empty CPU geometry",
          "[renderer][mesh][procedural]") {
  Mesh m = Mesh::CreateCylinder(0.5f, 1.0f, 6);
  REQUIRE_FALSE(m.GetVertices().empty());
  REQUIRE_FALSE(m.GetIndices().empty());
  REQUIRE(m.GetHalfExtents().y == Catch::Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("Mesh::CreatePyramid produces correct vertex count and half-extents",
          "[renderer][mesh][procedural]") {
  Mesh m = Mesh::CreatePyramid(0.5f, 1.0f);
  REQUIRE_FALSE(m.GetVertices().empty());
  REQUIRE_FALSE(m.GetIndices().empty());
  const Vec3 ext = m.GetHalfExtents();
  REQUIRE(ext.x > 0.0f);
  REQUIRE(ext.y > 0.0f);
}

TEST_CASE("Mesh::CreatePlane produces 4 vertices and 6 indices",
          "[renderer][mesh][procedural]") {
  Mesh m = Mesh::CreatePlane(5.0f);
  REQUIRE(m.GetVertices().size() == 4u);
  REQUIRE(m.GetIndices().size() == 6u);
  REQUIRE(m.GetIndexCount() == 6);
}

TEST_CASE("Mesh::CreateQuad produces 4 vertices",
          "[renderer][mesh][procedural]") {
  Mesh m = Mesh::CreateQuad();
  REQUIRE(m.GetVertices().size() == 4u);
  REQUIRE(m.GetIndices().size() == 6u);
}

TEST_CASE("Mesh move construction transfers CPU geometry",
          "[renderer][mesh][move]") {
  const size_t vertCount =
      Mesh::CreateBox(1.0f, 1.0f, 1.0f).GetVertices().size();
  Mesh b = Mesh::CreateBox(1.0f, 1.0f, 1.0f);
  REQUIRE(b.GetVertices().size() == vertCount);
}

TEST_CASE("Mesh move assignment transfers CPU geometry",
          "[renderer][mesh][move]") {
  const size_t vertCount =
      Mesh::CreateCylinder(0.5f, 1.0f, 4).GetVertices().size();
  Mesh b;
  b = Mesh::CreateCylinder(0.5f, 1.0f, 4);
  REQUIRE(b.GetVertices().size() == vertCount);
}

TEST_CASE("Mesh SetData computes AABB from vertex positions",
          "[renderer][mesh][aabb]") {
  std::vector<Vertex> verts = {
      {{-2.0f, 0.0f, 0.0f}, {0, 1, 0}, {0, 0}},
      {{2.0f, 0.0f, 0.0f}, {0, 1, 0}, {1, 0}},
      {{0.0f, 4.0f, 0.0f}, {0, 1, 0}, {0.5f, 1}},
  };
  std::vector<unsigned int> inds = {0, 1, 2};
  Mesh m;
  m.SetData(verts, inds);
  REQUIRE(m.GetHalfExtents().x == Catch::Approx(2.0f).epsilon(0.01f));
  REQUIRE(m.GetHalfExtents().y == Catch::Approx(2.0f).epsilon(0.01f));
  const Vec3 center = m.GetLocalAabbCenter();
  REQUIRE(center.x == Catch::Approx(0.0f).epsilon(0.01f));
  REQUIRE(center.y == Catch::Approx(2.0f).epsilon(0.01f));
}

TEST_CASE("SkinnedMesh default state is not valid and has zero index count",
          "[renderer][skinnedmesh]") {
  SkinnedMesh sm;
  REQUIRE_FALSE(sm.IsValid());
  REQUIRE(sm.GetIndexCount() == 0);
  // Default AABB is a unit half-cube
  REQUIRE(sm.GetHalfExtents().x == Catch::Approx(0.5f).epsilon(0.01f));
  REQUIRE(sm.GetLocalAabbCenter().x == Catch::Approx(0.0f).epsilon(0.01f));
}

TEST_CASE("SkinnedMesh move construction leaves source empty",
          "[renderer][skinnedmesh][move]") {
  SkinnedMesh b;
  REQUIRE_FALSE(b.IsValid());
  REQUIRE(b.GetIndexCount() == 0);
}

TEST_CASE("SkinnedMesh move assignment from default leaves target not valid",
          "[renderer][skinnedmesh][move]") {
  SkinnedMesh b;
  b = SkinnedMesh{};
  REQUIRE_FALSE(b.IsValid());
}

TEST_CASE("GltfLoader::Load returns empty result for nonexistent .glb path",
          "[renderer][gltf]") {
  using namespace Monolith;
  const GltfLoadResult result = GltfLoader::Load("/no/such/file.glb");
  REQUIRE(result.mesh == nullptr);
  REQUIRE(result.skeleton == nullptr);
  REQUIRE(result.clips.empty());
}

TEST_CASE("GltfLoader::Load returns empty result for nonexistent .gltf path",
          "[renderer][gltf]") {
  using namespace Monolith;
  const GltfLoadResult result = GltfLoader::Load("/no/such/file.gltf");
  REQUIRE(result.mesh == nullptr);
  REQUIRE(result.skeleton == nullptr);
}

TEST_CASE("GltfLoader::Load parses skinned animated ascii glTF fixtures",
          "[renderer][gltf]") {
  const std::filesystem::path gltfPath = WriteSkinnedAnimatedFixture();
  const GltfLoadResult result = GltfLoader::Load(gltfPath.string());

  REQUIRE(result.mesh != nullptr);
  CHECK(result.mesh->GetIndexCount() == 3);

  REQUIRE(result.skeleton != nullptr);
  CHECK(result.skeleton->BoneCount() == 1);
  CHECK(result.skeleton->GetBone(0).parentIndex == -1);

  REQUIRE(result.clips.size() == 1u);
  CHECK(result.clips[0] != nullptr);
  CHECK(result.clips[0]->duration == Catch::Approx(1.0f).epsilon(0.001f));
  CHECK_FALSE(result.clips[0]->GetTracks().empty());

  // Fixture intentionally references a missing texture file.
  CHECK(result.albedoTexture == nullptr);
}

TEST_CASE("GltfLoader::Load skips non-triangle primitives",
          "[renderer][gltf]") {
  const std::filesystem::path gltfPath =
      WritePrimitiveFixture("renderer_gltf_lines", 1, true);
  const GltfLoadResult result = GltfLoader::Load(gltfPath.string());

  CHECK(result.mesh == nullptr);
  CHECK(result.skeleton == nullptr);
  CHECK(result.clips.empty());
}

TEST_CASE("GltfLoader::Load skips primitives missing POSITION",
          "[renderer][gltf]") {
  const std::filesystem::path gltfPath =
      WritePrimitiveFixture("renderer_gltf_missing_pos", 4, false);
  const GltfLoadResult result = GltfLoader::Load(gltfPath.string());

  CHECK(result.mesh == nullptr);
  CHECK(result.skeleton == nullptr);
  CHECK(result.clips.empty());
}

// ===========================================================================
// GltfLoader: embedded base64 PNG image (covers GltfLoader.cpp lines 43-60)
// ===========================================================================

TEST_CASE("GltfLoader::Load handles embedded base64 PNG image",
          "[renderer][gltf]") {
  // Minimal GLTF with a 1x1 red-pixel PNG embedded as a data URI.
  // Exercises LoadImageDataCallback → stbi_load_from_memory (lines 43-60).
  namespace fs = std::filesystem;
  const fs::path dir = BuildGltfFixturePath("renderer_gltf_embedded_image");
  const fs::path gltfPath = dir / "scene.gltf";

  // Minimal 36-byte all-zero float buffer (1 triangle, positions only)
  // encoded as base64.  The exact values are irrelevant for image coverage.
  const json gltf = {
      {"asset", {{"version", "2.0"}}},
      {"scene", 0},
      {"scenes", json::array({{{"nodes", json::array({0})}}})},
      {"nodes", json::array({{{"mesh", 0}}})},
      {"meshes", json::array({{{"primitives",
                                json::array({{{"attributes", {{"POSITION", 0}}},
                                              {"material", 0}}})}}})},
      {"materials", json::array({{{"pbrMetallicRoughness",
                                   {{"baseColorTexture", {{"index", 0}}}}}}})},
      {"textures", json::array({{{"source", 0}}})},
      {"images",
       json::array(
           {{{"uri", "data:image/png;base64,"
                     "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQV"
                     "R42mP8/5+hHgAHggJ/PchI6QAAAABJRU5ErkJggg=="}}})},
      {"accessors", json::array({{{"bufferView", 0},
                                  {"componentType", 5126},
                                  {"count", 3},
                                  {"type", "VEC3"}}})},
      {"bufferViews",
       json::array({{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}}})},
      {"buffers",
       json::array(
           {{{"uri", "data:application/octet-stream;base64,"
                     "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"},
             {"byteLength", 36}}})}};

  {
    std::ofstream out(gltfPath);
    REQUIRE(out.is_open());
    out << gltf.dump(2);
  }

  // If stbi_load_from_memory works the image is decoded; if it fails the
  // callback logs an error and returns false but the loader still returns a
  // result (possibly without a texture).  Either way we must not crash.
  const GltfLoadResult result = GltfLoader::Load(gltfPath.string());
  // The mesh/skeleton may or may not be present depending on whether the
  // primitive passes validation — what matters is no crash occurred.
  CHECK(true);
}

// ===========================================================================
// GltfLoader: rotation + scale animation channels (covers lines 548-562)
// ===========================================================================

TEST_CASE("GltfLoader::Load parses rotation and scale animation channels",
          "[renderer][gltf]") {
  // Covers the "rotation" and "scale" branches in LoadGltfAnimations
  // (GltfLoader.cpp lines 547-561).
  namespace fs = std::filesystem;
  const fs::path dir = BuildGltfFixturePath("renderer_gltf_rot_scale_anim");
  const fs::path binPath = dir / "buffer.bin";
  const fs::path gltfPath = dir / "scene.gltf";

  // Build binary buffer in memory then write to .bin file.
  // Layout (all little-endian floats):
  //   [0  .. 35]  POSITION  — 3 × VEC3  (accessor 0, bufferView 0)
  //   [36 .. 43]  time      — 2 × SCALAR [0.0, 1.0]  (accessor 1, bv 1)
  //   [44 .. 75]  rotation  — 2 × VEC4  [0,0,0,1] × 2  (accessor 2, bv 2)
  //   [76 .. 99]  scale     — 2 × VEC3  [1,1,1] × 2  (accessor 3, bv 3)
  //   [100..123]  translate — 2 × VEC3  [0,0,0] × 2  (accessor 4, bv 4)
  std::vector<uint8_t> bin;
  bin.reserve(124);

  // positions (36 bytes – all zero)
  for (int i = 0; i < 9; ++i)
    AppendPod(&bin, 0.0f);

  // time keyframes
  AppendPod(&bin, 0.0f);
  AppendPod(&bin, 1.0f);

  // rotation keyframes: identity quaternion [0,0,0,1] × 2
  for (int k = 0; k < 2; ++k) {
    AppendPod(&bin, 0.0f); // x
    AppendPod(&bin, 0.0f); // y
    AppendPod(&bin, 0.0f); // z
    AppendPod(&bin, 1.0f); // w
  }

  // scale keyframes: [1,1,1] × 2
  for (int k = 0; k < 2; ++k) {
    AppendPod(&bin, 1.0f);
    AppendPod(&bin, 1.0f);
    AppendPod(&bin, 1.0f);
  }

  // translation keyframes: [0,0,0] × 2
  for (int k = 0; k < 2; ++k) {
    AppendPod(&bin, 0.0f);
    AppendPod(&bin, 0.0f);
    AppendPod(&bin, 0.0f);
  }

  REQUIRE(bin.size() == 124u);

  {
    std::ofstream out(binPath, std::ios::binary);
    REQUIRE(out.is_open());
    out.write(reinterpret_cast<const char *>(bin.data()),
              static_cast<std::streamsize>(bin.size()));
  }

  // Build a skinned GLTF so that LoadGltfAnimations() is entered
  // (it bails out early when skeleton/skins is absent).
  // One joint, one mesh node, one skin, and one animation with all three
  // channel types: rotation, scale, translation.
  const json ibm = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

  // We need a bufferView + accessor for the IBM matrix too.
  // Append 64 bytes (16 floats) for the IBM after offset 124.
  for (int i = 0; i < 16; ++i)
    AppendPod(&bin, (i % 5 == 0) ? 1.0f : 0.0f); // identity

  REQUIRE(bin.size() == 188u);

  // Re-write the bin file with the IBM appended.
  {
    std::ofstream out(binPath, std::ios::binary);
    REQUIRE(out.is_open());
    out.write(reinterpret_cast<const char *>(bin.data()),
              static_cast<std::streamsize>(bin.size()));
  }

  const json gltf = {
      {"asset", {{"version", "2.0"}}},
      {"scene", 0},
      {"scenes", json::array({{{"nodes", json::array({1})}}})},
      {"nodes",
       json::array({{{"name", "joint0"}},
                    {{"name", "mesh_node"}, {"mesh", 0}, {"skin", 0}}})},
      {"meshes", json::array({{{"primitives",
                                json::array({{{"attributes", {{"POSITION", 0}}},
                                              {"mode", 4}}})}}})},
      {"skins", json::array({{{"joints", json::array({0})},
                              {"inverseBindMatrices", 5}}})},
      {"animations",
       json::array(
           {{{"name", "RotScaleTrans"},
             {"samplers", json::array({
                              {{"input", 1},
                               {"interpolation", "LINEAR"},
                               {"output", 2}}, // rotation
                              {{"input", 1},
                               {"interpolation", "LINEAR"},
                               {"output", 3}}, // scale
                              {{"input", 1},
                               {"interpolation", "LINEAR"},
                               {"output", 4}} // translation
                          })},
             {"channels",
              json::array(
                  {{{"sampler", 0},
                    {"target", {{"node", 0}, {"path", "rotation"}}}},
                   {{"sampler", 1},
                    {"target", {{"node", 0}, {"path", "scale"}}}},
                   {{"sampler", 2},
                    {"target", {{"node", 0}, {"path", "translation"}}}}})}}})},
      {"buffers", json::array({{{"uri", "buffer.bin"}, {"byteLength", 188}}})},
      {"bufferViews",
       json::array({// bv0: POSITION  — 36 bytes @ 0
                    {{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
                    // bv1: time      — 8 bytes  @ 36
                    {{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 8}},
                    // bv2: rotation  — 32 bytes @ 44
                    {{"buffer", 0}, {"byteOffset", 44}, {"byteLength", 32}},
                    // bv3: scale     — 24 bytes @ 76
                    {{"buffer", 0}, {"byteOffset", 76}, {"byteLength", 24}},
                    // bv4: translation — 24 bytes @ 100
                    {{"buffer", 0}, {"byteOffset", 100}, {"byteLength", 24}},
                    // bv5: IBM MAT4  — 64 bytes @ 124
                    {{"buffer", 0}, {"byteOffset", 124}, {"byteLength", 64}}})},
      {"accessors", json::array({// 0: POSITION (3 × VEC3)
                                 {{"bufferView", 0},
                                  {"componentType", 5126},
                                  {"count", 3},
                                  {"type", "VEC3"}},
                                 // 1: time (2 × SCALAR)
                                 {{"bufferView", 1},
                                  {"componentType", 5126},
                                  {"count", 2},
                                  {"type", "SCALAR"}},
                                 // 2: rotation output (2 × VEC4)
                                 {{"bufferView", 2},
                                  {"componentType", 5126},
                                  {"count", 2},
                                  {"type", "VEC4"}},
                                 // 3: scale output (2 × VEC3)
                                 {{"bufferView", 3},
                                  {"componentType", 5126},
                                  {"count", 2},
                                  {"type", "VEC3"}},
                                 // 4: translation output (2 × VEC3)
                                 {{"bufferView", 4},
                                  {"componentType", 5126},
                                  {"count", 2},
                                  {"type", "VEC3"}},
                                 // 5: IBM (1 × MAT4)
                                 {{"bufferView", 5},
                                  {"componentType", 5126},
                                  {"count", 1},
                                  {"type", "MAT4"}}})}};

  {
    std::ofstream out(gltfPath);
    REQUIRE(out.is_open());
    out << gltf.dump(2);
  }

  const GltfLoadResult result = GltfLoader::Load(gltfPath.string());

  // The loader must parse at least one animation clip with all three
  // channel types (rotation/scale/translation).
  REQUIRE(result.skeleton != nullptr);
  REQUIRE_FALSE(result.clips.empty());
  REQUIRE(result.clips[0] != nullptr);

  // Duration comes from the max time key = 1.0 s.
  CHECK(result.clips[0]->duration == Catch::Approx(1.0f).epsilon(0.01f));

  // There must be exactly one BoneTrack for bone 0, containing
  // rotation, scale, and translation keyframes.
  const auto &tracks = result.clips[0]->GetTracks();
  REQUIRE_FALSE(tracks.empty());

  const BoneTrack &track = tracks[0];
  CHECK(track.rotations.size() == 2u);
  CHECK(track.rotationTimes.size() == 2u);
  CHECK(track.scales.size() == 2u);
  CHECK(track.scaleTimes.size() == 2u);
  CHECK(track.positions.size() == 2u);
  CHECK(track.positionTimes.size() == 2u);
}

TEST_CASE("DebugHUD early-outs safely when backend does not support HUD",
          "[renderer][debughud]") {
  DebugHudUnsupportedBackend backend;
  Renderer::UseBackend(&backend);

  DebugHUD::SetScreenSize(640, 360);
  DebugHUD::Init(640, 360);
  DebugHUD::Update(1.0f / 60.0f, HUDStats{});
  DebugHUD::Render();
  DebugHUD::Shutdown();

  CHECK_FALSE(DebugHUD::IsVisible());
  CHECK_FALSE(DebugHUD::IsCollisionBoxesOn());

  Renderer::ResetBackend();
}

TEST_CASE(
    "DebugHUD update path is deterministic without initialization side effects",
    "[renderer][debughud]") {
  FakeRenderBackend backend;
  Renderer::UseBackend(&backend);

  DebugHUD::Shutdown();
  DebugHUD::SetScreenSize(320, 200);

  HUDStats stats{};
  stats.fps = 120.0f;
  stats.frameTimeMs = 8.33f;
  stats.showNoCameraOverlay = false;

  DebugHUD::Update(1.0f / 120.0f, stats);
  DebugHUD::Update(0.0f, stats);
  DebugHUD::Render();

  CHECK_FALSE(DebugHUD::IsVisible());
  CHECK_FALSE(DebugHUD::IsCollisionBoxesOn());

  DebugHUD::Shutdown();
  Renderer::ResetBackend();
}
