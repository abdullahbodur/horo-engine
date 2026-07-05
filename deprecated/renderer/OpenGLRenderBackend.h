#pragma once

#include <array>
#include <memory>
#include <vector>

#include "renderer/IRenderBackend.h"
#include "renderer/Shader.h"

namespace Horo {
    class OpenGLRenderBackend : public IRenderBackend {
    public:
        ~OpenGLRenderBackend() override;

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

        bool BeginEditorViewportRenderTarget(uint32_t width, uint32_t height,
                                             std::string *outError) override;

        void EndEditorViewportRenderTarget() override;

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
        void SetViewport(int x, int y, int w, int h) override;
        std::array<int, 4> GetViewport() const override;

        void Begin2dOverlay() override;
        void End2dOverlay() override;

        void BeginDebugBlend() override;
        void EndDebugBlend() override;

        void SetupOpaqueRenderState() override;
        void ClearColorAndDepth(float r, float g, float b, float a) override;

        bool ReadbackRegionRgba8(int x, int y, int w, int h, uint32_t *pixels,
                                 std::string *outError) override;

    private:
        static constexpr int kShadowCascadeCount = 4;

        struct ShadowFrameData {
            std::array<Mat4, kShadowCascadeCount> lightSpaceMatrices{};
            std::array<float, kShadowCascadeCount> cascadeSplits{};
            std::array<float, kShadowCascadeCount> texelWorldSizes{};
            int cascadeCount = 0;
            bool usesAtlas = false;
        };

        void UploadLights(Shader &shader);
        void UploadShadowState(Shader &shader);
        void QueueOpaqueMesh(const MeshDrawCommand &command);
        void QueueOpaqueSkinnedMesh(const SkinnedMeshDrawCommand &command);
        void DrawMeshNow(const MeshDrawCommand &command);
        void DrawSkinnedMeshNow(const SkinnedMeshDrawCommand &command);
        void DrawQueuedOpaquePass();
        void DrawShadowDepthPass();
        void DrawPointShadowDepthPass();
        void DrawShadowMeshes(Shader &shader);
        void DrawShadowSkinnedMeshes(Shader &shader);
        bool EnsureShadowResources();
        bool EnsurePointShadowResources();
        void ReleaseShadowResources();
        int FindShadowCastingLightIndex() const;
        int FindPointShadowLightIndex() const;
        ShadowFrameData BuildShadowFrameData(const Light &light) const;
        Mat4 BuildPointShadowViewProjection(const Light &light, int faceIndex) const;

        RenderView m_activeView;
        RenderPassId m_activePassId = RenderPassId::OpaqueScene;
        std::vector<Light> m_lights;
        ShadowFrameData m_shadowFrameData;
        std::vector<MeshDrawCommand> m_opaqueMeshCommands;
        std::vector<SkinnedMeshDrawCommand> m_opaqueSkinnedCommands;
        std::unique_ptr<Shader> m_shadowDepthShader;
        std::unique_ptr<Shader> m_shadowSkinnedDepthShader;
        std::unique_ptr<Shader> m_pointShadowDepthShader;
        std::unique_ptr<Shader> m_pointShadowSkinnedDepthShader;
        std::shared_ptr<IFramebuffer> m_editorViewportFramebuffer;
        uint64_t m_editorViewportFramebufferGeneration = 0;
        unsigned int m_shadowFramebuffer = 0;
        unsigned int m_shadowDepthTexture = 0;
        unsigned int m_pointShadowFramebuffer = 0;
        unsigned int m_pointShadowCubeTexture = 0;
        unsigned int m_pointShadowDepthTexture = 0;
        int m_drawCalls = 0;
        unsigned int m_lastLightProgram = 0;
        bool m_frameActive = false;
        bool m_passActive = false;
        bool m_previousDepthTestEnabled = true;
        bool m_hasPassStateOverride = false;
        // State saved by Begin2dOverlay / restored by End2dOverlay.
        bool m_overlayDepthWas = false;
        bool m_overlayBlendWas = false;
        bool m_overlayCullWas  = false;
        // State saved by BeginDebugBlend / restored by EndDebugBlend.
        bool m_debugBlendWas = false;
    };
} // namespace Horo
