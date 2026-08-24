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

    void ViewportNavigationController::TryBeginNavigation(const ViewportNavigationUpdateContext &context,
                                                          ViewportInteractionCapture &capture) {
        using enum Mode;
        using enum Input::PointerButton;
        if (mode_ == None && context.hovered) {
            const Input::RawInputSnapshot &input = context.input;
            const Input::ButtonState primary = input.State(Primary);
            const Input::ButtonState secondary = input.State(Secondary);
            const Input::ButtonState middle = input.State(Middle);
            if (secondary.pressed && capture.Begin(Secondary))
                mode_ = Fly;
            else if (middle.pressed && capture.Begin(Middle))
                mode_ = Pan;
            else if (input.modifiers.alt && primary.pressed && capture.Begin(Primary))
                mode_ = Orbit;
        }
    }

    void ViewportNavigationController::EndReleasedNavigation(const Input::RawInputSnapshot &input, ViewportInteractionCapture &capture) {
        using enum Mode;
        using enum Input::PointerButton;
        if (const bool navigationHeld = (mode_ == Fly && input.State(Secondary).down) || (mode_ == Pan && input.State(Middle).down) ||
                                        (mode_ == Orbit && input.State(Primary).down);
            mode_ != None && !navigationHeld) {
            capture.Finish();
            Reset();
        }
    }

    void ViewportNavigationController::ApplyPointerNavigation(const ViewportNavigationUpdateContext &context, const float orbitSensitivity,
                                                              const float panSensitivity, EditorViewportNavigationDelta &navigation) const {
        using enum Mode;
        const Input::RawInputSnapshot &input = context.input;
        const EditorWorkspaceViewModel &viewModel = context.viewModel;
        if (mode_ == Fly || mode_ == Orbit) {
            const float lookSensitivity = 0.003F * orbitSensitivity;
            navigation.yawRadians = -input.pointer.deltaX * lookSensitivity;
            navigation.pitchRadians = -input.pointer.deltaY * lookSensitivity * (context.gui.settings.settings.invertOrbitY ? -1.0F : 1.0F);
            navigation.orbit = mode_ == Orbit;
            return;
        }

        const float viewportHeight = std::max(context.height, 1.0F);
        const float targetDistance = Math::Length(viewModel.viewportCamera.target - viewModel.viewportCamera.position);
        const float worldUnitsPerPixel =
            viewModel.viewportCamera.projection == Runtime::CameraProjection::Perspective
                ? 2.0F * targetDistance * std::tan(viewModel.viewportCamera.verticalFovRadians * 0.5F) / viewportHeight
                : viewModel.viewportCamera.orthographicHeight / viewportHeight;
        navigation.moveRight = -input.pointer.deltaX * worldUnitsPerPixel * panSensitivity;
        navigation.moveUp = input.pointer.deltaY * worldUnitsPerPixel * panSensitivity;
    }

    void ViewportNavigationController::ApplyFlyNavigation(const ViewportNavigationUpdateContext &context,
                                                          EditorViewportNavigationDelta &navigation) const {
        using enum Mode;
        using enum Input::Key;
        if (mode_ != Fly)
            return;
        const Input::RawInputSnapshot &input = context.input;
        const float speed =
            (input.modifiers.shift ? FastFlyMovementSpeed : FlyMovementSpeed) * std::clamp(context.deltaSeconds, 0.0F, 0.1F);
        navigation.moveForward = (input.State(W).down ? speed : 0.0F) - (input.State(S).down ? speed : 0.0F);
        navigation.moveRight += (input.State(D).down ? speed : 0.0F) - (input.State(A).down ? speed : 0.0F);
        navigation.moveUp += (input.State(E).down ? speed : 0.0F) - (input.State(Q).down ? speed : 0.0F);
    }

    EditorViewportNavigationDelta ViewportNavigationController::BuildNavigationDelta(const ViewportNavigationUpdateContext &context) const {
        using enum Mode;
        EditorViewportNavigationDelta navigation;
        if (mode_ != None) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            const float orbitSensitivity =
                std::clamp(static_cast<float>(context.gui.settings.settings.orbitSensitivity) / 100.0F, 0.1F, 3.0F);
            const float panSensitivity = std::clamp(static_cast<float>(context.gui.settings.settings.panSensitivity) / 100.0F, 0.1F, 3.0F);
            ApplyPointerNavigation(context, orbitSensitivity, panSensitivity, navigation);
            ApplyFlyNavigation(context, navigation);
        }
        if (mode_ == None && context.hovered && context.input.pointer.wheelY != 0.0F)
            navigation.dollyScale = std::exp(-context.input.pointer.wheelY * 0.15F);
        return navigation;
    }

    void ViewportNavigationController::EmitNavigationCommand(const ViewportNavigationUpdateContext &context,
                                                             const EditorViewportNavigationDelta &navigation,
                                                             const ViewportInteractionCapture &capture) const {
        using enum Mode;
        using enum Input::PointerButton;
        const Input::RawInputSnapshot &input = context.input;
        const Input::InputContextToken *workspaceContext = capture.WorkspaceContext();
        Input::InputRouter *router = capture.Router();
        if (mode_ == None && context.hovered && workspaceContext != nullptr && router != nullptr &&
            router->ReadAction(*workspaceContext, Input::ActionId{kActionViewportFocusSelected}).pressed &&
            context.viewModel.primarySelectionWorldBounds.has_value()) {
            context.command.command = EditorWorkspaceViewCommand::FocusViewportSelection;
            context.command.floatPayload = context.width / context.height;
        } else if (HasNavigation(navigation)) {
            context.command.command = EditorWorkspaceViewCommand::NavigateViewport;
            context.command.viewportNavigationPayload = navigation;
        } else if (mode_ == None && context.hovered && !input.modifiers.alt && input.State(Primary).pressed) {
            context.command.command = EditorWorkspaceViewCommand::PickViewport;
            context.command.viewportPickPayload =
                ViewportPickRequest{.normalizedX = std::clamp((input.pointer.x - context.origin.x) / context.width, 0.0F, 1.0F),
                                    .normalizedY = std::clamp((input.pointer.y - context.origin.y) / context.height, 0.0F, 1.0F),
                                    .aspect = context.width / context.height,
                                    .depthRange = context.depthRange,
                                    .toggleSelection = input.modifiers.control || input.modifiers.command};
        }
    }

    void ViewportNavigationController::Update(const ViewportNavigationUpdateContext &context, ViewportInteractionCapture &capture) {
        TryBeginNavigation(context, capture);
        EndReleasedNavigation(context.input, capture);
        EmitNavigationCommand(context, BuildNavigationDelta(context), capture);
    }
}  // namespace Horo::Editor
