/** @copydoc EditorViewportModel.h */

#include "editor/project_model/EditorViewportModel.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace Horo::Editor
{
namespace
{
[[nodiscard]] Math::Vec3 RotateAroundAxis(const Math::Vec3 value, const Math::Vec3 axis, const float radians) noexcept
{
    const Math::Vec3 normalizedAxis = Math::Normalize(axis);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return value * cosine + Math::Cross(normalizedAxis, value) * sine +
           normalizedAxis * (Math::Dot(normalizedAxis, value) * (1.0F - cosine));
}

[[nodiscard]] bool IsFinite(const EditorViewportNavigationDelta &delta) noexcept
{
    return std::isfinite(delta.yawRadians) && std::isfinite(delta.pitchRadians) && std::isfinite(delta.moveRight) &&
           std::isfinite(delta.moveUp) && std::isfinite(delta.moveForward) && std::isfinite(delta.dollyScale) &&
           delta.dollyScale > 0.0F;
}

[[nodiscard]] bool IsEmpty(const EditorViewportNavigationDelta &delta) noexcept
{
    return delta.yawRadians == 0.0F && delta.pitchRadians == 0.0F && delta.moveRight == 0.0F && delta.moveUp == 0.0F &&
           delta.moveForward == 0.0F && delta.dollyScale == 1.0F;
}

[[nodiscard]] bool IsValid(const SceneObjectTransformPreview &preview) noexcept
{
    const Math::Quaternion rotation = preview.localTransform.rotation;
    const float rotationLengthSquared =
        rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
    return preview.object.IsValid() && Math::IsFinite(preview.localTransform.translation) &&
           Math::IsFinite(preview.localTransform.scale) && std::isfinite(rotation.x) && std::isfinite(rotation.y) &&
           std::isfinite(rotation.z) && std::isfinite(rotation.w) && rotationLengthSquared > 0.0F;
}

[[nodiscard]] Error MakeViewportError(std::string code, std::string message)
{
    return Error{ErrorCode{std::move(code)},
                 ErrorDomainId{"horo.editor.viewport"},
                 ErrorSeverity::Error,
                 std::move(message),
                 {}};
}
} // namespace

/** @copydoc EditorViewportModel::EditorViewportModel */
EditorViewportModel::EditorViewportModel(EditorDataBus &events) noexcept : events_(&events)
{
}

/** @copydoc EditorViewportModel::Current */
const EditorViewportSnapshot &EditorViewportModel::Current() const noexcept
{
    return current_;
}

/** @copydoc EditorViewportModel::Navigate */
Result<void> EditorViewportModel::Navigate(const EditorViewportNavigationDelta &delta)
{
    if (!IsFinite(delta))
    {
        return Result<void>::Failure(
            MakeViewportError("editor.viewport.invalid_navigation", "Viewport navigation delta must be finite."));
    }
    if (IsEmpty(delta))
    {
        return Result<void>::Success();
    }

    EditorViewportCamera camera = current_.camera;
    const Math::Vec3 sceneUp{0.0F, 1.0F, 0.0F};
    const float targetDistance = Math::Length(camera.target - camera.position);
    Math::Vec3 forward = Math::Normalize(camera.target - camera.position);
    forward = Math::Normalize(RotateAroundAxis(forward, sceneUp, delta.yawRadians));
    Math::Vec3 right = Math::Normalize(Math::Cross(forward, sceneUp));
    const Math::Vec3 pitchedForward = Math::Normalize(RotateAroundAxis(forward, right, delta.pitchRadians));
    if (std::fabs(Math::Dot(pitchedForward, sceneUp)) < 0.995F)
    {
        forward = pitchedForward;
    }
    right = Math::Normalize(Math::Cross(forward, sceneUp));
    const Math::Vec3 localUp = Math::Normalize(Math::Cross(right, forward));

    if (delta.orbit)
    {
        camera.position = camera.target - forward * targetDistance;
    }
    else
    {
        camera.target = camera.position + forward * targetDistance;
    }

    const Math::Vec3 translation = right * delta.moveRight + localUp * delta.moveUp + forward * delta.moveForward;
    camera.position += translation;
    camera.target += translation;
    if (delta.dollyScale != 1.0F)
    {
        if (camera.projection == Runtime::CameraProjection::Perspective)
        {
            const float distance =
                std::clamp(targetDistance * delta.dollyScale, camera.nearPlane * 2.0F, camera.farPlane * 0.8F);
            camera.position = camera.target - forward * distance;
        }
        else
        {
            camera.orthographicHeight = std::clamp(camera.orthographicHeight * delta.dollyScale, 0.01F, 100000.0F);
        }
    }
    camera.up = sceneUp;
    if (!camera.IsValid())
    {
        return Result<void>::Failure(
            MakeViewportError("editor.viewport.invalid_camera", "Viewport navigation produced an invalid camera."));
    }

    current_.camera = camera;
    ++current_.revision.value;
    events_->Publish(ViewportChangedEvent{current_.revision, ViewportChangeKind::CameraMoved});
    return Result<void>::Success();
}

/** @copydoc EditorViewportModel::SetProjection */
Result<void> EditorViewportModel::SetProjection(const Runtime::CameraProjection projection)
{
    if (projection != Runtime::CameraProjection::Perspective && projection != Runtime::CameraProjection::Orthographic)
        return Result<void>::Failure(
            MakeViewportError("editor.viewport.invalid_projection", "Viewport projection is invalid."));
    if (current_.camera.projection == projection)
        return Result<void>::Success();
    EditorViewportCamera camera = current_.camera;
    const Math::Vec3 forward = Math::Normalize(camera.target - camera.position);
    const float distance = Math::Length(camera.target - camera.position);
    if (projection == Runtime::CameraProjection::Orthographic)
        camera.orthographicHeight = std::max(0.01F, 2.0F * distance * std::tan(camera.verticalFovRadians * 0.5F));
    else
    {
        const float perspectiveDistance =
            camera.orthographicHeight / (2.0F * std::tan(camera.verticalFovRadians * 0.5F));
        camera.position = camera.target - forward * perspectiveDistance;
    }
    camera.projection = projection;
    if (!camera.IsValid())
        return Result<void>::Failure(
            MakeViewportError("editor.viewport.invalid_camera", "Projection change produced an invalid camera."));
    current_.camera = camera;
    ++current_.revision.value;
    events_->Publish(ViewportChangedEvent{current_.revision, ViewportChangeKind::CameraProjectionChanged});
    return Result<void>::Success();
}

/** @copydoc EditorViewportModel::Focus */
Result<void> EditorViewportModel::Focus(const Math::Aabb &worldBounds, const float aspect)
{
    if (!worldBounds.IsValid() || !std::isfinite(aspect) || aspect <= 0.0F)
        return Result<void>::Failure(
            MakeViewportError("editor.viewport.invalid_focus_bounds", "Focus bounds and aspect must be valid."));
    const Result<Math::BoundingSphere> sphereResult = Math::SphereFromAabb(worldBounds);
    if (sphereResult.HasError())
        return Result<void>::Failure(sphereResult.ErrorValue());
    const Math::BoundingSphere sphere = sphereResult.Value();
    const float radius = std::max(sphere.radius, 0.25F);
    EditorViewportCamera camera = current_.camera;
    const Math::Vec3 forward = Math::Normalize(camera.target - camera.position);
    camera.target = sphere.center;
    if (camera.projection == Runtime::CameraProjection::Perspective)
    {
        const float verticalHalfFov = camera.verticalFovRadians * 0.5F;
        const float horizontalHalfFov = std::atan(std::tan(verticalHalfFov) * aspect);
        const float limitingHalfFov = std::min(verticalHalfFov, horizontalHalfFov);
        const float distance = radius / std::sin(limitingHalfFov) * 1.2F;
        camera.position = camera.target - forward * distance;
    }
    else
    {
        camera.orthographicHeight = 2.0F * radius * 1.2F / std::min(aspect, 1.0F);
        camera.position = camera.target - forward * Math::Length(current_.camera.target - current_.camera.position);
    }
    if (!camera.IsValid())
        return Result<void>::Failure(
            MakeViewportError("editor.viewport.invalid_camera", "Focus operation produced an invalid camera."));
    current_.camera = camera;
    ++current_.revision.value;
    events_->Publish(ViewportChangedEvent{current_.revision, ViewportChangeKind::CameraFocused});
    return Result<void>::Success();
}

/** @copydoc EditorViewportModel::SetTransformPreview */
Result<void> EditorViewportModel::SetTransformPreview(const SceneObjectTransformPreview &preview)
{
    if (!IsValid(preview))
    {
        return Result<void>::Failure(MakeViewportError("editor.viewport.invalid_transform_preview",
                                                       "Viewport transform preview must be finite."));
    }
    if (current_.transformPreview == preview)
    {
        return Result<void>::Success();
    }
    current_.transformPreview = preview;
    ++current_.revision.value;
    events_->Publish(ViewportChangedEvent{current_.revision, ViewportChangeKind::ScenePreviewChanged});
    return Result<void>::Success();
}

/** @copydoc EditorViewportModel::ClearTransformPreview */
bool EditorViewportModel::ClearTransformPreview()
{
    if (!current_.transformPreview.has_value())
    {
        return false;
    }
    current_.transformPreview.reset();
    ++current_.revision.value;
    events_->Publish(ViewportChangedEvent{current_.revision, ViewportChangeKind::ScenePreviewChanged});
    return true;
}
} // namespace Horo::Editor
