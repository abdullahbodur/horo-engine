#pragma once

#include "EditorUiTestHarness.h"
#include "Horo/Editor/GuiRoute.h"
#include "Horo/Runtime/Scene/SceneComponents.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

struct ImGuiTestContext;

namespace Horo::Editor
{
    class GuiScreenHost;
}

namespace Horo::Tests
{
    /** @brief Owns the real HoroEditor screen, modal, project, input, and workspace composition for one scenario. */
    class FullEditorUiTestHost
    {
    public:
        FullEditorUiTestHost(IEditorUiTestSurface& surface, std::string locale = "en-US",
                             std::optional<std::string> recentProjectName = std::nullopt);
        ~FullEditorUiTestHost();
        FullEditorUiTestHost(const FullEditorUiTestHost&) = delete;
        FullEditorUiTestHost& operator=(const FullEditorUiTestHost&) = delete;

        /** @brief Advances and draws one complete editor frame. */
        void DrawFrame(ImGuiTestContext* context);

        /** @brief Returns the active top-level route. */
        [[nodiscard]] Editor::GuiRouteKind ActiveRoute() const noexcept;

        /** @brief Returns the isolated root where setup-created projects are stored. */
        [[nodiscard]] const std::filesystem::path& ProjectsRoot() const noexcept;

        /** @brief Reports whether the given route has been submitted to its real screen Draw callback. */
        [[nodiscard]] bool WasRouteDrawn(Editor::GuiRouteKind route) const noexcept;

        /** @brief Returns how many frames submitted the given route to its real screen Draw callback. */
        [[nodiscard]] std::size_t RouteDrawCount(Editor::GuiRouteKind route) const noexcept;

        /** @brief Returns the real screen host for cross-surface assertions. */
        [[nodiscard]] Editor::GuiScreenHost& Screens() noexcept;

        /** @brief Returns the central input router driven by the Test Engine IO bridge. */
        [[nodiscard]] Input::InputRouter& Input() noexcept;

        /** @brief Returns the projection published by the active workspace to the viewport scene handoff. */
        [[nodiscard]] Runtime::CameraProjection ViewportProjection() const noexcept;

        /** @brief Reports whether the selected renderer has produced a viewport image. */
        [[nodiscard]] bool RendererReady() const noexcept;

        /** @brief Returns the canonical renderer selected for this E2E composition. */
        [[nodiscard]] std::string_view RendererName() const noexcept;

    private:
        struct State;
        std::unique_ptr<State> state_;
    };
} // namespace Horo::Tests
