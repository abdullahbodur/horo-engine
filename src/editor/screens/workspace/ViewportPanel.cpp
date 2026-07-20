#include "ViewportPanel.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/input/EditorInputActions.h"
#include "editor/renderer/EditorViewportScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <string>

namespace Horo::Editor
{
namespace
{
[[nodiscard]] bool HasNavigation(const EditorViewportNavigationDelta &delta) noexcept
{
    return delta.yawRadians != 0.0F || delta.pitchRadians != 0.0F || delta.moveRight != 0.0F || delta.moveUp != 0.0F ||
           delta.moveForward != 0.0F || delta.dollyScale != 1.0F;
}

[[nodiscard]] std::optional<ImVec2> ProjectToViewport(const EditorViewportCamera &camera,
                                                      const Math::Vec3 worldPosition, const ImVec2 origin,
                                                      const float width, const float height) noexcept
{
    if (width <= 0.0F || height <= 0.0F)
    {
        return std::nullopt;
    }
    const Result<Math::Mat4> viewProjection =
        BuildEditorViewportViewProjection(camera, width / height, Math::ClipDepthRange::NegativeOneToOne);
    if (viewProjection.HasError())
        return std::nullopt;
    const Result<Math::Vec3> projected = Math::TryProject(viewProjection.Value(), worldPosition);
    if (projected.HasError() || projected.Value().z < -1.0F || projected.Value().z > 1.0F)
        return std::nullopt;
    return ImVec2{origin.x + (projected.Value().x * 0.5F + 0.5F) * width,
                  origin.y + (0.5F - projected.Value().y * 0.5F) * height};
}

[[nodiscard]] float DistanceToSegment(const ImVec2 point, const ImVec2 start, const ImVec2 end) noexcept
{
    const ImVec2 segment{end.x - start.x, end.y - start.y};
    const ImVec2 relative{point.x - start.x, point.y - start.y};
    const float lengthSquared = segment.x * segment.x + segment.y * segment.y;
    const float parameter =
        lengthSquared > 0.0F ? std::clamp((relative.x * segment.x + relative.y * segment.y) / lengthSquared, 0.0F, 1.0F)
                             : 0.0F;
    const ImVec2 nearest{start.x + segment.x * parameter, start.y + segment.y * parameter};
    const float deltaX = point.x - nearest.x;
    const float deltaY = point.y - nearest.y;
    return std::sqrt(deltaX * deltaX + deltaY * deltaY);
}

[[nodiscard]] float Distance(const ImVec2 lhs, const ImVec2 rhs) noexcept
{
    const float x = lhs.x - rhs.x;
    const float y = lhs.y - rhs.y;
    return std::sqrt(x * x + y * y);
}

[[nodiscard]] constexpr Math::Vec3 LocalAxis(const int axis) noexcept
{
    return axis == 0 ? Math::Vec3{1.0F, 0.0F, 0.0F}
                     : (axis == 1 ? Math::Vec3{0.0F, 1.0F, 0.0F} : Math::Vec3{0.0F, 0.0F, 1.0F});
}

[[nodiscard]] std::optional<Math::Vec3> RotationPlaneVector(const EditorViewportCamera &camera, const Math::Vec3 center,
                                                            const Math::Vec3 normal, const ImVec2 mouse,
                                                            const ImVec2 origin, const float width,
                                                            const float height) noexcept
{
    if (width <= 0.0F || height <= 0.0F)
        return std::nullopt;
    const Result<Math::Ray> ray =
        BuildEditorViewportRay(camera, (mouse.x - origin.x) / width, (mouse.y - origin.y) / height, width / height);
    const Result<Math::Plane> plane = Math::TryMakePlane(center, normal);
    if (ray.HasError() || plane.HasError())
        return std::nullopt;
    const Result<std::optional<Math::RayHit>> hit = Math::IntersectRayPlane(ray.Value(), plane.Value());
    if (hit.HasError() || !hit.Value().has_value())
        return std::nullopt;
    const Result<Math::Vec3> vector = Math::TryNormalize(hit.Value()->position - center);
    return vector.HasValue() ? std::optional{vector.Value()} : std::nullopt;
}

void SetTranslation(Math::Mat4 &matrix, const Math::Vec3 translation) noexcept
{
    matrix.values[12] = translation.x;
    matrix.values[13] = translation.y;
    matrix.values[14] = translation.z;
}

[[nodiscard]] Math::Mat4 WorldAxisScaleMatrix(const Math::Vec3 axis, const float factor) noexcept
{
    Math::Mat4 result = Math::Mat4::Identity();
    const float delta = factor - 1.0F;
    result.values[0] += delta * axis.x * axis.x;
    result.values[4] += delta * axis.x * axis.y;
    result.values[8] += delta * axis.x * axis.z;
    result.values[1] += delta * axis.y * axis.x;
    result.values[5] += delta * axis.y * axis.y;
    result.values[9] += delta * axis.y * axis.z;
    result.values[2] += delta * axis.z * axis.x;
    result.values[6] += delta * axis.z * axis.y;
    result.values[10] += delta * axis.z * axis.z;
    return result;
}

void PreserveScaleSigns(Math::Vec3 &value, const Math::Vec3 initial) noexcept
{
    const auto preserve = [](const float candidate, const float original) {
        const float sign = original < 0.0F ? -1.0F : 1.0F;
        return sign * std::max(std::fabs(candidate), 0.0001F);
    };
    value = {preserve(value.x, initial.x), preserve(value.y, initial.y), preserve(value.z, initial.z)};
}

void MultiplyScaleComponent(Math::Vec3 &scale, const int axis, const float factor) noexcept
{
    auto apply = [factor](float &value) {
        const float sign = value < 0.0F ? -1.0F : 1.0F;
        value = sign * std::max(std::fabs(value) * factor, 0.0001F);
    };
    if (axis == 0 || axis == 3)
        apply(scale.x);
    if (axis == 1 || axis == 3)
        apply(scale.y);
    if (axis == 2 || axis == 3)
        apply(scale.z);
}
} // namespace

void ViewportPanel::OnAttach(PanelContext &ctx)
{
    viewportRenderer_ = ctx.viewportRenderer;
    inputRouter_ = ctx.inputRouter;
    workspaceInputContext_ = ctx.workspaceInputContext;
}

void ViewportPanel::OnDetach()
{
    if (inputRouter_ != nullptr && pointerCapture_.IsActive())
        inputRouter_->CancelCapture(Input::CaptureCancellationReason::OwnerDestroyed);
    FinishCapture();
    gizmoDrag_.reset();
    viewportRenderer_ = nullptr;
    inputRouter_ = nullptr;
    workspaceInputContext_ = nullptr;
}

void ViewportPanel::OnInputCaptureCancelled(const Input::CaptureCancellationReason) noexcept
{
    cancelTransformOnNextDraw_ = cancelTransformOnNextDraw_ || gizmoDrag_.has_value();
    gizmoDrag_.reset();
    navigationMode_ = NavigationMode::None;
    pointerCapture_.Release();
    toolInputContext_.Reset();
}

bool ViewportPanel::BeginCapture(const Input::PointerButton button)
{
    if (inputRouter_ == nullptr || workspaceInputContext_ == nullptr || pointerCapture_.IsActive())
        return false;
    toolInputContext_ = inputRouter_->PushContext(Input::InputContextId{"editor.viewport.capture"},
                                                  Input::InputContextKind::EditorToolCapture);
    auto captured = inputRouter_->CapturePointer(toolInputContext_, button, *this);
    if (captured.HasError())
    {
        toolInputContext_.Reset();
        return false;
    }
    pointerCapture_ = std::move(captured).Value();
    return true;
}

void ViewportPanel::FinishCapture() noexcept
{
    pointerCapture_.Release();
    toolInputContext_.Reset();
    navigationMode_ = NavigationMode::None;
}

void ViewportPanel::DrawInteraction(ImDrawList *dl, const ImVec2 &orig, const float w, const float h,
                                    const bool hovered, const EditorWorkspaceViewModel &vm,
                                    EditorWorkspaceViewCommandData &cmd, const EditorGuiContext &ctx,
                                    const float deltaSeconds)
{
    if (inputRouter_ == nullptr || workspaceInputContext_ == nullptr)
        return;
    const Input::RawInputSnapshot &input = inputRouter_->Snapshot();
    const ImVec2 mouse{input.pointer.x, input.pointer.y};
    const auto primary = input.State(Input::PointerButton::Primary);
    const auto secondary = input.State(Input::PointerButton::Secondary);
    const auto middle = input.State(Input::PointerButton::Middle);

    if (cancelTransformOnNextDraw_)
    {
        cancelTransformOnNextDraw_ = false;
        cmd.command = EditorWorkspaceViewCommand::CancelObjectTransformPreview;
    }

    const auto selectedObject = vm.primarySelection.has_value()
                                    ? std::ranges::find(vm.objects, *vm.primarySelection, &SceneObject::id)
                                    : vm.objects.end();
    const bool transformTool = vm.activeTransformTool == EditorTransformTool::Move ||
                               vm.activeTransformTool == EditorTransformTool::Rotate ||
                               vm.activeTransformTool == EditorTransformTool::Scale;
    if (gizmoDrag_.has_value() &&
        (selectedObject == vm.objects.end() || selectedObject->id != gizmoDrag_->object || !transformTool ||
         vm.activeTransformTool != gizmoDrag_->tool || vm.activeTransformSpace != gizmoDrag_->space))
    {
        inputRouter_->CancelCapture(Input::CaptureCancellationReason::Explicit);
        cancelTransformOnNextDraw_ = false;
        cmd.command = EditorWorkspaceViewCommand::CancelObjectTransformPreview;
        return;
    }
    std::optional<int> hoveredAxis;
    std::array<Math::Vec3, 3> worldAxes{};
    std::optional<ImVec2> gizmoCenter;
    std::array<ImVec2, 3> screenDirections{};
    std::array<float, 3> pixelsPerWorldUnit{};
    const std::array axisColors{
        ImGui::GetColorU32(ImVec4{0.88F, 0.33F, 0.29F, 1.0F}),
        ImGui::GetColorU32(ImVec4{0.37F, 0.72F, 0.54F, 1.0F}),
        ImGui::GetColorU32(ImVec4{0.29F, 0.56F, 0.85F, 1.0F}),
    };

    if (transformTool && selectedObject != vm.objects.end() && vm.primarySelectionWorldTransform.has_value() &&
        vm.primarySelectionParentWorldTransform.has_value())
    {
        const Math::Mat4 &worldTransform = *vm.primarySelectionWorldTransform;
        Math::Vec3 gizmoPosition = Math::TransformPoint(worldTransform, {});
        if (gizmoDrag_.has_value())
            gizmoPosition = gizmoDrag_->currentWorldPosition;
        gizmoCenter = ProjectToViewport(vm.viewportCamera, gizmoPosition, orig, w, h);
        if (vm.activeTransformSpace == EditorTransformSpace::World)
            worldAxes = {LocalAxis(0), LocalAxis(1), LocalAxis(2)};
        else if (const Result<Math::Transform> worldTrs = Math::TryDecomposeAffineTRS(worldTransform);
                 worldTrs.HasValue())
            worldAxes = {
                worldTrs.Value().rotation.Rotate(LocalAxis(0)),
                worldTrs.Value().rotation.Rotate(LocalAxis(1)),
                worldTrs.Value().rotation.Rotate(LocalAxis(2)),
            };
        else
            worldAxes = {LocalAxis(0), LocalAxis(1), LocalAxis(2)};

        float closestDistance = std::numeric_limits<float>::max();
        if (gizmoCenter.has_value())
        {
            if (vm.activeTransformTool == EditorTransformTool::Rotate)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    const Math::Vec3 basisU = worldAxes[(axis + 1) % 3];
                    const Math::Vec3 basisV = Math::Normalize(Math::Cross(worldAxes[axis], basisU));
                    const auto projectedUnit = ProjectToViewport(vm.viewportCamera, gizmoPosition + basisU, orig, w, h);
                    if (!projectedUnit.has_value() || basisV == Math::Vec3{})
                        continue;
                    const float unitPixels = std::max(Distance(*projectedUnit, *gizmoCenter), 1.0F);
                    const float radiusWorld = (42.0F + static_cast<float>(axis) * 3.0F) / unitPixels;
                    pixelsPerWorldUnit[axis] = unitPixels;
                    float distance = std::numeric_limits<float>::max();
                    std::optional<ImVec2> previous;
                    for (int segment = 0; segment <= 64; ++segment)
                    {
                        const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(segment) / 64.0F;
                        const Math::Vec3 point =
                            gizmoPosition + (basisU * std::cos(angle) + basisV * std::sin(angle)) * radiusWorld;
                        const auto projected = ProjectToViewport(vm.viewportCamera, point, orig, w, h);
                        if (previous.has_value() && projected.has_value())
                        {
                            distance = std::min(distance, DistanceToSegment(mouse, *previous, *projected));
                            dl->AddLine(*previous, *projected, axisColors[axis], 2.0F);
                        }
                        previous = projected;
                    }
                    const bool active = gizmoDrag_.has_value() && gizmoDrag_->axis == axis;
                    const bool hit = hovered && distance <= 5.0F;
                    if (active || hit)
                        dl->AddCircle(*gizmoCenter, 4.0F, Theme::U32(Theme::Text()), 16, 2.0F);
                    if (hit && distance < closestDistance)
                    {
                        closestDistance = distance;
                        hoveredAxis = axis;
                    }
                }
            }
            else
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    const auto projected =
                        ProjectToViewport(vm.viewportCamera, gizmoPosition + worldAxes[axis], orig, w, h);
                    if (!projected)
                        continue;
                    const ImVec2 delta{projected->x - gizmoCenter->x, projected->y - gizmoCenter->y};
                    pixelsPerWorldUnit[axis] = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                    if (pixelsPerWorldUnit[axis] < 4.0F)
                        continue;
                    screenDirections[axis] = {delta.x / pixelsPerWorldUnit[axis], delta.y / pixelsPerWorldUnit[axis]};
                    const ImVec2 end{gizmoCenter->x + screenDirections[axis].x * 48.0F,
                                     gizmoCenter->y + screenDirections[axis].y * 48.0F};
                    const float distance = DistanceToSegment(mouse, *gizmoCenter, end);
                    const bool active = gizmoDrag_.has_value() && gizmoDrag_->axis == axis;
                    const bool hit = hovered && distance <= 7.0F;
                    dl->AddLine(*gizmoCenter, end, active || hit ? Theme::U32(Theme::Text()) : axisColors[axis],
                                active || hit ? 4.0F : 2.5F);
                    if (vm.activeTransformTool == EditorTransformTool::Scale)
                        dl->AddRectFilled({end.x - 4.0F, end.y - 4.0F}, {end.x + 4.0F, end.y + 4.0F}, axisColors[axis]);
                    else
                        dl->AddCircleFilled(end, hit ? 5.0F : 4.0F, axisColors[axis]);
                    if (hit && distance < closestDistance)
                    {
                        closestDistance = distance;
                        hoveredAxis = axis;
                    }
                }
                if (vm.activeTransformTool == EditorTransformTool::Scale)
                {
                    const bool uniformHit = hovered && Distance(mouse, *gizmoCenter) <= 8.0F;
                    dl->AddRectFilled(
                        {gizmoCenter->x - 5.0F, gizmoCenter->y - 5.0F}, {gizmoCenter->x + 5.0F, gizmoCenter->y + 5.0F},
                        uniformHit ? Theme::U32(Theme::Text()) : ImGui::GetColorU32(ImVec4{0.8F, 0.8F, 0.8F, 1.0F}));
                    if (uniformHit)
                        hoveredAxis = 3;
                }
                else
                    dl->AddCircleFilled(*gizmoCenter, 4.0F, Theme::U32(Theme::Text()));
            }
        }

        if (!gizmoDrag_.has_value() && hoveredAxis.has_value() && primary.pressed &&
            BeginCapture(Input::PointerButton::Primary))
        {
            const Result<Math::Mat4> parentInverse = Math::TryInverseAffine(*vm.primarySelectionParentWorldTransform);
            if (parentInverse.HasValue())
            {
                const int axis = *hoveredAxis;
                const Math::Vec3 chosenAxis = axis < 3 ? worldAxes[axis] : Math::Vec3{};
                const ImVec2 direction = axis < 3 ? screenDirections[axis] : ImVec2{0.7071F, -0.7071F};
                const std::optional<Math::Vec3> startPlaneVector =
                    vm.activeTransformTool == EditorTransformTool::Rotate
                        ? RotationPlaneVector(vm.viewportCamera, gizmoPosition, chosenAxis, mouse, orig, w, h)
                        : std::optional<Math::Vec3>{Math::Vec3{}};
                if (!startPlaneVector.has_value())
                {
                    FinishCapture();
                    return;
                }
                gizmoDrag_ = TransformGizmoDrag{.object = selectedObject->id,
                                                .tool = vm.activeTransformTool,
                                                .space = vm.activeTransformSpace,
                                                .axis = axis,
                                                .initialTransform = selectedObject->localTransform,
                                                .draftTransform = selectedObject->localTransform,
                                                .initialWorldTransform = worldTransform,
                                                .parentWorldTransform = *vm.primarySelectionParentWorldTransform,
                                                .initialWorldPosition = gizmoPosition,
                                                .currentWorldPosition = gizmoPosition,
                                                .worldAxis = chosenAxis,
                                                .startPlaneVector = *startPlaneVector,
                                                .parentWorldInverse = parentInverse.Value(),
                                                .startMouse = mouse,
                                                .screenDirection = direction,
                                                .gizmoCenter = *gizmoCenter,
                                                .pixelsPerWorldUnit =
                                                    axis < 3 ? std::max(pixelsPerWorldUnit[axis], 1.0F) : 1.0F};
                navigationMode_ = NavigationMode::None;
            }
            else
                FinishCapture();
        }
    }

    if (gizmoDrag_.has_value())
    {
        if (input.State(Input::Key::Escape).pressed)
        {
            inputRouter_->CancelCapture(Input::CaptureCancellationReason::Escape);
            cmd.command = EditorWorkspaceViewCommand::CancelObjectTransformPreview;
            cancelTransformOnNextDraw_ = false;
        }
        else if (primary.down)
        {
            TransformGizmoDrag &drag = *gizmoDrag_;
            Math::Transform next = drag.initialTransform;
            const ImVec2 mouseDelta{mouse.x - drag.startMouse.x, mouse.y - drag.startMouse.y};
            const float projectedPixels = mouseDelta.x * drag.screenDirection.x + mouseDelta.y * drag.screenDirection.y;
            if (drag.tool == EditorTransformTool::Move)
            {
                const float worldDistance = projectedPixels / drag.pixelsPerWorldUnit;
                const Math::Vec3 worldPosition = drag.initialWorldPosition + drag.worldAxis * worldDistance;
                next.translation = Math::TransformAffinePoint(drag.parentWorldInverse, worldPosition);
                drag.currentWorldPosition = worldPosition;
            }
            else if (drag.tool == EditorTransformTool::Rotate)
            {
                const auto current = RotationPlaneVector(vm.viewportCamera, drag.initialWorldPosition, drag.worldAxis,
                                                         mouse, orig, w, h);
                if (current.has_value())
                {
                    const float angle =
                        std::atan2(Math::Dot(drag.worldAxis, Math::Cross(drag.startPlaneVector, *current)),
                                   Math::Dot(drag.startPlaneVector, *current));
                    if (drag.space == EditorTransformSpace::Local)
                    {
                        const Math::Quaternion delta = Math::Quaternion::FromAxisAngle(LocalAxis(drag.axis), angle);
                        next.rotation = (drag.initialTransform.rotation * delta).Normalized();
                    }
                    else
                    {
                        const Math::Quaternion delta = Math::Quaternion::FromAxisAngle(drag.worldAxis, angle);
                        Math::Mat4 desiredWorld =
                            Math::Multiply(Math::Transform{.rotation = delta}.ToMatrix(), drag.initialWorldTransform);
                        SetTranslation(desiredWorld, drag.initialWorldPosition);
                        const Result<Math::Transform> local =
                            Math::TryDecomposeAffineTRS(Math::Multiply(drag.parentWorldInverse, desiredWorld));
                        if (local.HasValue())
                            next = local.Value();
                    }
                }
            }
            else if (drag.tool == EditorTransformTool::Scale)
            {
                const float factor = std::clamp(std::exp(projectedPixels / 120.0F), 0.01F, 100.0F);
                if (drag.space == EditorTransformSpace::Local)
                    MultiplyScaleComponent(next.scale, drag.axis, factor);
                else
                {
                    Math::Mat4 scaleMatrix = drag.axis == 3
                                                 ? Math::Transform{.scale = {factor, factor, factor}}.ToMatrix()
                                                 : WorldAxisScaleMatrix(drag.worldAxis, factor);
                    Math::Mat4 desiredWorld = Math::Multiply(scaleMatrix, drag.initialWorldTransform);
                    SetTranslation(desiredWorld, drag.initialWorldPosition);
                    const Result<Math::Transform> local =
                        Math::TryDecomposeAffineTRS(Math::Multiply(drag.parentWorldInverse, desiredWorld));
                    if (local.HasValue())
                    {
                        next = local.Value();
                        PreserveScaleSigns(next.scale, drag.initialTransform.scale);
                    }
                }
            }
            if (next != drag.draftTransform)
            {
                drag.draftTransform = next;
                cmd.command = EditorWorkspaceViewCommand::PreviewObjectTransform;
                cmd.objectPayload = drag.object;
                cmd.transformPayload = next;
            }
        }
        else if (primary.released)
        {
            if (gizmoDrag_->draftTransform != gizmoDrag_->initialTransform)
            {
                cmd.command = EditorWorkspaceViewCommand::CommitObjectTransform;
                cmd.objectPayload = gizmoDrag_->object;
                cmd.transformPayload = gizmoDrag_->draftTransform;
            }
            gizmoDrag_.reset();
            FinishCapture();
        }
        return;
    }

    if (navigationMode_ == NavigationMode::None && hovered)
    {
        if (secondary.pressed && BeginCapture(Input::PointerButton::Secondary))
            navigationMode_ = NavigationMode::Fly;
        else if (middle.pressed && BeginCapture(Input::PointerButton::Middle))
            navigationMode_ = NavigationMode::Pan;
        else if (input.modifiers.alt && primary.pressed && BeginCapture(Input::PointerButton::Primary))
            navigationMode_ = NavigationMode::Orbit;
    }
    const bool navigationHeld = (navigationMode_ == NavigationMode::Fly && secondary.down) ||
                                (navigationMode_ == NavigationMode::Pan && middle.down) ||
                                (navigationMode_ == NavigationMode::Orbit && primary.down);
    if (navigationMode_ != NavigationMode::None && !navigationHeld)
        FinishCapture();

    EditorViewportNavigationDelta navigation;
    if (navigationMode_ != NavigationMode::None)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        const float orbitSensitivity = std::clamp(ctx.settings.settings.orbitSensitivity / 100.0F, 0.1F, 3.0F);
        const float panSensitivity = std::clamp(ctx.settings.settings.panSensitivity / 100.0F, 0.1F, 3.0F);
        const float lookSensitivity = 0.003F * orbitSensitivity;
        if (navigationMode_ == NavigationMode::Fly || navigationMode_ == NavigationMode::Orbit)
        {
            navigation.yawRadians = -input.pointer.deltaX * lookSensitivity;
            navigation.pitchRadians =
                -input.pointer.deltaY * lookSensitivity * (ctx.settings.settings.invertOrbitY ? -1.0F : 1.0F);
            navigation.orbit = navigationMode_ == NavigationMode::Orbit;
        }
        else
        {
            const float viewportHeight = std::max(h, 1.0F);
            const float targetDistance = Math::Length(vm.viewportCamera.target - vm.viewportCamera.position);
            const float worldUnitsPerPixel =
                vm.viewportCamera.projection == Runtime::CameraProjection::Perspective
                    ? 2.0F * targetDistance * std::tan(vm.viewportCamera.verticalFovRadians * 0.5F) / viewportHeight
                    : vm.viewportCamera.orthographicHeight / viewportHeight;
            navigation.moveRight = -input.pointer.deltaX * worldUnitsPerPixel * panSensitivity;
            navigation.moveUp = input.pointer.deltaY * worldUnitsPerPixel * panSensitivity;
        }
        if (navigationMode_ == NavigationMode::Fly)
        {
            const float speed = (input.modifiers.shift ? 12.0F : 3.0F) * std::clamp(deltaSeconds, 0.0F, 0.1F);
            navigation.moveForward =
                (input.State(Input::Key::W).down ? speed : 0.0F) - (input.State(Input::Key::S).down ? speed : 0.0F);
            navigation.moveRight +=
                (input.State(Input::Key::D).down ? speed : 0.0F) - (input.State(Input::Key::A).down ? speed : 0.0F);
            navigation.moveUp +=
                (input.State(Input::Key::E).down ? speed : 0.0F) - (input.State(Input::Key::Q).down ? speed : 0.0F);
        }
    }
    if (navigationMode_ == NavigationMode::None && hovered && input.pointer.wheelY != 0.0F)
        navigation.dollyScale = std::exp(-input.pointer.wheelY * 0.15F);

    if (navigationMode_ == NavigationMode::None && hovered && workspaceInputContext_ != nullptr &&
        inputRouter_->ReadAction(*workspaceInputContext_, Input::ActionId{kActionViewportFocusSelected}).pressed &&
        vm.primarySelectionWorldBounds.has_value())
    {
        cmd.command = EditorWorkspaceViewCommand::FocusViewportSelection;
        cmd.floatPayload = w / h;
    }
    else if (HasNavigation(navigation))
    {
        cmd.command = EditorWorkspaceViewCommand::NavigateViewport;
        cmd.viewportNavigationPayload = navigation;
    }
    else if (navigationMode_ == NavigationMode::None && hovered && !input.modifiers.alt && primary.pressed)
    {
        cmd.command = EditorWorkspaceViewCommand::PickViewport;
        cmd.viewportPickPayload = ViewportPickRequest{.normalizedX = std::clamp((mouse.x - orig.x) / w, 0.0F, 1.0F),
                                                      .normalizedY = std::clamp((mouse.y - orig.y) / h, 0.0F, 1.0F),
                                                      .aspect = w / h};
    }
}

void ViewportPanel::DrawIcon(ImDrawList *dl, const ImVec2 &pos, const ImVec2 &size, const ImU32 color)
{
    const float ox = pos.x + (size.x - 14.0f) * 0.5f;
    const float oy = pos.y + (size.y - 14.0f) * 0.5f;

    // Simple viewport icon (screen/camera)
    dl->AddRect(ImVec2(ox + 2, oy + 3), ImVec2(ox + 12, oy + 11), color, 1.0f, 0, 1.5f);
    dl->AddCircle(ImVec2(ox + 7, oy + 7), 2.0f, color, 0, 1.5f);
}

auto ViewportPanel::DrawPanel(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &vm,
                              EditorWorkspaceViewCommandData &cmd, const EditorGuiContext &ctx) -> void
{
    const std::array tabNames{ctx.localization.Get("editor", "workspace.panel.viewport").c_str()};
    Ui::DrawDockTabs(tabNames, 0, ctx.theme.fonts);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::BeginChild("##Content", ImVec2(size.x, size.y - 28.0f), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 orig = ImGui::GetCursorScreenPos();
    const float w = size.x;
    const float h = size.y - 28.0f; // Adjust for tabs
    const float cx = orig.x + w * 0.5F;
    const float horizon = orig.y + h * 0.38F;
    const float ground = orig.y + h;

    const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    if (viewportRenderer_ != nullptr)
    {
        const bool renderable = w > 0.0F && h > 0.0F && framebufferScale.x > 0.0F && framebufferScale.y > 0.0F;
        viewportRenderer_->RequestExtent(
                renderable
                    ? EditorViewportExtent{
                        .width = static_cast<std::uint32_t>(std::max(1.0F, w * framebufferScale.x)),
                        .height = static_cast<std::uint32_t>(std::max(1.0F, h * framebufferScale.y)),
                    }
                    : EditorViewportExtent{});
    }

    const EditorViewportTextureView textureView =
        viewportRenderer_ != nullptr ? viewportRenderer_->TextureView() : EditorViewportTextureView{};
    const bool hasRenderedViewport =
        viewportRenderer_ != nullptr && viewportRenderer_->IsReady() && textureView.IsValid();
    if (hasRenderedViewport)
    {
        const auto texture = static_cast<ImTextureID>(textureView.textureId);
        dl->AddImage(texture, orig, ImVec2(orig.x + w, orig.y + h), ImVec2(textureView.u0, textureView.v0),
                     ImVec2(textureView.u1, textureView.v1));
    }
    else
    {
        dl->AddRectFilledMultiColor(orig, ImVec2(orig.x + w, orig.y + h),
                                    ImGui::GetColorU32(ImVec4(0.05F, 0.06F, 0.09F, 1.0F)),
                                    ImGui::GetColorU32(ImVec4(0.05F, 0.06F, 0.09F, 1.0F)),
                                    ImGui::GetColorU32(ImVec4(0.09F, 0.11F, 0.15F, 1.0F)),
                                    ImGui::GetColorU32(ImVec4(0.09F, 0.11F, 0.15F, 1.0F)));

        const ImU32 gridCol = ImGui::GetColorU32(ImVec4(0.16F, 0.20F, 0.27F, 1.0F));
        constexpr int kLines = 14;
        for (int g = 0; g <= kLines; ++g)
        {
            const float t = static_cast<float>(g) / kLines;
            const float xOff = (t - 0.5F) * w;
            dl->AddLine(ImVec2(cx + xOff, ground), ImVec2(cx, horizon), gridCol, 0.7F);

            const float gt = static_cast<float>(g) / kLines;
            const float yPos = ground - gt * (ground - horizon);
            const float hw = w * (1.0F - gt * 0.90F) * 0.5F;
            dl->AddLine(ImVec2(cx - hw, yPos), ImVec2(cx + hw, yPos), gridCol, 0.7F);
        }

        dl->AddRectFilledMultiColor(ImVec2(orig.x, horizon - 12.0F), ImVec2(orig.x + w, horizon + 22.0F),
                                    ImGui::GetColorU32(ImVec4(0.01F, 0.22F, 0.44F, 0.0F)),
                                    ImGui::GetColorU32(ImVec4(0.01F, 0.22F, 0.44F, 0.0F)),
                                    ImGui::GetColorU32(ImVec4(0.03F, 0.38F, 0.60F, 0.35F)),
                                    ImGui::GetColorU32(ImVec4(0.03F, 0.38F, 0.60F, 0.35F)));
    }

    if (hasRenderedViewport && w > 0.0F && h > 0.0F)
    {
        const ImVec2 projectionMinimum{orig.x + 10.0F, orig.y + 8.0F};
        const ImVec2 projectionMaximum{projectionMinimum.x + 190.0F,
                                       projectionMinimum.y + ImGui::GetFontSize() + 14.0F};
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool pointerOverProjection = mouse.x >= projectionMinimum.x && mouse.x <= projectionMaximum.x &&
                                           mouse.y >= projectionMinimum.y && mouse.y <= projectionMaximum.y;
        const bool interactionActive = navigationMode_ != NavigationMode::None || gizmoDrag_.has_value();
        if (!pointerOverProjection || interactionActive)
        {
            ImGui::SetCursorScreenPos(orig);
            ImGui::InvisibleButton("##ViewportSurface", ImVec2(w, h),
                                   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                       ImGuiButtonFlags_MouseButtonMiddle);
            DrawInteraction(dl, orig, w, h, ImGui::IsItemHovered(), vm, cmd, ctx, ImGui::GetIO().DeltaTime);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(orig.x + 10.0F, orig.y + 8.0F));
    const char *projectionItems[]{
        ctx.localization.Get("editor", "workspace.viewport.perspective_shaded").c_str(),
        ctx.localization.Get("editor", "workspace.viewport.orthographic_shaded").c_str(),
    };
    int projectionIndex = vm.viewportCamera.projection == Runtime::CameraProjection::Perspective ? 0 : 1;
    ImGui::PushItemWidth(190.0F);
    if (Ui::ComboControl("viewport_projection", &projectionIndex, projectionItems, 2, ctx.theme.fonts))
    {
        cmd.command = EditorWorkspaceViewCommand::ChangeViewportProjection;
        cmd.viewportProjectionPayload =
            projectionIndex == 0 ? Runtime::CameraProjection::Perspective : Runtime::CameraProjection::Orthographic;
    }
    ImGui::PopItemWidth();
    ImGui::SetCursorScreenPos(ImVec2(orig.x + 10.0F, orig.y + 42.0F));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.32F, 0.38F, 0.48F, 1.0F));
    const std::size_t objectCountValue = vm.objects.size();
    const std::string objectCount = std::vformat(ctx.localization.Get("editor", "workspace.viewport.object_count"),
                                                 std::make_format_args(objectCountValue));
    ImGui::TextUnformatted(objectCount.c_str());
    ImGui::PopStyleColor();

    if (!hasRenderedViewport)
    {
        const std::string &msg = ctx.localization.Get("editor", "workspace.viewport.renderer_missing");
        const float msgW = ImGui::CalcTextSize(msg.c_str()).x;
        ImGui::SetCursorScreenPos(ImVec2(cx - msgW * 0.5F, orig.y + h * 0.52F));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.28F, 0.32F, 0.40F, 1.0F));
        ImGui::TextUnformatted(msg.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace Horo::Editor
