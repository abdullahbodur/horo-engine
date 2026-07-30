#include "ViewportNavigationController.h"

#include "Horo/Editor/EditorSettingsService.h"
#include "editor/input/EditorInputActions.h"
#include "editor/screens/workspace/panels/viewport/interaction/ViewportInteractionCapture.h"

#include <algorithm>
#include <cmath>

namespace Horo::Editor {
    namespace {
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
                                              const Math::ClipDepthRange depthRange) {
        const Input::ButtonState primary = input.State(Input::PointerButton::Primary);
        const Input::ButtonState secondary = input.State(Input::PointerButton::Secondary);
        const Input::ButtonState middle = input.State(Input::PointerButton::Middle);

        if (mode_ == Mode::None && hovered) {
            if (secondary.pressed && capture.Begin(Input::PointerButton::Secondary))
                mode_ = Mode::Fly;
            else if (middle.pressed && capture.Begin(Input::PointerButton::Middle))
                mode_ = Mode::Pan;
            else if (input.modifiers.alt && primary.pressed && capture.Begin(Input::PointerButton::Primary))
                mode_ = Mode::Orbit;
        }

        const bool navigationHeld =
            (mode_ == Mode::Fly && secondary.down) || (mode_ == Mode::Pan && middle.down) || (mode_ == Mode::Orbit && primary.down);
        if (mode_ != Mode::None && !navigationHeld) {
            capture.Finish();
            Reset();
        }

        EditorViewportNavigationDelta navigation;
        if (mode_ != Mode::None) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            const float orbitSensitivity = std::clamp(context.settings.settings.orbitSensitivity / 100.0F, 0.1F, 3.0F);
            const float panSensitivity = std::clamp(context.settings.settings.panSensitivity / 100.0F, 0.1F, 3.0F);
            const float lookSensitivity = 0.003F * orbitSensitivity;
            if (mode_ == Mode::Fly || mode_ == Mode::Orbit) {
                navigation.yawRadians = -input.pointer.deltaX * lookSensitivity;
                navigation.pitchRadians = -input.pointer.deltaY * lookSensitivity * (context.settings.settings.invertOrbitY ? -1.0F : 1.0F);
                navigation.orbit = mode_ == Mode::Orbit;
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
            if (mode_ == Mode::Fly) {
                const float speed = (input.modifiers.shift ? 12.0F : 3.0F) * std::clamp(deltaSeconds, 0.0F, 0.1F);
                navigation.moveForward =
                    (input.State(Input::Key::W).down ? speed : 0.0F) - (input.State(Input::Key::S).down ? speed : 0.0F);
                navigation.moveRight += (input.State(Input::Key::D).down ? speed : 0.0F) - (input.State(Input::Key::A).down ? speed : 0.0F);
                navigation.moveUp += (input.State(Input::Key::E).down ? speed : 0.0F) - (input.State(Input::Key::Q).down ? speed : 0.0F);
            }
        }

        if (mode_ == Mode::None && hovered && input.pointer.wheelY != 0.0F)
            navigation.dollyScale = std::exp(-input.pointer.wheelY * 0.15F);

        Input::InputContextToken *workspaceContext = capture.WorkspaceContext();
        Input::InputRouter *router = capture.Router();
        if (mode_ == Mode::None && hovered && workspaceContext != nullptr && router != nullptr &&
            router->ReadAction(*workspaceContext, Input::ActionId{kActionViewportFocusSelected}).pressed &&
            viewModel.primarySelectionWorldBounds.has_value()) {
            command.command = EditorWorkspaceViewCommand::FocusViewportSelection;
            command.floatPayload = width / height;
        } else if (HasNavigation(navigation)) {
            command.command = EditorWorkspaceViewCommand::NavigateViewport;
            command.viewportNavigationPayload = navigation;
        } else if (mode_ == Mode::None && hovered && !input.modifiers.alt && primary.pressed) {
            command.command = EditorWorkspaceViewCommand::PickViewport;
            command.viewportPickPayload = ViewportPickRequest{.normalizedX = std::clamp((input.pointer.x - origin.x) / width, 0.0F, 1.0F),
                                                              .normalizedY = std::clamp((input.pointer.y - origin.y) / height, 0.0F, 1.0F),
                                                              .aspect = width / height,
                                                              .depthRange = depthRange,
                                                              .toggleSelection = input.modifiers.control || input.modifiers.command};
        }
    }
}  // namespace Horo::Editor
