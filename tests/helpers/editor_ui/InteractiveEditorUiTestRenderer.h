#pragma once

#include "Horo/Runtime/Render/RenderFrontend.h"
#include "editor/renderer/EditorGuiRenderer.h"
#include "editor/renderer/EditorViewportRenderer.h"

#include <memory>
#include <optional>

namespace Horo::Tests
{
    /** @brief Runs the production renderer frame contract for one interactive UI-test surface. */
    class InteractiveEditorUiTestRenderer final
    {
    public:
        [[nodiscard]] static std::unique_ptr<InteractiveEditorUiTestRenderer> Create(
            std::unique_ptr<Render::RenderFrontend> frontend,
            std::unique_ptr<Editor::IEditorGuiRenderer> guiRenderer,
            std::unique_ptr<Editor::IEditorViewportRenderer> viewportRenderer);

        ~InteractiveEditorUiTestRenderer();
        InteractiveEditorUiTestRenderer(const InteractiveEditorUiTestRenderer&) = delete;
        InteractiveEditorUiTestRenderer& operator=(const InteractiveEditorUiTestRenderer&) = delete;

        /** @brief Begins one production renderer and GUI frame for the drawable extent. */
        void BeginFrame(Render::FramebufferExtent outputExtent);

        /** @brief Executes the requested viewport pass followed by the primary output pass. */
        void RenderViewport(Editor::EditorViewportSceneView scene);

        /** @brief Encodes ImGui draw data and presents the active renderer frame. */
        void Present();

        /** @brief Cancels active work and releases renderer resources in dependency order. */
        void Shutdown() noexcept;

        /** @brief Returns the backend-specific viewport adapter owned by this composition. */
        [[nodiscard]] Editor::IEditorViewportRenderer& ViewportRenderer() noexcept;

    private:
        InteractiveEditorUiTestRenderer(std::unique_ptr<Render::RenderFrontend> frontend,
                                        std::unique_ptr<Editor::IEditorGuiRenderer> guiRenderer,
                                        std::unique_ptr<Editor::IEditorViewportRenderer> viewportRenderer) noexcept;

        void Initialize();
        void ExecutePasses(Editor::EditorViewportSceneView scene, bool includeViewport);

        std::unique_ptr<Render::RenderFrontend> frontend_;
        std::unique_ptr<Editor::IEditorGuiRenderer> guiRenderer_;
        std::unique_ptr<Editor::IEditorViewportRenderer> viewportRenderer_;
        Render::RenderTargetHandle viewportTarget_{};
        Render::FramebufferExtent outputExtent_{};
        std::optional<Render::RenderFrameScope> frame_;
        std::uint64_t frameNumber_{1};
        bool guiInitialized_{false};
        bool executorAttached_{false};
        bool frameExecuted_{false};
    };
} // namespace Horo::Tests
