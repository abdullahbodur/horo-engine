#include "editor/document/EditorViewportPicking.h"
#include "EditorRenderExtractionErrors.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace Horo::Editor
{
namespace
{
[[nodiscard]] Error MakePickingError(const ErrorCodeDescriptor &descriptor, std::string message)
{
    return MakeError(descriptor, std::move(message));
}
} // namespace

/** @copydoc PickEditorViewportScene */
Result<std::optional<SceneObjectId>> PickEditorViewportScene(const EditorViewportSceneSnapshot &scene,
                                                             const EditorViewportPickQuery &query)
{
    if (!std::isfinite(query.normalizedX) || !std::isfinite(query.normalizedY) || !std::isfinite(query.aspect) ||
        query.normalizedX < 0.0F || query.normalizedX > 1.0F || query.normalizedY < 0.0F || query.normalizedY > 1.0F ||
        query.aspect <= 0.0F)
    {
        return Result<std::optional<SceneObjectId>>::Failure(
            MakePickingError(ViewportPickingErrors::InvalidQuery, "Viewport pick query is outside valid bounds."));
    }
    if (!scene.View().IsValid() || scene.instances.size() != scene.instanceObjects.size())
    {
        return Result<std::optional<SceneObjectId>>::Failure(MakePickingError(
            ViewportPickingErrors::InvalidScene, "Viewport pick snapshot is invalid or has inconsistent identity data."));
    }
    if (std::ranges::any_of(scene.instanceObjects, [](const SceneObjectId object) { return !object.IsValid(); }))
    {
        return Result<std::optional<SceneObjectId>>::Failure(MakePickingError(
            ViewportPickingErrors::InvalidIdentity, "Viewport pick snapshot contains an invalid scene object identity."));
    }

    const Result<Math::Ray> ray =
        BuildEditorViewportRay(scene.camera, query.normalizedX, query.normalizedY, query.aspect);
    if (ray.HasError())
        return Result<std::optional<SceneObjectId>>::Failure(ray.ErrorValue());

    float nearestDistance = std::numeric_limits<float>::max();
    std::optional<SceneObjectId> nearestObject;
    for (std::size_t index = 0; index < scene.instances.size(); ++index)
    {
        const Result<Math::Aabb> worldBounds =
            Math::TransformAabb(scene.instances[index].localBounds, scene.instances[index].localToWorld);
        if (worldBounds.HasError())
            return Result<std::optional<SceneObjectId>>::Failure(worldBounds.ErrorValue());
        const Result<std::optional<Math::RayHit>> hit = Math::IntersectRayAabb(ray.Value(), worldBounds.Value());
        if (hit.HasError())
            return Result<std::optional<SceneObjectId>>::Failure(hit.ErrorValue());
        if (hit.Value().has_value() && hit.Value()->distance < nearestDistance)
        {
            nearestDistance = hit.Value()->distance;
            nearestObject = scene.instanceObjects[index];
        }
    }
    return Result<std::optional<SceneObjectId>>::Success(nearestObject);
}
} // namespace Horo::Editor
