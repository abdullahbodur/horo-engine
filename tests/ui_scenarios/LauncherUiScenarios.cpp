#include "launcher/UiTestHarness.h"

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION

#include <string>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>

#include "launcher/StandaloneEditorShell.h"
#include "tests/UiTestRegistry.h"

namespace Monolith {
namespace {
namespace fs = std::filesystem;

Standalone::StandaloneEditorShell* AsLauncherShell(UiAutomationRunState* state) {
  if (!state || !state->shellContext)
    return nullptr;
  return static_cast<Standalone::StandaloneEditorShell*>(state->shellContext);
}

ImGuiWindow* FindWindowContaining(const char* token) {
  if (!token || !*token || GImGui == nullptr)
    return nullptr;

  ImGuiContext& context = *GImGui;
  for (ImGuiWindow* window : context.Windows) {
    if (!window || !window->Name)
      continue;
    if (std::string(window->Name).find(token) != std::string::npos)
      return window;
  }
  return nullptr;
}

void CaptureScreenshotTo(ImGuiTestContext* ctx, const fs::path& dir, const char* filename) {
  if (!ctx || !ctx->CaptureArgs || filename == nullptr || dir.empty())
    return;
  const std::string full = (dir / filename).string();
  if (full.size() >= IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile))
    return;
  ImStrncpy(ctx->CaptureArgs->InOutputFile, full.c_str(), IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
  ctx->CaptureScreenshot(0);
}

bool BeginVideoCapture(ImGuiTestContext* ctx, const UiAutomationRunState* state, const char* filename) {
  if (!ctx || !state || !state->videoEnabled || filename == nullptr || state->uiCaptureOutputDir.empty())
    return false;
  if (!ctx->CaptureArgs)
    return false;
  const std::string full = (state->uiCaptureOutputDir / filename).string();
  if (full.size() >= IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile))
    return false;
  ctx->CaptureReset();
  ImStrncpy(ctx->CaptureArgs->InOutputFile, full.c_str(), IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
  return ctx->CaptureBeginVideo();
}

bool BeginSuiteVideoCaptureIfNeeded(ImGuiTestContext* ctx, UiAutomationRunState* state) {
  if (!ctx || !state || !state->videoEnabled)
    return false;
  if (state->videoCaptureOpen)
    return true;
  const bool started = BeginVideoCapture(ctx, state, "launcher_ui__full_suite__run.mp4");
  state->videoCaptureOpen = started;
  return started;
}

void EnsureProjectCreatedFromLauncher(ImGuiTestContext* ctx,
                                      UiAutomationRunState* state,
                                      bool allowScreenshot = true) {
  IM_CHECK(ctx != nullptr);
  IM_CHECK(state != nullptr);
  Standalone::StandaloneEditorShell* shell = AsLauncherShell(state);
  IM_CHECK(shell != nullptr);

  if (shell->HasActiveProject())
    return;

  ctx->SetRef("Horo Launcher");
  IM_CHECK(ctx->ItemExists("Open Existing Project"));
  IM_CHECK(ctx->ItemExists("Create New Project"));

  ImGuiWindow* launcherPanel = FindWindowContaining("LauncherPanel");
  IM_CHECK(launcherPanel != nullptr);
  ctx->SetRef(launcherPanel);
  ctx->ItemInputValue("##new-project-name", "UiSmokeGame");
  ctx->ItemInputValue("##new-project-path", state->projectRoot.string().c_str());
  ctx->ItemClick("Create Project");
  ctx->Yield(3);
  IM_CHECK(shell->HasActiveProject());
  if (state->captureEnabled && allowScreenshot) {
    CaptureScreenshotTo(ctx, state->uiCaptureOutputDir,
                        "launcher_ui__create_project_from_launcher__expect_project_created.png");
  }
}

ImGuiTest* RegisterLauncherSmokeTest(ImGuiTestEngine* engine, UiAutomationRunState* state) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "create_project_from_launcher");
  test->UserData = state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* testState = static_cast<UiAutomationRunState*>(ctx->Test->UserData);
    IM_CHECK(testState != nullptr);
    BeginSuiteVideoCaptureIfNeeded(ctx, testState);
    EnsureProjectCreatedFromLauncher(ctx, testState, !testState->videoEnabled);

    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Configure"));
    IM_CHECK(ctx->ItemExists("Build"));
    IM_CHECK(ctx->ItemExists("Run Game"));
  };
  return test;
}

ImGuiTest* RegisterLauncherBackToHomeTest(ImGuiTestEngine* engine, UiAutomationRunState* state) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "back_to_home_returns_launcher");
  test->UserData = state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* testState = static_cast<UiAutomationRunState*>(ctx->Test->UserData);
    IM_CHECK(testState != nullptr);
    BeginSuiteVideoCaptureIfNeeded(ctx, testState);
    EnsureProjectCreatedFromLauncher(ctx, testState, !testState->videoEnabled);

    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Back To Home"));
    ctx->ItemClick("Back To Home");
    ctx->Yield(2);
    Standalone::StandaloneEditorShell* shell = AsLauncherShell(testState);
    IM_CHECK(shell != nullptr);
    IM_CHECK(!shell->HasActiveProject());
    if (testState->captureEnabled && !testState->videoEnabled) {
      CaptureScreenshotTo(
          ctx,
          testState->uiCaptureOutputDir,
          "launcher_ui__back_to_home_returns_launcher__expect_launcher_home_visible.png");
    }

    ctx->SetRef("Horo Launcher");
    IM_CHECK(ctx->ItemExists("Create New Project"));
    ImGuiWindow* recentProjectsList = FindWindowContaining("RecentProjectsList");
    IM_CHECK(recentProjectsList != nullptr);
    ctx->SetRef(recentProjectsList);
    IM_CHECK(ctx->ItemExists("UiSmokeGame"));
    if (testState->captureEnabled && !testState->videoEnabled) {
      CaptureScreenshotTo(
          ctx,
          testState->uiCaptureOutputDir,
          "launcher_ui__back_to_home_returns_launcher__expect_recent_project_listed.png");
    }
  };
  return test;
}

ImGuiTest* RegisterLauncherRecentProjectsTest(ImGuiTestEngine* engine, UiAutomationRunState* state) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "open_project_from_recent_projects");
  test->UserData = state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* testState = static_cast<UiAutomationRunState*>(ctx->Test->UserData);
    IM_CHECK(testState != nullptr);
    BeginSuiteVideoCaptureIfNeeded(ctx, testState);
    EnsureProjectCreatedFromLauncher(ctx, testState, !testState->videoEnabled);

    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Back To Home"));
    ctx->ItemClick("Back To Home");
    ctx->Yield(2);
    Standalone::StandaloneEditorShell* shell = AsLauncherShell(testState);
    IM_CHECK(shell != nullptr);
    IM_CHECK(!shell->HasActiveProject());

    ctx->SetRef("Horo Launcher");
    ImGuiWindow* recentProjectsList = FindWindowContaining("RecentProjectsList");
    IM_CHECK(recentProjectsList != nullptr);
    ctx->SetRef(recentProjectsList);
    IM_CHECK(ctx->ItemExists("UiSmokeGame"));
    ctx->ItemClick("UiSmokeGame");
    ctx->Yield(3);

    IM_CHECK(shell->HasActiveProject());
    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Back To Home"));
    if (testState->captureEnabled && !testState->videoEnabled) {
      CaptureScreenshotTo(
          ctx,
          testState->uiCaptureOutputDir,
          "launcher_ui__open_project_from_recent_projects__expect_project_reopened.png");
    }
    if (testState->videoCaptureOpen) {
      ctx->CaptureEndVideo();
      ctx->CaptureReset();
      testState->videoCaptureOpen = false;
    }
  };
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

