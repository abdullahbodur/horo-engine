#include "renderer/Renderer.h"

// TODO(renderer-abstraction): CreateDefaultOwnedBackend assert message below references
// "OpenGL" by name — update to "default render backend" in Goal 3.
#include <array>
#include <functional>
#include <memory>
#include <optional>

#include "core/Assert.h"
#include "renderer/RenderBackendFactory.h"

namespace Horo {
    namespace {
        std::unique_ptr<IRenderBackend> CreateDefaultOwnedBackend() {
            RenderBackendCreateResult createResult = CreateRenderBackend({});
            HORO_ASSERT(createResult.backend != nullptr,
                            "Failed to create the default OpenGL render backend");
            return std::move(createResult.backend);
        }

        std::unique_ptr<IRenderBackend> &OwnedBackend() {
            static std::unique_ptr<IRenderBackend> backend = CreateDefaultOwnedBackend();
            return backend;
        }

        std::optional<std::reference_wrapper<IRenderBackend> > &ExternalBackend() {
            static std::optional<std::reference_wrapper<IRenderBackend> > backend;
            return backend;
        }

        IRenderBackend *ResetToDefaultBackend() {
            OwnedBackend() = CreateDefaultOwnedBackend();
            ExternalBackend().reset();
            return OwnedBackend().get();
        }

        IRenderBackend *ActiveBackendImpl() {
            if (ExternalBackend().has_value())
                return &ExternalBackend()->get();
            return OwnedBackend().get();
        }
    } // namespace

    bool Renderer::s_frameActive = false;
    bool Renderer::s_passActive = false;

    IRenderBackend *Renderer::ActiveBackend() {
        HORO_ASSERT(ActiveBackendImpl() != nullptr,
                        "Renderer backend pointer should never be null");
        return ActiveBackendImpl();
    }

    RenderBackendInitResult
    Renderer::InitializeBackend(const RenderBackendSelection &selection) {
        HORO_ASSERT(
            !s_frameActive && !s_passActive,
            "Cannot initialize render backend while a frame or pass is active");

        RenderBackendInitResult out;
        out.requested = selection.requested;

        RenderBackendCreateResult createResult = CreateRenderBackend(selection);
        out.selected = createResult.selected;
        out.error = std::move(createResult.error);
        if (!createResult.backend)
            return out;

        OwnedBackend() = std::move(createResult.backend);
        ExternalBackend().reset();
        out.ok = true;
        out.selected = OwnedBackend()->GetBackendId();
        out.capabilities = OwnedBackend()->GetCapabilities();
        return out;
    }

    RenderBackendId Renderer::GetBackendId() {
        return ActiveBackend()->GetBackendId();
    }

    RenderBackendCapabilities Renderer::GetBackendCapabilities() {
        return ActiveBackend()->GetCapabilities();
    }

    IRenderBackend *Renderer::GetBackendForInterop() { return ActiveBackend(); }

    bool Renderer::IsBackendSupported(RenderBackendId backendId) {
        return Horo::IsRenderBackendSupported(backendId);
    }

    void Renderer::UseBackend(IRenderBackend *backend) {
        HORO_ASSERT(!s_frameActive && !s_passActive,
                        "Cannot swap render backend while a frame or pass is active");
        if (backend) {
            OwnedBackend().reset();
            ExternalBackend() = std::ref(*backend);
            return;
        }
        ResetToDefaultBackend();
    }

    void Renderer::ResetBackend() {
        HORO_ASSERT(
            !s_frameActive && !s_passActive,
            "Cannot reset render backend while a frame or pass is active");
        ResetToDefaultBackend();
    }

    void Renderer::BeginFrame(const RenderFrameConfig &frame) {
        HORO_ASSERT(
            !s_frameActive,
            "Renderer::BeginFrame called while a frame is already active");

        ActiveBackend()->BeginFrame(frame);
        s_frameActive = true;
    }

    void Renderer::EndFrame() {
        HORO_ASSERT(s_frameActive,
                        "Renderer::EndFrame called without an active frame");
        if (!s_frameActive)
            return;

        if (s_passActive)
            EndPass();

        ActiveBackend()->EndFrame();
        s_frameActive = false;
    }

    void Renderer::BeginPass(const RenderPassConfig &pass) {
        HORO_ASSERT(s_frameActive,
                        "Renderer::BeginPass called without an active frame");
        HORO_ASSERT(
            !s_passActive,
            "Renderer::BeginPass called while another pass is still active");
        if (!s_frameActive || s_passActive)
            return;

        ActiveBackend()->BeginPass(pass);
        s_passActive = true;
    }

    void Renderer::EndPass() {
        HORO_ASSERT(s_passActive,
                        "Renderer::EndPass called without an active pass");
        if (!s_passActive)
            return;

        ActiveBackend()->EndPass();
        s_passActive = false;
    }

    bool Renderer::IsFrameActive() { return s_frameActive; }

    bool Renderer::IsPassActive() { return s_passActive; }

    void Renderer::Submit(const Mesh &mesh, const Mat4 &modelMatrix,
                          const Material &material) {
        ActiveBackend()->DrawMesh(MeshDrawCommand{&mesh, &material, modelMatrix});
    }

    void Renderer::SubmitSkinned(const SkinnedMesh &mesh, const Mat4 &modelMatrix,
                                 const Material &material,
                                 const std::vector<Mat4> &boneMatrices) {
        ActiveBackend()->DrawSkinnedMesh(
            SkinnedMeshDrawCommand{&mesh, &material, modelMatrix, &boneMatrices});
    }

    void Renderer::SubmitWireframe(const Mesh &mesh, const Mat4 &model,
                                   Shader &shader, float r, float g,
                                   float b) {
        ActiveBackend()->DrawWireframe(
            WireframeDrawCommand{&mesh, &shader, model, {r, g, b, 1.0f}});
    }

    int Renderer::GetDrawCallCount() { return ActiveBackend()->GetDrawCallCount(); }

    bool Renderer::ReadbackColorBgr8(int width, int height,
                                     std::vector<uint8_t> &outPixels,
                                     std::string *outError) {
        return ActiveBackend()->ReadbackColorBgr8(width, height, outPixels, outError);
    }

    bool Renderer::ReadbackDepth32F(int width, int height,
                                    std::vector<float> &outDepth,
                                    std::string *outError) {
        return ActiveBackend()->ReadbackDepth32F(width, height, outDepth, outError);
    }

    bool Renderer::EnsureEditorViewportRenderTarget(uint32_t width, uint32_t height,
                                                    std::string *outError) {
        return ActiveBackend()->EnsureEditorViewportRenderTarget(width, height,
                                                                 outError);
    }

    bool Renderer::TryGetEditorViewportRenderTargetHandle(
        RenderTargetHandle *outHandle, bool needsYFlip, std::string *outError) {
        return ActiveBackend()->TryGetEditorViewportRenderTargetHandle(
            outHandle, needsYFlip, outError);
    }

    bool Renderer::BindEditorViewportRenderTarget() {
        if (auto *backend = ActiveBackendImpl())
            return backend->BindEditorViewportRenderTarget();
        return false;
    }

    void Renderer::UnbindEditorViewportRenderTarget() {
        if (auto *backend = ActiveBackendImpl())
            backend->UnbindEditorViewportRenderTarget();
    }

    std::shared_ptr<IShader> Renderer::CreateShader(const std::string &vert,
                                                     const std::string &frag) {
        return ActiveBackend()->CreateShader(vert, frag);
    }

    std::shared_ptr<IShader> Renderer::CreateShaderFromFile(
        const std::string &vertPath, const std::string &fragPath) {
        return ActiveBackend()->CreateShaderFromFile(vertPath, fragPath);
    }

    std::shared_ptr<ITexture> Renderer::CreateTexture(const TextureSpec &spec) {
        return ActiveBackend()->CreateTexture(spec);
    }

    std::shared_ptr<ITexture> Renderer::CreateTextureFromFile(
        const std::string &path) {
        return ActiveBackend()->CreateTextureFromFile(path);
    }

    std::shared_ptr<IFramebuffer> Renderer::CreateFramebuffer(
        const FramebufferSpec &spec) {
        return ActiveBackend()->CreateFramebuffer(spec);
    }

    std::shared_ptr<IVertexBuffer> Renderer::CreateVertexBuffer(float *vertices,
                                                                  uint32_t size) {
        return ActiveBackend()->CreateVertexBuffer(vertices, size);
    }

    std::shared_ptr<IVertexBuffer> Renderer::CreateVertexBuffer(uint32_t size) {
        return ActiveBackend()->CreateVertexBuffer(size);
    }

    std::shared_ptr<IIndexBuffer> Renderer::CreateIndexBuffer(uint32_t *indices,
                                                               uint32_t count) {
        return ActiveBackend()->CreateIndexBuffer(indices, count);
    }

    std::shared_ptr<IVertexArray> Renderer::CreateVertexArray() {
        return ActiveBackend()->CreateVertexArray();
    }

    void Renderer::SetViewport(int x, int y, int w, int h) {
        // Guard against resize callbacks firing before a backend is set.
        if (auto *backend = ActiveBackendImpl())
            backend->SetViewport(x, y, w, h);
    }

    void Renderer::EnableScissor(int x, int y, int w, int h) {
        if (auto *backend = ActiveBackendImpl()) {
            backend->EnableScissor(x, y, w, h);
        }
    }

    void Renderer::DisableScissor() {
        if (auto *backend = ActiveBackendImpl()) {
            backend->DisableScissor();
        }
    }

    std::array<int, 4> Renderer::GetViewport() {
        return ActiveBackend()->GetViewport();
    }

    void Renderer::Begin2dOverlay()         { ActiveBackend()->Begin2dOverlay(); }
    void Renderer::End2dOverlay()           { ActiveBackend()->End2dOverlay(); }
    void Renderer::SetupOpaqueRenderState() { ActiveBackend()->SetupOpaqueRenderState(); }

    void Renderer::ClearColorAndDepth(float r, float g, float b, float a) {
        ActiveBackend()->ClearColorAndDepth(r, g, b, a);
    }

    bool Renderer::ReadbackRegionRgba8(int x, int y, int w, int h,
                                        uint32_t *pixels, std::string *outError) {
        return ActiveBackend()->ReadbackRegionRgba8(x, y, w, h, pixels, outError);
    }
} // namespace Horo
