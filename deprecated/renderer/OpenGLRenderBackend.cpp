#include "renderer/OpenGLRenderBackend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <string>

#include <glad/glad.h>

#include "core/Assert.h"
#include "math/MathUtils.h"
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
        constexpr int kShadowAtlasSize = 4096;
        constexpr int kShadowCascadeTileSize = kShadowAtlasSize / 2;
        constexpr int kPointShadowMapSize = 2048;
        constexpr int kShadowTextureSlot = 5;
        constexpr int kPointShadowTextureSlot = 6;
        constexpr float kShadowNearPlane = 0.1f;
        constexpr float kDirectionalShadowDistance = 160.0f;
        constexpr float kCascadeSplitLambda = 0.65f;
        constexpr float kShadowDepthPadding = 24.0f;
        constexpr GLenum kGlFramebufferBinding = 0x8CA6;
        constexpr GLenum kGlCullFaceMode = 0x0B45;
        constexpr GLenum kGlTextureCubeMapPositiveX = 0x8515;
        constexpr GLenum kGlR32F = 0x822E;

        const char *const SHADOW_DEPTH_VERT = R"glsl(
#version 410 core
layout(location = 0) in vec3 a_position;

uniform mat4 u_model;
uniform mat4 u_lightSpaceMatrix;

void main()
{
    gl_Position = u_lightSpaceMatrix * u_model * vec4(a_position, 1.0);
}
)glsl";

        const char *const SHADOW_SKINNED_DEPTH_VERT = R"glsl(
#version 410 core
layout(location = 0) in vec3 a_position;
layout(location = 3) in ivec4 a_boneIndices;
layout(location = 4) in vec4 a_boneWeights;

uniform mat4 u_model;
uniform mat4 u_lightSpaceMatrix;
uniform mat4 u_boneMatrices[64];

void main()
{
    mat4 skin =
        u_boneMatrices[a_boneIndices.x] * a_boneWeights.x +
        u_boneMatrices[a_boneIndices.y] * a_boneWeights.y +
        u_boneMatrices[a_boneIndices.z] * a_boneWeights.z +
        u_boneMatrices[a_boneIndices.w] * a_boneWeights.w;
    float weightSum = a_boneWeights.x + a_boneWeights.y + a_boneWeights.z + a_boneWeights.w;
    if (weightSum <= 0.0001)
        skin = mat4(1.0);
    gl_Position = u_lightSpaceMatrix * u_model * skin * vec4(a_position, 1.0);
}
)glsl";

        const char *const SHADOW_DEPTH_FRAG = R"glsl(
#version 410 core
void main()
{
}
)glsl";

        const char *const POINT_SHADOW_DEPTH_VERT = R"glsl(
#version 410 core
layout(location = 0) in vec3 a_position;

uniform mat4 u_model;
uniform mat4 u_lightViewProjection;

out vec3 v_worldPos;

void main()
{
    vec4 world = u_model * vec4(a_position, 1.0);
    v_worldPos = world.xyz;
    gl_Position = u_lightViewProjection * world;
}
)glsl";

        const char *const POINT_SHADOW_SKINNED_DEPTH_VERT = R"glsl(
#version 410 core
layout(location = 0) in vec3 a_position;
layout(location = 3) in ivec4 a_boneIndices;
layout(location = 4) in vec4 a_boneWeights;

uniform mat4 u_model;
uniform mat4 u_lightViewProjection;
uniform mat4 u_boneMatrices[64];

out vec3 v_worldPos;

void main()
{
    mat4 skin =
        u_boneMatrices[a_boneIndices.x] * a_boneWeights.x +
        u_boneMatrices[a_boneIndices.y] * a_boneWeights.y +
        u_boneMatrices[a_boneIndices.z] * a_boneWeights.z +
        u_boneMatrices[a_boneIndices.w] * a_boneWeights.w;
    float weightSum = a_boneWeights.x + a_boneWeights.y + a_boneWeights.z + a_boneWeights.w;
    if (weightSum <= 0.0001)
        skin = mat4(1.0);
    vec4 world = u_model * skin * vec4(a_position, 1.0);
    v_worldPos = world.xyz;
    gl_Position = u_lightViewProjection * world;
}
)glsl";

        const char *const POINT_SHADOW_DEPTH_FRAG = R"glsl(
#version 410 core
in vec3 v_worldPos;

uniform vec3 u_lightPosition;
uniform float u_farPlane;

out float FragDistance;

void main()
{
    FragDistance = length(v_worldPos - u_lightPosition);
}
)glsl";

        Shader *ResolveMaterialShader(const Material &material) {
            return material.shader && material.shader->IsValid() ? material.shader.get()
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
            shader->SetFloat("u_uvScale", material.uvScale);

            // glTF-aligned PBR texture slots (HORO-67). Slot order is fixed and
            // mirrored by the fragment shaders' sampler declarations.
            //   slot 0 = albedoMap        / u_hasTexture
            //   slot 1 = normalMap        / u_hasNormalMap
            //   slot 2 = metallicRoughnessMap / u_hasMetallicRoughnessMap
            //   slot 3 = emissiveMap      / u_hasEmissiveMap
            //   slot 4 = occlusionMap     / u_hasOcclusionMap
            shader->SetInt("u_albedoMap", 0);
            shader->SetInt("u_normalMap", 1);
            shader->SetInt("u_metallicRoughnessMap", 2);
            shader->SetInt("u_emissiveMap", 3);
            shader->SetInt("u_occlusionMap", 4);

            const auto bindOptional = [](const std::shared_ptr<Texture> &tex, int slot) {
                if (tex && tex->IsValid()) {
                    tex->Bind(slot);
                    return 1;
                }
                static const OpenGLTexture s_defaultTex = OpenGLTexture::CreateWhite1x1();
                s_defaultTex.Bind(slot);
                return 0;
            };

            const int hasAlbedo = bindOptional(material.albedoMap, 0);
            const int hasNormal = bindOptional(material.normalMap, 1);
            const int hasMR = bindOptional(material.metallicRoughnessMap, 2);
            const int hasEmiss = bindOptional(material.emissiveMap, 3);
            const int hasOccl = bindOptional(material.occlusionMap, 4);

            shader->SetInt("u_hasTexture", hasAlbedo);
            shader->SetInt("u_hasNormalMap", hasNormal);
            shader->SetInt("u_hasMetallicRoughnessMap", hasMR);
            shader->SetInt("u_hasEmissiveMap", hasEmiss);
            shader->SetInt("u_hasOcclusionMap", hasOccl);
        }

        bool IsShadowMappedLight(const Light &light) {
            return light.type == Light::Type::Directional ||
                   light.type == Light::Type::Spot;
        }

        bool ExtractClipPlanes(const Mat4 &projection, float *outNear,
                               float *outFar) {
            if (!outNear || !outFar)
                return false;

            const float depthScale = projection(2, 2);
            const float depthOffset = projection(2, 3);
            const bool perspective = Abs(projection(3, 2) + 1.0f) < 0.001f;
            const float nearPlane =
                perspective ? depthOffset / (depthScale - 1.0f)
                            : (depthOffset + 1.0f) / depthScale;
            const float farPlane =
                perspective ? depthOffset / (depthScale + 1.0f)
                            : (depthOffset - 1.0f) / depthScale;
            if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
                nearPlane <= 0.0f || farPlane <= nearPlane) {
                return false;
            }

            *outNear = nearPlane;
            *outFar = farPlane;
            return true;
        }

        std::array<Vec3, 8> BuildFrustumSliceCorners(
            const RenderView &view, float cameraNear, float cameraFar,
            float sliceNear, float sliceFar) {
            const Mat4 inverseViewProjection =
                (view.projection * view.view).Inverse();
            std::array<Vec3, 8> corners{};
            size_t cornerIndex = 0;
            const float nearT =
                (sliceNear - cameraNear) / (cameraFar - cameraNear);
            const float farT =
                (sliceFar - cameraNear) / (cameraFar - cameraNear);

            for (int y = 0; y < 2; ++y) {
                for (int x = 0; x < 2; ++x) {
                    const float ndcX = x == 0 ? -1.0f : 1.0f;
                    const float ndcY = y == 0 ? -1.0f : 1.0f;
                    const Vec3 fullNear = inverseViewProjection.TransformPoint(
                        {ndcX, ndcY, -1.0f});
                    const Vec3 fullFar = inverseViewProjection.TransformPoint(
                        {ndcX, ndcY, 1.0f});
                    corners[cornerIndex++] =
                        Vec3::Lerp(fullNear, fullFar, nearT);
                    corners[cornerIndex++] =
                        Vec3::Lerp(fullNear, fullFar, farT);
                }
            }
            return corners;
        }
    } // namespace

    OpenGLRenderBackend::~OpenGLRenderBackend() { ReleaseShadowResources(); }

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
        glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, outDepth.data());
        return true;
    }

    bool OpenGLRenderBackend::EnsureEditorViewportRenderTarget(uint32_t width,
                                                               uint32_t height,
                                                               std::string *outError) {
        if (width == 0 || height == 0) {
            if (outError)
                *outError = "Editor viewport render-target dimensions must be non-zero.";
            return false;
        }

        FramebufferSpec spec;
        spec.width = width;
        spec.height = height;
        spec.attachmentSpec = {{{FramebufferTextureFormat::RGBA8},
                                {FramebufferTextureFormat::DEPTH24STENCIL8}}};

        if (!m_editorViewportFramebuffer) {
            m_editorViewportFramebuffer = std::make_shared<OpenGLFramebuffer>(spec);
            ++m_editorViewportFramebufferGeneration;
            const bool success = m_editorViewportFramebuffer->GetColorAttachmentId() != 0;
            if (!success && outError)
                *outError =
                    "Editor viewport render target is unavailable (no OpenGL context).";
            return success;
        }

        if (const FramebufferSpec &current = m_editorViewportFramebuffer->GetSpec();
            current.width != width || current.height != height) {
            m_editorViewportFramebuffer->Resize(width, height);
            ++m_editorViewportFramebufferGeneration;
        }
        const bool success = m_editorViewportFramebuffer->GetColorAttachmentId() != 0;
        if (!success && outError)
            *outError =
                "Editor viewport render target is unavailable (no OpenGL context).";
        return success;
    }

    bool OpenGLRenderBackend::TryGetEditorViewportRenderTargetHandle(
        RenderTargetHandle *outHandle, bool needsYFlip, std::string *outError) {
        if (outHandle)
            *outHandle = {};
        if (!m_editorViewportFramebuffer) {
            if (outError)
                *outError = "Editor viewport render target has not been created.";
            return false;
        }

        const uint32_t textureId = m_editorViewportFramebuffer->GetColorAttachmentId();
        if (textureId == 0) {
            if (outError)
                *outError = "Editor viewport render target is unavailable.";
            return false;
        }

        const FramebufferSpec &spec = m_editorViewportFramebuffer->GetSpec();
        if (outHandle) {
            *outHandle = RenderTargetHandle::OpenGLTexture(
                textureId, needsYFlip, spec.width, spec.height,
                m_editorViewportFramebufferGeneration);
        }
        return true;
    }

    bool OpenGLRenderBackend::BeginEditorViewportRenderTarget(uint32_t width,
                                                              uint32_t height,
                                                              std::string *outError) {
        if (!EnsureEditorViewportRenderTarget(width, height, outError))
            return false;
        m_editorViewportFramebuffer->Bind();
        return true;
    }

    void OpenGLRenderBackend::EndEditorViewportRenderTarget() {
        if (m_editorViewportFramebuffer)
            m_editorViewportFramebuffer->Unbind();
    }

    void OpenGLRenderBackend::BeginFrame(const RenderFrameConfig &frame) {
        HORO_ASSERT(!m_frameActive,
                    "OpenGLRenderBackend::BeginFrame called while a frame is active");

        // Bind default textures to all shader sampler slots so the GPU
        // always has a loadable texture, preventing "GLD_TEXTURE_INDEX_2D
        // is unloadable" warnings on Apple's Metal OpenGL driver.
        static const OpenGLTexture s_defaultTex = OpenGLTexture::CreateWhite1x1();
        for (int slot = 0; slot <= kPointShadowTextureSlot; ++slot)
            s_defaultTex.Bind(slot);

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
        m_opaqueMeshCommands.clear();
        m_opaqueSkinnedCommands.clear();
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
        HORO_ASSERT(m_frameActive,
                    "OpenGLRenderBackend::BeginPass called without an active frame");
        HORO_ASSERT(!m_passActive,
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
        using enum RenderPassId;
        if (m_hasPassStateOverride && m_activePassId == WireframeOverlay) {
            if (m_previousDepthTestEnabled)
                glEnable(GL_DEPTH_TEST);
            else
                glDisable(GL_DEPTH_TEST);
            glLineWidth(1.0f);
            m_hasPassStateOverride = false;
        }

        if (m_activePassId == OpaqueScene ||
            m_activePassId == CompatibilityScene) {
            DrawQueuedOpaquePass();
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
            shader.SetVec3(base + "position", m_lights[static_cast<size_t>(i)].position);
            shader.SetVec3(base + "direction",
                           m_lights[static_cast<size_t>(i)].direction);
            shader.SetVec3(base + "color",
                           m_lights[static_cast<size_t>(i)].color *
                               m_lights[static_cast<size_t>(i)].intensity);
            shader.SetFloat(base + "radius", m_lights[static_cast<size_t>(i)].radius);
        }
        m_lastLightProgram = programId;
    }

    void OpenGLRenderBackend::UploadShadowState(Shader &shader) {
        const int shadowLightIndex = FindShadowCastingLightIndex();
        const bool shadowEnabled =
            shadowLightIndex >= 0 && m_shadowDepthTexture != 0 &&
            m_shadowFrameData.cascadeCount > 0;
        shader.SetInt("u_shadowEnabled", shadowEnabled ? 1 : 0);
        shader.SetInt("u_shadowLightIndex", shadowLightIndex);
        shader.SetInt("u_shadowMap", kShadowTextureSlot);
        shader.SetInt("u_shadowCascadeCount",
                      shadowEnabled ? m_shadowFrameData.cascadeCount : 0);
        shader.SetInt("u_shadowUsesAtlas",
                      shadowEnabled && m_shadowFrameData.usesAtlas ? 1 : 0);
        if (shadowEnabled) {
            shader.SetMat4Array("u_shadowLightSpaceMatrices",
                                m_shadowFrameData.cascadeCount,
                                m_shadowFrameData.lightSpaceMatrices[0].Data());
            for (int cascade = 0; cascade < m_shadowFrameData.cascadeCount;
                 ++cascade) {
                const std::string suffix = std::format("[{}]", cascade);
                shader.SetFloat("u_shadowCascadeSplits" + suffix,
                                m_shadowFrameData.cascadeSplits[cascade]);
                shader.SetFloat("u_shadowTexelWorldSizes" + suffix,
                                m_shadowFrameData.texelWorldSizes[cascade]);
            }
            glActiveTexture(GL_TEXTURE0 + kShadowTextureSlot);
            glBindTexture(GL_TEXTURE_2D, m_shadowDepthTexture);
        }

        const int pointShadowLightIndex = FindPointShadowLightIndex();
        const bool pointShadowEnabled =
            pointShadowLightIndex >= 0 && m_pointShadowCubeTexture != 0;
        shader.SetInt("u_pointShadowEnabled", pointShadowEnabled ? 1 : 0);
        shader.SetInt("u_pointShadowLightIndex", pointShadowLightIndex);
        shader.SetInt("u_pointShadowMap", kPointShadowTextureSlot);
        shader.SetFloat(
            "u_pointShadowFarPlane",
            pointShadowEnabled
                ? std::max(
                      5.0f,
                      m_lights[static_cast<size_t>(pointShadowLightIndex)].radius)
                : 1.0f);
        if (pointShadowEnabled) {
            glActiveTexture(GL_TEXTURE0 + kPointShadowTextureSlot);
            glBindTexture(GL_TEXTURE_CUBE_MAP, m_pointShadowCubeTexture);
        }
    }

    void OpenGLRenderBackend::DrawMesh(const MeshDrawCommand &command) {
        if (!m_passActive || !command.mesh || !command.material)
            return;

        if (m_activePassId == RenderPassId::OpaqueScene ||
            m_activePassId == RenderPassId::CompatibilityScene) {
            QueueOpaqueMesh(command);
            return;
        }

        DrawMeshNow(command);
    }

    void OpenGLRenderBackend::DrawSkinnedMesh(const SkinnedMeshDrawCommand &command) {
        if (!m_passActive || !command.mesh || !command.material)
            return;

        if (m_activePassId == RenderPassId::OpaqueScene ||
            m_activePassId == RenderPassId::CompatibilityScene) {
            QueueOpaqueSkinnedMesh(command);
            return;
        }

        DrawSkinnedMeshNow(command);
    }

    void OpenGLRenderBackend::QueueOpaqueMesh(const MeshDrawCommand &command) {
        m_opaqueMeshCommands.push_back(command);
    }

    void OpenGLRenderBackend::QueueOpaqueSkinnedMesh(
        const SkinnedMeshDrawCommand &command) {
        m_opaqueSkinnedCommands.push_back(command);
    }

    void OpenGLRenderBackend::DrawMeshNow(const MeshDrawCommand &command) {
        BindMaterial(*command.material);
        Shader *shader = ResolveMaterialShader(*command.material);
        if (!shader)
            return;

        shader->SetMat4("u_model", command.modelMatrix);
        shader->SetMat4("u_view", m_activeView.view);
        shader->SetMat4("u_projection", m_activeView.projection);
        shader->SetVec3("u_cameraPos", m_activeView.cameraPosition);
        UploadLights(*shader);
        UploadShadowState(*shader);

        command.mesh->Draw();
        ++m_drawCalls;
    }

    void OpenGLRenderBackend::DrawSkinnedMeshNow(
        const SkinnedMeshDrawCommand &command) {
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
                boneMatrices ? std::min(static_cast<int>(boneMatrices->size()), 64) : 0;
            boneCount > 0)
            shader->SetMat4Array("u_boneMatrices", boneCount, (*boneMatrices)[0].Data());

        UploadLights(*shader);
        UploadShadowState(*shader);

        command.mesh->Draw();
        ++m_drawCalls;
    }

    void OpenGLRenderBackend::DrawQueuedOpaquePass() {
        if (m_opaqueMeshCommands.empty() && m_opaqueSkinnedCommands.empty())
            return;

        DrawShadowDepthPass();
        DrawPointShadowDepthPass();

        for (const MeshDrawCommand &command : m_opaqueMeshCommands)
            DrawMeshNow(command);
        for (const SkinnedMeshDrawCommand &command : m_opaqueSkinnedCommands)
            DrawSkinnedMeshNow(command);

        m_opaqueMeshCommands.clear();
        m_opaqueSkinnedCommands.clear();
    }

    void OpenGLRenderBackend::DrawShadowMeshes(Shader &shader) {
        for (const MeshDrawCommand &command : m_opaqueMeshCommands) {
            if (!command.mesh)
                continue;
            shader.SetMat4("u_model", command.modelMatrix);
            command.mesh->Draw();
            ++m_drawCalls;
        }
    }

    void OpenGLRenderBackend::DrawShadowSkinnedMeshes(Shader &shader) {
        for (const SkinnedMeshDrawCommand &command : m_opaqueSkinnedCommands) {
            if (!command.mesh)
                continue;
            shader.SetMat4("u_model", command.modelMatrix);
            const std::vector<Mat4> *boneMatrices = command.boneMatrices;
            if (const int boneCount =
                    boneMatrices
                        ? std::min(static_cast<int>(boneMatrices->size()), 64)
                        : 0;
                boneCount > 0) {
                shader.SetMat4Array("u_boneMatrices", boneCount,
                                    (*boneMatrices)[0].Data());
            }
            command.mesh->Draw();
            ++m_drawCalls;
        }
    }

    void OpenGLRenderBackend::DrawShadowDepthPass() {
        const int shadowLightIndex = FindShadowCastingLightIndex();
        if (shadowLightIndex < 0)
            return;
        if (!EnsureShadowResources())
            return;

        if (!m_shadowDepthShader) {
            m_shadowDepthShader = std::make_unique<Shader>(
                Shader::FromSource(SHADOW_DEPTH_VERT, SHADOW_DEPTH_FRAG));
        }
        if (!m_shadowSkinnedDepthShader) {
            m_shadowSkinnedDepthShader = std::make_unique<Shader>(
                Shader::FromSource(SHADOW_SKINNED_DEPTH_VERT, SHADOW_DEPTH_FRAG));
        }
        if (!m_shadowDepthShader || !m_shadowDepthShader->IsValid())
            return;

        const Light &shadowLight =
            m_lights[static_cast<size_t>(shadowLightIndex)];
        m_shadowFrameData = BuildShadowFrameData(shadowLight);
        if (m_shadowFrameData.cascadeCount <= 0)
            return;

        GLint previousFramebuffer = 0;
        std::array<GLint, 4> previousViewport{};
        GLint previousCullFaceMode = GL_BACK;
        const bool polygonOffsetWasEnabled =
            glIsEnabled(GL_POLYGON_OFFSET_FILL) == GL_TRUE;
        glGetIntegerv(kGlFramebufferBinding, &previousFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport.data());
        glGetIntegerv(kGlCullFaceMode, &previousCullFaceMode);

        glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer);
        glClear(GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_FRONT);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);

        for (int cascade = 0; cascade < m_shadowFrameData.cascadeCount;
             ++cascade) {
            if (m_shadowFrameData.usesAtlas) {
                const int tileX = (cascade % 2) * kShadowCascadeTileSize;
                const int tileY = (cascade / 2) * kShadowCascadeTileSize;
                glViewport(tileX, tileY, kShadowCascadeTileSize,
                           kShadowCascadeTileSize);
            } else {
                glViewport(0, 0, kShadowAtlasSize, kShadowAtlasSize);
            }

            const Mat4 &lightSpaceMatrix =
                m_shadowFrameData.lightSpaceMatrices[cascade];
            m_shadowDepthShader->Bind();
            m_shadowDepthShader->SetMat4("u_lightSpaceMatrix",
                                         lightSpaceMatrix);
            DrawShadowMeshes(*m_shadowDepthShader);

            if (m_shadowSkinnedDepthShader &&
                m_shadowSkinnedDepthShader->IsValid()) {
                m_shadowSkinnedDepthShader->Bind();
                m_shadowSkinnedDepthShader->SetMat4("u_lightSpaceMatrix",
                                                    lightSpaceMatrix);
                DrawShadowSkinnedMeshes(*m_shadowSkinnedDepthShader);
            }
        }

        if (!polygonOffsetWasEnabled)
            glDisable(GL_POLYGON_OFFSET_FILL);
        glCullFace(static_cast<GLenum>(previousCullFaceMode));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2],
                   previousViewport[3]);
        m_lastLightProgram = 0;
    }

    void OpenGLRenderBackend::DrawPointShadowDepthPass() {
        const int pointLightIndex = FindPointShadowLightIndex();
        if (pointLightIndex < 0)
            return;
        if (!EnsurePointShadowResources())
            return;

        if (!m_pointShadowDepthShader) {
            m_pointShadowDepthShader = std::make_unique<Shader>(
                Shader::FromSource(POINT_SHADOW_DEPTH_VERT, POINT_SHADOW_DEPTH_FRAG));
        }
        if (!m_pointShadowSkinnedDepthShader) {
            m_pointShadowSkinnedDepthShader = std::make_unique<Shader>(
                Shader::FromSource(POINT_SHADOW_SKINNED_DEPTH_VERT,
                                   POINT_SHADOW_DEPTH_FRAG));
        }
        if (!m_pointShadowDepthShader || !m_pointShadowDepthShader->IsValid())
            return;

        const Light &light = m_lights[static_cast<size_t>(pointLightIndex)];
        const float farPlane = std::max(5.0f, light.radius);

        GLint previousFramebuffer = 0;
        std::array<GLint, 4> previousViewport{};
        GLint previousCullFaceMode = GL_BACK;
        const bool polygonOffsetWasEnabled =
            glIsEnabled(GL_POLYGON_OFFSET_FILL) == GL_TRUE;
        glGetIntegerv(kGlFramebufferBinding, &previousFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport.data());
        glGetIntegerv(kGlCullFaceMode, &previousCullFaceMode);

        glViewport(0, 0, kPointShadowMapSize, kPointShadowMapSize);
        glBindFramebuffer(GL_FRAMEBUFFER, m_pointShadowFramebuffer);
        glCullFace(GL_FRONT);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);

        for (int face = 0; face < 6; ++face) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   kGlTextureCubeMapPositiveX + face,
                                   m_pointShadowCubeTexture, 0);
            glClearColor(farPlane, farPlane, farPlane, farPlane);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            const Mat4 lightViewProjection =
                BuildPointShadowViewProjection(light, face);

            m_pointShadowDepthShader->Bind();
            m_pointShadowDepthShader->SetMat4("u_lightViewProjection",
                                              lightViewProjection);
            m_pointShadowDepthShader->SetVec3("u_lightPosition", light.position);
            m_pointShadowDepthShader->SetFloat("u_farPlane", farPlane);
            DrawShadowMeshes(*m_pointShadowDepthShader);

            if (m_pointShadowSkinnedDepthShader &&
                m_pointShadowSkinnedDepthShader->IsValid()) {
                m_pointShadowSkinnedDepthShader->Bind();
                m_pointShadowSkinnedDepthShader->SetMat4("u_lightViewProjection",
                                                         lightViewProjection);
                m_pointShadowSkinnedDepthShader->SetVec3("u_lightPosition",
                                                         light.position);
                m_pointShadowSkinnedDepthShader->SetFloat("u_farPlane", farPlane);
                DrawShadowSkinnedMeshes(*m_pointShadowSkinnedDepthShader);
            }
        }

        if (!polygonOffsetWasEnabled)
            glDisable(GL_POLYGON_OFFSET_FILL);
        glCullFace(static_cast<GLenum>(previousCullFaceMode));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2],
                   previousViewport[3]);
        m_lastLightProgram = 0;
    }

    bool OpenGLRenderBackend::EnsureShadowResources() {
        if (m_shadowFramebuffer != 0 && m_shadowDepthTexture != 0)
            return true;

        glGenFramebuffers(1, &m_shadowFramebuffer);
        glGenTextures(1, &m_shadowDepthTexture);
        glBindTexture(GL_TEXTURE_2D, m_shadowDepthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowAtlasSize,
                     kShadowAtlasSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                        GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               m_shadowDepthTexture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        const bool complete =
            glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (!complete)
            ReleaseShadowResources();
        return complete;
    }

    bool OpenGLRenderBackend::EnsurePointShadowResources() {
        if (m_pointShadowFramebuffer != 0 && m_pointShadowCubeTexture != 0 &&
            m_pointShadowDepthTexture != 0)
            return true;

        glGenFramebuffers(1, &m_pointShadowFramebuffer);
        glGenTextures(1, &m_pointShadowCubeTexture);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_pointShadowCubeTexture);
        for (int face = 0; face < 6; ++face) {
            glTexImage2D(kGlTextureCubeMapPositiveX + face, 0, kGlR32F,
                         kPointShadowMapSize, kPointShadowMapSize, 0, GL_RED,
                         GL_FLOAT, nullptr);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        glGenTextures(1, &m_pointShadowDepthTexture);
        glBindTexture(GL_TEXTURE_2D, m_pointShadowDepthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kPointShadowMapSize,
                     kPointShadowMapSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, m_pointShadowFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               kGlTextureCubeMapPositiveX,
                               m_pointShadowCubeTexture, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               m_pointShadowDepthTexture, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_NONE);
        const bool complete =
            glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (!complete)
            ReleaseShadowResources();
        return complete;
    }

    void OpenGLRenderBackend::ReleaseShadowResources() {
        m_shadowFrameData = {};
        if (m_shadowDepthTexture != 0) {
            glDeleteTextures(1, &m_shadowDepthTexture);
            m_shadowDepthTexture = 0;
        }
        if (m_shadowFramebuffer != 0) {
            glDeleteFramebuffers(1, &m_shadowFramebuffer);
            m_shadowFramebuffer = 0;
        }
        if (m_pointShadowCubeTexture != 0) {
            glDeleteTextures(1, &m_pointShadowCubeTexture);
            m_pointShadowCubeTexture = 0;
        }
        if (m_pointShadowDepthTexture != 0) {
            glDeleteTextures(1, &m_pointShadowDepthTexture);
            m_pointShadowDepthTexture = 0;
        }
        if (m_pointShadowFramebuffer != 0) {
            glDeleteFramebuffers(1, &m_pointShadowFramebuffer);
            m_pointShadowFramebuffer = 0;
        }
    }

    int OpenGLRenderBackend::FindShadowCastingLightIndex() const {
        for (size_t i = 0; i < m_lights.size(); ++i) {
            if (IsShadowMappedLight(m_lights[i]))
                return static_cast<int>(i);
        }
        return -1;
    }

    int OpenGLRenderBackend::FindPointShadowLightIndex() const {
        for (size_t i = 0; i < m_lights.size(); ++i) {
            if (m_lights[i].type == Light::Type::Point)
                return static_cast<int>(i);
        }
        return -1;
    }

    OpenGLRenderBackend::ShadowFrameData
    OpenGLRenderBackend::BuildShadowFrameData(const Light &light) const {
        ShadowFrameData result;
        if (light.type == Light::Type::Spot) {
            const Vec3 direction = light.direction.Normalized();
            const Vec3 up = Abs(Vec3::Dot(direction, Vec3::Up())) > 0.95f
                                ? Vec3::Forward()
                                : Vec3::Up();
            const Mat4 lightView =
                Mat4::LookAt(light.position, light.position + direction, up);
            const float farPlane = std::max(5.0f, light.radius);
            result.lightSpaceMatrices[0] =
                Mat4::Perspective(ToRadians(55.0f), 1.0f, kShadowNearPlane,
                                  farPlane) *
                lightView;
            result.cascadeSplits[0] = farPlane;
            result.texelWorldSizes[0] =
                2.0f * farPlane * Tan(ToRadians(55.0f) * 0.5f) /
                static_cast<float>(kShadowAtlasSize);
            result.cascadeCount = 1;
            return result;
        }

        Vec3 direction = light.direction.Normalized();
        if (direction.LengthSq() <= EPSILON)
            direction = Vec3::Down();
        const Vec3 up =
            Abs(Vec3::Dot(direction, Vec3::Up())) > 0.95f ? Vec3::Forward()
                                                          : Vec3::Up();

        float cameraNear = 0.1f;
        float cameraFar = 1000.0f;
        if (!ExtractClipPlanes(m_activeView.projection, &cameraNear, &cameraFar))
            return result;
        const float shadowFar = std::min(cameraFar, kDirectionalShadowDistance);
        if (shadowFar <= cameraNear)
            return result;

        float previousSplit = cameraNear;
        for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
            const float splitRatio =
                static_cast<float>(cascade + 1) /
                static_cast<float>(kShadowCascadeCount);
            const float logarithmicSplit =
                cameraNear * std::pow(shadowFar / cameraNear, splitRatio);
            const float uniformSplit =
                cameraNear + (shadowFar - cameraNear) * splitRatio;
            const float cascadeFar =
                std::lerp(uniformSplit, logarithmicSplit, kCascadeSplitLambda);
            const std::array<Vec3, 8> corners = BuildFrustumSliceCorners(
                m_activeView, cameraNear, cameraFar, previousSplit, cascadeFar);

            Vec3 center = Vec3::Zero();
            for (const Vec3 &corner : corners)
                center += corner;
            center /= static_cast<float>(corners.size());

            float radius = 0.0f;
            for (const Vec3 &corner : corners)
                radius = std::max(radius, Vec3::Distance(center, corner));
            radius = std::ceil(radius * 16.0f) / 16.0f;

            const Vec3 eye =
                center - direction * (radius + kShadowDepthPadding);
            Mat4 lightView = Mat4::LookAt(eye, center, up);

            float minX = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float minY = std::numeric_limits<float>::max();
            float maxY = std::numeric_limits<float>::lowest();
            float minZ = std::numeric_limits<float>::max();
            float maxZ = std::numeric_limits<float>::lowest();
            for (const Vec3 &corner : corners) {
                const Vec3 lightSpaceCorner = lightView.TransformPoint(corner);
                minX = std::min(minX, lightSpaceCorner.x);
                maxX = std::max(maxX, lightSpaceCorner.x);
                minY = std::min(minY, lightSpaceCorner.y);
                maxY = std::max(maxY, lightSpaceCorner.y);
                minZ = std::min(minZ, lightSpaceCorner.z);
                maxZ = std::max(maxZ, lightSpaceCorner.z);
            }

            const float extent =
                std::max(std::max(maxX - minX, maxY - minY), 0.001f);
            const float texelWorldSize =
                extent / static_cast<float>(kShadowCascadeTileSize);
            float centerX = (minX + maxX) * 0.5f;
            float centerY = (minY + maxY) * 0.5f;
            centerX = std::floor(centerX / texelWorldSize) * texelWorldSize;
            centerY = std::floor(centerY / texelWorldSize) * texelWorldSize;
            const float halfExtent = extent * 0.5f + texelWorldSize * 2.0f;
            minX = centerX - halfExtent;
            maxX = centerX + halfExtent;
            minY = centerY - halfExtent;
            maxY = centerY + halfExtent;

            const float nearPlane =
                std::max(kShadowNearPlane, -maxZ - kShadowDepthPadding);
            const float farPlane =
                std::max(nearPlane + 1.0f, -minZ + kShadowDepthPadding);
            Mat4 lightProjection =
                Mat4::Orthographic(minX, maxX, minY, maxY, nearPlane, farPlane);
            const Vec4 shadowOrigin =
                (lightProjection * lightView) * Vec4(Vec3::Zero(), 1.0f);
            const float originScale =
                static_cast<float>(kShadowCascadeTileSize) * 0.5f;
            const float roundedOriginX =
                std::round(shadowOrigin.x * originScale);
            const float roundedOriginY =
                std::round(shadowOrigin.y * originScale);
            lightProjection(0, 3) +=
                (roundedOriginX - shadowOrigin.x * originScale) / originScale;
            lightProjection(1, 3) +=
                (roundedOriginY - shadowOrigin.y * originScale) / originScale;
            result.lightSpaceMatrices[cascade] = lightProjection * lightView;
            result.cascadeSplits[cascade] = cascadeFar;
            result.texelWorldSizes[cascade] = texelWorldSize;
            previousSplit = cascadeFar;
        }
        result.cascadeCount = kShadowCascadeCount;
        result.usesAtlas = true;
        return result;
    }

    Mat4 OpenGLRenderBackend::BuildPointShadowViewProjection(
        const Light &light, int faceIndex) const {
        static const std::array<Vec3, 6> kDirections = {
            Vec3{1.0f, 0.0f, 0.0f},  Vec3{-1.0f, 0.0f, 0.0f},
            Vec3{0.0f, 1.0f, 0.0f},  Vec3{0.0f, -1.0f, 0.0f},
            Vec3{0.0f, 0.0f, 1.0f},  Vec3{0.0f, 0.0f, -1.0f},
        };
        static const std::array<Vec3, 6> kUps = {
            Vec3{0.0f, -1.0f, 0.0f}, Vec3{0.0f, -1.0f, 0.0f},
            Vec3{0.0f, 0.0f, 1.0f},  Vec3{0.0f, 0.0f, -1.0f},
            Vec3{0.0f, -1.0f, 0.0f}, Vec3{0.0f, -1.0f, 0.0f},
        };

        const int clampedFace = std::clamp(faceIndex, 0, 5);
        const float farPlane = std::max(5.0f, light.radius);
        const Mat4 projection =
            Mat4::Perspective(ToRadians(90.0f), 1.0f, kShadowNearPlane, farPlane);
        const Mat4 view = Mat4::LookAt(
            light.position, light.position + kDirections[static_cast<size_t>(clampedFace)],
            kUps[static_cast<size_t>(clampedFace)]);
        return projection * view;
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

    std::array<int, 4> OpenGLRenderBackend::GetViewport() const {
        std::array<int, 4> vp{};
        glGetIntegerv(GL_VIEWPORT, vp.data());
        return vp;
    }

    void OpenGLRenderBackend::Begin2dOverlay() {
        m_overlayDepthWas = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
        m_overlayBlendWas = glIsEnabled(GL_BLEND) == GL_TRUE;
        m_overlayCullWas = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
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

    void OpenGLRenderBackend::BeginDebugBlend() {
        m_debugBlendWas = glIsEnabled(GL_BLEND) == GL_TRUE;
        if (!m_debugBlendWas) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    void OpenGLRenderBackend::EndDebugBlend() {
        if (!m_debugBlendWas) {
            glDisable(GL_BLEND);
        }
    }

    void OpenGLRenderBackend::SetupOpaqueRenderState() {
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        glCullFace(GL_BACK);
    }

    void OpenGLRenderBackend::ClearColorAndDepth(float r, float g, float b, float a) {
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
        static constexpr GLenum kPackAlignment = 0x0D05;
        glPixelStorei(kPackAlignment, 1);
        while (glGetError() != GL_NO_ERROR) {
            // Drain prior errors so glReadPixels result is accurate
        }
        glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        if (const GLenum err = glGetError(); err != GL_NO_ERROR) {
            if (outError)
                *outError = std::format("ReadbackRegionRgba8: glReadPixels failed (GL "
                                        "error 0x{:x}).",
                                        err);
            return false;
        }
        return true;
    }

    // ── Resource factory ─────────────────────────────────────────────────────

    std::shared_ptr<IShader>
    OpenGLRenderBackend::CreateShader(const std::string &vertSrc,
                                      const std::string &fragSrc) {
        return std::make_shared<OpenGLShader>(OpenGLShader::FromSource(vertSrc, fragSrc));
    }

    std::shared_ptr<IShader>
    OpenGLRenderBackend::CreateShaderFromFile(const std::string &vertPath,
                                              const std::string &fragPath) {
        return std::make_shared<OpenGLShader>(
            OpenGLShader::FromFiles(vertPath, fragPath));
    }

    std::shared_ptr<ITexture>
    OpenGLRenderBackend::CreateTexture(const TextureSpec &spec) {
        return std::make_shared<OpenGLTexture>(OpenGLTexture::FromSpec(spec));
    }

    std::shared_ptr<ITexture>
    OpenGLRenderBackend::CreateTextureFromFile(const std::string &path) {
        return std::make_shared<OpenGLTexture>(OpenGLTexture::FromFile(path));
    }

    std::shared_ptr<IFramebuffer>
    OpenGLRenderBackend::CreateFramebuffer(const FramebufferSpec &spec) {
        return std::make_shared<OpenGLFramebuffer>(spec);
    }

    std::shared_ptr<IVertexBuffer>
    OpenGLRenderBackend::CreateVertexBuffer(float *vertices, uint32_t size) {
        return std::make_shared<OpenGLVertexBuffer>(vertices, size);
    }

    std::shared_ptr<IVertexBuffer>
    OpenGLRenderBackend::CreateVertexBuffer(uint32_t size) {
        return std::make_shared<OpenGLVertexBuffer>(size);
    }

    std::shared_ptr<IIndexBuffer>
    OpenGLRenderBackend::CreateIndexBuffer(uint32_t *indices, uint32_t count) {
        return std::make_shared<OpenGLIndexBuffer>(indices, count);
    }

    std::shared_ptr<IVertexArray> OpenGLRenderBackend::CreateVertexArray() {
        return std::make_shared<OpenGLVertexArray>();
    }
} // namespace Horo
