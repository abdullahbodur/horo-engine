#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

#include "renderer/null/NullRenderBackend.h"
#include "renderer/null/NullShader.h"
#include "renderer/null/NullTexture.h"
#include "renderer/null/NullVertexArray.h"

using namespace Horo;

// ── Factory returns ──────────────────────────────────────────────────────────

TEST_CASE("NullRenderBackend: CreateShader returns non-null", "[null-renderer]") {
    NullRenderBackend backend;
    REQUIRE(backend.CreateShader("", "") != nullptr);
}

TEST_CASE("NullRenderBackend: CreateShaderFromFile returns non-null", "[null-renderer]") {
    NullRenderBackend backend;
    REQUIRE(backend.CreateShaderFromFile("", "") != nullptr);
}

TEST_CASE("NullRenderBackend: CreateTexture returns non-null", "[null-renderer]") {
    NullRenderBackend backend;
    REQUIRE(backend.CreateTexture({}) != nullptr);
}

TEST_CASE("NullRenderBackend: CreateTextureFromFile returns non-null", "[null-renderer]") {
    NullRenderBackend backend;
    REQUIRE(backend.CreateTextureFromFile("dummy.png") != nullptr);
}

TEST_CASE("NullRenderBackend: CreateFramebuffer returns non-null", "[null-renderer]") {
    NullRenderBackend backend;
    REQUIRE(backend.CreateFramebuffer({}) != nullptr);
}

TEST_CASE("NullRenderBackend: CreateVertexBuffer (static) returns non-null", "[null-renderer]") {
    NullRenderBackend backend;
    REQUIRE(backend.CreateVertexBuffer(nullptr, 0) != nullptr);
}

TEST_CASE("NullRenderBackend: CreateVertexBuffer (dynamic) returns non-null", "[null-renderer]") {
    NullRenderBackend backend;
    REQUIRE(backend.CreateVertexBuffer(static_cast<uint32_t>(64)) != nullptr);
}

TEST_CASE("NullRenderBackend: CreateIndexBuffer returns non-null", "[null-renderer]") {
    NullRenderBackend backend;
    REQUIRE(backend.CreateIndexBuffer(nullptr, 0) != nullptr);
}

TEST_CASE("NullRenderBackend: CreateVertexArray returns non-null", "[null-renderer]") {
    NullRenderBackend backend;
    REQUIRE(backend.CreateVertexArray() != nullptr);
}

// ── Call-count tracking ───────────────────────────────────────────────────────

TEST_CASE("NullShader: Bind/Unbind are tracked", "[null-renderer][null-shader]") {
    NullRenderBackend backend;
    auto shader = backend.CreateShader("", "");
    REQUIRE(shader != nullptr);

    shader->Bind();
    shader->Unbind();
    shader->Bind();

    auto *null = dynamic_cast<NullShader *>(shader.get());
    REQUIRE(null != nullptr);
    CHECK(null->bindCount   == 2);
    CHECK(null->unbindCount == 1);
}

TEST_CASE("NullShader: IsValid returns true", "[null-renderer][null-shader]") {
    NullRenderBackend backend;
    auto shader = backend.CreateShader("", "");
    REQUIRE(shader->IsValid());
}

TEST_CASE("NullTexture: Bind is tracked", "[null-renderer][null-texture]") {
    NullRenderBackend backend;
    auto tex = backend.CreateTexture({});
    tex->Bind(0);
    tex->Bind(1);

    auto *null = dynamic_cast<NullTexture *>(tex.get());
    REQUIRE(null != nullptr);
    CHECK(null->bindCount == 2);
}

TEST_CASE("NullTexture: dimensions from spec", "[null-renderer][null-texture]") {
    NullRenderBackend backend;
    TextureSpec spec;
    spec.width  = 128;
    spec.height = 64;
    auto tex = backend.CreateTexture(spec);
    CHECK(tex->GetWidth()  == 128);
    CHECK(tex->GetHeight() == 64);
}

TEST_CASE("NullVertexArray: Bind is tracked", "[null-renderer][null-varray]") {
    NullRenderBackend backend;
    auto va = backend.CreateVertexArray();
    va->Bind();
    va->Unbind();
    va->Bind();

    auto *null = dynamic_cast<NullVertexArray *>(va.get());
    REQUIRE(null != nullptr);
    CHECK(null->bindCount   == 2);
    CHECK(null->unbindCount == 1);
}

// ── Backend identity / capabilities ──────────────────────────────────────────

TEST_CASE("NullRenderBackend: GetDrawCallCount starts at 0", "[null-renderer]") {
    NullRenderBackend backend;
    CHECK(backend.GetDrawCallCount() == 0);
}

TEST_CASE("NullRenderBackend: SetViewport / GetViewport round-trip", "[null-renderer]") {
    NullRenderBackend backend;
    backend.SetViewport(10, 20, 800, 600);
    auto vp = backend.GetViewport();
    CHECK(vp[0] == 10);
    CHECK(vp[1] == 20);
    CHECK(vp[2] == 800);
    CHECK(vp[3] == 600);
}

TEST_CASE("NullRenderBackend: ReadbackColorBgr8 fills zeroes", "[null-renderer]") {
    NullRenderBackend backend;
    std::vector<uint8_t> pixels;
    bool ok = backend.ReadbackColorBgr8(4, 4, pixels, nullptr);
    REQUIRE(ok);
    REQUIRE(pixels.size() == 4u * 4u * 3u);
    for (auto b : pixels) CHECK(b == 0u);
}

TEST_CASE("NullRenderBackend: ReadbackDepth32F fills 1.0", "[null-renderer]") {
    NullRenderBackend backend;
    std::vector<float> depth;
    bool ok = backend.ReadbackDepth32F(2, 2, depth, nullptr);
    REQUIRE(ok);
    REQUIRE(depth.size() == 4u);
    for (auto d : depth) CHECK(d == 1.0f);
}

TEST_CASE("NullRenderBackend: NullIndexBuffer preserves count", "[null-renderer]") {
    NullRenderBackend backend;
    auto ib = backend.CreateIndexBuffer(nullptr, 42);
    REQUIRE(ib != nullptr);
    CHECK(ib->GetCount() == 42u);
}

TEST_CASE("NullRenderBackend: NullVertexArray stores vertex buffer", "[null-renderer]") {
    NullRenderBackend backend;
    auto va = backend.CreateVertexArray();
    auto vb = backend.CreateVertexBuffer(static_cast<uint32_t>(64));
    va->AddVertexBuffer(vb);

    REQUIRE(va->GetVertexBuffers().size() == 1u);
    CHECK(va->GetVertexBuffers()[0] == vb);
}

TEST_CASE("NullRenderBackend: NullFramebuffer Resize updates spec", "[null-renderer]") {
    NullRenderBackend backend;
    auto fb = backend.CreateFramebuffer({});
    fb->Resize(1920, 1080);
    CHECK(fb->GetSpec().width  == 1920u);
    CHECK(fb->GetSpec().height == 1080u);
}
