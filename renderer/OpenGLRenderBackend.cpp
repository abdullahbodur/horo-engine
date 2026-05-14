#include "renderer/OpenGLRenderBackend.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>

#include <glad/glad.h>

#include "core/Assert.h"
#include "renderer/IFramebuffer.h"
#include "renderer/IIndexBuffer.h"
#include "renderer/IShader.h"
#include "renderer/ITexture.h"
#include "renderer/IVertexArray.h"
#include "renderer/IVertexBuffer.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"
#include "renderer/Shader.h"
#include "renderer/SkinnedMesh.h"
#include "renderer/opengl/OpenGLFramebuffer.h"
#include "renderer/opengl/OpenGLIndexBuffer.h"
#include "renderer/opengl/OpenGLShader.h"
#include "renderer/opengl/OpenGLTexture.h"
#include "renderer/opengl/OpenGLVertexArray.h"
#include "renderer/opengl/OpenGLVertexBuffer.h"

namespace Horo {
    namespace {
        constexpr float kWireframeOverlayLineWidth = 1.5f;

        Shader *ResolveMaterialShader(const Material &material) {
            return material.shader && material.shader->IsValid()
                       ? material.shader.get()
                       : nullptr;
        }

        void BindMaterial(const Material &material) {
            Shader *shader = ResolveMaterialShader(material);
            if (!shader)
                return;

            shader->Bind();
            shader->SetVec4("u_color", material.color);
            shader->SetFloat("u_roughness", material.roughness);
            shader->SetFloat("u_metallic", material.metallic);
            shader->SetInt("u_albedoMap", 0);
            shader->SetFloat("u_uvScale", material.uvScale);

            const bool hasTexture = material.albedoMap && material.albedoMap->IsValid();
            if (hasTexture)
                material.albedoMap->Bind(0);
            shader->SetInt("u_hasTexture", hasTexture ? 1 : 0);
        }
    } // namespace

    RenderBackendCapabilities OpenGLRenderBackend::GetCapabilities() const {
        return GetDefaultRenderBackendCapabilities(RenderBackendId::OpenGL);
    }

    bool OpenGLRenderBackend::ReadbackColorBgr8(int width, int height,
                                                std::vector<uint8_t> &outPixels,
                                                std::string *outError) {
        if (width <= 0 || height <= 0) {
            if (outError)
                *outError = "OpenGL color readback requires positive dimensions.";
            return false;
        }

        outPixels.resize(static_cast<std::vector<uint8_t>::size_type>(width) *
                         static_cast<std::vector<uint8_t>::size_type>(height) * 3u);
        glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, outPixels.data());
        return true;
    }

    bool OpenGLRenderBackend::ReadbackDepth32F(int width, int height,
                                               std::vector<float> &outDepth,
                                               std::string *outError) {
        if (width <= 0 || height <= 0) {
            if (outError)
                *outError = "OpenGL depth readback requires positive dimensions.";
            return false;
        }

        outDepth.resize(static_cast<std::vector<float>::size_type>(width) *
                        static_cast<std::vector<float>::size_type>(height));
        glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT,
                     outDepth.data());
        return true;
    }

    bool OpenGLRenderBackend::EnsureEditorViewportRenderTarget(
        uint32_t, uint32_t, std::string *outError) {
        if (outError)
            *outError = "Editor viewport render-target provisioning is unavailable on "
                    "OpenGL backend.";
        return false;
    }

    bool OpenGLRenderBackend::TryGetEditorViewportRenderTargetHandle(
        RenderTargetHandle *outHandle, bool, std::string *outError) {
        if (outHandle)
            *outHandle = {};
        if (outError)
            *outError = "Editor viewport render-target handle is unavailable on OpenGL "
                    "backend.";
        return false;
    }

    void OpenGLRenderBackend::BeginFrame(const RenderFrameConfig &frame) {
        HORO_ASSERT(
            !m_frameActive,
            "OpenGLRenderBackend::BeginFrame called while a frame is active");
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glEnable(GL_MULTISAMPLE);

        GLbitfield clearMask = 0;
        if (frame.clearColorBuffer) {
            glClearColor(frame.clearColor.x, frame.clearColor.y, frame.clearColor.z,
                         frame.clearColor.w);
            clearMask |= GL_COLOR_BUFFER_BIT;
        }
        if (frame.clearDepthBuffer)
            clearMask |= GL_DEPTH_BUFFER_BIT;
        if (clearMask != 0)
            glClear(clearMask);

        m_lights = frame.lights;
        if (m_lights.size() > 8)
            m_lights.resize(8);
        m_drawCalls = 0;
        m_lastLightProgram = 0;
        m_frameActive = true;
    }

    void OpenGLRenderBackend::EndFrame() {
        if (m_passActive)
            EndPass();
        m_frameActive = false;
        m_lastLightProgram = 0;
    }

    void OpenGLRenderBackend::BeginPass(const RenderPassConfig &pass) {
        HORO_ASSERT(
            m_frameActive,
            "OpenGLRenderBackend::BeginPass called without an active frame");
        HORO_ASSERT(
            !m_passActive,
            "OpenGLRenderBackend::BeginPass called while a pass is active");
        if (!m_frameActive || m_passActive)
            return;

        m_activeView = pass.view;
        m_activePassId = pass.id;
        m_passActive = true;
        m_lastLightProgram = 0;

        if (pass.id == RenderPassId::WireframeOverlay) {
            m_previousDepthTestEnabled = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
            glDisable(GL_DEPTH_TEST);
            glLineWidth(kWireframeOverlayLineWidth);
            m_hasPassStateOverride = true;
        }
    }

    void OpenGLRenderBackend::EndPass() {
        if (m_hasPassStateOverride &&
            m_activePassId == RenderPassId::WireframeOverlay) {
            if (m_previousDepthTestEnabled)
                glEnable(GL_DEPTH_TEST);
            else
                glDisable(GL_DEPTH_TEST);
            glLineWidth(1.0f);
            m_hasPassStateOverride = false;
        }

        m_passActive = false;
        m_lastLightProgram = 0;
    }

    void OpenGLRenderBackend::UploadLights(Shader &shader) {
        const unsigned int programId = shader.GetProgramID();
        if (programId == m_lastLightProgram)
            return;

        const auto lightCount = static_cast<int>(m_lights.size());
        shader.SetInt("u_lightCount", lightCount);
        for (int i = 0; i < lightCount; ++i) {
            const std::string base = std::format("u_lights[{}].", i);
            shader.SetInt(base + "type",
                          static_cast<int>(m_lights[static_cast<size_t>(i)].type));
            shader.SetVec3(base + "position",
                           m_lights[static_cast<size_t>(i)].position);
            shader.SetVec3(base + "direction",
                           m_lights[static_cast<size_t>(i)].direction);
            shader.SetVec3(base + "color",
                           m_lights[static_cast<size_t>(i)].color *
                           m_lights[static_cast<size_t>(i)].intensity);
            shader.SetFloat(base + "radius", m_lights[static_cast<size_t>(i)].radius);
        }
        m_lastLightProgram = programId;
    }

    void OpenGLRenderBackend::DrawMesh(const MeshDrawCommand &command) {
        if (!m_passActive || !command.mesh || !command.material)
            return;

        BindMaterial(*command.material);
        Shader *shader = ResolveMaterialShader(*command.material);
        if (!shader)
            return;

        shader->SetMat4("u_model", command.modelMatrix);
        shader->SetMat4("u_view", m_activeView.view);
        shader->SetMat4("u_projection", m_activeView.projection);
        shader->SetVec3("u_cameraPos", m_activeView.cameraPosition);
        UploadLights(*shader);

        command.mesh->Draw();
        ++m_drawCalls;
    }

    void OpenGLRenderBackend::DrawSkinnedMesh(
        const SkinnedMeshDrawCommand &command) {
        if (!m_passActive || !command.mesh || !command.material)
            return;

        BindMaterial(*command.material);
        Shader *shader = ResolveMaterialShader(*command.material);
        if (!shader)
            return;

        shader->SetMat4("u_model", command.modelMatrix);
        shader->SetMat4("u_view", m_activeView.view);
        shader->SetMat4("u_projection", m_activeView.projection);
        shader->SetVec3("u_cameraPos", m_activeView.cameraPosition);

        const std::vector<Mat4> *boneMatrices = command.boneMatrices;
        if (const int boneCount =
                    boneMatrices
                        ? std::min(static_cast<int>(boneMatrices->size()), 64)
                        : 0;
            boneCount > 0)
            shader->SetMat4Array("u_boneMatrices", boneCount,
                                 (*boneMatrices)[0].Data());

        UploadLights(*shader);

        command.mesh->Draw();
        ++m_drawCalls;
    }

    void OpenGLRenderBackend::DrawWireframe(const WireframeDrawCommand &command) {
        if (!m_passActive || !command.mesh || !command.shader ||
            !command.shader->IsValid())
            return;

        command.shader->Bind();
        command.shader->SetMat4("u_model", command.modelMatrix);
        command.shader->SetMat4("u_view", m_activeView.view);
        command.shader->SetMat4("u_projection", m_activeView.projection);
        command.shader->SetVec4("u_color", command.color);

        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        command.mesh->DrawWireframe();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        ++m_drawCalls;
    }

    void OpenGLRenderBackend::SetViewport(int x, int y, int w, int h) {
        glViewport(x, y, w, h);
    }

    void OpenGLRenderBackend::EnableScissor(int x, int y, int w, int h) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, y, w, h);
    }

    void OpenGLRenderBackend::DisableScissor() {
        glDisable(GL_SCISSOR_TEST);
    }

    std::array<int, 4> OpenGLRenderBackend::GetViewport() const {
        std::array<int, 4> vp{};
        glGetIntegerv(GL_VIEWPORT, vp.data());
        return vp;
    }

    void OpenGLRenderBackend::Begin2dOverlay() {
        m_overlayDepthWas = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
        m_overlayBlendWas = glIsEnabled(GL_BLEND) == GL_TRUE;
        m_overlayCullWas  = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
    }

    void OpenGLRenderBackend::End2dOverlay() {
        if (m_overlayDepthWas)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
        if (m_overlayBlendWas)
            glEnable(GL_BLEND);
        else
            glDisable(GL_BLEND);
        if (m_overlayCullWas)
            glEnable(GL_CULL_FACE);
        else
            glDisable(GL_CULL_FACE);
    }

    void OpenGLRenderBackend::SetupOpaqueRenderState() {
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }

    void OpenGLRenderBackend::ClearColorAndDepth(float r, float g, float b,
                                                  float a) {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    bool OpenGLRenderBackend::ReadbackRegionRgba8(int x, int y, int w, int h,
                                                   uint32_t *pixels,
                                                   std::string *outError) {
        if (!pixels || w <= 0 || h <= 0) {
            if (outError)
                *outError = "ReadbackRegionRgba8: invalid parameters.";
            return false;
        }
        glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        if (const GLenum err = glGetError(); err != GL_NO_ERROR) {
            if (outError)
                *outError =
                    std::format("ReadbackRegionRgba8: glReadPixels failed (GL "
                                "error 0x{:x}).",
                                err);
            return false;
        }
        return true;
    }

    // ── Resource factory ─────────────────────────────────────────────────────

    std::shared_ptr<IShader> OpenGLRenderBackend::CreateShader(
        const std::string &vertSrc, const std::string &fragSrc) {
        return std::make_shared<OpenGLShader>(
            OpenGLShader::FromSource(vertSrc, fragSrc));
    }

    std::shared_ptr<IShader> OpenGLRenderBackend::CreateShaderFromFile(
        const std::string &vertPath, const std::string &fragPath) {
        return std::make_shared<OpenGLShader>(
            OpenGLShader::FromFiles(vertPath, fragPath));
    }

    std::shared_ptr<ITexture> OpenGLRenderBackend::CreateTexture(
        const TextureSpec &spec) {
        return std::make_shared<OpenGLTexture>(OpenGLTexture::FromSpec(spec));
    }

    std::shared_ptr<ITexture> OpenGLRenderBackend::CreateTextureFromFile(
        const std::string &path) {
        return std::make_shared<OpenGLTexture>(OpenGLTexture::FromFile(path));
    }

    std::shared_ptr<IFramebuffer> OpenGLRenderBackend::CreateFramebuffer(
        const FramebufferSpec &spec) {
        return std::make_shared<OpenGLFramebuffer>(spec);
    }

    std::shared_ptr<IVertexBuffer> OpenGLRenderBackend::CreateVertexBuffer(
        float *vertices, uint32_t size) {
        return std::make_shared<OpenGLVertexBuffer>(vertices, size);
    }

    std::shared_ptr<IVertexBuffer> OpenGLRenderBackend::CreateVertexBuffer(
        uint32_t size) {
        return std::make_shared<OpenGLVertexBuffer>(size);
    }

    std::shared_ptr<IIndexBuffer> OpenGLRenderBackend::CreateIndexBuffer(
        uint32_t *indices, uint32_t count) {
        return std::make_shared<OpenGLIndexBuffer>(indices, count);
    }

    std::shared_ptr<IVertexArray> OpenGLRenderBackend::CreateVertexArray() {
        return std::make_shared<OpenGLVertexArray>();
    }
} // namespace Horo
