#include "ViewportNavigationController.h"

#include "Horo/Editor/EditorSettingsService.h"
#include "editor/input/EditorInputActions.h"
#include "editor/screens/workspace/panels/viewport/interaction/ViewportInteractionCapture.h"

#include <algorithm>
#include <cmath>

namespace Horo::Editor {
    namespace {
        constexpr float FlyMovementSpeed = 1.0F;
        constexpr float FastFlyMovementSpeed = 4.0F;

        [[nodiscard]] bool HasNavigation(const EditorViewportNavigationDelta &delta) noexcept {
            return delta.yawRadians != 0.0F || delta.pitchRadians != 0.0F || delta.moveRight != 0.0F || delta.moveUp != 0.0F ||
                   delta.moveForward != 0.0F || delta.dollyScale != 1.0F;
        }
    }  // namespace

    void ViewportNavigationController::Reset() noexcept {
        mode_ = Mode::None;
    }

    bool ViewportNavigationController::IsActive() const noexcept {
        return mode_ != Mode::None;
    }

    void ViewportNavigationController::Update(const ImVec2 &origin, const float width, const float height, const bool hovered,
                                              const Input::RawInputSnapshot &input, const EditorWorkspaceViewModel &viewModel,
                                              EditorWorkspaceViewCommandData &command, const EditorGuiContext &context,
                                              const float deltaSeconds, ViewportInteractionCapture &capture,
                                              const Math::ClipDepthRange depthRange) {  // NOSONAR(cpp:S107)

        using enum Mode;
        using enum Input::PointerButton;

        const Input::ButtonState primary = input.State(Primary);
        const Input::ButtonState secondary = input.State(Secondary);
        const Input::ButtonState middle = input.State(Middle);

        if (mode_ == None && hovered) {
            if (secondary.pressed && capture.Begin(Secondary))
                mode_ = Fly;
            else if (middle.pressed && capture.Begin(Middle))
                mode_ = Pan;
            else if (input.modifiers.alt && primary.pressed && capture.Begin(Primary))
                mode_ = Orbit;
        }

        if (const bool navigationHeld =
                (mode_ == Fly && secondary.down) || (mode_ == Pan && middle.down) || (mode_ == Orbit && primary.down);
            mode_ != None && !navigationHeld) {
            capture.Finish();
            Reset();
        }

        EditorViewportNavigationDelta navigation;
        if (mode_ != None) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            const float orbitSensitivity = std::clamp(static_cast<float>(context.settings.settings.orbitSensitivity) / 100.0F, 0.1F, 3.0F);
            const float panSensitivity = std::clamp(static_cast<float>(context.settings.settings.panSensitivity) / 100.0F, 0.1F, 3.0F);
            const float lookSensitivity = 0.003F * orbitSensitivity;
            if (mode_ == Fly || mode_ == Orbit) {
                navigation.yawRadians = -input.pointer.deltaX * lookSensitivity;
                navigation.pitchRadians = -input.pointer.deltaY * lookSensitivity * (context.settings.settings.invertOrbitY ? -1.0F : 1.0F);
                navigation.orbit = mode_ == Orbit;
            } else {
                const float viewportHeight = std::max(height, 1.0F);
                const float targetDistance = Math::Length(viewModel.viewportCamera.target - viewModel.viewportCamera.position);
                const float worldUnitsPerPixel =
                    viewModel.viewportCamera.projection == Runtime::CameraProjection::Perspective
                        ? 2.0F * targetDistance * std::tan(viewModel.viewportCamera.verticalFovRadians * 0.5F) / viewportHeight
                        : viewModel.viewportCamera.orthographicHeight / viewportHeight;
                navigation.moveRight = -input.pointer.deltaX * worldUnitsPerPixel * panSensitivity;
                navigation.moveUp = input.pointer.deltaY * worldUnitsPerPixel * panSensitivity;
            }
            if (mode_ == Fly) {
                using enum Input::Key;
                const float speed =
                    (input.modifiers.shift ? FastFlyMovementSpeed : FlyMovementSpeed) * std::clamp(deltaSeconds, 0.0F, 0.1F);
                navigation.moveForward = (input.State(W).down ? speed : 0.0F) - (input.State(S).down ? speed : 0.0F);
                navigation.moveRight += (input.State(D).down ? speed : 0.0F) - (input.State(A).down ? speed : 0.0F);
                navigation.moveUp += (input.State(E).down ? speed : 0.0F) - (input.State(Q).down ? speed : 0.0F);
            }
        }

        if (mode_ == None && hovered && input.pointer.wheelY != 0.0F)
            navigation.dollyScale = std::exp(-input.pointer.wheelY * 0.15F);

        const Input::InputContextToken *workspaceContext = capture.WorkspaceContext();
        Input::InputRouter *router = capture.Router();
        if (mode_ == None && hovered && workspaceContext != nullptr && router != nullptr &&
            router->ReadAction(*workspaceContext, Input::ActionId{kActionViewportFocusSelected}).pressed &&
            viewModel.primarySelectionWorldBounds.has_value()) {
            command.command = EditorWorkspaceViewCommand::FocusViewportSelection;
            command.floatPayload = width / height;
        } else if (HasNavigation(navigation)) {
            command.command = EditorWorkspaceViewCommand::NavigateViewport;
            command.viewportNavigationPayload = navigation;
        } else if (mode_ == None && hovered && !input.modifiers.alt && primary.pressed) {
            command.command = EditorWorkspaceViewCommand::PickViewport;
            command.viewportPickPayload = ViewportPickRequest{.normalizedX = std::clamp((input.pointer.x - origin.x) / width, 0.0F, 1.0F),
                                                              .normalizedY = std::clamp((input.pointer.y - origin.y) / height, 0.0F, 1.0F),
                                                              .aspect = width / height,
                                                              .depthRange = depthRange,
                                                              .toggleSelection = input.modifiers.control || input.modifiers.command};
        }
    }
}  // namespace Horo::Editor
