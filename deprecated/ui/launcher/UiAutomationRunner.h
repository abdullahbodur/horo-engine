/** @file UiAutomationRunner.h
 *  @brief Drives the UI automation test session from the main application loop. */
#pragma once

#include <memory>

struct GLFWwindow;

namespace Horo::Launcher {
    class LauncherEditorShell;
}

namespace Horo::Editor {
    class EditorLayer;
}

namespace Horo {

/** @brief Coordinates the full lifecycle of a UI automation run inside the running application. */
class UiAutomationRunner {
public:
    /** @brief Applies any process-wide setup that must happen before the window and renderer are created.
     *  @param runUiAutomation True when UI automation has been requested for this session. */
    static void PrepareEnvironmentBeforeAppStart(bool runUiAutomation);

    /** @brief Constructs an idle runner; does not start any test session. */
    UiAutomationRunner();

    /** @brief Destroys the runner and releases all associated resources. */
    ~UiAutomationRunner();

    UiAutomationRunner(const UiAutomationRunner &) = delete;

    UiAutomationRunner &operator=(const UiAutomationRunner &) = delete;

    /** @brief Starts the automation session if it was requested, wiring in the required context pointers.
     *  @param runUiAutomation True when UI automation should begin.
     *  @param shellContext    Launcher shell used by scenarios that interact with the launcher UI.
     *  @param editorContext   Editor layer used by scenarios that interact with the editor UI. */
    void StartIfRequested(bool runUiAutomation,
                          Launcher::LauncherEditorShell *shellContext,
                          Editor::EditorLayer *editorContext) const;

    /** @brief Advances the automation engine by one frame and optionally captures the result.
     *  @param nativeWindowHandle GLFW window handle used for frame capture. */
    void PostRenderFrame(GLFWwindow *nativeWindowHandle) const;

    /** @brief Finalizes the current session and writes any pending results or captured data. */
    void Shutdown() const;

    /** @brief Releases the ImGui test engine context; call after the ImGui context is torn down. */
    void DestroyContext() const;

    /** @brief Returns true when all queued scenarios passed.
     *  @return True if the completed automation run had no failures. */
    bool DidPass() const;

    /** @brief Returns true when an automation session is currently running.
     *  @return True if the runner is active. */
    bool IsActive() const;

private:
    /** @brief Opaque implementation; defined in the corresponding .cpp file. */
    struct Impl;
    std::unique_ptr<Impl> m_impl; /**< Owned implementation instance. */
};

} // namespace Horo
