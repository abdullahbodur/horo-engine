#include "EditorViewportRendererOpenGL.h"

#include "OpenGLViewportShaders.h"
#include "editor/renderer/EditorRendererErrors.h"
#include "editor/renderer/grid/EditorViewportGridGeometry.h"
#include "editor/screens/workspace/panels/viewport/visualizers/light/LightVisualizerGeometry.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <glad/gl.h>
#include <limits>
#include <string>
#include <utility>

namespace Horo::Editor {
    namespace {
        constexpr std::uint32_t maxViewportDimension = 8192;

        [[nodiscard]] Error MakeViewportError(const ErrorCodeDescriptor &descriptor, std::string message) {
            return MakeError(descriptor, std::move(message));
        }

        template <typename Handle>
        [[nodiscard]] Result<bool> ResourceReady(const Render::RenderFrontend &frontend, const Handle handle,
                                                 const Render::ResourceOperationId operation) {
            if (!handle.IsValid())
                return Result<bool>::Success(false);
            const auto state = frontend.ResourceState(handle);
            if (state.HasValue())
                return Result<bool>::Success(state.Value() == Render::RenderResourceState::Ready);
            if (operation.IsValid()) {
                const Result<void> completion = frontend.ResourceOperationResult(operation);
                if (completion.HasError() && completion.ErrorValue().code.Value() != "render.frontend.resource.operation_pending")
                    return Result<bool>::Failure(completion.ErrorValue());
            }
            return Result<bool>::Failure(state.ErrorValue());
        }

        [[nodiscard]] Result<bool> AdvanceTexture(Render::RenderFrontend &frontend, const Render::RenderTextureDescriptor &descriptor,
                                                  Render::RenderTextureHandle &handle, Render::ResourceOperationId &operation) {
            if (!handle.IsValid()) {
                auto created = frontend.CreateTexture(descriptor);
                if (created.HasError())
                    return Result<bool>::Failure(created.ErrorValue());
                handle = created.Value().handle;
                operation = created.Value().operation;
            }
            return ResourceReady(frontend, handle, operation);
        }

        [[nodiscard]] Result<bool> AdvanceTextureView(Render::RenderFrontend &frontend,
                                                      const Render::RenderTextureViewDescriptor &descriptor,
                                                      Render::RenderTextureViewHandle &handle, Render::ResourceOperationId &operation) {
            if (!handle.IsValid()) {
                auto created = frontend.CreateTextureView(descriptor);
                if (created.HasError())
                    return Result<bool>::Failure(created.ErrorValue());
                handle = created.Value().handle;
                operation = created.Value().operation;
            }
            return ResourceReady(frontend, handle, operation);
        }

        [[nodiscard]] Result<bool> AdvanceRenderTarget(Render::RenderFrontend &frontend, const Render::RenderTargetDescriptor &descriptor,
                                                       Render::RenderTargetHandle &handle, Render::ResourceOperationId &operation) {
            if (!handle.IsValid()) {
                auto created = frontend.CreateRenderTarget(descriptor);
                if (created.HasError())
                    return Result<bool>::Failure(created.ErrorValue());
                handle = created.Value().handle;
                operation = created.Value().operation;
            }
            return ResourceReady(frontend, handle, operation);
        }

        [[nodiscard]] bool ExtentChanged(const EditorViewportExtent allocated, const EditorViewportExtent requested) noexcept {
            return allocated.IsValid() && allocated != requested;
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

        void RestoreCapability(const GLenum capability, const GLboolean enabled) noexcept {
            if (enabled == GL_TRUE)
                glEnable(capability);
            else
                glDisable(capability);
        }

        /** @brief Restores host-owned OpenGL bindings and fixed-function state on every pass exit. */
        class OpenGLStateSnapshot final {
        public:
            OpenGLStateSnapshot() noexcept {
                depthTestEnabled_ = glIsEnabled(GL_DEPTH_TEST);
                scissorTestEnabled_ = glIsEnabled(GL_SCISSOR_TEST);
                blendEnabled_ = glIsEnabled(GL_BLEND);
                cullFaceEnabled_ = glIsEnabled(GL_CULL_FACE);
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer_);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer_);
                glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray_);
                glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer_);
                glGetIntegerv(GL_DEPTH_FUNC, &depthFunction_);
                glGetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode_);
                glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture_);
                glActiveTexture(GL_TEXTURE7);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &shadowTexture_);
                glGetIntegerv(GL_VIEWPORT, viewport_.data());
                glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor_.data());
                glGetBooleanv(GL_COLOR_WRITEMASK, colorMask_.data());
                glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask_);
            }

            ~OpenGLStateSnapshot() {
                glBindVertexArray(static_cast<GLuint>(vertexArray_));
                glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer_));
                glUseProgram(static_cast<GLuint>(program_));
                glDepthFunc(static_cast<GLenum>(depthFunction_));
                RestoreCapability(GL_DEPTH_TEST, depthTestEnabled_);
                RestoreCapability(GL_SCISSOR_TEST, scissorTestEnabled_);
                RestoreCapability(GL_BLEND, blendEnabled_);
                RestoreCapability(GL_CULL_FACE, cullFaceEnabled_);
                glCullFace(static_cast<GLenum>(cullFaceMode_));
                glColorMask(colorMask_[0], colorMask_[1], colorMask_[2], colorMask_[3]);
                glDepthMask(depthMask_);
                glClearColor(clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer_));
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer_));
                glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
                glActiveTexture(GL_TEXTURE7);
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(shadowTexture_));
                glActiveTexture(static_cast<GLenum>(activeTexture_));
            }

            OpenGLStateSnapshot(const OpenGLStateSnapshot &) = delete;
            OpenGLStateSnapshot &operator=(const OpenGLStateSnapshot &) = delete;

        private:
            GLint drawFramebuffer_{0};
            GLint readFramebuffer_{0};
            GLint program_{0};
            GLint vertexArray_{0};
            GLint arrayBuffer_{0};
            GLint depthFunction_{0};
            GLint cullFaceMode_{0};
            GLint activeTexture_{0};
            GLint shadowTexture_{0};
            std::array<GLint, 4> viewport_{};
            std::array<GLfloat, 4> clearColor_{};
            std::array<GLboolean, 4> colorMask_{};
            GLboolean depthMask_{GL_TRUE};
            GLboolean depthTestEnabled_{GL_FALSE};
            GLboolean scissorTestEnabled_{GL_FALSE};
            GLboolean blendEnabled_{GL_FALSE};
            GLboolean cullFaceEnabled_{GL_FALSE};
        };
    }  // namespace

    /** @copydoc EditorViewportRendererOpenGL::EditorViewportRendererOpenGL */
    EditorViewportRendererOpenGL::EditorViewportRendererOpenGL(Render::RenderFrontend &frontend) noexcept : frontend_(&frontend) {}

    /** @copydoc EditorViewportRendererOpenGL::~EditorViewportRendererOpenGL */
    EditorViewportRendererOpenGL::~EditorViewportRendererOpenGL() {
        Shutdown();
    }

    /** @copydoc EditorViewportRendererOpenGL::Initialize */
    Result<void> EditorViewportRendererOpenGL::Initialize() {
        if (initialized_ || frontend_ == nullptr) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportAlreadyInitialized, "Editor viewport renderer is already initialized."));
        }
        if (const Result<void> program = CreateProgram(); program.HasError()) {
            Shutdown();
            return program;
        }
        if (const Result<void> shadowResources = CreateShadowProgram(); shadowResources.HasError()) {
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
        ReleaseTarget();
        for (auto &[id, mesh] : meshes_)
            ReleaseMesh(mesh);
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
        uniforms_ = {};
        requestedExtent_ = {};
        gridOptions_ = {};
        lightVisualizerOptions_ = {};
        targetHandle_ = {};
        editorImageIdentity_ = 0;
        meshesReady_ = false;
        initialized_ = false;
    }

    /** @copydoc EditorViewportRendererOpenGL::RequestExtent */
    void EditorViewportRendererOpenGL::RequestExtent(const EditorViewportExtent extent) noexcept {
        requestedExtent_.width = std::min(extent.width, maxViewportDimension);
        requestedExtent_.height = std::min(extent.height, maxViewportDimension);
    }

    /** @copydoc EditorViewportRendererOpenGL::PrepareResources */
    Result<std::optional<Render::RenderTargetHandle>> EditorViewportRendererOpenGL::PrepareResources(Render::RenderFrontend &frontend,
                                                                                                     const Render::RenderSceneView &scene) {
        if (!initialized_ || frontend_ != &frontend) {
            return Result<std::optional<Render::RenderTargetHandle>>::Failure(
                MakeViewportError(RendererErrors::ViewportNotInitialized, "Viewport resource owner is not initialized."));
        }
        if (const Result<void> meshes = SynchronizeMeshes(scene.meshResources); meshes.HasError())
            return Result<std::optional<Render::RenderTargetHandle>>::Failure(meshes.ErrorValue());
        if (const Result<void> shadow = SynchronizeShadowResources(); shadow.HasError())
            return Result<std::optional<Render::RenderTargetHandle>>::Failure(shadow.ErrorValue());
        if (requestedExtent_.IsValid()) {
            if (const Result<void> target = SynchronizeTarget(requestedExtent_); target.HasError())
                return Result<std::optional<Render::RenderTargetHandle>>::Failure(target.ErrorValue());
        }
        if (targetHandle_.IsValid()) {
            const auto ready = frontend_->ResourceState(targetHandle_);
            if (ready.HasValue() && ready.Value() == Render::RenderResourceState::Ready)
                return Result<std::optional<Render::RenderTargetHandle>>::Success(targetHandle_);
        }
        return Result<std::optional<Render::RenderTargetHandle>>::Success(std::nullopt);
    }

    /** @copydoc EditorViewportRendererOpenGL::RequestGrid */
    void EditorViewportRendererOpenGL::RequestGrid(const EditorViewportGridOptions &options) noexcept {
        gridOptions_ = options;
        if (!std::isfinite(gridOptions_.targetMinorSpacingPixels) || gridOptions_.targetMinorSpacingPixels <= 0.0F)
            gridOptions_.targetMinorSpacingPixels = 48.0F;
    }

    /** @copydoc EditorViewportRendererOpenGL::RequestLightVisualizer */
    void EditorViewportRendererOpenGL::RequestLightVisualizer(const EditorViewportLightVisualizerOptions &options) noexcept {
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
    Result<void> EditorViewportRendererOpenGL::ExecuteStaticMeshPass(const Render::StaticMeshPassDescriptor &descriptor) {
        if (!initialized_) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportNotInitialized, "Viewport renderer is not initialized."));
        }
        // A panel must request an extent every UI frame. Consuming the request keeps
        // hidden/inactive viewport tabs from spending GPU time in the background.
        const EditorViewportExtent requestedExtent = std::exchange(requestedExtent_, {});
        if (!requestedExtent.IsValid())
            return Result<void>::Success();
        if (const Result<void> valid = ValidatePassRequest(descriptor, requestedExtent); valid.HasError())
            return valid;
        targetHandle_ = descriptor.target;
        const auto shadow = BuildEditorViewportDirectionalShadowView(descriptor.scene, Math::ClipDepthRange::NegativeOneToOne);
        if (shadow.HasError())
            return Result<void>::Failure(shadow.ErrorValue());
        const float aspect = static_cast<float>(allocatedExtent_.width) / static_cast<float>(allocatedExtent_.height);
        const OpenGLStateSnapshot stateSnapshot;
        return RenderViewportPass(descriptor, shadow.Value(), aspect);
    }

    Result<void> EditorViewportRendererOpenGL::ValidatePassRequest(const Render::StaticMeshPassDescriptor &descriptor,
                                                                   const EditorViewportExtent requestedExtent) const {
        if (!descriptor.IsValid() || descriptor.extent.width != requestedExtent.width ||
            descriptor.extent.height != requestedExtent.height) {
            return Result<void>::Failure(MakeViewportError(RendererErrors::ViewportInvalidScene, "Editor viewport scene data is invalid."));
        }
        if (targetHandle_.IsValid() && targetHandle_ != descriptor.target) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportStaleTarget, "Viewport pass references a stale render target."));
        }
        if (requestedExtent.width != allocatedExtent_.width || requestedExtent.height != allocatedExtent_.height)
            return Result<void>::Failure(MakeViewportError(RendererErrors::ViewportStaleTarget, "Viewport target extent is not ready."));
        return Result<void>::Success();
    }

    Result<void> EditorViewportRendererOpenGL::RenderViewportPass(const Render::StaticMeshPassDescriptor &descriptor,
                                                                  const std::optional<EditorViewportDirectionalShadowView> &shadow,
                                                                  const float aspect) {
        if (shadow.has_value()) {
            if (const Result<void> renderedShadow = DrawDirectionalShadowMap(descriptor.scene, *shadow); renderedShadow.HasError()) {
                return renderedShadow;
            }
        }
        if (const Result<void> bound = resourceBridge_.BindRenderTarget(*frontend_, targetHandle_); bound.HasError())
            return bound;
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
        if (const Result<void> boundShadow = resourceBridge_.BindTexture(*frontend_, shadowDepthTextureView_, 7); boundShadow.HasError())
            return boundShadow;
        UploadLighting(descriptor.scene, shadow);
        if (const Result<void> grid = DrawGrid(descriptor.scene.camera, aspect, static_cast<float>(allocatedExtent_.height));
            grid.HasError()) {
            return grid;
        }
        if (const Result<void> meshes = DrawSceneMeshes(descriptor.scene, aspect); meshes.HasError())
            return meshes;
        if (const Result<void> visualizer = DrawLightVisualizer(descriptor.scene.camera, aspect); visualizer.HasError())
            return visualizer;
        return Result<void>::Success();
    }

    Result<void> EditorViewportRendererOpenGL::DrawSceneMeshes(const Render::RenderSceneView &scene, const float aspect) {
        for (const Render::RenderStaticMeshInstance &instance : scene.instances) {
            const auto mesh = meshes_.find(instance.mesh.id.value);
            if (mesh == meshes_.end()) {
                return Result<void>::Failure(
                    MakeViewportError(RendererErrors::ViewportStaleMeshResource, "Viewport instance references a stale mesh resource."));
            }
            if (const Result<void> bound = resourceBridge_.BindMesh(*frontend_, mesh->second.mesh); bound.HasError())
                return bound;
            const Result<Math::Mat4> mvp =
                BuildRenderMvp(scene.camera, instance.localToWorld, aspect, Math::ClipDepthRange::NegativeOneToOne);
            if (mvp.HasError())
                return Result<void>::Failure(mvp.ErrorValue());
            glUniformMatrix4fv(uniforms_.mvp, 1, GL_FALSE, mvp.Value().values.data());
            glUniformMatrix4fv(uniforms_.model, 1, GL_FALSE, instance.localToWorld.values.data());
            glUniform3f(uniforms_.selectionColor, instance.presentation.tint.x, instance.presentation.tint.y, instance.presentation.tint.z);
            glUniform1f(uniforms_.selectionStrength, instance.presentation.tintStrength);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->second.indexCount), GL_UNSIGNED_INT, nullptr);
        }
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
            .textureId = editorImageIdentity_,
            .u0 = 0.0F,
            .v0 = 1.0F,
            .u1 = 1.0F,
            .v1 = 0.0F,
        };
    }

    /** @copydoc EditorViewportRendererOpenGL::IsReady */
    bool EditorViewportRendererOpenGL::IsReady() const noexcept {
        return initialized_ && meshesReady_ && editorImageIdentity_ != 0 && targetHandle_.IsValid() && allocatedExtent_.IsValid();
    }

    Result<void> EditorViewportRendererOpenGL::CreateProgram() {
        auto vertex = CompileShader(GL_VERTEX_SHADER, Detail::ViewportVertexShader);
        if (vertex.HasError()) {
            return Result<void>::Failure(vertex.ErrorValue());
        }
        auto fragment = CompileShader(GL_FRAGMENT_SHADER, Detail::ViewportFragmentShader);
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
        return LoadUniformLocations();
    }

    Result<void> EditorViewportRendererOpenGL::LoadUniformLocations() {
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
        const std::array locations{
            uniforms_.mvp,
            uniforms_.model,
            uniforms_.cameraPosition,
            uniforms_.lightCount,
            uniforms_.lightPositionKind,
            uniforms_.lightDirectionRange,
            uniforms_.lightColorIntensity,
            uniforms_.lightCone,
            uniforms_.shadowViewProjection,
            uniforms_.shadowMap,
            uniforms_.shadowEnabled,
            uniforms_.shadowLightIndex,
            uniforms_.selectionColor,
            uniforms_.selectionStrength,
        };
        if (!std::ranges::all_of(locations, [](const std::int32_t location) {
            return location >= 0;
        })) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportShaderContractInvalid, "Viewport shader is missing a required frame uniform."));
        }
        return Result<void>::Success();
    }

    Result<void> EditorViewportRendererOpenGL::CreateShadowProgram() {
        auto vertex = CompileShader(GL_VERTEX_SHADER, Detail::ShadowVertexShader);
        if (vertex.HasError())
            return Result<void>::Failure(vertex.ErrorValue());
        auto fragment = CompileShader(GL_FRAGMENT_SHADER, Detail::ShadowFragmentShader);
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

        return Result<void>::Success();
    }

    Result<void> EditorViewportRendererOpenGL::DrawDirectionalShadowMap(const Render::RenderSceneView &scene,
                                                                        const EditorViewportDirectionalShadowView &shadow) {
        constexpr auto shadowMapResolution = static_cast<GLsizei>(EditorViewportDirectionalShadowMapResolution);
        if (const Result<void> bound = resourceBridge_.BindRenderTarget(*frontend_, shadowTarget_); bound.HasError())
            return bound;
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
            if (const Result<void> bound = resourceBridge_.BindMesh(*frontend_, mesh->second.mesh); bound.HasError())
                return bound;
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
        meshesReady_ = true;

        for (const EditorViewportMeshResourceView &resource : resources) {
            auto [position, inserted] = meshes_.try_emplace(resource.handle.id.value);
            ResidentMesh &resident = position->second;
            if (!inserted && resident.sourceGeneration != resource.handle.generation)
                ReleaseMesh(resident);
            const auto ready = AdvanceResidentMesh(resource, resident);
            if (ready.HasError())
                return Result<void>::Failure(ready.ErrorValue());
            meshesReady_ = meshesReady_ && ready.Value();
        }

        RetireMissingMeshes(resources);
        return Result<void>::Success();
    }

    Result<bool> EditorViewportRendererOpenGL::AdvanceResidentMesh(const EditorViewportMeshResourceView &resource, ResidentMesh &resident) {
        if (resident.sourceGeneration == 0) {
            const Result<void> created = CreateResidentMeshBuffers(resource, resident);
            if (created.HasError())
                return Result<bool>::Failure(created.ErrorValue());
        }
        const auto vertexReady = ResourceReady(*frontend_, resident.vertexBuffer, resident.vertexOperation);
        if (vertexReady.HasError())
            return Result<bool>::Failure(vertexReady.ErrorValue());
        const auto indexReady = ResourceReady(*frontend_, resident.indexBuffer, resident.indexOperation);
        if (indexReady.HasError())
            return Result<bool>::Failure(indexReady.ErrorValue());
        if (!resident.mesh.IsValid() && vertexReady.Value() && indexReady.Value()) {
            auto mesh = frontend_->CreateMesh({.vertexBuffer = resident.vertexBuffer,
                                               .indexBuffer = resident.indexBuffer,
                                               .vertexStride = sizeof(Render::MeshVertex),
                                               .vertexCount = static_cast<std::uint32_t>(resource.vertices.size()),
                                               .indexFormat = Render::RenderIndexFormat::UInt32,
                                               .indexCount = resident.indexCount,
                                               .topology = Render::RenderPrimitiveTopology::Triangles,
                                               .localBounds = resource.localBounds});
            if (mesh.HasError())
                return Result<bool>::Failure(mesh.ErrorValue());
            resident.mesh = mesh.Value().handle;
            resident.meshOperation = mesh.Value().operation;
        }
        return ResourceReady(*frontend_, resident.mesh, resident.meshOperation);
    }

    Result<void> EditorViewportRendererOpenGL::CreateResidentMeshBuffers(const EditorViewportMeshResourceView &resource,
                                                                         ResidentMesh &resident) {
        if (resource.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
            resource.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
            return Result<void>::Failure(
                MakeViewportError(RendererErrors::ViewportGeometryCreationFailed, "Viewport mesh exceeds generic resource count limits."));
        }
        auto vertex = frontend_->CreateBuffer({.byteSize = resource.vertices.size_bytes(),
                                               .usage = Render::RenderBufferUsage::Vertex,
                                               .access = Render::RenderBufferAccess::DeviceLocal},
                                              std::as_bytes(resource.vertices));
        if (vertex.HasError())
            return Result<void>::Failure(vertex.ErrorValue());
        resident.vertexBuffer = vertex.Value().handle;
        resident.vertexOperation = vertex.Value().operation;
        auto index = frontend_->CreateBuffer({.byteSize = resource.indices.size_bytes(),
                                              .usage = Render::RenderBufferUsage::Index,
                                              .access = Render::RenderBufferAccess::DeviceLocal},
                                             std::as_bytes(resource.indices));
        if (index.HasError()) {
            ReleaseMesh(resident);
            return Result<void>::Failure(index.ErrorValue());
        }
        resident.indexBuffer = index.Value().handle;
        resident.indexOperation = index.Value().operation;
        resident.indexCount = static_cast<std::uint32_t>(resource.indices.size());
        resident.sourceGeneration = resource.handle.generation;
        return Result<void>::Success();
    }

    void EditorViewportRendererOpenGL::RetireMissingMeshes(const std::span<const EditorViewportMeshResourceView> resources) noexcept {
        for (auto mesh = meshes_.begin(); mesh != meshes_.end();) {
            const bool present = std::ranges::any_of(resources, [&](const EditorViewportMeshResourceView &resource) {
                return resource.handle.id.value == mesh->first && resource.handle.generation == mesh->second.sourceGeneration;
            });
            if (!present) {
                ReleaseMesh(mesh->second);
                mesh = meshes_.erase(mesh);
            } else {
                ++mesh;
            }
        }
    }

    Result<void> EditorViewportRendererOpenGL::SynchronizeTarget(const EditorViewportExtent extent) {
        if (ExtentChanged(allocatedExtent_, extent))
            ReleaseViewportTarget();
        allocatedExtent_ = extent;
        const Render::FramebufferExtent framebufferExtent{extent.width, extent.height};
        const auto targetReady = AdvanceViewportTarget(framebufferExtent);
        if (targetReady.HasError())
            return Result<void>::Failure(targetReady.ErrorValue());
        if (!targetReady.Value())
            return Result<void>::Success();
        auto image = resourceBridge_.EditorImageIdentity(*frontend_, colorTextureView_);
        if (image.HasError())
            return Result<void>::Failure(image.ErrorValue());
        editorImageIdentity_ = image.Value();
        return Result<void>::Success();
    }

    Result<bool> EditorViewportRendererOpenGL::AdvanceViewportTarget(const Render::FramebufferExtent extent) {
        const auto texturesReady = AdvanceViewportTextures(extent);
        if (texturesReady.HasError() || !texturesReady.Value())
            return texturesReady;
        const auto viewsReady = AdvanceViewportViews();
        if (viewsReady.HasError() || !viewsReady.Value())
            return viewsReady;
        return AdvanceRenderTarget(*frontend_,
                                   {.colorAttachment = colorTextureView_, .depthAttachment = depthTextureView_, .extent = extent},
                                   targetHandle_, targetOperation_);
    }

    Result<bool> EditorViewportRendererOpenGL::AdvanceViewportTextures(const Render::FramebufferExtent extent) {
        const auto color = AdvanceTexture(*frontend_,
                                          {.extent = extent,
                                           .format = Render::RenderTextureFormat::Rgba8Unorm,
                                           .usage = Render::RenderTextureUsage::Sampled | Render::RenderTextureUsage::RenderAttachment},
                                          colorTexture_, colorTextureOperation_);
        if (color.HasError())
            return Result<bool>::Failure(color.ErrorValue());
        const auto depth = AdvanceTexture(*frontend_,
                                          {.extent = extent,
                                           .format = Render::RenderTextureFormat::Depth24Stencil8,
                                           .usage = Render::RenderTextureUsage::RenderAttachment},
                                          depthTexture_, depthTextureOperation_);
        if (depth.HasError())
            return Result<bool>::Failure(depth.ErrorValue());
        return Result<bool>::Success(color.Value() && depth.Value());
    }

    Result<bool> EditorViewportRendererOpenGL::AdvanceViewportViews() {
        const auto color = AdvanceTextureView(*frontend_,
                                              {.texture = colorTexture_,
                                               .format = Render::RenderTextureFormat::Rgba8Unorm,
                                               .aspect = Render::RenderTextureAspect::Color},
                                              colorTextureView_, colorTextureViewOperation_);
        if (color.HasError())
            return Result<bool>::Failure(color.ErrorValue());
        const auto depth = AdvanceTextureView(*frontend_,
                                              {.texture = depthTexture_,
                                               .format = Render::RenderTextureFormat::Depth24Stencil8,
                                               .aspect = Render::RenderTextureAspect::DepthStencil},
                                              depthTextureView_, depthTextureViewOperation_);
        if (depth.HasError())
            return Result<bool>::Failure(depth.ErrorValue());
        return Result<bool>::Success(color.Value() && depth.Value());
    }

    Result<void> EditorViewportRendererOpenGL::SynchronizeShadowResources() {
        const auto targetReady = AdvanceShadowTarget();
        if (targetReady.HasError())
            return Result<void>::Failure(targetReady.ErrorValue());
        meshesReady_ = meshesReady_ && targetReady.Value();
        return Result<void>::Success();
    }

    Result<bool> EditorViewportRendererOpenGL::AdvanceShadowTarget() {
        constexpr Render::FramebufferExtent shadowExtent{EditorViewportDirectionalShadowMapResolution,
                                                         EditorViewportDirectionalShadowMapResolution};
        const auto textureReady =
            AdvanceTexture(*frontend_,
                           {.extent = shadowExtent,
                            .format = Render::RenderTextureFormat::Depth32Float,
                            .usage = Render::RenderTextureUsage::Sampled | Render::RenderTextureUsage::RenderAttachment},
                           shadowDepthTexture_, shadowDepthTextureOperation_);
        if (textureReady.HasError() || !textureReady.Value())
            return textureReady;
        const auto viewReady = AdvanceTextureView(*frontend_,
                                                  {.texture = shadowDepthTexture_,
                                                   .format = Render::RenderTextureFormat::Depth32Float,
                                                   .aspect = Render::RenderTextureAspect::Depth},
                                                  shadowDepthTextureView_, shadowDepthTextureViewOperation_);
        if (viewReady.HasError() || !viewReady.Value())
            return viewReady;
        return AdvanceRenderTarget(*frontend_, {.depthAttachment = shadowDepthTextureView_, .extent = shadowExtent}, shadowTarget_,
                                   shadowTargetOperation_);
    }

    void EditorViewportRendererOpenGL::ReleaseMesh(ResidentMesh &mesh) noexcept {
        if (frontend_ != nullptr) {
            if (mesh.mesh.IsValid())
                static_cast<void>(frontend_->ReleaseMesh(mesh.mesh));
            if (mesh.indexBuffer.IsValid())
                static_cast<void>(frontend_->ReleaseBuffer(mesh.indexBuffer));
            if (mesh.vertexBuffer.IsValid())
                static_cast<void>(frontend_->ReleaseBuffer(mesh.vertexBuffer));
        }
        mesh = {};
    }

    void EditorViewportRendererOpenGL::ReleaseTarget() noexcept {
        ReleaseShadowResources();
        ReleaseViewportTarget();
    }

    void EditorViewportRendererOpenGL::ReleaseShadowResources() noexcept {
        if (frontend_ != nullptr) {
            if (shadowTarget_.IsValid())
                static_cast<void>(frontend_->ReleaseRenderTarget(shadowTarget_));
            if (shadowDepthTextureView_.IsValid())
                static_cast<void>(frontend_->ReleaseTextureView(shadowDepthTextureView_));
            if (shadowDepthTexture_.IsValid())
                static_cast<void>(frontend_->ReleaseTexture(shadowDepthTexture_));
        }
        shadowDepthTexture_ = {};
        shadowDepthTextureView_ = {};
        shadowTarget_ = {};
        shadowDepthTextureOperation_ = {};
        shadowDepthTextureViewOperation_ = {};
        shadowTargetOperation_ = {};
    }

    void EditorViewportRendererOpenGL::ReleaseViewportTarget() noexcept {
        if (frontend_ != nullptr) {
            if (targetHandle_.IsValid())
                static_cast<void>(frontend_->ReleaseRenderTarget(targetHandle_));
            if (depthTextureView_.IsValid())
                static_cast<void>(frontend_->ReleaseTextureView(depthTextureView_));
            if (colorTextureView_.IsValid())
                static_cast<void>(frontend_->ReleaseTextureView(colorTextureView_));
            if (depthTexture_.IsValid())
                static_cast<void>(frontend_->ReleaseTexture(depthTexture_));
            if (colorTexture_.IsValid())
                static_cast<void>(frontend_->ReleaseTexture(colorTexture_));
        }
        colorTexture_ = {};
        depthTexture_ = {};
        colorTextureView_ = {};
        depthTextureView_ = {};
        colorTextureOperation_ = {};
        depthTextureOperation_ = {};
        colorTextureViewOperation_ = {};
        depthTextureViewOperation_ = {};
        targetOperation_ = {};
        targetHandle_ = {};
        editorImageIdentity_ = 0;
        allocatedExtent_ = {};
    }
}  // namespace Horo::Editor
