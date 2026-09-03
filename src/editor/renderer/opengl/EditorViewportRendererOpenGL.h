#pragma once

#include "editor/renderer/EditorViewportRenderer.h"
#include "editor/renderer/opengl/OpenGLViewportResourceBridge.h"

#include <optional>
#include <unordered_map>

namespace Horo::Editor {
    /** @brief OpenGL editor adapter that renders backend-neutral editor scene instances into an offscreen target. */
    class EditorViewportRendererOpenGL final : public IEditorViewportRenderer {
    public:
        /** @brief Borrows the frontend that owns every generic viewport resource. */
        explicit EditorViewportRendererOpenGL(Render::RenderFrontend &frontend) noexcept;
        ~EditorViewportRendererOpenGL() override;

        EditorViewportRendererOpenGL(const EditorViewportRendererOpenGL &) = delete;
        EditorViewportRendererOpenGL &operator=(const EditorViewportRendererOpenGL &) = delete;

        /** @brief Creates shader, geometry, and offscreen target state in the current OpenGL context. */
        [[nodiscard]] Result<void> Initialize();

        /** @brief Releases all OpenGL objects; repeated calls are safe. */
        void Shutdown() noexcept;

        void RequestExtent(EditorViewportExtent extent) noexcept override;
        void RequestGrid(const EditorViewportGridOptions &options) noexcept override;
        void RequestLightVisualizer(const EditorViewportLightVisualizerOptions &options) noexcept override;
        [[nodiscard]] EditorViewportExtent RequestedExtent() const noexcept override;
        [[nodiscard]] Math::ClipDepthRange ClipDepthRange() const noexcept override;
        [[nodiscard]] Result<void> ExecuteStaticMeshPass(const Render::StaticMeshPassDescriptor &descriptor) override;
        [[nodiscard]] Result<std::optional<Render::RenderTargetHandle>> PrepareResources(Render::RenderFrontend &frontend,
                                                                                         const Render::RenderSceneView &scene) override;
        [[nodiscard]] EditorViewportTextureView TextureView() const noexcept override;
        [[nodiscard]] bool IsReady() const noexcept override;

    private:
        [[nodiscard]] Result<void> CreateProgram();
        [[nodiscard]] Result<void> CreateShadowProgram();
        [[nodiscard]] Result<void> ValidatePassRequest(const Render::StaticMeshPassDescriptor &descriptor,
                                                       EditorViewportExtent requestedExtent) const;
        [[nodiscard]] Result<void> RenderViewportPass(const Render::StaticMeshPassDescriptor &descriptor,
                                                      const std::optional<EditorViewportDirectionalShadowView> &shadow, float aspect);
        [[nodiscard]] Result<void> SynchronizeShadowResources();
        [[nodiscard]] Result<bool> AdvanceShadowTarget();
        [[nodiscard]] Result<void> DrawDirectionalShadowMap(const Render::RenderSceneView &scene,
                                                            const EditorViewportDirectionalShadowView &shadow);
        [[nodiscard]] Result<void> DrawSceneMeshes(const Render::RenderSceneView &scene, float aspect);
        [[nodiscard]] Result<void> DrawGrid(const Render::RenderCameraView &camera, float aspect, float viewportHeightPixels) const;
        [[nodiscard]] Result<void> DrawLightVisualizer(const Render::RenderCameraView &camera, float aspect);
        void UploadLighting(const Render::RenderSceneView &scene, const std::optional<EditorViewportDirectionalShadowView> &shadow) const;

        struct ResidentMesh {
            Render::RenderBufferHandle vertexBuffer;
            Render::RenderBufferHandle indexBuffer;
            Render::RenderMeshHandle mesh;
            Render::ResourceOperationId vertexOperation;
            Render::ResourceOperationId indexOperation;
            Render::ResourceOperationId meshOperation;
            std::uint32_t indexCount{0};
            std::uint32_t sourceGeneration{0};
        };

        struct ViewportTargetResources {
            Render::RenderTextureHandle colorTexture;
            Render::RenderTextureHandle depthTexture;
            Render::RenderTextureViewHandle colorView;
            Render::RenderTextureViewHandle depthView;
            Render::RenderTargetHandle target;
            Render::ResourceOperationId colorTextureOperation;
            Render::ResourceOperationId depthTextureOperation;
            Render::ResourceOperationId colorViewOperation;
            Render::ResourceOperationId depthViewOperation;
            Render::ResourceOperationId targetOperation;
            EditorViewportExtent extent;
            std::uintptr_t imageIdentity{0};
        };

        struct UniformLocations {
            std::int32_t mvp{-1};
            std::int32_t model{-1};
            std::int32_t cameraPosition{-1};
            std::int32_t lightCount{-1};
            std::int32_t lightPositionKind{-1};
            std::int32_t lightDirectionRange{-1};
            std::int32_t lightColorIntensity{-1};
            std::int32_t lightCone{-1};
            std::int32_t shadowViewProjection{-1};
            std::int32_t shadowMap{-1};
            std::int32_t shadowEnabled{-1};
            std::int32_t shadowLightIndex{-1};
            std::int32_t shadowMvp{-1};
            std::int32_t selectionColor{-1};
            std::int32_t selectionStrength{-1};
        };

        [[nodiscard]] Result<void> LoadUniformLocations();
        [[nodiscard]] Result<void> SynchronizeMeshes(std::span<const EditorViewportMeshResourceView> resources);
        [[nodiscard]] Result<bool> SynchronizeMesh(const EditorViewportMeshResourceView &resource);
        [[nodiscard]] Result<bool> AdvanceResidentMesh(const EditorViewportMeshResourceView &resource, ResidentMesh &resident);
        [[nodiscard]] Result<void> CreateResidentMeshBuffers(const EditorViewportMeshResourceView &resource, ResidentMesh &resident);
        [[nodiscard]] bool IsResidentMeshReady(const ResidentMesh &resident) const;
        void RetireMissingMeshes(std::span<const EditorViewportMeshResourceView> resources) noexcept;
        [[nodiscard]] Result<void> SynchronizeTarget(EditorViewportExtent extent);
        [[nodiscard]] Result<bool> AdvanceViewportTarget(ViewportTargetResources &resources);
        [[nodiscard]] Result<bool> AdvanceViewportTextures(ViewportTargetResources &resources);
        [[nodiscard]] Result<bool> AdvanceViewportViews(ViewportTargetResources &resources);
        void ReleaseMesh(ResidentMesh &mesh) noexcept;
        void ReleaseTarget() noexcept;
        void ReleaseShadowResources() noexcept;
        void ReleaseViewportTarget(ViewportTargetResources &resources) noexcept;

        std::uint32_t program_{0};
        std::uint32_t shadowProgram_{0};
        Render::RenderTextureHandle shadowDepthTexture_;
        Render::RenderTextureViewHandle shadowDepthTextureView_;
        Render::RenderTargetHandle shadowTarget_;
        Render::ResourceOperationId shadowDepthTextureOperation_;
        Render::ResourceOperationId shadowDepthTextureViewOperation_;
        Render::ResourceOperationId shadowTargetOperation_;
        std::uint32_t gridVertexArray_{0};
        std::uint32_t gridVertexBuffer_{0};
        std::unordered_map<std::uint64_t, ResidentMesh> meshes_;
        std::unordered_map<std::uint64_t, ResidentMesh> pendingMeshes_;
        Render::RenderFrontend *frontend_{nullptr};
        OpenGLViewportResourceBridge resourceBridge_;
        ViewportTargetResources viewportTarget_;
        ViewportTargetResources pendingViewportTarget_;
        UniformLocations uniforms_{};
        EditorViewportExtent requestedExtent_{};
        EditorViewportExtent allocatedExtent_{};
        EditorViewportGridOptions gridOptions_{};
        EditorViewportLightVisualizerOptions lightVisualizerOptions_{};
        bool initialized_{false};
        bool meshesReady_{false};
    };

}  // namespace Horo::Editor
