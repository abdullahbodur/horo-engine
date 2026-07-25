#pragma once

#include "editor/renderer/EditorViewportRenderer.h"

#include <memory>
#include <string_view>

struct ImGuiContext;

namespace Horo::Tests
{
    /** @brief Presentation boundary used by the UI test harness frame loop. */
    class IEditorUiTestSurface
    {
    public:
        virtual ~IEditorUiTestSurface() = default;

        /** @brief Attaches the surface to the newly created ImGui context. */
        virtual void Initialize(ImGuiContext& context) = 0;

        /** @brief Prepares one frame; returns false when an interactive window requests close. */
        [[nodiscard]] virtual bool BeginFrame() = 0;

        /** @brief Presents the current ImGui draw data, or performs a headless no-op. */
        virtual void Present() = 0;

        /** @brief Releases backend resources before the ImGui context is destroyed. */
        virtual void Shutdown() noexcept = 0;

        /** @brief Reports whether the surface owns a visible interactive window. */
        [[nodiscard]] virtual bool IsInteractive() const noexcept = 0;

        /** @brief Returns the viewport renderer selected for this test composition. */
        [[nodiscard]] virtual Editor::IEditorViewportRenderer& ViewportRenderer() noexcept = 0;

        /** @brief Executes the selected renderer for the viewport requested by the current UI frame. */
        virtual void RenderViewport(Editor::EditorViewportSceneView scene) = 0;

        /** @brief Returns the canonical renderer identity used by diagnostics and assertions. */
        [[nodiscard]] virtual std::string_view RendererName() const noexcept = 0;
    };

    /** @brief Creates the deterministic null renderer used by CI and default local tests. */
    [[nodiscard]] std::unique_ptr<IEditorUiTestSurface> CreateHeadlessEditorUiTestSurface();

    /** @brief Creates the visible production OpenGL renderer composition. */
    [[nodiscard]] std::unique_ptr<IEditorUiTestSurface> CreateInteractiveOpenGlEditorUiTestSurface();

    /** @brief Creates the visible production Metal renderer composition. */
    [[nodiscard]] std::unique_ptr<IEditorUiTestSurface> CreateInteractiveMetalEditorUiTestSurface();

    /** @brief Selects the null, OpenGL, or Metal test composition from HORO_UI_TEST_RENDERER. */
    [[nodiscard]] std::unique_ptr<IEditorUiTestSurface> CreateEditorUiTestSurfaceFromEnvironment();
} // namespace Horo::Tests
