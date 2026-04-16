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
  LOG_INFO("UI scenario capture screenshot: %s", full.c_str());
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
  LOG_INFO("UI scenario begin video capture: %s", full.c_str());
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
  LOG_INFO("UI scenario end video capture.");
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

void EnsureProjectCreatedFromLauncher(ImGuiTestContext* ctx,
                                      UiAutomationRunState* state,
                                      bool allowScreenshot);
void ReturnToLauncherFromEditor(ImGuiTestContext* ctx, UiAutomationRunState* state);

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

void AssertLauncherHomeVisible(ImGuiTestContext* ctx) {
  ctx->SetRef("Horo Launcher");
  IM_CHECK(ctx->ItemExists("Create New Project"));
}

void AssertRecentProjectListed(ImGuiTestContext* ctx) {
  ImGuiWindow* recentProjectsList = FindWindowContaining("RecentProjectsList");
  IM_CHECK(recentProjectsList != nullptr);
  ctx->SetRef(recentProjectsList);
  IM_CHECK(ctx->ItemExists("UiSmokeGame"));
}

void ReopenProjectFromRecentProjects(ImGuiTestContext* ctx) {
  AssertLauncherHomeVisible(ctx);
  AssertRecentProjectListed(ctx);
  LOG_INFO("UI scenario action: click recent project 'UiSmokeGame'");
  ctx->ItemClick("UiSmokeGame");
  ctx->Yield(3);
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
  EnsureProjectCreatedFromLauncher(ctx, testState, !testState->videoEnabled);

  ctx->SetRef("##toolbar");
  IM_CHECK(ctx->ItemExists("File"));
  IM_CHECK(ctx->ItemExists("Add"));
  IM_CHECK(ctx->ItemExists("Edit"));
  LOG_INFO("UI scenario done: launcher_ui/create_project_from_launcher");
}

void RunLauncherBackToHomeTest(ImGuiTestContext* ctx) {
  UiAutomationRunState* testState = RequireTestState(ctx, "launcher_ui/back_to_home_returns_launcher");
  IM_CHECK(testState != nullptr);
  if (testState == nullptr)
    return;
  VideoCaptureScope captureScope(ctx, testState);
  const bool captureStarted = BeginTestVideoCaptureIfNeeded(
      ctx, testState, "launcher_ui__back_to_home_returns_launcher__run.mp4");
  (void)captureStarted;
  EnsureProjectCreatedFromLauncher(ctx, testState, !testState->videoEnabled);
  ReturnToLauncherFromEditor(ctx, testState);
  CaptureIfEnabled(ctx, testState, "launcher_ui__back_to_home_returns_launcher__expect_launcher_home_visible.png");
  AssertLauncherHomeVisible(ctx);
  AssertRecentProjectListed(ctx);
  CaptureIfEnabled(ctx, testState, "launcher_ui__back_to_home_returns_launcher__expect_recent_project_listed.png");
  LOG_INFO("UI scenario done: launcher_ui/back_to_home_returns_launcher");
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
  EnsureProjectCreatedFromLauncher(ctx, testState, !testState->videoEnabled);
  ReturnToLauncherFromEditor(ctx, testState);
  Launcher::LauncherEditorShell* shell = AsLauncherShell(testState);
  IM_CHECK(shell != nullptr);

  ReopenProjectFromRecentProjects(ctx);
  IM_CHECK(shell->HasActiveProject());
  ctx->SetRef("##toolbar");
  IM_CHECK(ctx->ItemExists("File"));
  CaptureIfEnabled(ctx, testState, "launcher_ui__open_project_from_recent_projects__expect_project_reopened.png");
  LOG_INFO("UI scenario done: launcher_ui/open_project_from_recent_projects");
}

void EnsureProjectCreatedFromLauncher(ImGuiTestContext* ctx,
                                      UiAutomationRunState* state,
                                      bool allowScreenshot = true) {
  IM_CHECK(ctx != nullptr);
  IM_CHECK(state != nullptr);
  Launcher::LauncherEditorShell* shell = AsLauncherShell(state);
  IM_CHECK(shell != nullptr);

  if (shell->HasActiveProject()) {
    LOG_INFO("UI scenario project already active, skipping launcher creation flow.");
    return;
  }

  ctx->SetRef("Horo Launcher");
  IM_CHECK(ctx->ItemExists("Open Existing Project"));
  IM_CHECK(ctx->ItemExists("Create New Project"));

  ImGuiWindow* launcherPanel = FindWindowContaining("LauncherPanel");
  IM_CHECK(launcherPanel != nullptr);
  ctx->SetRef(launcherPanel);
  ctx->ItemInputValue("##new-project-name", "UiSmokeGame");
  ctx->ItemInputValue("##new-project-path", state->projectRoot.string().c_str());
  LOG_INFO("UI scenario creating project '%s' at '%s'.", "UiSmokeGame", state->projectRoot.string().c_str());
  ctx->ItemClick("Create Project");
  ctx->Yield(3);
  IM_CHECK(shell->HasActiveProject());
  if (state->captureEnabled && allowScreenshot) {
    CaptureScreenshotTo(ctx, state->uiCaptureOutputDir,
                        "launcher_ui__create_project_from_launcher__expect_project_created.png");
  }
}

void ReturnToLauncherFromEditor(ImGuiTestContext* ctx, UiAutomationRunState* state) {
  IM_CHECK(ctx != nullptr);
  IM_CHECK(state != nullptr);
  Launcher::LauncherEditorShell* shell = AsLauncherShell(state);
  IM_CHECK(shell != nullptr);
  IM_CHECK(shell->HasActiveProject());

  ctx->SetRef("##toolbar");
  IM_CHECK(ctx->ItemExists("File"));
  ctx->ItemClick("File");
  ctx->Yield(1);
  IM_CHECK(ctx->ItemExists("Close Project"));
  LOG_INFO("UI scenario action: click 'Close Project' from File menu");
  ctx->ItemClick("Close Project");
  ctx->Yield(2);
  IM_CHECK(!shell->HasActiveProject());
}

ImGuiTest* RegisterLauncherSmokeTest(ImGuiTestEngine* engine, UiAutomationRunState* state) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "create_project_from_launcher");
  test->UserData = state;
  test->TestFunc = &RunLauncherSmokeTest;
  return test;
}

ImGuiTest* RegisterLauncherBackToHomeTest(ImGuiTestEngine* engine, UiAutomationRunState* state) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "back_to_home_returns_launcher");
  test->UserData = state;
  test->TestFunc = &RunLauncherBackToHomeTest;
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
  RegisterUiScenario("launcher/back_to_home_returns_launcher", &RegisterLauncherBackToHomeTest);
  RegisterUiScenario("launcher/open_project_from_recent_projects", &RegisterLauncherRecentProjectsTest);
}

}  // namespace Monolith

#endif

