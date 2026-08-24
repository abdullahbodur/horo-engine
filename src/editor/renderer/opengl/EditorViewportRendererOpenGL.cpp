#include "EditorViewportRendererOpenGL.h"

#include "editor/renderer/EditorRendererErrors.h"
#include "editor/renderer/grid/EditorViewportGridGeometry.h"
#include "editor/screens/workspace/panels/viewport/visualizers/light/LightVisualizerGeometry.h"

#include <SDL3/SDL_video.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <glad/gl.h>
#include <string>
#include <utility>

namespace Horo::Editor {
    namespace {
        constexpr std::uint32_t maxViewportDimension = 8192;

        [[nodiscard]] Error MakeViewportError(const ErrorCodeDescriptor &descriptor, std::string message) {
            return MakeError(descriptor, std::move(message));
        }

        [[nodiscard]] Result<std::uint32_t> CompileShader(const std::uint32_t type, const char *source) {
            const std::uint32_t shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);

            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled == GL_TRUE) {
                return Result<std::uint32_t>::Success(shader);
            }

            GLint logLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
            std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
            glGetShaderInfoLog(shader, logLength, nullptr, log.data());
            glDeleteShader(shader);
            return Result<std::uint32_t>::Failure(
                MakeViewportError(RendererErrors::ViewportShaderCompileFailed, "Viewport shader compilation failed: " + log));
        }
    }  // namespace

    /** @copydoc EditorViewportRendererOpenGL::~EditorViewportRendererOpenGL */
    EditorViewportRendererOpenGL::~EditorViewportRendererOpenGL() {
        Shutdown();
    }

    /** @copydoc EditorViewportRendererOpenGL::Initialize */
    Result<void> EditorViewportRendererOpenGL::Initialize() {
        if (initialized_) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportAlreadyInitialized, "Editor viewport renderer is already initialized."));
        }
        if (gladLoadGL(SDL_GL_GetProcAddress) == 0) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportOpenGLDispatchFailed, "Failed to load OpenGL entry points."));
        }
        if (const Result<void> program = CreateProgram(); program.HasError()) {
            Shutdown();
            return program;
        }
        if (const Result<void> shadowResources = CreateShadowResources(); shadowResources.HasError()) {
            Shutdown();
            return shadowResources;
        }
        GLint previousVertexArray = 0;
        GLint previousArrayBuffer = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
        glGenVertexArrays(1, &gridVertexArray_);
        glGenBuffers(1, &gridVertexBuffer_);
        glBindVertexArray(gridVertexArray_);
        glBindBuffer(GL_ARRAY_BUFFER, gridVertexBuffer_);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(Math::Vec3)), nullptr);
        glEnableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glVertexAttrib3f(1, 0.0F, 1.0F, 0.0F);
        glDisableVertexAttribArray(2);
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
        glBindVertexArray(static_cast<GLuint>(previousVertexArray));
        if (gridVertexArray_ == 0 || gridVertexBuffer_ == 0) {
            Shutdown();
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportGeometryCreationFailed, "Failed to create viewport grid geometry."));
        }
        initialized_ = true;
        return Result<void>::Success();
    }

    /** @copydoc EditorViewportRendererOpenGL::Shutdown */
    void EditorViewportRendererOpenGL::Shutdown() noexcept {
        DestroyTarget();
        for (auto &[id, mesh] : meshes_)
            DestroyMesh(mesh);
        meshes_.clear();
        if (gridVertexBuffer_ != 0) {
            glDeleteBuffers(1, &gridVertexBuffer_);
            gridVertexBuffer_ = 0;
        }
        if (gridVertexArray_ != 0) {
            glDeleteVertexArrays(1, &gridVertexArray_);
            gridVertexArray_ = 0;
        }
        if (program_ != 0) {
            glDeleteProgram(program_);
            program_ = 0;
        }
        if (shadowProgram_ != 0) {
            glDeleteProgram(shadowProgram_);
            shadowProgram_ = 0;
        }
        if (shadowDepthTexture_ != 0) {
            glDeleteTextures(1, &shadowDepthTexture_);
            shadowDepthTexture_ = 0;
        }
        if (shadowFramebuffer_ != 0) {
            glDeleteFramebuffers(1, &shadowFramebuffer_);
            shadowFramebuffer_ = 0;
        }
        uniforms_ = {};
        requestedExtent_ = {};
        gridOptions_ = {};
        lightVisualizerOptions_ = {};
        targetHandle_ = {};
        initialized_ = false;
    }

    /** @copydoc EditorViewportRendererOpenGL::RequestExtent */
    void EditorViewportRendererOpenGL::RequestExtent(const EditorViewportExtent extent) noexcept {
        requestedExtent_.width = std::min(extent.width, maxViewportDimension);
        requestedExtent_.height = std::min(extent.height, maxViewportDimension);
    }

    /** @copydoc EditorViewportRendererOpenGL::RequestGrid */
    void EditorViewportRendererOpenGL::RequestGrid(EditorViewportGridOptions options) noexcept {
        if (!std::isfinite(options.targetMinorSpacingPixels) || options.targetMinorSpacingPixels <= 0.0F)
            options.targetMinorSpacingPixels = 48.0F;
        gridOptions_ = options;
    }

    /** @copydoc EditorViewportRendererOpenGL::RequestLightVisualizer */
    void EditorViewportRendererOpenGL::RequestLightVisualizer(const EditorViewportLightVisualizerOptions options) noexcept {
        lightVisualizerOptions_ = options;
    }

    /** @copydoc EditorViewportRendererOpenGL::RequestedExtent */
    EditorViewportExtent EditorViewportRendererOpenGL::RequestedExtent() const noexcept {
        return requestedExtent_;
    }

    /** @copydoc EditorViewportRendererOpenGL::ClipDepthRange */
    Math::ClipDepthRange EditorViewportRendererOpenGL::ClipDepthRange() const noexcept {
        return Math::ClipDepthRange::NegativeOneToOne;
    }

    /** @copydoc EditorViewportRendererOpenGL::ExecuteStaticMeshPass */
    Result<void> EditorViewportRendererOpenGL::ExecuteStaticMeshPass(  // NOSONAR(cpp:S3776)
        const Render::StaticMeshPassDescriptor &descriptor) {
        if (!initialized_) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportNotInitialized, "Viewport renderer is not initialized."));
        }
        // A panel must request an extent every UI frame. Consuming the request keeps
        // hidden/inactive viewport tabs from spending GPU time in the background.
        const EditorViewportExtent requestedExtent = std::exchange(requestedExtent_, {});
        if (!requestedExtent.IsValid()) {
            return Result<void>::Success();
        }
        if (!descriptor.IsValid() || descriptor.extent.width != requestedExtent.width ||
            descriptor.extent.height != requestedExtent.height) {
            return Result<void>::Failure(MakeViewportError(RendererErrors::ViewportInvalidScene, "Editor viewport scene data is invalid."));
        }
        if (targetHandle_.IsValid() && targetHandle_ != descriptor.target) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportStaleTarget, "Viewport pass references a stale render target."));
        }
        targetHandle_ = descriptor.target;
        if (requestedExtent.width != allocatedExtent_.width || requestedExtent.height != allocatedExtent_.height) {
            if (const Result<void> recreated = RecreateTarget(requestedExtent); recreated.HasError()) {
                return recreated;
            }
        }
        if (const Result<void> synchronized = SynchronizeMeshes(descriptor.scene.meshResources); synchronized.HasError())
            return synchronized;
        const Result<std::optional<EditorViewportDirectionalShadowView>> shadow =
            BuildEditorViewportDirectionalShadowView(descriptor.scene, Math::ClipDepthRange::NegativeOneToOne);
        if (shadow.HasError())
            return Result<void>::Failure(shadow.ErrorValue());

        const float aspect = static_cast<float>(allocatedExtent_.width) / static_cast<float>(allocatedExtent_.height);

        GLint previousDrawFramebuffer = 0;
        GLint previousReadFramebuffer = 0;
        GLint previousProgram = 0;
        GLint previousVertexArray = 0;
        GLint previousArrayBuffer = 0;
        GLint previousDepthFunction = 0;
        GLint previousCullFaceMode = 0;
        GLint previousActiveTexture = 0;
        GLint previousShadowTexture = 0;
        std::array<GLint, 4> previousViewport{};
        std::array<GLfloat, 4> previousClearColor{};
        const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
        const GLboolean scissorTestWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
        const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
        const GLboolean cullFaceWasEnabled = glIsEnabled(GL_CULL_FACE);
        std::array<GLboolean, 4> previousColorMask{};
        GLboolean previousDepthMask = GL_TRUE;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
        glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunction);
        glGetIntegerv(GL_CULL_FACE_MODE, &previousCullFaceMode);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
        glActiveTexture(GL_TEXTURE7);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousShadowTexture);
        glGetIntegerv(GL_VIEWPORT, previousViewport.data());
        glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor.data());
        glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask.data());
        glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);

        if (shadow.Value().has_value()) {
            if (const Result<void> renderedShadow = DrawDirectionalShadowMap(descriptor.scene, *shadow.Value());
                renderedShadow.HasError()) {
                return renderedShadow;
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glViewport(0, 0, static_cast<GLsizei>(allocatedExtent_.width), static_cast<GLsizei>(allocatedExtent_.height));
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glClearColor(descriptor.clearColor.red, descriptor.clearColor.green, descriptor.clearColor.blue, descriptor.clearColor.alpha);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(program_);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, shadowDepthTexture_);
        UploadLighting(descriptor.scene, shadow.Value());
        if (const Result<void> grid = DrawGrid(descriptor.scene.camera, aspect, static_cast<float>(allocatedExtent_.height));
            grid.HasError()) {
            return grid;
        }
        for (const Render::RenderStaticMeshInstance &instance : descriptor.scene.instances) {
            const auto mesh = meshes_.find(instance.mesh.id.value);
            if (mesh == meshes_.end())
                return Result<void>::Failure(
                    MakeViewportError(RendererErrors::ViewportStaleMeshResource, "Viewport instance references a stale mesh resource."));
            glBindVertexArray(mesh->second.vertexArray);
            const Result<Math::Mat4> mvp =
                BuildRenderMvp(descriptor.scene.camera, instance.localToWorld, aspect, Math::ClipDepthRange::NegativeOneToOne);
            if (mvp.HasError())
                return Result<void>::Failure(mvp.ErrorValue());
            glUniformMatrix4fv(uniforms_.mvp, 1, GL_FALSE, mvp.Value().values.data());
            glUniformMatrix4fv(uniforms_.model, 1, GL_FALSE, instance.localToWorld.values.data());
            glUniform3f(uniforms_.selectionColor, instance.presentation.tint.x, instance.presentation.tint.y, instance.presentation.tint.z);
            glUniform1f(uniforms_.selectionStrength, instance.presentation.tintStrength);

            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->second.indexCount), GL_UNSIGNED_INT, nullptr);
        }
        if (const Result<void> visualizer = DrawLightVisualizer(descriptor.scene.camera, aspect); visualizer.HasError())
            return visualizer;
        glBindVertexArray(static_cast<GLuint>(previousVertexArray));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
        glUseProgram(static_cast<GLuint>(previousProgram));
        glDepthFunc(static_cast<GLenum>(previousDepthFunction));
        if (depthTestWasEnabled == GL_TRUE) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        if (scissorTestWasEnabled == GL_TRUE) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
        if (blendWasEnabled == GL_TRUE) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
        if (cullFaceWasEnabled == GL_TRUE) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
        glCullFace(static_cast<GLenum>(previousCullFaceMode));
        glColorMask(previousColorMask[0], previousColorMask[1], previousColorMask[2], previousColorMask[3]);
        glDepthMask(previousDepthMask);
        glClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousShadowTexture));
        glActiveTexture(static_cast<GLenum>(previousActiveTexture));
        return Result<void>::Success();
    }

    /** @copydoc EditorViewportRendererOpenGL::DrawLightVisualizer */
    Result<void> EditorViewportRendererOpenGL::DrawLightVisualizer(const Render::RenderCameraView &camera, const float aspect) {
        if (!lightVisualizerOptions_.selectedLight.has_value())
            return Result<void>::Success();

        LightVisualizerGeometry geometry;
        if (!BuildLightVisualizerGeometry(LightVisualizerGeometryRequest{.camera = camera, .light = *lightVisualizerOptions_.selectedLight},
                                          geometry)) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportInvalidScene, "Failed to build selected Light visualizer geometry."));
        }
        const Result<Math::Mat4> viewProjection =
            BuildRenderMvp(camera, Math::Mat4::Identity(), aspect, Math::ClipDepthRange::NegativeOneToOne);
        if (viewProjection.HasError())
            return Result<void>::Failure(viewProjection.ErrorValue());

        glDepthMask(GL_FALSE);
        glBindVertexArray(gridVertexArray_);
        glBindBuffer(GL_ARRAY_BUFFER, gridVertexBuffer_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(geometry.Lines().size_bytes()), geometry.Lines().data(), GL_DYNAMIC_DRAW);
        glUniformMatrix4fv(uniforms_.mvp, 1, GL_FALSE, viewProjection.Value().values.data());
        const Math::Mat4 identity = Math::Mat4::Identity();
        glUniformMatrix4fv(uniforms_.model, 1, GL_FALSE, identity.values.data());
        glUniform3f(uniforms_.selectionColor, geometry.color.x, geometry.color.y, geometry.color.z);
        glUniform1f(uniforms_.selectionStrength, 1.0F);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(geometry.vertexCount));
        glDepthMask(GL_TRUE);
        return Result<void>::Success();
    }

    /** @copydoc EditorViewportRendererOpenGL::DrawGrid */
    Result<void> EditorViewportRendererOpenGL::DrawGrid(const Render::RenderCameraView &camera, const float aspect,
                                                        const float viewportHeightPixels) const {
        if (!gridOptions_.visible)
            return Result<void>::Success();

        ViewportGridGeometry geometry;
        if (!BuildViewportGridGeometry(
                ViewportGridGeometryRequest{
                    .camera = camera,
                    .aspect = aspect,
                    .viewportHeightPixels = viewportHeightPixels,
                    .targetMinorSpacingPixels = gridOptions_.targetMinorSpacingPixels,
                    .targetLineWidthPixels = gridOptions_.targetLineWidthPixels,
                },
                geometry)) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportInvalidScene, "Failed to build finite viewport grid geometry."));
        }

        const Result<Math::Mat4> viewProjection =
            BuildRenderMvp(camera, Math::Mat4::Identity(), aspect, Math::ClipDepthRange::NegativeOneToOne);
        if (viewProjection.HasError())
            return Result<void>::Failure(viewProjection.ErrorValue());

        glDepthMask(GL_FALSE);
        glBindVertexArray(gridVertexArray_);
        glUniformMatrix4fv(uniforms_.mvp, 1, GL_FALSE, viewProjection.Value().values.data());
        const Math::Mat4 identity = Math::Mat4::Identity();
        glUniformMatrix4fv(uniforms_.model, 1, GL_FALSE, identity.values.data());
        glUniform1f(uniforms_.selectionStrength, 1.0F);
        const auto drawBatch = [&](const ViewportGridLineBatch &batch) {
            if (batch.positions.empty())
                return;
            glBindBuffer(GL_ARRAY_BUFFER, gridVertexBuffer_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch.positions.size_bytes()), batch.positions.data(), GL_DYNAMIC_DRAW);
            glUniform3f(uniforms_.selectionColor, batch.color.x, batch.color.y, batch.color.z);
            const GLenum primitive = batch.topology == ViewportGridPrimitiveTopology::Triangles ? GL_TRIANGLES : GL_LINES;
            glDrawArrays(primitive, 0, static_cast<GLsizei>(batch.positions.size()));
        };
        drawBatch(geometry.RegularLines());
        drawBatch(geometry.Axes());
        glDepthMask(GL_TRUE);
        return Result<void>::Success();
    }

    /** @copydoc EditorViewportRendererOpenGL::TextureView */
    EditorViewportTextureView EditorViewportRendererOpenGL::TextureView() const noexcept {
        return EditorViewportTextureView{
            .textureId = static_cast<std::uintptr_t>(colorTexture_),
            .u0 = 0.0F,
            .v0 = 1.0F,
            .u1 = 1.0F,
            .v1 = 0.0F,
        };
    }

    /** @copydoc EditorViewportRendererOpenGL::IsReady */
    bool EditorViewportRendererOpenGL::IsReady() const noexcept {
        return initialized_ && colorTexture_ != 0 && allocatedExtent_.IsValid();
    }

    Result<void> EditorViewportRendererOpenGL::CreateProgram() {
        static constexpr const char *vertexSource = R"glsl(#version 150 core
in vec3 aPosition;
in vec3 aNormal;
in vec2 aUv;
out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec4 vShadowPosition;
uniform mat4 uMvp;
uniform mat4 uModel;
uniform mat4 uShadowViewProjection;
void main()
{
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    vWorldPosition = worldPosition.xyz;
    mat3 modelBasis = mat3(uModel);
    vec3 inverseRow0 = cross(modelBasis[1], modelBasis[2]);
    vec3 inverseRow1 = cross(modelBasis[2], modelBasis[0]);
    vec3 inverseRow2 = cross(modelBasis[0], modelBasis[1]);
    float determinant = dot(modelBasis[0], inverseRow0);
    float inverseDeterminant = abs(determinant) > 0.0000001 ? 1.0 / determinant : 1.0;
    mat3 normalMatrix = mat3(inverseRow0 * inverseDeterminant,
                             inverseRow1 * inverseDeterminant,
                             inverseRow2 * inverseDeterminant);
    vWorldNormal = normalize(normalMatrix * aNormal);
    vShadowPosition = uShadowViewProjection * worldPosition;
    gl_Position = uMvp * vec4(aPosition, 1.0);
}
)glsl";
        static constexpr const char *fragmentSource = R"glsl(#version 150 core
const int MaxLights = 16;
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec4 vShadowPosition;
out vec4 outColor;
uniform vec3 uCameraPosition;
uniform int uLightCount;
uniform vec4 uLightPositionKind[MaxLights];
uniform vec4 uLightDirectionRange[MaxLights];
uniform vec4 uLightColorIntensity[MaxLights];
uniform vec2 uLightCone[MaxLights];
uniform sampler2D uShadowMap;
uniform int uShadowEnabled;
uniform int uShadowLightIndex;
uniform vec3 uSelectionColor;
uniform float uSelectionStrength;
float directionalShadow(vec3 normal, vec3 lightDirection)
{
    if (uShadowEnabled == 0)
        return 1.0;
    vec3 projected = vShadowPosition.xyz / vShadowPosition.w;
    vec3 shadowCoordinate = projected * 0.5 + 0.5;
    if (shadowCoordinate.x <= 0.0 || shadowCoordinate.x >= 1.0 ||
        shadowCoordinate.y <= 0.0 || shadowCoordinate.y >= 1.0 ||
        shadowCoordinate.z <= 0.0 || shadowCoordinate.z >= 1.0)
        return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float bias = max(0.0025 * (1.0 - dot(normal, lightDirection)), 0.00045);
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            float closestDepth = texture(uShadowMap, shadowCoordinate.xy + vec2(x, y) * texel).r;
            visibility += shadowCoordinate.z - bias <= closestDepth ? 1.0 : 0.0;
        }
    return visibility / 9.0;
}
void main()
{
    vec3 normal = normalize(vWorldNormal);
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    vec3 baseColor = vec3(0.68, 0.70, 0.74);
    vec3 lighting = baseColor * 0.08;
    for (int index = 0; index < uLightCount; ++index)
    {
        int kind = int(uLightPositionKind[index].w + 0.5);
        vec3 lightDirection;
        float attenuation = 1.0;
        if (kind == 0)
        {
            lightDirection = normalize(-uLightDirectionRange[index].xyz);
        }
        else
        {
            vec3 toLight = uLightPositionKind[index].xyz - vWorldPosition;
            float distanceToLight = length(toLight);
            lightDirection = distanceToLight > 0.0001 ? toLight / distanceToLight : normal;
            float range = max(uLightDirectionRange[index].w, 0.0001);
            float normalizedDistance = distanceToLight / range;
            float rangeFade = max(1.0 - normalizedDistance * normalizedDistance, 0.0);
            attenuation = rangeFade * rangeFade;
            if (kind == 2)
            {
                float coneCosine = dot(normalize(uLightDirectionRange[index].xyz), -lightDirection);
                attenuation *= smoothstep(uLightCone[index].y, uLightCone[index].x, coneCosine);
            }
        }
        float diffuse = max(dot(normal, lightDirection), 0.0);
        vec3 halfDirection = normalize(lightDirection + viewDirection);
        float specular = pow(max(dot(normal, halfDirection), 0.0), 48.0) * 0.18;
        vec3 radiance = uLightColorIntensity[index].rgb * uLightColorIntensity[index].a * attenuation;
        float visibility = index == uShadowLightIndex ? directionalShadow(normal, lightDirection) : 1.0;
        lighting += radiance * (baseColor * diffuse + specular) * visibility;
    }
    vec3 mapped = lighting / (lighting + vec3(1.0));
    vec3 displayColor = pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(mix(displayColor, uSelectionColor, uSelectionStrength), 1.0);
}
)glsl";

        auto vertex = CompileShader(GL_VERTEX_SHADER, vertexSource);
        if (vertex.HasError()) {
            return Result<void>::Failure(vertex.ErrorValue());
        }
        auto fragment = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
        if (fragment.HasError()) {
            glDeleteShader(vertex.Value());
            return Result<void>::Failure(fragment.ErrorValue());
        }

        program_ = glCreateProgram();
        glAttachShader(program_, vertex.Value());
        glAttachShader(program_, fragment.Value());
        glBindAttribLocation(program_, 0, "aPosition");
        glBindAttribLocation(program_, 1, "aNormal");
        glBindAttribLocation(program_, 2, "aUv");
        glBindFragDataLocation(program_, 0, "outColor");
        glLinkProgram(program_);
        glDeleteShader(vertex.Value());
        glDeleteShader(fragment.Value());

        GLint linked = GL_FALSE;
        glGetProgramiv(program_, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            GLint logLength = 0;
            glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &logLength);
            std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
            glGetProgramInfoLog(program_, logLength, nullptr, log.data());
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportShaderLinkFailed, "Viewport shader linking failed: " + log));
        }
        uniforms_.mvp = glGetUniformLocation(program_, "uMvp");
        uniforms_.model = glGetUniformLocation(program_, "uModel");
        uniforms_.cameraPosition = glGetUniformLocation(program_, "uCameraPosition");
        uniforms_.lightCount = glGetUniformLocation(program_, "uLightCount");
        uniforms_.lightPositionKind = glGetUniformLocation(program_, "uLightPositionKind[0]");
        uniforms_.lightDirectionRange = glGetUniformLocation(program_, "uLightDirectionRange[0]");
        uniforms_.lightColorIntensity = glGetUniformLocation(program_, "uLightColorIntensity[0]");
        uniforms_.lightCone = glGetUniformLocation(program_, "uLightCone[0]");
        uniforms_.shadowViewProjection = glGetUniformLocation(program_, "uShadowViewProjection");
        uniforms_.shadowMap = glGetUniformLocation(program_, "uShadowMap");
        uniforms_.shadowEnabled = glGetUniformLocation(program_, "uShadowEnabled");
        uniforms_.shadowLightIndex = glGetUniformLocation(program_, "uShadowLightIndex");
        uniforms_.selectionColor = glGetUniformLocation(program_, "uSelectionColor");
        uniforms_.selectionStrength = glGetUniformLocation(program_, "uSelectionStrength");
        if (uniforms_.mvp < 0 || uniforms_.model < 0 || uniforms_.cameraPosition < 0 || uniforms_.lightCount < 0 ||
            uniforms_.lightPositionKind < 0 || uniforms_.lightDirectionRange < 0 || uniforms_.lightColorIntensity < 0 ||
            uniforms_.lightCone < 0 || uniforms_.shadowViewProjection < 0 || uniforms_.shadowMap < 0 || uniforms_.shadowEnabled < 0 ||
            uniforms_.shadowLightIndex < 0 || uniforms_.selectionColor < 0 || uniforms_.selectionStrength < 0) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportShaderContractInvalid, "Viewport shader is missing a required frame uniform."));
        }

        return Result<void>::Success();
    }

    Result<void> EditorViewportRendererOpenGL::CreateShadowResources() {
        static constexpr const char *shadowVertexSource = R"glsl(#version 150 core
in vec3 aPosition;
uniform mat4 uShadowMvp;
void main()
{
    gl_Position = uShadowMvp * vec4(aPosition, 1.0);
}
)glsl";
        static constexpr const char *shadowFragmentSource = R"glsl(#version 150 core
void main() {}
)glsl";

        auto vertex = CompileShader(GL_VERTEX_SHADER, shadowVertexSource);
        if (vertex.HasError())
            return Result<void>::Failure(vertex.ErrorValue());
        auto fragment = CompileShader(GL_FRAGMENT_SHADER, shadowFragmentSource);
        if (fragment.HasError()) {
            glDeleteShader(vertex.Value());
            return Result<void>::Failure(fragment.ErrorValue());
        }
        shadowProgram_ = glCreateProgram();
        glAttachShader(shadowProgram_, vertex.Value());
        glAttachShader(shadowProgram_, fragment.Value());
        glBindAttribLocation(shadowProgram_, 0, "aPosition");
        glLinkProgram(shadowProgram_);
        glDeleteShader(vertex.Value());
        glDeleteShader(fragment.Value());

        GLint linked = GL_FALSE;
        glGetProgramiv(shadowProgram_, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            GLint logLength = 0;
            glGetProgramiv(shadowProgram_, GL_INFO_LOG_LENGTH, &logLength);
            std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
            glGetProgramInfoLog(shadowProgram_, logLength, nullptr, log.data());
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportShaderLinkFailed, "Viewport shadow shader linking failed: " + log));
        }
        uniforms_.shadowMvp = glGetUniformLocation(shadowProgram_, "uShadowMvp");
        if (uniforms_.shadowMvp < 0) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportShaderContractInvalid, "Viewport shadow shader is missing its matrix uniform."));
        }

        constexpr auto shadowMapResolution = static_cast<GLsizei>(EditorViewportDirectionalShadowMapResolution);
        GLint previousDrawFramebuffer = 0;
        GLint previousReadFramebuffer = 0;
        GLint previousTexture = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
        glGenFramebuffers(1, &shadowFramebuffer_);
        glGenTextures(1, &shadowDepthTexture_);
        glBindTexture(GL_TEXTURE_2D, shadowDepthTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowMapResolution, shadowMapResolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        constexpr std::array<GLfloat, 4> borderColor{1.0F, 1.0F, 1.0F, 1.0F};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor.data());
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepthTexture_, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
        if (shadowFramebuffer_ == 0 || shadowDepthTexture_ == 0 || status != GL_FRAMEBUFFER_COMPLETE) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportFramebufferIncomplete, "OpenGL directional shadow framebuffer is incomplete."));
        }
        return Result<void>::Success();
    }

    Result<void> EditorViewportRendererOpenGL::DrawDirectionalShadowMap(const Render::RenderSceneView &scene,
                                                                        const EditorViewportDirectionalShadowView &shadow) {
        constexpr auto shadowMapResolution = static_cast<GLsizei>(EditorViewportDirectionalShadowMapResolution);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffer_);
        glViewport(0, 0, shadowMapResolution, shadowMapResolution);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(shadowProgram_);
        for (const Render::RenderStaticMeshInstance &instance : scene.instances) {
            const auto mesh = meshes_.find(instance.mesh.id.value);
            if (mesh == meshes_.end()) {
                return Result<void>::Failure(
                    MakeViewportError(RendererErrors::ViewportStaleMeshResource, "Shadow pass instance references a stale mesh resource."));
            }
            const Math::Mat4 shadowMvp = Math::Multiply(shadow.viewProjection, instance.localToWorld);
            glUniformMatrix4fv(uniforms_.shadowMvp, 1, GL_FALSE, shadowMvp.values.data());
            glBindVertexArray(mesh->second.vertexArray);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->second.indexCount), GL_UNSIGNED_INT, nullptr);
        }
        glCullFace(GL_BACK);
        return Result<void>::Success();
    }

    void EditorViewportRendererOpenGL::UploadLighting(const Render::RenderSceneView &scene,
                                                      const std::optional<EditorViewportDirectionalShadowView> &shadow) const {
        std::array<float, Render::MaximumForwardLights * 4> positionKind{};
        std::array<float, Render::MaximumForwardLights * 4> directionRange{};
        std::array<float, Render::MaximumForwardLights * 4> colorIntensity{};
        std::array<float, Render::MaximumForwardLights * 2> cones{};
        for (std::size_t index = 0; index < scene.lights.size(); ++index) {
            const Render::RenderLight &light = scene.lights[index];
            positionKind[index * 4] = light.position.x;
            positionKind[index * 4 + 1] = light.position.y;
            positionKind[index * 4 + 2] = light.position.z;
            positionKind[index * 4 + 3] = static_cast<float>(light.kind);
            directionRange[index * 4] = light.direction.x;
            directionRange[index * 4 + 1] = light.direction.y;
            directionRange[index * 4 + 2] = light.direction.z;
            directionRange[index * 4 + 3] = light.range;
            colorIntensity[index * 4] = light.color.x;
            colorIntensity[index * 4 + 1] = light.color.y;
            colorIntensity[index * 4 + 2] = light.color.z;
            colorIntensity[index * 4 + 3] = light.intensity;
            cones[index * 2] = light.innerConeCosine;
            cones[index * 2 + 1] = light.outerConeCosine;
        }
        glUniform3f(uniforms_.cameraPosition, scene.camera.position.x, scene.camera.position.y, scene.camera.position.z);
        glUniform1i(uniforms_.lightCount, static_cast<GLint>(scene.lights.size()));
        const Math::Mat4 shadowViewProjection = shadow.has_value() ? shadow->viewProjection : Math::Mat4::Identity();
        glUniformMatrix4fv(uniforms_.shadowViewProjection, 1, GL_FALSE, shadowViewProjection.values.data());
        glUniform1i(uniforms_.shadowMap, 7);
        glUniform1i(uniforms_.shadowEnabled, shadow.has_value() ? 1 : 0);
        glUniform1i(uniforms_.shadowLightIndex, shadow.has_value() ? static_cast<GLint>(shadow->lightIndex) : -1);
        if (!scene.lights.empty()) {
            glUniform4fv(uniforms_.lightPositionKind, static_cast<GLsizei>(scene.lights.size()), positionKind.data());
            glUniform4fv(uniforms_.lightDirectionRange, static_cast<GLsizei>(scene.lights.size()), directionRange.data());
            glUniform4fv(uniforms_.lightColorIntensity, static_cast<GLsizei>(scene.lights.size()), colorIntensity.data());
            glUniform2fv(uniforms_.lightCone, static_cast<GLsizei>(scene.lights.size()), cones.data());
        }
    }

    Result<void> EditorViewportRendererOpenGL::SynchronizeMeshes(const std::span<const EditorViewportMeshResourceView> resources) {
        GLint previousVertexArray = 0;
        GLint previousArrayBuffer = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);

        for (const EditorViewportMeshResourceView &resource : resources) {
            if (auto existing = meshes_.find(resource.handle.id.value); existing != meshes_.end()) {
                if (existing->second.generation == resource.handle.generation)
                    continue;
                DestroyMesh(existing->second);
                meshes_.erase(existing);
            }
            GpuMesh mesh{.indexCount = static_cast<std::uint32_t>(resource.indices.size()), .generation = resource.handle.generation};
            glGenVertexArrays(1, &mesh.vertexArray);
            glBindVertexArray(mesh.vertexArray);
            glGenBuffers(1, &mesh.vertexBuffer);
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(resource.vertices.size_bytes()), resource.vertices.data(),
                         GL_STATIC_DRAW);
            glGenBuffers(1, &mesh.indexBuffer);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(resource.indices.size_bytes()), resource.indices.data(),
                         GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(Render::MeshVertex)), nullptr);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(Render::MeshVertex)),
                                  reinterpret_cast<const void *>(
                                      static_cast<std::uintptr_t>(offsetof(Render::MeshVertex, normal))));  // NOSONAR(cpp:S3630)
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(Render::MeshVertex)),
                                  reinterpret_cast<const void *>(
                                      static_cast<std::uintptr_t>(offsetof(Render::MeshVertex, uv))));  // NOSONAR(cpp:S3630)
            if (mesh.vertexArray == 0 || mesh.vertexBuffer == 0 || mesh.indexBuffer == 0) {
                DestroyMesh(mesh);
                glBindVertexArray(static_cast<GLuint>(previousVertexArray));
                glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
                return Result<void>::Failure(
                    MakeViewportError(RendererErrors::ViewportGeometryCreationFailed, "Failed to upload a viewport mesh resource."));
            }
            meshes_.try_emplace(resource.handle.id.value, mesh);
        }
        for (auto mesh = meshes_.begin(); mesh != meshes_.end();) {
            const bool present = std::ranges::any_of(resources, [&](const EditorViewportMeshResourceView &resource) {
                return resource.handle.id.value == mesh->first && resource.handle.generation == mesh->second.generation;
            });
            if (!present) {
                DestroyMesh(mesh->second);
                mesh = meshes_.erase(mesh);
            } else
                ++mesh;
        }
        glBindVertexArray(static_cast<GLuint>(previousVertexArray));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
        return Result<void>::Success();
    }

    void EditorViewportRendererOpenGL::DestroyMesh(GpuMesh &mesh) noexcept {
        if (mesh.indexBuffer != 0)
            glDeleteBuffers(1, &mesh.indexBuffer);
        if (mesh.vertexBuffer != 0)
            glDeleteBuffers(1, &mesh.vertexBuffer);
        if (mesh.vertexArray != 0)
            glDeleteVertexArrays(1, &mesh.vertexArray);
        mesh = {};
    }

    Result<void> EditorViewportRendererOpenGL::RecreateTarget(const EditorViewportExtent extent) {
        GLint previousDrawFramebuffer = 0;
        GLint previousReadFramebuffer = 0;
        GLint previousTexture = 0;
        GLint previousRenderbuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);

        if (framebuffer_ == 0) {
            glGenFramebuffers(1, &framebuffer_);
        }
        if (colorTexture_ == 0) {
            glGenTextures(1, &colorTexture_);
        }
        if (depthBuffer_ == 0) {
            glGenRenderbuffers(1, &depthBuffer_);
        }

        // Keep object identities stable across resize. ImGui records the texture ID
        // before the viewport render pass executes later in the same frame.
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glBindTexture(GL_TEXTURE_2D, colorTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(extent.width), static_cast<GLsizei>(extent.height), 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture_, 0);

        glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, static_cast<GLsizei>(extent.width),
                              static_cast<GLsizei>(extent.height));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            DestroyTarget();
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportFramebufferIncomplete, "OpenGL viewport framebuffer is incomplete."));
        }
        allocatedExtent_ = extent;
        return Result<void>::Success();
    }

    void EditorViewportRendererOpenGL::DestroyTarget() noexcept {
        if (depthBuffer_ != 0) {
            glDeleteRenderbuffers(1, &depthBuffer_);
            depthBuffer_ = 0;
        }
        if (colorTexture_ != 0) {
            glDeleteTextures(1, &colorTexture_);
            colorTexture_ = 0;
        }
        if (framebuffer_ != 0) {
            glDeleteFramebuffers(1, &framebuffer_);
            framebuffer_ = 0;
        }
        allocatedExtent_ = {};
    }
}  // namespace Horo::Editor
