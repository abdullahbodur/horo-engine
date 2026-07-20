#pragma once

#include "EditorUiTestHarness.h"
#include "Horo/Editor/GuiRoute.h"
#include "Horo/Runtime/Scene/SceneComponents.h"

#include <filesystem>
#include <memory>
#include <string>

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
    explicit FullEditorUiTestHost(std::string locale = "en-US");
    ~FullEditorUiTestHost();
    FullEditorUiTestHost(const FullEditorUiTestHost&) = delete;
    FullEditorUiTestHost& operator=(const FullEditorUiTestHost&) = delete;

    /** @brief Advances and draws one complete editor frame. */
    void DrawFrame(ImGuiTestContext* context);

    /** @brief Returns the active top-level route. */
    [[nodiscard]] Editor::GuiRouteKind ActiveRoute() const noexcept;

    /** @brief Returns the isolated root where setup-created projects are stored. */
    [[nodiscard]] const std::filesystem::path& ProjectsRoot() const noexcept;

    /** @brief Returns the real screen host for cross-surface assertions. */
    [[nodiscard]] Editor::GuiScreenHost& Screens() noexcept;

    /** @brief Returns the central input router driven by the Test Engine IO bridge. */
    [[nodiscard]] Input::InputRouter& Input() noexcept;

    /** @brief Returns the projection published by the active workspace to the viewport scene handoff. */
    [[nodiscard]] Runtime::CameraProjection ViewportProjection() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace Horo::Tests
