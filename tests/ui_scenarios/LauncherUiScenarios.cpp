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

template <typename Predicate>
bool WaitForCondition(ImGuiTestContext* ctx, int maxFrames, Predicate&& predicate) {
  if (!ctx)
    return false;
  for (int frame = 0; frame < maxFrames; ++frame) {
    if (predicate())
      return true;
    ctx->Yield(1);
  }
  return predicate();
}

Launcher::LauncherEditorShell* AsLauncherShell(UiAutomationRunState* state) {
  if (!state || !state->shellContext)
    return nullptr;
  return state->shellContext;
}

ImGuiWindow* FindWindowContaining(const char* token) {
  if (!*token || GImGui == nullptr)
    return nullptr;

  const ImGuiContext& context = *GImGui;
  for (ImGuiWindow* window : context.Windows) {
    if (!window || !window->Name)
      continue;
    const std::string_view windowName(window->Name);
    const std::string_view tokenView(token);
    if (!std::ranges::search(windowName, tokenView).empty())
      return window;
  }
  return nullptr;
}

void CaptureScreenshotTo(ImGuiTestContext* ctx, const fs::path& dir, const char* filename) {
  if (!ctx || !ctx->CaptureArgs || dir.empty())
    return;
  const std::string full = (dir / filename).string();
  if (full.size() >= IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile))
    return;
  LOG_DEBUG("UI scenario capture screenshot: %s", full.c_str());
  ImStrncpy(ctx->CaptureArgs->InOutputFile, full.c_str(), IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
  ctx->CaptureScreenshot(0);
}

bool BeginVideoCapture(ImGuiTestContext* ctx, const UiAutomationRunState* state, const char* filename) {
  if (state->uiCaptureOutputDir.empty())
    return false;
  if (!ctx->CaptureArgs)
    return false;
  const std::string full = (state->uiCaptureOutputDir / filename).string();
  if (full.size() >= IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile))
    return false;
  LOG_DEBUG("UI scenario begin video capture: %s", full.c_str());
  ctx->CaptureReset();
  ImStrncpy(ctx->CaptureArgs->InOutputFile, full.c_str(), IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
  return ctx->CaptureBeginVideo();
}

bool BeginTestVideoCaptureIfNeeded(ImGuiTestContext* ctx,
                                   UiAutomationRunState* state,
                                   const char* filename) {
  if (!ctx || !state || !state->videoEnabled || !*filename)
    return false;
  if (state->videoCaptureOpen)
    return true;
  const bool started = BeginVideoCapture(ctx, state, filename);
  state->videoCaptureOpen = started;
  return started;
}

void EndTestVideoCaptureIfNeeded(ImGuiTestContext* ctx, UiAutomationRunState* state) {
  if (!ctx || !state || !state->videoCaptureOpen)
    return;
  LOG_DEBUG("UI scenario end video capture.");
  ctx->CaptureEndVideo();
  ctx->CaptureReset();
  state->videoCaptureOpen = false;
}

struct VideoCaptureScope {
  ImGuiTestContext* ctx = nullptr;
  UiAutomationRunState* state = nullptr;

  explicit VideoCaptureScope(ImGuiTestContext* inCtx, UiAutomationRunState* inState)
      : ctx(inCtx), state(inState) {}
  ~VideoCaptureScope() { EndTestVideoCaptureIfNeeded(ctx, state); }
};

bool EnsureProjectCreatedFromLauncher(ImGuiTestContext* ctx,
                                      UiAutomationRunState* state,
                                      bool allowScreenshot);
bool ReturnToLauncherFromEditor(ImGuiTestContext* ctx, UiAutomationRunState* state);

UiAutomationRunState* GetTestState(ImGuiTestContext* ctx, const char* scenarioName) {
  LOG_INFO("UI scenario start: %s", scenarioName);
  if (ctx == nullptr || ctx->Test == nullptr)
    return nullptr;
  return static_cast<UiAutomationRunState*>(ctx->Test->UserData);
}

UiAutomationRunState* RequireTestState(ImGuiTestContext* ctx, const char* scenarioName) {
  return GetTestState(ctx, scenarioName);
}

void CaptureIfEnabled(ImGuiTestContext* ctx,
                      const UiAutomationRunState* state,
                      const char* filename) {
  if (!state->captureEnabled || state->videoEnabled)
    return;
  CaptureScreenshotTo(ctx, state->uiCaptureOutputDir, filename);
}

bool AssertLauncherHomeVisible(ImGuiTestContext* ctx) {
  if (!ctx)
    return false;
  ctx->SetRef("Horo Launcher");
  const bool visible = WaitForCondition(ctx, 180, [&]() {
    return ctx->ItemExists("Create New Project");
  });
  if (!visible)
    LOG_WARN("Launcher home did not become visible within timeout.");
  return visible;
}

bool AssertRecentProjectListed(ImGuiTestContext* ctx) {
  if (!ctx)
    return false;
  ImGuiWindow* recentProjectsList = FindWindowContaining("RecentProjectsList");
  if (!recentProjectsList)
    return false;
  ctx->SetRef(recentProjectsList);
  const bool listed = WaitForCondition(ctx, 180, [&]() {
    return ctx->ItemExists("UiSmokeGame");
  });
  if (!listed)
    LOG_WARN("Recent project 'UiSmokeGame' was not listed within timeout.");
  return listed;
}

bool ReopenProjectFromRecentProjects(ImGuiTestContext* ctx) {
  if (!AssertLauncherHomeVisible(ctx))
    return false;
  if (!AssertRecentProjectListed(ctx))
    return false;
  LOG_DEBUG("UI scenario action: click recent project 'UiSmokeGame'");
  ctx->ItemClick("UiSmokeGame");
  ctx->Yield(1);
  return true;
}

void RunLauncherSmokeTest(ImGuiTestContext* ctx) {
  UiAutomationRunState* testState = RequireTestState(ctx, "launcher_ui/create_project_from_launcher");
  IM_CHECK(testState != nullptr);
  if (testState == nullptr)
    return;
  VideoCaptureScope captureScope(ctx, testState);
  const bool captureStarted = BeginTestVideoCaptureIfNeeded(
      ctx, testState, "launcher_ui__create_project_from_launcher__run.mp4");
  (void)captureStarted;
  const bool created = EnsureProjectCreatedFromLauncher(ctx, testState, !testState->videoEnabled);
  IM_CHECK(created);
  if (!created)
    return;

  ctx->SetRef("##toolbar");
  IM_CHECK(ctx->ItemExists("File"));
  IM_CHECK(ctx->ItemExists("Add"));
  IM_CHECK(ctx->ItemExists("Edit"));
  LOG_INFO("UI scenario done: launcher_ui/create_project_from_launcher");
}

void RunLauncherRecentProjectsTest(ImGuiTestContext* ctx) {
  UiAutomationRunState* testState =
      RequireTestState(ctx, "launcher_ui/open_project_from_recent_projects");
  IM_CHECK(testState != nullptr);
  if (testState == nullptr)
    return;
  VideoCaptureScope captureScope(ctx, testState);
  const bool captureStarted = BeginTestVideoCaptureIfNeeded(
      ctx, testState, "launcher_ui__open_project_from_recent_projects__run.mp4");
  (void)captureStarted;
  const bool created = EnsureProjectCreatedFromLauncher(ctx, testState, !testState->videoEnabled);
  IM_CHECK(created);
  if (!created)
    return;
  const bool returned = ReturnToLauncherFromEditor(ctx, testState);
  IM_CHECK(returned);
  if (!returned)
    return;
  Launcher::LauncherEditorShell* shell = AsLauncherShell(testState);
  IM_CHECK(shell != nullptr);
  if (!shell)
    return;

  const bool reopened = ReopenProjectFromRecentProjects(ctx);
  IM_CHECK(reopened);
  if (!reopened)
    return;
  const bool projectOpened = WaitForCondition(ctx, 180, [&]() {
    return shell->HasActiveProject();
  });
  IM_CHECK(projectOpened);
  if (!projectOpened) {
    LOG_WARN("UI scenario failed to reopen recent project within timeout.");
    return;
  }
  ctx->SetRef("##toolbar");
  const bool fileMenuReady = WaitForCondition(ctx, 120, [&]() {
    return ctx->ItemExists("File");
  });
  IM_CHECK(fileMenuReady);
  CaptureIfEnabled(ctx, testState, "launcher_ui__open_project_from_recent_projects__expect_project_reopened.png");
  LOG_INFO("UI scenario done: launcher_ui/open_project_from_recent_projects");
}

bool EnsureProjectCreatedFromLauncher(ImGuiTestContext* ctx,
                                      UiAutomationRunState* state,
                                      bool allowScreenshot = true) {
  Launcher::LauncherEditorShell* shell = AsLauncherShell(state);
  if (!ctx || !state || !shell)
    return false;

  if (shell->HasActiveProject()) {
    LOG_DEBUG("UI scenario project already active, skipping launcher creation flow.");
    return true;
  }

  ctx->SetRef("Horo Launcher");
  const bool launcherReady = WaitForCondition(ctx, 180, [&]() {
    return ctx->ItemExists("Open Existing Project") && ctx->ItemExists("Create New Project");
  });
  if (!launcherReady)
    return false;

  ImGuiWindow* launcherPanel = FindWindowContaining("LauncherPanel");
  if (!launcherPanel)
    return false;
  ctx->SetRef(launcherPanel);
  ctx->ItemInputValue("##new-project-name", "UiSmokeGame");
  ctx->ItemInputValue("##new-project-path", state->projectRoot.string().c_str());
  LOG_DEBUG("UI scenario creating project '%s' at '%s'.", "UiSmokeGame", state->projectRoot.string().c_str());
  ctx->ItemClick("Create Project");
  const bool projectCreated = WaitForCondition(ctx, 180, [&]() {
    return shell->HasActiveProject();
  });
  if (!projectCreated) {
    LOG_WARN("UI scenario failed to observe active project after creation within timeout.");
    return false;
  }
  if (state->captureEnabled && allowScreenshot) {
    CaptureScreenshotTo(ctx, state->uiCaptureOutputDir,
                        "launcher_ui__create_project_from_launcher__expect_project_created.png");
  }
  return true;
}

bool ReturnToLauncherFromEditor(ImGuiTestContext* ctx, UiAutomationRunState* state) {
  Launcher::LauncherEditorShell* shell = AsLauncherShell(state);
  if (!ctx || !state || !shell || !shell->HasActiveProject())
    return false;

  // Prefer UI path through the File menu when it is interactable.
  ctx->SetRef("##toolbar");
  const bool fileVisible = WaitForCondition(ctx, 90, [&]() {
    return ctx->ItemExists("File");
  });
  if (fileVisible) {
    ctx->ItemClick("File");
    ctx->Yield(1);
    const bool closeVisible = WaitForCondition(ctx, 90, [&]() {
      return ctx->ItemExists("Close Project");
    });
    if (closeVisible) {
      LOG_DEBUG("UI scenario action: click 'Close Project' from File menu");
      ctx->ItemClick("Close Project");
      const bool returnedHome = WaitForCondition(ctx, 120, [&]() {
        return !shell->HasActiveProject();
      });
      if (returnedHome)
        return true;
      LOG_WARN("UI scenario failed to return home after File->Close Project action.");
    } else {
      LOG_WARN("UI scenario could not find 'Close Project' menu item.");
    }
  } else {
    LOG_WARN("UI scenario could not find 'File' menu on toolbar.");
  }

  // Fallback path: close project via shell API if UI controls are unavailable in test context.
  LOG_WARN("UI scenario using direct shell close fallback.");
  shell->CloseProject();
  const bool returnedHome = WaitForCondition(ctx, 120, [&]() {
    return !shell->HasActiveProject();
  });
  if (!returnedHome) {
    LOG_WARN("UI scenario failed to return to launcher home within timeout.");
    return false;
  }
  return true;
}

ImGuiTest* RegisterLauncherSmokeTest(ImGuiTestEngine* engine, UiAutomationRunState* state) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "create_project_from_launcher");
  test->UserData = state;
  test->TestFunc = &RunLauncherSmokeTest;
  return test;
}

ImGuiTest* RegisterLauncherRecentProjectsTest(ImGuiTestEngine* engine, UiAutomationRunState* state) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "open_project_from_recent_projects");
  test->UserData = state;
  test->TestFunc = &RunLauncherRecentProjectsTest;
  return test;
}

}  // namespace

void RegisterLauncherUiScenarioSet() {
  RegisterUiScenario("launcher/create_project_from_launcher", &RegisterLauncherSmokeTest);
  RegisterUiScenario("launcher/open_project_from_recent_projects", &RegisterLauncherRecentProjectsTest);
}

}  // namespace Monolith

#endif

