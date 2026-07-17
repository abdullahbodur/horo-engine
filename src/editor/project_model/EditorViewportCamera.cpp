/** @copydoc EditorViewportCamera.h */

#include "editor/project_model/EditorViewportCamera.h"

#include <cmath>

namespace Horo::Editor
{
/** @copydoc EditorViewportCamera::IsValid */
bool EditorViewportCamera::IsValid() const noexcept
{
    const Math::Vec3 direction = target - position;
    const float directionLengthSquared =
        direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    const float upLengthSquared = up.x * up.x + up.y * up.y + up.z * up.z;
    const Math::Vec3 side{direction.y * up.z - direction.z * up.y, direction.z * up.x - direction.x * up.z,
                          direction.x * up.y - direction.y * up.x};
    const float sideLengthSquared = side.x * side.x + side.y * side.y + side.z * side.z;
    const bool projectionValid = (projection == Runtime::CameraProjection::Perspective && verticalFovRadians > 0.0F &&
                                  verticalFovRadians < Math::Pi) ||
                                 (projection == Runtime::CameraProjection::Orthographic && orthographicHeight > 0.0F);
    return Math::IsFinite(position) && Math::IsFinite(target) && Math::IsFinite(up) &&
           std::isfinite(verticalFovRadians) && std::isfinite(orthographicHeight) && std::isfinite(nearPlane) &&
           std::isfinite(farPlane) && directionLengthSquared > 0.0F && upLengthSquared > 0.0F &&
           sideLengthSquared > 0.0F && verticalFovRadians > 0.0F && verticalFovRadians < Math::Pi && projectionValid &&
           nearPlane > 0.0F && farPlane > nearPlane;
}
} // namespace Horo::Editor
