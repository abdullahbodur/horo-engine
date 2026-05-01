#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "renderer/IRenderBackend.h"
#include "renderer/null/NullFramebuffer.h"
#include "renderer/null/NullIndexBuffer.h"
#include "renderer/null/NullShader.h"
#include "renderer/null/NullTexture.h"
#include "renderer/null/NullVertexArray.h"
#include "renderer/null/NullVertexBuffer.h"

namespace Horo {

// A zero-GPU backend used for headless unit tests and CI validation.
// Every IRenderBackend method is a no-op; resource factory methods return
// concrete Null* objects whose call-counts can be inspected in tests.
class NullRenderBackend final : public IRenderBackend {
public:
    // ── Lifecycle ──────────────────────────────────────────────────────────────
    void BeginFrame(const RenderFrameConfig &) override { m_drawCalls = 0; }
    void EndFrame()                            override {}
    void BeginPass(const RenderPassConfig &)   override {}
    void EndPass()                             override {}

    // ── Draw submission ────────────────────────────────────────────────────────
    void DrawMesh(const MeshDrawCommand &)               override { ++m_drawCalls; }
    void DrawSkinnedMesh(const SkinnedMeshDrawCommand &) override { ++m_drawCalls; }
    void DrawWireframe(const WireframeDrawCommand &)     override { ++m_drawCalls; }

    // ── Identity / capabilities ────────────────────────────────────────────────
    RenderBackendId GetBackendId() const override { return RenderBackendId::Null; }

    RenderBackendCapabilities GetCapabilities() const override { return {}; }

    int GetDrawCallCount() const override { return m_drawCalls; }

    // ── Readback (succeeds with zeroed/1.0 data) ───────────────────────────────
    bool ReadbackColorBgr8(int w, int h, std::vector<uint8_t> &out,
                           std::string *) override {
        if (w <= 0 || h <= 0) return false;
        out.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 3u, 0u);
        return true;
    }

    bool ReadbackDepth32F(int w, int h, std::vector<float> &out,
                          std::string *) override {
        if (w <= 0 || h <= 0) return false;
        out.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 1.0f);
        return true;
    }

    bool EnsureEditorViewportRenderTarget(uint32_t, uint32_t,
                                          std::string *outError) override {
        if (outError) *outError = "NullRenderBackend does not support viewport render targets";
        return false;
    }

    bool TryGetEditorViewportRenderTargetHandle(RenderTargetHandle *outHandle,
                                                bool,
                                                std::string *) override {
        if (outHandle) *outHandle = {};
        return false;
    }

    // ── Viewport ──────────────────────────────────────────────────────────────
    void SetViewport(int x, int y, int w, int h) override {
        m_viewport = {x, y, w, h};
    }
    std::array<int, 4> GetViewport() const override { return m_viewport; }

    // ── 2-D overlay / offscreen helpers ───────────────────────────────────────
    void Begin2dOverlay()        override {}
    void End2dOverlay()          override {}
    void SetupOpaqueRenderState() override {}
    void ClearColorAndDepth(float, float, float, float) override {}

    bool ReadbackRegionRgba8(int, int, int, int,
                             uint32_t *,
                             std::string *) override { return false; }

    // ── Resource Factory ──────────────────────────────────────────────────────
    std::shared_ptr<IShader> CreateShader(const std::string &,
                                          const std::string &) override {
        return std::make_shared<NullShader>();
    }

    std::shared_ptr<IShader> CreateShaderFromFile(const std::string &,
                                                   const std::string &) override {
        return std::make_shared<NullShader>();
    }

    std::shared_ptr<ITexture> CreateTexture(const TextureSpec &spec) override {
        return std::make_shared<NullTexture>(spec);
    }

    std::shared_ptr<ITexture> CreateTextureFromFile(const std::string &) override {
        return std::make_shared<NullTexture>();
    }

    std::shared_ptr<IFramebuffer> CreateFramebuffer(const FramebufferSpec &spec) override {
        return std::make_shared<NullFramebuffer>(spec);
    }

    std::shared_ptr<IVertexBuffer> CreateVertexBuffer(float *, uint32_t) override {
        return std::make_shared<NullVertexBuffer>();
    }

    std::shared_ptr<IVertexBuffer> CreateVertexBuffer(uint32_t) override {
        return std::make_shared<NullVertexBuffer>();
    }

    std::shared_ptr<IIndexBuffer> CreateIndexBuffer(uint32_t *, uint32_t count) override {
        return std::make_shared<NullIndexBuffer>(count);
    }

    std::shared_ptr<IVertexArray> CreateVertexArray() override {
        return std::make_shared<NullVertexArray>();
    }

private:
    int                m_drawCalls = 0;
    std::array<int, 4> m_viewport  = {0, 0, 0, 0};
};

} // namespace Horo
