#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Runtime/Input.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"

#include <imgui.h>

namespace Horo::Editor {
    class ViewportInteractionCapture;

    /** @brief Frame-local inputs used to update viewport navigation. */
    struct ViewportNavigationUpdateContext {
        ImVec2 origin{};
        float width{0.0F};
        float height{0.0F};
        bool hovered{false};
        const Input::RawInputSnapshot &input;
        const EditorWorkspaceViewModel &viewModel;
        EditorWorkspaceViewCommandData &command;
        const EditorGuiContext &gui;
        float deltaSeconds{0.0F};
        Math::ClipDepthRange depthRange{Math::ClipDepthRange::NegativeOneToOne};
    };

    /** @brief Maps routed viewport input to camera, focus, and picking commands without owning viewport state. */
    class ViewportNavigationController {
    public:
        void Reset() noexcept;

        [[nodiscard]] bool IsActive() const noexcept;

        void Update(const ViewportNavigationUpdateContext &context, ViewportInteractionCapture &capture);

    private:
        enum class Mode {
            None,
            Fly,
            Pan,
            Orbit,
        };

        void TryBeginNavigation(const ViewportNavigationUpdateContext &context, ViewportInteractionCapture &capture);
        void EndReleasedNavigation(const Input::RawInputSnapshot &input, ViewportInteractionCapture &capture);
        [[nodiscard]] EditorViewportNavigationDelta BuildNavigationDelta(const ViewportNavigationUpdateContext &context) const;
        void EmitNavigationCommand(const ViewportNavigationUpdateContext &context, const EditorViewportNavigationDelta &navigation,
                                   const ViewportInteractionCapture &capture) const;

        Mode mode_{Mode::None};
    };
}  // namespace Horo::Editor
