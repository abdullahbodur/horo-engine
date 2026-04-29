#pragma once

#include <cstdint>
#include <vector>

#include "renderer/IRenderBackend.h"

namespace Horo {
    class OpenGLRenderBackend : public IRenderBackend {
    public:
        void BeginFrame(const RenderFrameConfig &frame) override;

        void EndFrame() override;

        void BeginPass(const RenderPassConfig &pass) override;

        void EndPass() override;

        void DrawMesh(const MeshDrawCommand &command) override;

        void DrawSkinnedMesh(const SkinnedMeshDrawCommand &command) override;

        void DrawWireframe(const WireframeDrawCommand &command) override;

        RenderBackendId GetBackendId() const override {
            return RenderBackendId::OpenGL;
        }

        RenderBackendCapabilities GetCapabilities() const override;

        int GetDrawCallCount() const override { return m_drawCalls; }

        bool ReadbackColorBgr8(int width, int height, std::vector<uint8_t> &outPixels,
                               std::string *outError) override;

        bool ReadbackDepth32F(int width, int height, std::vector<float> &outDepth,
                              std::string *outError) override;

        bool EnsureEditorViewportRenderTarget(uint32_t width, uint32_t height,
                                              std::string *outError) override;

        bool TryGetEditorViewportRenderTargetHandle(RenderTargetHandle *outHandle,
                                                    bool needsYFlip,
                                                    std::string *outError) override;

        // ── Resource Factory (implemented in renderer/opengl/) ──────────────
        std::shared_ptr<IShader>       CreateShader(const std::string& vertSrc,
                                                    const std::string& fragSrc) override;
        std::shared_ptr<IShader>       CreateShaderFromFile(const std::string& vertPath,
                                                            const std::string& fragPath) override;
        std::shared_ptr<ITexture>      CreateTexture(const TextureSpec& spec) override;
        std::shared_ptr<ITexture>      CreateTextureFromFile(const std::string& path) override;
        std::shared_ptr<IFramebuffer>  CreateFramebuffer(const FramebufferSpec& spec) override;
        std::shared_ptr<IVertexBuffer> CreateVertexBuffer(float* vertices, uint32_t size) override;
        std::shared_ptr<IVertexBuffer> CreateVertexBuffer(uint32_t size) override;
        std::shared_ptr<IIndexBuffer>  CreateIndexBuffer(uint32_t* indices, uint32_t count) override;
        std::shared_ptr<IVertexArray>  CreateVertexArray() override;

    private:
        void UploadLights(const Shader &shader);

        RenderView m_activeView;
        RenderPassId m_activePassId = RenderPassId::OpaqueScene;
        std::vector<Light> m_lights;
        int m_drawCalls = 0;
        unsigned int m_lastLightProgram = 0;
        bool m_frameActive = false;
        bool m_passActive = false;
        bool m_previousDepthTestEnabled = true;
        bool m_hasPassStateOverride = false;
    };
} // namespace Horo
