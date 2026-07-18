/** @copydoc EditorViewportScene.h */

#include "editor/renderer/EditorViewportScene.h"
#include "editor/renderer/EditorRendererErrors.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace Horo::Editor
{
/** @copydoc EditorViewportSceneView::IsValid */
bool EditorViewportSceneView::IsValid() const noexcept
{
    if (!camera.IsValid())
    {
        return false;
    }
    for (std::size_t index = 0; index < meshResources.size(); ++index)
    {
        const EditorViewportMeshResourceView &resource = meshResources[index];
        if (!resource.IsValid())
        {
            return false;
        }
        for (std::size_t other = index + 1; other < meshResources.size(); ++other)
            if (meshResources[other].handle == resource.handle)
                return false;
        for (const Render::MeshVertex &vertex : resource.vertices)
            if (!Math::IsFinite(vertex.position) || !Math::IsFinite(vertex.normal) || !Math::IsFinite(vertex.uv))
                return false;
        for (const std::uint32_t vertexIndex : resource.indices)
            if (vertexIndex >= resource.vertices.size())
                return false;
    }
    for (const EditorViewportInstance &instance : instances)
    {
        if (!instance.IsValid())
            return false;
        if (std::ranges::find(meshResources, instance.mesh, &EditorViewportMeshResourceView::handle) ==
            meshResources.end())
            return false;
    }
    return true;
}

/** @copydoc BuildEditorViewportMvp */
Math::Mat4 BuildEditorViewportMvp(const EditorViewportCamera &camera, const Math::Mat4 &localToWorld,
                                  const float aspect, const Math::ClipDepthRange depthRange) noexcept
{
    const Result<Math::Mat4> viewProjection = BuildEditorViewportViewProjection(camera, aspect, depthRange);
    assert(viewProjection.HasValue());
    return Math::Multiply(viewProjection.Value(), localToWorld);
}

/** @copydoc BuildRenderMvp */
Result<Math::Mat4> BuildRenderMvp(const Render::RenderCameraView& camera, const Math::Mat4& localToWorld,
                                  const float aspect, const Math::ClipDepthRange depthRange) noexcept
{
    if (!camera.IsValid() || !Math::IsFinite(localToWorld) || !std::isfinite(aspect) || aspect <= 0.0F)
    {
        return Result<Math::Mat4>::Failure(MakeError(
            RendererErrors::InvalidRenderCamera, "Render camera, transform, or aspect ratio is invalid."));
    }
    const Result<Math::Mat4> view = Math::TryLookAt(camera.position, camera.target, camera.up);
    if (view.HasError())
        return Result<Math::Mat4>::Failure(view.ErrorValue());
    const Result<Math::Mat4> projection =
        camera.projection.kind == Render::RenderProjectionKind::Perspective
            ? Math::TryPerspective(camera.projection.verticalFovRadians, aspect, camera.projection.nearPlane,
                                   camera.projection.farPlane, depthRange)
            : Math::TryOrthographic(camera.projection.orthographicHeight, aspect, camera.projection.nearPlane,
                                    camera.projection.farPlane, depthRange);
    if (projection.HasError())
        return Result<Math::Mat4>::Failure(projection.ErrorValue());
    return Result<Math::Mat4>::Success(Math::Multiply(Math::Multiply(projection.Value(), view.Value()), localToWorld));
}

/** @copydoc BuildEditorViewportViewProjection */
Result<Math::Mat4> BuildEditorViewportViewProjection(const EditorViewportCamera &camera, const float aspect,
                                                     const Math::ClipDepthRange depthRange) noexcept
{
    if (!camera.IsValid() || !std::isfinite(aspect) || aspect <= 0.0F)
    {
        return Result<Math::Mat4>::Failure(
            MakeError(RendererErrors::InvalidCamera, "Viewport camera or aspect ratio is invalid."));
    }
    const Result<Math::Mat4> view = Math::TryLookAt(camera.position, camera.target, camera.up);
    if (view.HasError())
        return Result<Math::Mat4>::Failure(view.ErrorValue());
    const Result<Math::Mat4> projection =
        camera.projection == Runtime::CameraProjection::Perspective
            ? Math::TryPerspective(camera.verticalFovRadians, aspect, camera.nearPlane, camera.farPlane, depthRange)
            : Math::TryOrthographic(camera.orthographicHeight, aspect, camera.nearPlane, camera.farPlane, depthRange);
    if (projection.HasError())
        return Result<Math::Mat4>::Failure(projection.ErrorValue());
    return Result<Math::Mat4>::Success(Math::Multiply(projection.Value(), view.Value()));
}

/** @copydoc BuildEditorViewportRay */
Result<Math::Ray> BuildEditorViewportRay(const EditorViewportCamera &camera, const float normalizedX,
                                         const float normalizedY, const float aspect) noexcept
{
    if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) || normalizedX < 0.0F || normalizedX > 1.0F ||
        normalizedY < 0.0F || normalizedY > 1.0F)
    {
        return Result<Math::Ray>::Failure(
            MakeError(RendererErrors::InvalidCoordinates, "Viewport coordinates are outside normalized bounds."));
    }
    const Result<Math::Mat4> viewProjection =
        BuildEditorViewportViewProjection(camera, aspect, Math::ClipDepthRange::NegativeOneToOne);
    if (viewProjection.HasError())
        return Result<Math::Ray>::Failure(viewProjection.ErrorValue());
    const Result<Math::Mat4> inverse = Math::TryInverse(viewProjection.Value());
    if (inverse.HasError())
        return Result<Math::Ray>::Failure(inverse.ErrorValue());
    const Math::Vec2 ndc{normalizedX * 2.0F - 1.0F, 1.0F - normalizedY * 2.0F};
    const Result<Math::Vec3> nearPoint = Math::TryUnproject(inverse.Value(), {ndc.x, ndc.y, -1.0F});
    const Result<Math::Vec3> farPoint = Math::TryUnproject(inverse.Value(), {ndc.x, ndc.y, 1.0F});
    if (nearPoint.HasError())
        return Result<Math::Ray>::Failure(nearPoint.ErrorValue());
    if (farPoint.HasError())
        return Result<Math::Ray>::Failure(farPoint.ErrorValue());
    if (camera.projection == Runtime::CameraProjection::Perspective)
        return Math::TryMakeRay(camera.position, farPoint.Value() - camera.position, camera.nearPlane, camera.farPlane);
    return Math::TryMakeRay(nearPoint.Value(), farPoint.Value() - nearPoint.Value(), 0.0F,
                            camera.farPlane - camera.nearPlane);
}

Render::RenderCameraView ToRenderCamera(const EditorViewportCamera &camera) noexcept
{
    return Render::RenderCameraView{
        .position = camera.position,
        .target = camera.target,
        .up = camera.up,
        .projection = Render::RenderProjectionDescriptor{
            .kind = camera.projection == Runtime::CameraProjection::Perspective
                        ? Render::RenderProjectionKind::Perspective
                        : Render::RenderProjectionKind::Orthographic,
            .verticalFovRadians = camera.verticalFovRadians,
            .orthographicHeight = camera.orthographicHeight,
            .nearPlane = camera.nearPlane,
            .farPlane = camera.farPlane,
        },
    };
}
} // namespace Horo::Editor
