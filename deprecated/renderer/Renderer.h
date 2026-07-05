#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "math/Mat4.h"
#include "renderer/IFramebuffer.h"
#include "renderer/IIndexBuffer.h"
#include "renderer/IRenderBackend.h"
#include "renderer/IShader.h"
#include "renderer/ITexture.h"
#include "renderer/IVertexArray.h"
#include "renderer/IVertexBuffer.h"
#include "renderer/RenderBackend.h"
#include "renderer/RenderTargetHandle.h"
#include "renderer/RenderTypes.h"

namespace Horo {
    class Mesh;
    class Shader;
    class Material;
    class SkinnedMesh;

    class Renderer { // NOSONAR(cxx:S1200) - Renderer is a backend-agnostic facade; splitting would fragment the public API
    public:
        static RenderBackendInitResult
        InitializeBackend(const RenderBackendSelection &selection = {});

        static RenderBackendId GetBackendId();

        static RenderBackendCapabilities GetBackendCapabilities();

        static bool IsBackendSupported(RenderBackendId backendId);

        static IRenderBackend *GetBackendForInterop();

        // Test seam: temporarily override the active backend with an externally owned
        // implementation.
        static void UseBackend(IRenderBackend *backend);

        static void ResetBackend();

        static void BeginFrame(const RenderFrameConfig &frame);

        static void EndFrame();

        static void BeginPass(const RenderPassConfig &pass);

        static void EndPass();

        static bool IsFrameActive();

        static bool IsPassActive();

        // Submit a mesh for rendering with a given model matrix and material
        static void Submit(const Mesh &mesh, const Mat4 &modelMatrix,
                           const Material &material);

        // Submit a skinned mesh — uploads boneMatrices to u_boneMatrices[0..N] via
        // SetMat4Array.
        static void SubmitSkinned(const SkinnedMesh &mesh, const Mat4 &modelMatrix,
                                  const Material &material,
                                  const std::vector<Mat4> &boneMatrices);

        // Submit a mesh in wireframe mode using a plain color
        static void SubmitWireframe(const Mesh &mesh, const Mat4 &modelMatrix,
                                    Shader &shader, float r = 0.2f,
                                    float g = 0.8f, float b = 0.2f);

        static int GetDrawCallCount();

        static bool ReadbackColorBgr8(int width, int height,
                                      std::vector<uint8_t> &outPixels,
                                      std::string *outError = nullptr);

        static bool ReadbackDepth32F(int width, int height,
                                     std::vector<float> &outDepth,
                                     std::string *outError = nullptr);

        static bool EnsureEditorViewportRenderTarget(uint32_t width, uint32_t height,
                                                     std::string *outError = nullptr);

        static bool
        TryGetEditorViewportRenderTargetHandle(RenderTargetHandle *outHandle,
                                               bool needsYFlip = false,
                                               std::string *outError = nullptr);

        static bool BeginEditorViewportRenderTarget(uint32_t width, uint32_t height,
                                                    std::string *outError = nullptr);

        static void EndEditorViewportRenderTarget();

        // ── Resource Factory ────────────────────────────────────────────────────
        static std::shared_ptr<IShader>       CreateShader(const std::string& vert, const std::string& frag);
        static std::shared_ptr<IShader>       CreateShaderFromFile(const std::string& vertPath, const std::string& fragPath);
        static std::shared_ptr<ITexture>      CreateTexture(const TextureSpec& spec);
        static std::shared_ptr<ITexture>      CreateTextureFromFile(const std::string& path);
        static std::shared_ptr<IFramebuffer>  CreateFramebuffer(const FramebufferSpec& spec);
        static std::shared_ptr<IVertexBuffer> CreateVertexBuffer(float* vertices, uint32_t size);
        static std::shared_ptr<IVertexBuffer> CreateVertexBuffer(uint32_t size);
        static std::shared_ptr<IIndexBuffer>  CreateIndexBuffer(uint32_t* indices, uint32_t count);
        static std::shared_ptr<IVertexArray>  CreateVertexArray();

        // ── Backend-agnostic render helpers ─────────────────────────────────────
        static void SetViewport(int x, int y, int w, int h);
        static std::array<int, 4> GetViewport();
        static void Begin2dOverlay();
        static void End2dOverlay();
        static void BeginDebugBlend();
        static void EndDebugBlend();
        static void SetupOpaqueRenderState();
        static void ClearColorAndDepth(float r, float g, float b, float a = 1.f);
        static bool ReadbackRegionRgba8(int x, int y, int w, int h,
                                        uint32_t *pixels,
                                        std::string *outError = nullptr);

    private:
        static IRenderBackend *ActiveBackend();

        static bool s_frameActive;
        static bool s_passActive;
    };
} // namespace Horo
