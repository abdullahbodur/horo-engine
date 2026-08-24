#include "TransformGizmoGeometry.h"

#include "Horo/Editor/EditorTheme.h"
#include "TransformGizmoMath.h"
#include "editor/renderer/EditorViewportScene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace Horo::Editor {
    namespace {
        [[nodiscard]] std::optional<ImVec2> ProjectToViewport(const EditorViewportCamera &camera, const Math::Vec3 worldPosition,
                                                              const ImVec2 origin, const float width, const float height,
                                                              const Math::ClipDepthRange depthRange) noexcept {
            if (width <= 0.0F || height <= 0.0F)
                return std::nullopt;
            const Result<Math::Mat4> viewProjection = BuildEditorViewportViewProjection(camera, width / height, depthRange);
            if (viewProjection.HasError())
                return std::nullopt;
            const Result<Math::Vec3> projected = Math::TryProject(viewProjection.Value(), worldPosition);
            if (const float minimumDepth = depthRange == Math::ClipDepthRange::NegativeOneToOne ? -1.0F : 0.0F;
                projected.HasError() || projected.Value().z < minimumDepth || projected.Value().z > 1.0F)
                return std::nullopt;
            return ImVec2{origin.x + (projected.Value().x * 0.5F + 0.5F) * width, origin.y + (0.5F - projected.Value().y * 0.5F) * height};
        }

        [[nodiscard]] float DistanceToSegment(const ImVec2 point, const ImVec2 start, const ImVec2 end) noexcept {
            const ImVec2 segment{end.x - start.x, end.y - start.y};
            const ImVec2 relative{point.x - start.x, point.y - start.y};
            const float lengthSquared = segment.x * segment.x + segment.y * segment.y;
            const float parameter =
                lengthSquared > 0.0F ? std::clamp((relative.x * segment.x + relative.y * segment.y) / lengthSquared, 0.0F, 1.0F) : 0.0F;
            const ImVec2 nearest{start.x + segment.x * parameter, start.y + segment.y * parameter};
            const float deltaX = point.x - nearest.x;
            const float deltaY = point.y - nearest.y;
            return std::sqrt(deltaX * deltaX + deltaY * deltaY);
        }

        [[nodiscard]] float Distance(const ImVec2 lhs, const ImVec2 rhs) noexcept {
            const float x = lhs.x - rhs.x;
            const float y = lhs.y - rhs.y;
            return std::sqrt(x * x + y * y);
        }

        void DrawRotationHandles(ImDrawList &drawList, const TransformGizmoGeometryRequest &request, TransformGizmoFrameGeometry &geometry,
                                 const std::array<ImU32, 3> &axisColors) {
            float closestDistance = std::numeric_limits<float>::max();
            for (int axis = 0; axis < 3; ++axis) {
                const Math::Vec3 basisU = geometry.worldAxes[(axis + 1) % 3];
                const Math::Vec3 basisV = Math::Normalize(Math::Cross(geometry.worldAxes[axis], basisU));
                const auto projectedUnit = ProjectToViewport(request.camera, geometry.worldPosition + basisU, request.origin, request.width,
                                                             request.height, request.depthRange);
                if (!projectedUnit.has_value() || basisV == Math::Vec3{})
                    continue;
                const float unitPixels = std::max(Distance(*projectedUnit, *geometry.center), 1.0F);
                const float radiusWorld = (42.0F + static_cast<float>(axis) * 3.0F) / unitPixels;
                geometry.pixelsPerWorldUnit[axis] = unitPixels;
                float distance = std::numeric_limits<float>::max();
                std::optional<ImVec2> previous;
                for (int segment = 0; segment <= 64; ++segment) {
                    const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(segment) / 64.0F;
                    const Math::Vec3 point = geometry.worldPosition + (basisU * std::cos(angle) + basisV * std::sin(angle)) * radiusWorld;
                    const auto projected =
                        ProjectToViewport(request.camera, point, request.origin, request.width, request.height, request.depthRange);
                    if (previous.has_value() && projected.has_value()) {
                        distance = std::min(distance, DistanceToSegment(request.pointer, *previous, *projected));
                        drawList.AddLine(*previous, *projected, axisColors[axis], 2.0F);
                    }
                    previous = projected;
                }
                const bool active = request.activeAxis == axis;
                const bool hit = request.hovered && distance <= 5.0F;
                if (active || hit)
                    drawList.AddCircle(*geometry.center, 4.0F, Theme::U32(Theme::Text()), 16, 2.0F);
                if (hit && distance < closestDistance) {
                    closestDistance = distance;
                    geometry.hoveredAxis = axis;
                }
            }
        }

        void DrawLinearAxisHandle(ImDrawList &drawList, const TransformGizmoGeometryRequest &request, TransformGizmoFrameGeometry &geometry,
                                  const int axis, const ImU32 axisColor, float &closestDistance) {
            const auto projected = ProjectToViewport(request.camera, geometry.worldPosition + geometry.worldAxes[axis], request.origin,
                                                     request.width, request.height, request.depthRange);
            if (!projected.has_value())
                return;
            const ImVec2 delta{projected->x - geometry.center->x, projected->y - geometry.center->y};
            geometry.pixelsPerWorldUnit[axis] = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            if (geometry.pixelsPerWorldUnit[axis] < 4.0F)
                return;
            geometry.screenDirections[axis] = {
                delta.x / geometry.pixelsPerWorldUnit[axis],
                delta.y / geometry.pixelsPerWorldUnit[axis],
            };
            const ImVec2 end{
                geometry.center->x + geometry.screenDirections[axis].x * 48.0F,
                geometry.center->y + geometry.screenDirections[axis].y * 48.0F,
            };
            const float distance = DistanceToSegment(request.pointer, *geometry.center, end);
            const bool active = request.activeAxis == axis;
            const bool hit = request.hovered && distance <= 7.0F;
            drawList.AddLine(*geometry.center, end, active || hit ? Theme::U32(Theme::Text()) : axisColor, active || hit ? 4.0F : 2.5F);
            if (request.tool == EditorTransformTool::Scale)
                drawList.AddRectFilled({end.x - 4.0F, end.y - 4.0F}, {end.x + 4.0F, end.y + 4.0F}, axisColor);
            else
                drawList.AddCircleFilled(end, hit ? 5.0F : 4.0F, axisColor);
            if (hit && distance < closestDistance) {
                closestDistance = distance;
                geometry.hoveredAxis = axis;
            }
        }

        void DrawLinearHandles(ImDrawList &drawList, const TransformGizmoGeometryRequest &request, TransformGizmoFrameGeometry &geometry,
                               const std::array<ImU32, 3> &axisColors) {
            float closestDistance = std::numeric_limits<float>::max();
            for (int axis = 0; axis < 3; ++axis) {
                DrawLinearAxisHandle(drawList, request, geometry, axis, axisColors[axis], closestDistance);
            }
            if (request.tool == EditorTransformTool::Scale) {
                const bool uniformHit = request.hovered && Distance(request.pointer, *geometry.center) <= 8.0F;
                drawList.AddRectFilled({geometry.center->x - 5.0F, geometry.center->y - 5.0F},
                                       {geometry.center->x + 5.0F, geometry.center->y + 5.0F},
                                       uniformHit ? Theme::U32(Theme::Text()) : ImGui::GetColorU32(ImVec4{0.8F, 0.8F, 0.8F, 1.0F}));
                if (uniformHit)
                    geometry.hoveredAxis = 3;
            } else
                drawList.AddCircleFilled(*geometry.center, 4.0F, Theme::U32(Theme::Text()));
        }
    }  // namespace

    /** @copydoc DrawTransformGizmoGeometry */
    Result<TransformGizmoFrameGeometry> DrawTransformGizmoGeometry(ImDrawList &drawList, const TransformGizmoGeometryRequest &request) {
        TransformGizmoFrameGeometry geometry;
        geometry.worldPosition = request.activeWorldPosition.value_or(Math::TransformPoint(request.worldTransform, {}));
        const Result<std::array<Math::Vec3, 3>> worldAxes = ResolveTransformGizmoWorldAxes(request.worldTransform, request.space);
        if (worldAxes.HasError())
            return Result<TransformGizmoFrameGeometry>::Failure(worldAxes.ErrorValue());
        geometry.worldAxes = worldAxes.Value();
        geometry.center =
            ProjectToViewport(request.camera, geometry.worldPosition, request.origin, request.width, request.height, request.depthRange);
        if (!geometry.center.has_value())
            return Result<TransformGizmoFrameGeometry>::Success(std::move(geometry));

        const std::array axisColors{
            ImGui::GetColorU32(ImVec4{0.88F, 0.33F, 0.29F, 1.0F}),
            ImGui::GetColorU32(ImVec4{0.37F, 0.72F, 0.54F, 1.0F}),
            ImGui::GetColorU32(ImVec4{0.29F, 0.56F, 0.85F, 1.0F}),
        };
        if (request.tool == EditorTransformTool::Rotate)
            DrawRotationHandles(drawList, request, geometry, axisColors);
        else
            DrawLinearHandles(drawList, request, geometry, axisColors);
        return Result<TransformGizmoFrameGeometry>::Success(std::move(geometry));
    }

    /** @copydoc ProjectTransformGizmoRotationVector */
    std::optional<Math::Vec3> ProjectTransformGizmoRotationVector(const EditorViewportCamera &camera, const Math::Vec3 center,
                                                                  const Math::Vec3 normal, const ImVec2 pointer, const ImVec2 origin,
                                                                  const float width, const float height,
                                                                  const Math::ClipDepthRange depthRange) noexcept {  // NOSONAR(cpp:S107)
        if (width <= 0.0F || height <= 0.0F)
            return std::nullopt;
        const Result<Math::Ray> ray =
            BuildEditorViewportRay(camera, (pointer.x - origin.x) / width, (pointer.y - origin.y) / height, width / height, depthRange);
        const Result<Math::Plane> plane = Math::TryMakePlane(center, normal);
        if (ray.HasError() || plane.HasError())
            return std::nullopt;
        const Result<std::optional<Math::RayHit>> hit = Math::IntersectRayPlane(ray.Value(), plane.Value());
        if (hit.HasError() || !hit.Value().has_value())
            return std::nullopt;
        const Result<Math::Vec3> vector = Math::TryNormalize(hit.Value()->position - center);
        return vector.HasValue() ? std::optional{vector.Value()} : std::nullopt;
    }
}  // namespace Horo::Editor
