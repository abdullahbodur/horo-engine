#include "launcher/UiTestHarness.h"

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION

#include <algorithm>
#include <string>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>

#include "core/Logger.h"
#include "launcher/LauncherEditorShell.h"
#include "tests/UiTestRegistry.h"

namespace Monolith {
    namespace {
        namespace fs = std::filesystem;

        template<typename Predicate>
        bool WaitForCondition(ImGuiTestContext *ctx, int maxFrames,
                              Predicate &&predicate) {
            if (!ctx)
                return false;
            for (int frame = 0; frame < maxFrames; ++frame) {
                if (predicate())
                    return true;
                ctx->Yield(1);
            }
            return predicate();
        }

        Launcher::LauncherEditorShell *AsLauncherShell(UiAutomationRunState *state) {
            if (!state || !state->shellContext)
                return nullptr;
            return state->shellContext;
        }

        ImGuiWindow *FindWindowContaining(const char *token) {
            if (!*token || GImGui == nullptr)
                return nullptr;

            const ImGuiContext &context = *GImGui;
            for (ImGuiWindow *window: context.Windows) {
                if (!window || !window->Name)
                    continue;
                const std::string_view windowName(window->Name);
                const std::string_view tokenView(token);
                if (!std::ranges::search(windowName, tokenView).empty())
                    return window;
            }
            return nullptr;
        }

        void CaptureScreenshotTo(ImGuiTestContext *ctx, const fs::path &dir,
                                 const char *filename) {
            if (!ctx || !ctx->CaptureArgs || dir.empty())
                return;
            const std::string full = (dir / filename).string();
            if (full.size() >= IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile))
                return;
            LogDebug("UI scenario capture screenshot: {}", full);
            ImStrncpy(ctx->CaptureArgs->InOutputFile, full.c_str(),
                      IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
            ctx->CaptureScreenshot(0);
        }

        bool BeginVideoCapture(ImGuiTestContext *ctx, const UiAutomationRunState *state,
                               const char *filename) {
            if (state->uiCaptureOutputDir.empty())
                return false;
            if (!ctx->CaptureArgs)
                return false;
            const std::string full = (state->uiCaptureOutputDir / filename).string();
            if (full.size() >= IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile))
                return false;
            LogDebug("UI scenario begin video capture: {}", full);
            ctx->CaptureReset();
            ImStrncpy(ctx->CaptureArgs->InOutputFile, full.c_str(),
                      IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
            return ctx->CaptureBeginVideo();
        }

        bool BeginTestVideoCaptureIfNeeded(ImGuiTestContext *ctx,
                                           UiAutomationRunState *state,
                                           const char *filename) {
            if (!ctx || !state || !state->videoEnabled || !*filename)
                return false;
            if (state->videoCaptureOpen)
                return true;
            const bool started = BeginVideoCapture(ctx, state, filename);
            state->videoCaptureOpen = started;
            return started;
        }

        void EndTestVideoCaptureIfNeeded(ImGuiTestContext *ctx,
                                         UiAutomationRunState *state) {
            if (!ctx || !state || !state->videoCaptureOpen)
                return;
            LogDebug("UI scenario end video capture.");
            ctx->CaptureEndVideo();
            ctx->CaptureReset();
            state->videoCaptureOpen = false;
        }

        struct VideoCaptureScope {
            ImGuiTestContext *ctx = nullptr;
            UiAutomationRunState *state = nullptr;

            explicit VideoCaptureScope(ImGuiTestContext *inCtx,
                                       UiAutomationRunState *inState)
                : ctx(inCtx), state(inState) {
            }

            ~VideoCaptureScope() { EndTestVideoCaptureIfNeeded(ctx, state); }

            VideoCaptureScope(const VideoCaptureScope &) = delete;

            VideoCaptureScope &operator=(const VideoCaptureScope &) = delete;

            VideoCaptureScope(VideoCaptureScope &&) = delete;

            VideoCaptureScope &operator=(VideoCaptureScope &&) = delete;
        };

        bool EnsureProjectCreatedFromLauncher(ImGuiTestContext *ctx,
                                              UiAutomationRunState *state,
                                              bool allowScreenshot);

        bool ReturnToLauncherFromEditor(ImGuiTestContext *ctx,
                                        UiAutomationRunState *state);

        UiAutomationRunState *GetTestState(ImGuiTestContext *ctx,
                                           const char *scenarioName) {
            LogInfo("UI scenario start: {}", scenarioName);
            if (ctx == nullptr || ctx->Test == nullptr)
                return nullptr;
            return static_cast<UiAutomationRunState *>(ctx->Test->UserData);
        }

        UiAutomationRunState *RequireTestState(ImGuiTestContext *ctx,
                                               const char *scenarioName) {
            return GetTestState(ctx, scenarioName);
        }

        void CaptureIfEnabled(ImGuiTestContext *ctx, const UiAutomationRunState *state,
                              const char *filename) {
            if (!state->captureEnabled || state->videoEnabled)
                return;
            CaptureScreenshotTo(ctx, state->uiCaptureOutputDir, filename);
        }

        bool AssertLauncherHomeVisible(ImGuiTestContext *ctx) {
            if (!ctx)
                return false;
            ctx->SetRef("Horo Launcher");
            const bool visible = WaitForCondition(
                ctx, 180, [ctx]() { return ctx->ItemExists("Create New Project"); });
            if (!visible)
                LogWarn("Launcher home did not become visible within timeout.");
            return visible;
        }

        bool AssertRecentProjectListed(ImGuiTestContext *ctx) {
            if (!ctx)
                return false;
            ImGuiWindow *recentProjectsList = FindWindowContaining("RecentProjectsList");
            if (!recentProjectsList)
                return false;
            ctx->SetRef(recentProjectsList);
            const bool listed = WaitForCondition(
                ctx, 180, [ctx]() { return ctx->ItemExists("UiSmokeGame"); });
            if (!listed)
                LogWarn("Recent project 'UiSmokeGame' was not listed within timeout.");
            return listed;
        }

        bool ReopenProjectFromRecentProjects(ImGuiTestContext *ctx) {
            if (!AssertLauncherHomeVisible(ctx))
                return false;
            if (!AssertRecentProjectListed(ctx))
                return false;
            LogDebug("UI scenario action: click recent project 'UiSmokeGame'");
            ctx->ItemClick("UiSmokeGame");
            ctx->Yield(1);
            return true;
        }

        void RunLauncherSmokeTest(ImGuiTestContext *ctx) {
            UiAutomationRunState *testState =
                    RequireTestState(ctx, "launcher_ui/create_project_from_launcher");
            IM_CHECK(testState != nullptr);
            if (testState == nullptr)
                return;
            VideoCaptureScope captureScope(ctx, testState);
            const bool captureStarted = BeginTestVideoCaptureIfNeeded(
                ctx, testState, "launcher_ui__create_project_from_launcher__run.mp4");
            (void) captureStarted;
            const bool created = EnsureProjectCreatedFromLauncher(
                ctx, testState, !testState->videoEnabled);
            IM_CHECK(created);
            if (!created)
                return;

            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("File"));
            IM_CHECK(ctx->ItemExists("Add"));
            IM_CHECK(ctx->ItemExists("Edit"));
            LogInfo("UI scenario done: launcher_ui/create_project_from_launcher");
        }

        void RunLauncherRecentProjectsTest(ImGuiTestContext *ctx) {
            UiAutomationRunState *testState =
                    RequireTestState(ctx, "launcher_ui/open_project_from_recent_projects");
            IM_CHECK(testState != nullptr);
            if (testState == nullptr)
                return;
            VideoCaptureScope captureScope(ctx, testState);
            const bool captureStarted = BeginTestVideoCaptureIfNeeded(
                ctx, testState,
                "launcher_ui__open_project_from_recent_projects__run.mp4");
            (void) captureStarted;
            const bool created = EnsureProjectCreatedFromLauncher(
                ctx, testState, !testState->videoEnabled);
            IM_CHECK(created);
            if (!created)
                return;
            const bool returned = ReturnToLauncherFromEditor(ctx, testState);
            IM_CHECK(returned);
            if (!returned)
                return;
            const Launcher::LauncherEditorShell *shell = AsLauncherShell(testState);
            IM_CHECK(shell != nullptr);
            if (!shell)
                return;

            const bool reopened = ReopenProjectFromRecentProjects(ctx);
            IM_CHECK(reopened);
            if (!reopened)
                return;
            const bool projectOpened = WaitForCondition(
                ctx, 180, [shell]() { return shell->HasActiveProject(); });
            IM_CHECK(projectOpened);
            if (!projectOpened) {
                LogWarn("UI scenario failed to reopen recent project within timeout.");
                return;
            }
            ctx->SetRef("##toolbar");
            const bool fileMenuReady =
                    WaitForCondition(ctx, 120, [ctx]() { return ctx->ItemExists("File"); });
            IM_CHECK(fileMenuReady);
            CaptureIfEnabled(ctx, testState,
                             "launcher_ui__open_project_from_recent_projects__expect_"
                             "project_reopened.png");
            LogInfo("UI scenario done: launcher_ui/open_project_from_recent_projects");
        }

        bool EnsureProjectCreatedFromLauncher(ImGuiTestContext *ctx,
                                              UiAutomationRunState *state,
                                              bool allowScreenshot = true) {
            const Launcher::LauncherEditorShell *shell = AsLauncherShell(state);
            if (!ctx || !state || !shell)
                return false;

            if (shell->HasActiveProject()) {
                LogDebug(
                    "UI scenario project already active, skipping launcher creation flow.");
                return true;
            }

            ctx->SetRef("Horo Launcher");
            if (const bool launcherReady = WaitForCondition(ctx, 180, [ctx]() {
                    return ctx->ItemExists("Open Existing Project") &&
                           ctx->ItemExists("Create New Project");
                });
                !launcherReady) {
                return false;
            }

            ImGuiWindow *launcherPanel = FindWindowContaining("LauncherPanel");
            if (!launcherPanel)
                return false;
            ctx->SetRef(launcherPanel);
            ctx->ItemInputValue("##new-project-name", "UiSmokeGame");
            ctx->ItemInputValue("##new-project-path",
                                state->projectRoot.string().c_str());
            LogDebug("UI scenario creating project '{}' at '{}'.", "UiSmokeGame",
                     state->projectRoot.string());
            ctx->ItemClick("Create Project");
            if (const bool projectCreated = WaitForCondition(
                    ctx, 180, [shell]() { return shell->HasActiveProject(); });
                !projectCreated) {
                LogWarn("UI scenario failed to observe active project after creation "
                    "within timeout.");
                return false;
            }
            if (state->captureEnabled && allowScreenshot) {
                CaptureScreenshotTo(ctx, state->uiCaptureOutputDir,
                                    "launcher_ui__create_project_from_launcher__expect_"
                                    "project_created.png");
            }
            return true;
        }

        bool ReturnToLauncherFromEditor(ImGuiTestContext *ctx,
                                        UiAutomationRunState *state) {
            Launcher::LauncherEditorShell *shell = AsLauncherShell(state);
            if (!ctx || !state || !shell || !shell->HasActiveProject())
                return false;

            // Prefer UI path through the File menu when it is interactable.
            ctx->SetRef("##toolbar");
            if (const bool fileVisible =
                        WaitForCondition(ctx, 90, [ctx]() { return ctx->ItemExists("File"); });
                fileVisible) {
                ctx->ItemClick("File");
                ctx->Yield(1);
                const bool closeVisible = WaitForCondition(
                    ctx, 90, [ctx]() { return ctx->ItemExists("Close Project"); });
                if (closeVisible) {
                    LogDebug("UI scenario action: click 'Close Project' from File menu");
                    ctx->ItemClick("Close Project");
                    if (const bool returnedHome = WaitForCondition(
                            ctx, 120, [shell]() { return !shell->HasActiveProject(); });
                        returnedHome) {
                        return true;
                    }
                    LogWarn("UI scenario failed to return home after File->Close Project "
                        "action.");
                } else {
                    LogWarn("UI scenario could not find 'Close Project' menu item.");
                }
            } else {
                LogWarn("UI scenario could not find 'File' menu on toolbar.");
            }

            // Fallback path: close project via shell API if UI controls are unavailable
            // in test context.
            LogWarn("UI scenario using direct shell close fallback.");
            shell->CloseProject();
            if (const bool returnedHome = WaitForCondition(
                    ctx, 120, [shell]() { return !shell->HasActiveProject(); });
                !returnedHome) {
                LogWarn("UI scenario failed to return to launcher home within timeout.");
                return false;
            }
            return true;
        }

        void RunLauncherHomeLayoutTest(ImGuiTestContext *ctx) {
            UiAutomationRunState *testState =
                    RequireTestState(ctx, "launcher_ui/launcher_home_layout");
            IM_CHECK(testState != nullptr);
            if (testState == nullptr)
                return;

            LogDebug("UI scenario action: wait for launcher home window");
            ctx->SetRef("Horo Launcher");
            const bool launcherReady = WaitForCondition(
                ctx, 180, [ctx]() { return ctx->ItemExists("Create New Project"); });
            IM_CHECK(launcherReady);
            if (!launcherReady)
                return;

            LogDebug("UI scenario action: find LauncherPanel window");
            ImGuiWindow *launcherPanel = FindWindowContaining("LauncherPanel");
            IM_CHECK(launcherPanel != nullptr);
            if (!launcherPanel)
                return;
            ctx->SetRef(launcherPanel);

            LogDebug("UI scenario action: assert input fields and buttons exist");
            IM_CHECK(ctx->ItemExists("##new-project-name"));
            IM_CHECK(ctx->ItemExists("##new-project-path"));
            IM_CHECK(ctx->ItemExists("Create Project"));

            ctx->SetRef("Horo Launcher");
            IM_CHECK(ctx->ItemExists("Open Existing Project"));

            CaptureIfEnabled(ctx, testState,
                             "launcher_ui__launcher_home_layout__expect_home.png");
            LogInfo("UI scenario done: launcher_ui/launcher_home_layout");
        }

        void RunLauncherProjectNameInputTest(ImGuiTestContext *ctx) {
            UiAutomationRunState *testState =
                    RequireTestState(ctx, "launcher_ui/launcher_project_name_input");
            IM_CHECK(testState != nullptr);
            if (testState == nullptr)
                return;

            Launcher::LauncherEditorShell *shell = AsLauncherShell(testState);
            IM_CHECK(shell != nullptr);
            if (!shell)
                return;

            if (shell->HasActiveProject()) {
                LogDebug("UI scenario action: project already active, returning to launcher home");
                const bool returned = ReturnToLauncherFromEditor(ctx, testState);
                IM_CHECK(returned);
                if (!returned)
                    return;
            }

            LogDebug("UI scenario action: assert launcher home is visible");
            const bool homeVisible = AssertLauncherHomeVisible(ctx);
            IM_CHECK(homeVisible);
            if (!homeVisible)
                return;

            LogDebug("UI scenario action: find LauncherPanel window");
            ImGuiWindow *launcherPanel = FindWindowContaining("LauncherPanel");
            IM_CHECK(launcherPanel != nullptr);
            if (!launcherPanel)
                return;
            ctx->SetRef(launcherPanel);

            LogDebug("UI scenario action: type project name into ##new-project-name");
            ctx->ItemInputValue("##new-project-name", "TestInputProject");
            ctx->Yield(2);

            LogDebug("UI scenario action: type project path into ##new-project-path");
            ctx->ItemInputValue("##new-project-path",
                                testState->projectRoot.string().c_str());
            ctx->Yield(2);

            LogDebug("UI scenario action: clear project name field");
            ctx->ItemInputValue("##new-project-name", "");

            LogInfo("UI scenario done: launcher_ui/launcher_project_name_input");
        }

        void RunLauncherCreateAndVerifyToolbarTest(ImGuiTestContext *ctx) {
            UiAutomationRunState *testState =
                    RequireTestState(ctx, "launcher_ui/launcher_create_and_verify_toolbar");
            IM_CHECK(testState != nullptr);
            if (testState == nullptr)
                return;

            LogDebug("UI scenario action: ensure project created from launcher");
            const bool created = EnsureProjectCreatedFromLauncher(
                ctx, testState, !testState->videoEnabled);
            IM_CHECK(created);
            if (!created)
                return;

            LogDebug("UI scenario action: wait for toolbar File menu");
            ctx->SetRef("##toolbar");
            const bool fileMenuReady = WaitForCondition(
                ctx, 180, [ctx]() { return ctx->ItemExists("File"); });
            IM_CHECK(fileMenuReady);
            if (!fileMenuReady)
                return;

            LogDebug("UI scenario action: assert all 4 toolbar menu buttons visible");
            IM_CHECK(ctx->ItemExists("File"));
            IM_CHECK(ctx->ItemExists("Add"));
            IM_CHECK(ctx->ItemExists("Edit"));
            IM_CHECK(ctx->ItemExists("View"));

            CaptureIfEnabled(ctx, testState,
                             "launcher_ui__create_and_verify_toolbar__expect_toolbar.png");
            LogInfo("UI scenario done: launcher_ui/launcher_create_and_verify_toolbar");
        }

        ImGuiTest *RegisterLauncherSmokeTest(ImGuiTestEngine *engine,
                                             UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "launcher_ui", "create_project_from_launcher");
            test->UserData = state;
            test->TestFunc = &RunLauncherSmokeTest;
            return test;
        }

        ImGuiTest *RegisterLauncherRecentProjectsTest(ImGuiTestEngine *engine,
                                                      UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "launcher_ui",
                                               "open_project_from_recent_projects");
            test->UserData = state;
            test->TestFunc = &RunLauncherRecentProjectsTest;
            return test;
        }

        ImGuiTest *RegisterLauncherHomeLayoutTest(ImGuiTestEngine *engine,
                                                  UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "launcher_ui", "launcher_home_layout");
            test->UserData = state;
            test->TestFunc = &RunLauncherHomeLayoutTest;
            return test;
        }

        ImGuiTest *RegisterLauncherProjectNameInputTest(ImGuiTestEngine *engine,
                                                        UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "launcher_ui", "launcher_project_name_input");
            test->UserData = state;
            test->TestFunc = &RunLauncherProjectNameInputTest;
            return test;
        }

        ImGuiTest *RegisterLauncherCreateAndVerifyToolbarTest(ImGuiTestEngine *engine,
                                                              UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "launcher_ui",
                                               "launcher_create_and_verify_toolbar");
            test->UserData = state;
            test->TestFunc = &RunLauncherCreateAndVerifyToolbarTest;
            return test;
        }
    } // namespace

    void RegisterLauncherUiScenarioSet() {
        RegisterUiScenario("launcher/create_project_from_launcher",
                           &RegisterLauncherSmokeTest);
        RegisterUiScenario("launcher/open_project_from_recent_projects",
                           &RegisterLauncherRecentProjectsTest);
        RegisterUiScenario("launcher/launcher_home_layout",
                           &RegisterLauncherHomeLayoutTest);
        RegisterUiScenario("launcher/launcher_project_name_input",
                           &RegisterLauncherProjectNameInputTest);
        RegisterUiScenario("launcher/launcher_create_and_verify_toolbar",
                           &RegisterLauncherCreateAndVerifyToolbarTest);
    }
} // namespace Monolith

#endif
