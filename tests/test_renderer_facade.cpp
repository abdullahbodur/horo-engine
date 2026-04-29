// test_renderer_facade.cpp
//
// Unit tests for renderer/Renderer.cpp (the static facade) and
// renderer/RenderTargetHandle.h — no OpenGL context required.
//
// All Renderer tests wire in a NullRenderBackend via Renderer::UseBackend so
// that no GL calls are made, then restore the default backend on scope exit.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "renderer/IRenderBackend.h"
#include "renderer/RenderBackend.h"
#include "renderer/RenderTargetHandle.h"
#include "renderer/Renderer.h"
#include "renderer/null/NullRenderBackend.h"

using namespace Horo;

// RAII guard: installs a NullRenderBackend and restores the default on destruction.
struct NullBackendScope {
    NullRenderBackend backend;
    NullBackendScope()  { Renderer::UseBackend(&backend); }
    ~NullBackendScope() { Renderer::UseBackend(nullptr);  }
};

// ===========================================================================
// RenderTargetHandle — pure-data, no GPU
// ===========================================================================

TEST_CASE("RenderTargetHandle: default-constructed handle is invalid", "[renderer][facade][rth]") {
    RenderTargetHandle h;
    CHECK_FALSE(h.IsValid());
    CHECK(h.nativeHandle == 0u);
    CHECK(h.nativeType   == RenderNativeHandleType::None);
    CHECK(h.width        == 0u);
    CHECK(h.height       == 0u);
}

TEST_CASE("RenderTargetHandle: OpenGLTexture factory sets all fields", "[renderer][facade][rth]") {
    auto h = RenderTargetHandle::OpenGLTexture(42u, true, 800u, 600u, 7u);
    CHECK(h.IsValid());
    CHECK(h.backendId    == RenderBackendId::OpenGL);
    CHECK(h.nativeType   == RenderNativeHandleType::OpenGLTexture2D);
    CHECK(h.nativeHandle == 42u);
    CHECK(h.width        == 800u);
    CHECK(h.height       == 600u);
    CHECK(h.generation   == 7u);
    CHECK(h.needsYFlip   == true);
}

TEST_CASE("RenderTargetHandle: OpenGLTexture with id=0 is invalid", "[renderer][facade][rth]") {
    CHECK_FALSE(RenderTargetHandle::OpenGLTexture(0u).IsValid());
}

TEST_CASE("RenderTargetHandle: OpenGLTexture defaults — no y-flip, zero dimensions", "[renderer][facade][rth]") {
    auto h = RenderTargetHandle::OpenGLTexture(1u);
    CHECK(h.needsYFlip == false);
    CHECK(h.width      == 0u);
    CHECK(h.height     == 0u);
    CHECK(h.generation == 0u);
}

TEST_CASE("RenderTargetHandle: VulkanDescriptorSet factory sets all fields", "[renderer][facade][rth]") {
    auto h = RenderTargetHandle::VulkanDescriptorSet(0xBEEFu, false, 1920u, 1080u, 3u);
    CHECK(h.IsValid());
    CHECK(h.backendId    == RenderBackendId::Vulkan);
    CHECK(h.nativeType   == RenderNativeHandleType::VulkanImGuiDescriptorSet);
    CHECK(h.nativeHandle == 0xBEEFu);
    CHECK(h.width        == 1920u);
    CHECK(h.height       == 1080u);
    CHECK(h.generation   == 3u);
    CHECK(h.needsYFlip   == false);
}

// ===========================================================================
// Renderer::UseBackend / GetBackendId
// ===========================================================================

TEST_CASE("Renderer::UseBackend: installs external backend (GetBackendId delegates)", "[renderer][facade]") {
    NullRenderBackend nb;
    Renderer::UseBackend(&nb);
    // NullRenderBackend::GetBackendId() returns Auto
    CHECK(Renderer::GetBackendId() == RenderBackendId::Auto);
    Renderer::UseBackend(nullptr);
}

TEST_CASE("Renderer::UseBackend: nullptr resets to default OpenGL backend", "[renderer][facade]") {
    NullRenderBackend nb;
    Renderer::UseBackend(&nb);
    Renderer::UseBackend(nullptr);
    CHECK(Renderer::GetBackendId() == RenderBackendId::OpenGL);
}

TEST_CASE("Renderer::IsBackendSupported: OpenGL is always supported", "[renderer][facade]") {
    CHECK(Renderer::IsBackendSupported(RenderBackendId::OpenGL));
}

// ===========================================================================
// Frame / pass lifecycle
// ===========================================================================

TEST_CASE("Renderer: IsFrameActive and IsPassActive are false at startup", "[renderer][facade][lifecycle]") {
    NullBackendScope scope;
    CHECK_FALSE(Renderer::IsFrameActive());
    CHECK_FALSE(Renderer::IsPassActive());
}

TEST_CASE("Renderer: BeginFrame activates frame; EndFrame deactivates it", "[renderer][facade][lifecycle]") {
    NullBackendScope scope;
    Renderer::BeginFrame({});
    CHECK(Renderer::IsFrameActive());
    CHECK_FALSE(Renderer::IsPassActive());
    Renderer::EndFrame();
    CHECK_FALSE(Renderer::IsFrameActive());
}

TEST_CASE("Renderer: BeginPass requires active frame and activates pass", "[renderer][facade][lifecycle]") {
    NullBackendScope scope;
    Renderer::BeginFrame({});
    Renderer::BeginPass({});
    CHECK(Renderer::IsFrameActive());
    CHECK(Renderer::IsPassActive());
    Renderer::EndPass();
    CHECK_FALSE(Renderer::IsPassActive());
    CHECK(Renderer::IsFrameActive());
    Renderer::EndFrame();
}

TEST_CASE("Renderer: EndFrame auto-closes an active pass", "[renderer][facade][lifecycle]") {
    NullBackendScope scope;
    Renderer::BeginFrame({});
    Renderer::BeginPass({});
    Renderer::EndFrame();  // should close pass then frame
    CHECK_FALSE(Renderer::IsPassActive());
    CHECK_FALSE(Renderer::IsFrameActive());
}

// ===========================================================================
// Viewport
// ===========================================================================

TEST_CASE("Renderer::SetViewport / GetViewport round-trip via null backend", "[renderer][facade][viewport]") {
    NullBackendScope scope;
    Renderer::SetViewport(5, 10, 1280, 720);
    const auto vp = Renderer::GetViewport();
    CHECK(vp[0] == 5);
    CHECK(vp[1] == 10);
    CHECK(vp[2] == 1280);
    CHECK(vp[3] == 720);
}

// ===========================================================================
// Draw call count
// ===========================================================================

TEST_CASE("Renderer::GetDrawCallCount is 0 on fresh null backend", "[renderer][facade]") {
    NullBackendScope scope;
    CHECK(Renderer::GetDrawCallCount() == 0);
}

// ===========================================================================
// Readback delegation
// ===========================================================================

TEST_CASE("Renderer::ReadbackColorBgr8 delegates to null backend (zeroed pixels)", "[renderer][facade][readback]") {
    NullBackendScope scope;
    std::vector<uint8_t> pixels;
    CHECK(Renderer::ReadbackColorBgr8(2, 3, pixels, nullptr));
    CHECK(pixels.size() == static_cast<size_t>(2 * 3 * 3));
    for (auto b : pixels) CHECK(b == 0u);
}

TEST_CASE("Renderer::ReadbackDepth32F delegates to null backend (1.0 fill)", "[renderer][facade][readback]") {
    NullBackendScope scope;
    std::vector<float> depth;
    CHECK(Renderer::ReadbackDepth32F(4, 4, depth, nullptr));
    REQUIRE(depth.size() == 16u);
    for (auto d : depth) CHECK(d == 1.0f);
}

// ===========================================================================
// Factory delegators
// ===========================================================================

TEST_CASE("Renderer::CreateShader delegates to null backend", "[renderer][facade][factory]") {
    NullBackendScope scope;
    auto s = Renderer::CreateShader("", "");
    REQUIRE(s != nullptr);
    CHECK(s->IsValid());
}

TEST_CASE("Renderer::CreateShaderFromFile delegates to null backend", "[renderer][facade][factory]") {
    NullBackendScope scope;
    auto s = Renderer::CreateShaderFromFile("", "");
    REQUIRE(s != nullptr);
}

TEST_CASE("Renderer::CreateTexture delegates to null backend", "[renderer][facade][factory]") {
    NullBackendScope scope;
    auto t = Renderer::CreateTexture({});
    REQUIRE(t != nullptr);
}

TEST_CASE("Renderer::CreateTextureFromFile delegates to null backend", "[renderer][facade][factory]") {
    NullBackendScope scope;
    auto t = Renderer::CreateTextureFromFile("dummy.png");
    REQUIRE(t != nullptr);
}

TEST_CASE("Renderer::CreateFramebuffer delegates to null backend", "[renderer][facade][factory]") {
    NullBackendScope scope;
    auto fb = Renderer::CreateFramebuffer({});
    REQUIRE(fb != nullptr);
}

TEST_CASE("Renderer::CreateVertexBuffer (static data) delegates to null backend", "[renderer][facade][factory]") {
    NullBackendScope scope;
    auto vb = Renderer::CreateVertexBuffer(nullptr, 0u);
    REQUIRE(vb != nullptr);
}

TEST_CASE("Renderer::CreateVertexBuffer (dynamic size) delegates to null backend", "[renderer][facade][factory]") {
    NullBackendScope scope;
    auto vb = Renderer::CreateVertexBuffer(static_cast<uint32_t>(128u));
    REQUIRE(vb != nullptr);
}

TEST_CASE("Renderer::CreateIndexBuffer delegates to null backend and preserves count", "[renderer][facade][factory]") {
    NullBackendScope scope;
    auto ib = Renderer::CreateIndexBuffer(nullptr, 42u);
    REQUIRE(ib != nullptr);
    CHECK(ib->GetCount() == 42u);
}

TEST_CASE("Renderer::CreateVertexArray delegates to null backend", "[renderer][facade][factory]") {
    NullBackendScope scope;
    auto va = Renderer::CreateVertexArray();
    REQUIRE(va != nullptr);
}

// ===========================================================================
// Backend capabilities
// ===========================================================================

TEST_CASE("Renderer::GetBackendCapabilities delegates to null backend", "[renderer][facade]") {
    NullBackendScope scope;
    const auto caps = Renderer::GetBackendCapabilities();
    // NullRenderBackend returns all-false capabilities
    CHECK_FALSE(caps.supportsDebugDraw);
    CHECK_FALSE(caps.supportsOffscreenTargets);
    CHECK_FALSE(caps.supportsReadback);
}
