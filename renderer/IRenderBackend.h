#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "renderer/IFramebuffer.h"
#include "renderer/IIndexBuffer.h"
#include "renderer/IShader.h"
#include "renderer/ITexture.h"
#include "renderer/IVertexArray.h"
#include "renderer/IVertexBuffer.h"
#include "renderer/RenderBackend.h"
#include "renderer/RenderTargetHandle.h"
#include "renderer/RenderTypes.h"

namespace Horo {
    class IRenderBackend {
    public:
        virtual ~IRenderBackend() = default;

        virtual void BeginFrame(const RenderFrameConfig &frame) = 0;

        virtual void EndFrame() = 0;

        virtual void BeginPass(const RenderPassConfig &pass) = 0;

        virtual void EndPass() = 0;

        virtual void DrawMesh(const MeshDrawCommand &command) = 0;

        virtual void DrawSkinnedMesh(const SkinnedMeshDrawCommand &command) = 0;

        virtual void DrawWireframe(const WireframeDrawCommand &command) = 0;

        virtual RenderBackendId GetBackendId() const = 0;

        virtual RenderBackendCapabilities GetCapabilities() const = 0;

        virtual int GetDrawCallCount() const = 0;

        virtual bool ReadbackColorBgr8(int width, int height,
                                       std::vector<uint8_t> &outPixels,
                                       std::string *outError) = 0;

        virtual bool ReadbackDepth32F(int width, int height,
                                      std::vector<float> &outDepth,
                                      std::string *outError) = 0;

        virtual bool EnsureEditorViewportRenderTarget(uint32_t width, uint32_t height,
                                                      std::string *outError) = 0;

        virtual bool
        TryGetEditorViewportRenderTargetHandle(RenderTargetHandle *outHandle,
                                               bool needsYFlip,
                                               std::string *outError) = 0;

        virtual bool BeginEditorViewportRenderTarget(uint32_t width,
                                                     uint32_t height,
                                                     std::string *outError) {
            (void)width;
            (void)height;
            if (outError)
                *outError =
                    "Editor viewport render-target binding is unsupported by this backend.";
            return false;
        }

        virtual void EndEditorViewportRenderTarget() {}

        // ── Viewport ────────────────────────────────────────────────────────────
        virtual void SetViewport(int, int, int, int) { /* default no-op */ }
        virtual std::array<int, 4> GetViewport() const { return {0, 0, 0, 0}; }

        // ── 2-D overlay state ────────────────────────────────────────────────────
        virtual void Begin2dOverlay() { /* default no-op */ }
        virtual void End2dOverlay()   { /* default no-op */ }

        // ── Offscreen / thumbnail helpers ────────────────────────────────────────
        virtual void SetupOpaqueRenderState()              { /* default no-op */ }
        virtual void ClearColorAndDepth(float, float, float, float) { /* default no-op */ }

        // Read a sub-region of the currently bound READ framebuffer as RGBA8.
        // Returns false on failure (e.g. invalid coords or no GL context).
        virtual bool ReadbackRegionRgba8(int, int, int, int,
                                         uint32_t *,
                                         std::string *) { return false; }

        // ── Resource Factory ────────────────────────────────────────────────────
        // Default implementations return nullptr; concrete backends override these.
        // OpenGL implementations are wired up in feat/renderer-opengl-resource-classes.
        virtual std::shared_ptr<IShader>
        CreateShader(const std::string & /*vertSrc*/, const std::string & /*fragSrc*/) { return nullptr; }
        virtual std::shared_ptr<IShader>
        CreateShaderFromFile(const std::string & /*vertPath*/, const std::string & /*fragPath*/) { return nullptr; }
        virtual std::shared_ptr<ITexture>      CreateTexture(const TextureSpec &)         { return nullptr; }
        virtual std::shared_ptr<ITexture>      CreateTextureFromFile(const std::string &) { return nullptr; }
        virtual std::shared_ptr<IFramebuffer>  CreateFramebuffer(const FramebufferSpec &) { return nullptr; }
        virtual std::shared_ptr<IVertexBuffer> CreateVertexBuffer(float *, uint32_t)      { return nullptr; }
        virtual std::shared_ptr<IVertexBuffer> CreateVertexBuffer(uint32_t)               { return nullptr; }
        virtual std::shared_ptr<IIndexBuffer>  CreateIndexBuffer(uint32_t *, uint32_t)    { return nullptr; }
        virtual std::shared_ptr<IVertexArray>  CreateVertexArray()                        { return nullptr; }
    };
} // namespace Horo
