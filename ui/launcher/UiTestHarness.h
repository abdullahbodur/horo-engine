/** @file UiTestHarness.h
 *  @brief Shared state and scenario registration helpers for UI automation test scenarios. */
#pragma once

#include <filesystem>
#include <string>

#ifdef HORO_STANDALONE_UI_AUTOMATION
#include <imgui_test_engine/imgui_te_engine.h>
#endif

namespace Horo::Launcher {
    class LauncherEditorShell;
}

namespace Horo::Editor {
    class EditorLayer;
}

namespace Horo {

#ifdef HORO_STANDALONE_UI_AUTOMATION

/** @brief Mutable context shared across all scenarios within a single automation run. */
struct UiAutomationRunState {
    std::filesystem::path tempRoot;                /**< Temporary directory allocated for this run. */
    std::filesystem::path projectRoot;             /**< Project directory opened during the run. */
    std::filesystem::path uiCaptureOutputDir;      /**< Directory where frame captures are written. */

    bool captureEnabled = false;                   /**< True when screenshot capture is active. */
    bool videoEnabled = false;                     /**< True when video recording was requested. */
    bool videoCaptureOpen = false;                 /**< True while a video capture session is open. */
    bool videoCaptureOwnedByRegistry = false;      /**< True when the registry manages the video capture lifetime. */

    Launcher::LauncherEditorShell *shellContext = nullptr; /**< Launcher shell for scenarios that interact with the home screen. */
    Editor::EditorLayer *editorContext = nullptr;          /**< Editor layer for scenarios that interact with the editor. */
};

/** @brief Registers all compiled-in UI scenarios with the test engine and queues the filtered subset.
 *  @param engine          The ImGui test engine instance that will run the scenarios.
 *  @param state           Shared run state passed to every scenario callback.
 *  @param filter          Scenario name filter string; empty or "*" matches all.
 *  @param outQueuedCount  If non-null, receives the number of scenarios queued.
 *  @return True if at least one scenario was queued successfully. */
bool QueueRegisteredUiScenarios(ImGuiTestEngine *engine,
                                UiAutomationRunState *state,
                                const std::string &filter,
                                int *outQueuedCount = nullptr);

#else

/** @brief Stub run state used when UI automation is compiled out. */
struct UiAutomationRunState {
};

/** @brief No-op stub matching the QueueRegisteredUiScenarios signature when UI automation is disabled.
 *  @return Always returns false. */
inline bool QueueRegisteredUiScenarios(void *, UiAutomationRunState *,
                                       const std::string &, int * = nullptr) {
    return false;
}

#endif

} // namespace Horo
