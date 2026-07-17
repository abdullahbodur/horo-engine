#pragma once

#include "editor/renderer/EditorViewportRenderer.h"

#include <unordered_map>

namespace Horo::Editor
{
/** @brief OpenGL editor adapter that renders backend-neutral editor scene instances into an offscreen target. */
class EditorViewportRendererOpenGL final : public IEditorViewportRenderer
{
  public:
    EditorViewportRendererOpenGL() = default;
    ~EditorViewportRendererOpenGL() override;

    EditorViewportRendererOpenGL(const EditorViewportRendererOpenGL &) = delete;
    EditorViewportRendererOpenGL &operator=(const EditorViewportRendererOpenGL &) = delete;

    /** @brief Creates shader, geometry, and offscreen target state in the current OpenGL context. */
    [[nodiscard]] Result<void> Initialize();

    /** @brief Releases all OpenGL objects; repeated calls are safe. */
    void Shutdown() noexcept;

    void RequestExtent(EditorViewportExtent extent) noexcept override;
    [[nodiscard]] EditorViewportExtent RequestedExtent() const noexcept override;
    [[nodiscard]] Result<void> ExecuteStaticMeshPass(const Render::StaticMeshPassDescriptor &descriptor) override;
    [[nodiscard]] EditorViewportTextureView TextureView() const noexcept override;
    [[nodiscard]] bool IsReady() const noexcept override;

  private:
    [[nodiscard]] Result<void> CreateProgram();
    struct GpuMesh
    {
        std::uint32_t vertexArray{0};
        std::uint32_t vertexBuffer{0};
        std::uint32_t indexBuffer{0};
        std::uint32_t indexCount{0};
        std::uint32_t generation{0};
    };

    [[nodiscard]] Result<void> SynchronizeMeshes(std::span<const EditorViewportMeshResourceView> resources);
    void DestroyMesh(GpuMesh &mesh) noexcept;
    [[nodiscard]] Result<void> RecreateTarget(EditorViewportExtent extent);
    void DestroyTarget() noexcept;

    std::uint32_t program_{0};
    std::unordered_map<std::uint64_t, GpuMesh> meshes_;
    std::uint32_t framebuffer_{0};
    std::uint32_t colorTexture_{0};
    std::uint32_t depthBuffer_{0};
    std::int32_t mvpLocation_{-1};
    std::int32_t selectionColorLocation_{-1};
    std::int32_t selectionStrengthLocation_{-1};
    EditorViewportExtent requestedExtent_{};
    EditorViewportExtent allocatedExtent_{};
    Render::RenderTargetHandle targetHandle_{};
    bool initialized_{false};
};
} // namespace Horo::Editor
