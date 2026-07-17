#pragma once

/**
 * @file EditorViewportScene.h
 * @brief Editor-private generic mesh resource views and instances consumed by viewport adapters.
 */

#include "Horo/Math/SceneMath.h"
#include "Horo/Runtime/Render/Mesh.h"
#include "Horo/Runtime/Render/RenderScene.h"
#include "editor/project_model/EditorViewportCamera.h"

#include <span>

namespace Horo::Editor
{
/** @brief Non-owning immutable CPU mesh resource pinned by the owning extracted snapshot. */
using EditorViewportMeshResourceView = Render::RenderMeshResourceView;

/** @brief One backend-neutral renderable instance in the editor viewport scene. */
using EditorViewportInstance = Render::RenderStaticMeshInstance;

/** @brief Non-owning immutable editor scene view consumed synchronously by Render. */
struct EditorViewportSceneView
{
    EditorViewportCamera camera{};
    std::span<const EditorViewportMeshResourceView> meshResources{};
    std::span<const EditorViewportInstance> instances{};

    /** @brief Reports whether the camera and every instance contain supported finite values. */
    [[nodiscard]] bool IsValid() const noexcept;
};

/** @brief Builds one instance MVP using Horo scene conventions and the requested API clip-depth range. */
[[nodiscard]] Math::Mat4 BuildEditorViewportMvp(const EditorViewportCamera &camera, const Math::Mat4 &localToWorld,
                                                float aspect, Math::ClipDepthRange depthRange) noexcept;

/** @brief Builds one generic render-camera MVP using Horo scene conventions. */
[[nodiscard]] Result<Math::Mat4> BuildRenderMvp(const Render::RenderCameraView &camera,
                                                const Math::Mat4 &localToWorld, float aspect,
                                                Math::ClipDepthRange depthRange) noexcept;

/** @brief Builds the validated view-projection matrix for one editor camera. */
[[nodiscard]] Result<Math::Mat4> BuildEditorViewportViewProjection(const EditorViewportCamera &camera, float aspect,
                                                                   Math::ClipDepthRange depthRange) noexcept;

/** @brief Builds a world-space ray from top-left-origin normalized viewport coordinates. */
[[nodiscard]] Result<Math::Ray> BuildEditorViewportRay(const EditorViewportCamera &camera, float normalizedX,
                                                       float normalizedY, float aspect) noexcept;

/** @brief Converts editor camera state to the public backend-neutral render camera contract. */
[[nodiscard]] Render::RenderCameraView ToRenderCamera(const EditorViewportCamera &camera) noexcept;
} // namespace Horo::Editor
