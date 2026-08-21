#include "editor/screens/workspace/AssetSceneDrop.h"

#include "editor/renderer/EditorViewportScene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Horo::Editor {
    namespace {
        template <std::size_t Size> void CopyBounded(std::array<char, Size> &destination, const std::string_view source) noexcept {
            const std::size_t count = std::min(source.size(), Size - 1);
            std::memcpy(destination.data(), source.data(), count);
            destination[count] = '\0';
        }

        template <std::size_t Size> [[nodiscard]] bool IsTerminated(const std::array<char, Size> &value) noexcept {
            return std::find(value.begin(), value.end(), '\0') != value.end();
        }

        [[nodiscard]] float Snap(const float value, const float step) noexcept {
            return std::round(value / step) * step;
        }
    }  // namespace

    AssetSceneDragPayload MakeAssetSceneDragPayload(const std::string_view assetId, const std::string_view assetType,
                                                    const std::string_view absolutePath, const bool registered) noexcept {
        AssetSceneDragPayload payload;
        CopyBounded(payload.assetId, assetId);
        CopyBounded(payload.assetType, assetType);
        CopyBounded(payload.absolutePath, absolutePath);
        payload.registered = registered;
        return payload;
    }

    bool CanInstantiateAssetType(const std::string_view assetType) noexcept {
        return assetType == "core.mesh";
    }

    AssetSceneDropPolicyResult EvaluateAssetSceneDrop(const AssetSceneDragPayload &payload) noexcept {
        using enum Horo::Editor::AssetSceneDropRejection;
        if (!IsTerminated(payload.assetId) || !IsTerminated(payload.assetType) || !IsTerminated(payload.absolutePath)) {
            return {false, AssetSceneDropRejection::InvalidPayload};
        }
        if (!payload.registered || payload.assetId.front() == '\0')
            return {false, AssetSceneDropRejection::Unregistered};
        const auto parsed = Assets::AssetId::Parse(payload.assetId.data());
        if (parsed.HasError())
            return {false, AssetSceneDropRejection::InvalidPayload};
        if (!CanInstantiateAssetType(payload.assetType.data()))
            return {false, AssetSceneDropRejection::UnsupportedType};
        return {true, AssetSceneDropRejection::None};
    }

    Result<AssetViewportPlacement> ResolveAssetViewportPlacement(const AssetViewportPlacementRequest &request) {
        if (!request.scene.View().IsValid() || !std::isfinite(request.normalizedX) || !std::isfinite(request.normalizedY) ||
            !std::isfinite(request.aspect) || request.normalizedX < 0.0F || request.normalizedX > 1.0F || request.normalizedY < 0.0F ||
            request.normalizedY > 1.0F || request.aspect <= 0.0F || !request.localBounds.IsValid() ||
            !std::isfinite(request.fallbackDistance) || request.fallbackDistance <= 0.0F ||
            (request.snapToGrid && (!std::isfinite(request.gridStep) || request.gridStep <= 0.0F))) {
            return Result<AssetViewportPlacement>::Failure(Error{.message = "Asset viewport placement request is invalid."});
        }

        const Result<Math::Ray> ray =
            BuildEditorViewportRay(request.scene.camera, request.normalizedX, request.normalizedY, request.aspect, request.depthRange);
        if (ray.HasError())
            return Result<AssetViewportPlacement>::Failure(ray.ErrorValue());

        float nearest = std::numeric_limits<float>::max();
        for (const EditorViewportInstance &instance : request.scene.instances) {
            const Result<Math::Aabb> worldBounds = Math::TransformAabb(instance.localBounds, instance.localToWorld);
            if (worldBounds.HasError())
                return Result<AssetViewportPlacement>::Failure(worldBounds.ErrorValue());
            const Result<std::optional<Math::RayHit>> hit = Math::IntersectRayAabb(ray.Value(), worldBounds.Value());
            if (hit.HasError())
                return Result<AssetViewportPlacement>::Failure(hit.ErrorValue());
            if (hit.Value().has_value())
                nearest = std::min(nearest, hit.Value()->distance);
        }

        AssetViewportPlacement result;
        if (nearest != std::numeric_limits<float>::max()) {
            result.worldPosition = ray.Value().origin + ray.Value().direction * nearest;
            result.kind = AssetViewportPlacementKind::Surface;
        } else if (std::fabs(ray.Value().direction.y) > Math::DefaultEpsilon) {
            const float distance = -ray.Value().origin.y / ray.Value().direction.y;
            if (distance > 0.0F) {
                result.worldPosition = ray.Value().origin + ray.Value().direction * distance;
                result.kind = AssetViewportPlacementKind::GroundPlane;
            } else {
                result.worldPosition = ray.Value().origin + ray.Value().direction * request.fallbackDistance;
            }
        } else {
            result.worldPosition = ray.Value().origin + ray.Value().direction * request.fallbackDistance;
        }

        result.worldPosition.y -= request.localBounds.minimum.y;
        if (request.snapToGrid) {
            result.worldPosition.x = Snap(result.worldPosition.x, request.gridStep);
            result.worldPosition.y = Snap(result.worldPosition.y, request.gridStep);
            result.worldPosition.z = Snap(result.worldPosition.z, request.gridStep);
        }
        return Result<AssetViewportPlacement>::Success(result);
    }
}  // namespace Horo::Editor
