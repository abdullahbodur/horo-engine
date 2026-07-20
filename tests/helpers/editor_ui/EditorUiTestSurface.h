#pragma once

#include <memory>

struct ImGuiContext;

namespace Horo::Tests
{
/** @brief Presentation boundary used by the UI test harness frame loop. */
class IEditorUiTestSurface
{
  public:
    virtual ~IEditorUiTestSurface() = default;

    /** @brief Attaches the surface to the newly created ImGui context. */
    virtual void Initialize(ImGuiContext &context) = 0;

    /** @brief Prepares one frame; returns false when an interactive window requests close. */
    [[nodiscard]] virtual bool BeginFrame() = 0;

    /** @brief Presents the current ImGui draw data, or performs a headless no-op. */
    virtual void Present() = 0;

    /** @brief Releases backend resources before the ImGui context is destroyed. */
    virtual void Shutdown() noexcept = 0;

    /** @brief Reports whether the surface owns a visible interactive window. */
    [[nodiscard]] virtual bool IsInteractive() const noexcept = 0;
};

/** @brief Creates the deterministic null presentation used by CI and default local tests. */
[[nodiscard]] std::unique_ptr<IEditorUiTestSurface> CreateHeadlessEditorUiTestSurface();

/** @brief Creates the visible SDL3/OpenGL presentation used for local test observation. */
[[nodiscard]] std::unique_ptr<IEditorUiTestSurface> CreateInteractiveOpenGlEditorUiTestSurface();

/** @brief Selects headless or interactive presentation from HORO_UI_TEST_PRESENTATION. */
[[nodiscard]] std::unique_ptr<IEditorUiTestSurface> CreateEditorUiTestSurfaceFromEnvironment();
} // namespace Horo::Tests
