#pragma once

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

        // ── Resource Factory ────────────────────────────────────────────────────
        virtual std::shared_ptr<IShader>       CreateShader(const std::string& vertSrc,
                                                             const std::string& fragSrc) = 0;
        virtual std::shared_ptr<IShader>       CreateShaderFromFile(const std::string& vertPath,
                                                                     const std::string& fragPath) = 0;
        virtual std::shared_ptr<ITexture>      CreateTexture(const TextureSpec& spec) = 0;
        virtual std::shared_ptr<ITexture>      CreateTextureFromFile(const std::string& path) = 0;
        virtual std::shared_ptr<IFramebuffer>  CreateFramebuffer(const FramebufferSpec& spec) = 0;
        virtual std::shared_ptr<IVertexBuffer> CreateVertexBuffer(float* vertices, uint32_t size) = 0;
        virtual std::shared_ptr<IVertexBuffer> CreateVertexBuffer(uint32_t size) = 0; // dynamic
        virtual std::shared_ptr<IIndexBuffer>  CreateIndexBuffer(uint32_t* indices, uint32_t count) = 0;
        virtual std::shared_ptr<IVertexArray>  CreateVertexArray() = 0;
    };
} // namespace Horo
