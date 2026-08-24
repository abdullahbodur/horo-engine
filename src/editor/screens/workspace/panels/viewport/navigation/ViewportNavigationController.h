#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Runtime/Input.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"

#include <imgui.h>

namespace Horo::Editor {
    class ViewportInteractionCapture;

    /** @brief Maps routed viewport input to camera, focus, and picking commands without owning viewport state. */
    class ViewportNavigationController {
    public:
        void Reset() noexcept;

        [[nodiscard]] bool IsActive() const noexcept;

        void Update(const ImVec2 &origin, float width, float height, bool hovered, const Input::RawInputSnapshot &input,
                    const EditorWorkspaceViewModel &viewModel, EditorWorkspaceViewCommandData &command, const EditorGuiContext &context,
                    float deltaSeconds, ViewportInteractionCapture &capture,
                    Math::ClipDepthRange depthRange);  // NOSONAR(cpp:S107)

    private:
        enum class Mode {
            None,
            Fly,
            Pan,
            Orbit,
        };

        Mode mode_{Mode::None};
    };
}  // namespace Horo::Editor
