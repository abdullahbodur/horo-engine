// Launcher UI tests run headless (no GLFW/GPU): ImGui + StandaloneEditorShell only.
// Dear ImGui Test Engine still needs ImGuiScreenCaptureFunc; we stub framebuffer pixels
// (solid black) so CaptureScreenshot writes PNGs and the pipeline is covered. Real UI
// thumbnails need a windowed app + readback — see imgui_test_engine wiki "Screen & Video Captures".

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>

#include "core/ProjectPath.h"
#include "launcher/StandaloneEditorShell.h"

using namespace Monolith;
using namespace Monolith::Standalone;

namespace {

namespace fs = std::filesystem;

std::string ReadEnvString(const char* name) {
  if (!name || !*name)
    return {};
#ifdef _WIN32
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || !value)
    return {};
  const std::string out(value);
  free(value);
  return out;
#else
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string();
#endif
}

fs::path RepoRootFromTestSource() {
  return fs::path(__FILE__).parent_path().parent_path().lexically_normal();
}

bool IsBuildTreeRoot(const fs::path& candidate) {
  std::error_code ec;
  return fs::is_regular_file(candidate / "MonolithEngineConfig.cmake", ec) && !ec &&
         fs::is_regular_file(candidate / "MonolithEngineTargets.cmake", ec) && !ec;
}

fs::path FindBuildTreeRoot() {
  std::vector<fs::path> candidates;

  auto appendAncestors = [&candidates](fs::path current) {
    while (!current.empty()) {
      candidates.push_back(current);
      const fs::path parent = current.parent_path();
      if (parent == current)
        break;
      current = parent;
    }
  };

  appendAncestors(fs::current_path());
  appendAncestors(RepoRootFromTestSource() / "build");

  const fs::path buildDir = RepoRootFromTestSource() / "build";
  std::error_code ec;
  if (fs::is_directory(buildDir, ec) && !ec) {
    for (const fs::directory_entry& entry : fs::directory_iterator(buildDir)) {
      if (entry.is_directory())
        candidates.push_back(entry.path());
    }
  }

  for (const fs::path& candidate : candidates) {
    if (IsBuildTreeRoot(candidate))
      return candidate;
  }

  return fs::current_path();
}

struct CurrentPathGuard {
  fs::path previous;

  explicit CurrentPathGuard(const fs::path& nextPath)
      : previous(fs::current_path()) {
    fs::current_path(nextPath);
  }

  ~CurrentPathGuard() {
    fs::current_path(previous);
  }
};

struct ProjectPathGuard {
  fs::path previousRoot;

  explicit ProjectPathGuard(const fs::path& nextRoot)
      : previousRoot(ProjectPath::Root()) {
    ProjectPath::Init(nextRoot);
    ProjectPath::SetProjectRoot({});
  }

  ~ProjectPathGuard() {
    ProjectPath::Init(previousRoot);
    ProjectPath::SetProjectRoot({});
  }
};

struct HomeDirGuard {
  std::string previousUserProfile;
  std::string previousHomeDrive;
  std::string previousHomePath;
  std::string previousHome;

  static std::string ReadEnv(const char* name) {
    if (!name || !*name)
      return {};
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value)
      return {};
    std::string out(value);
    free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
  }

  explicit HomeDirGuard(const fs::path& nextHome)
      : previousUserProfile(ReadEnv("USERPROFILE")),
        previousHomeDrive(ReadEnv("HOMEDRIVE")),
        previousHomePath(ReadEnv("HOMEPATH")),
        previousHome(ReadEnv("HOME")) {
#ifdef _WIN32
    _putenv_s("USERPROFILE", nextHome.string().c_str());
    _putenv_s("HOMEDRIVE", "");
    _putenv_s("HOMEPATH", "");
#else
    setenv("HOME", nextHome.string().c_str(), 1);
#endif
  }

  ~HomeDirGuard() {
#ifdef _WIN32
    _putenv_s("USERPROFILE", previousUserProfile.c_str());
    _putenv_s("HOMEDRIVE", previousHomeDrive.c_str());
    _putenv_s("HOMEPATH", previousHomePath.c_str());
#else
    if (previousHome.empty())
      unsetenv("HOME");
    else
      setenv("HOME", previousHome.c_str(), 1);
#endif
  }
};

struct ImGuiContextGuard {
  bool active = false;

  ImGuiContextGuard() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    active = true;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.MousePos = ImVec2(32.0f, 32.0f);
    io.MouseDown[0] = false;
    io.Fonts->AddFontDefault();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  }

  void Release() {
    if (!active)
      return;
    ImGui::DestroyContext();
    active = false;
  }

  ~ImGuiContextGuard() {
    Release();
  }
};

struct LauncherUiHarness {
  fs::path uiCaptureOutputDir;
  fs::path tempRoot;
  fs::path projectRoot;
  fs::path buildRoot;
  fs::path homeRoot;
  CurrentPathGuard currentPathGuard;
  ProjectPathGuard projectPathGuard;
  HomeDirGuard homeDirGuard;
  StandaloneEditorShell shell;

  explicit LauncherUiHarness(fs::path captureOutputDir)
      : uiCaptureOutputDir(std::move(captureOutputDir)),
        tempRoot(fs::temp_directory_path() / "horo_launcher_ui_tests"),
        projectRoot(tempRoot / "UiSmokeGame"),
        buildRoot(FindBuildTreeRoot()),
        homeRoot(tempRoot / "home"),
        currentPathGuard(buildRoot),
        projectPathGuard(RepoRootFromTestSource()),
        homeDirGuard(homeRoot) {
    std::error_code ec;
    fs::remove_all(tempRoot, ec);
    fs::create_directories(homeRoot);
    shell.Initialize();
  }

  ~LauncherUiHarness() {
    shell.Shutdown();
  }
};

void RenderLauncherShellFrame(StandaloneEditorShell* shell) {
  REQUIRE(shell != nullptr);
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1280.0f, 720.0f);
  io.DeltaTime = 1.0f / 60.0f;
  ImGui::NewFrame();
  shell->Update();
  shell->RenderOverlay();
  ImGui::Render();
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

// Stub framebuffer (see file comment): satisfies test engine capture API.
static bool LauncherUiScreenCaptureFunc(ImGuiID viewport_id,
                                        int x,
                                        int y,
                                        int w,
                                        int h,
                                        unsigned int* pixels,
                                        void* user_data) {
  IM_UNUSED(viewport_id);
  IM_UNUSED(x);
  IM_UNUSED(y);
  IM_UNUSED(user_data);
  if (!pixels || w <= 0 || h <= 0)
    return false;
  const int n = w * h;
  for (int i = 0; i < n; ++i)
    pixels[i] = 0xFF000000u;
  return true;
}

static void SetCaptureOutputFile(ImGuiTestContext* ctx, const fs::path& dir, const char* filename) {
  IM_CHECK(ctx != nullptr);
  IM_CHECK(ctx->CaptureArgs != nullptr);
  const std::string full = (dir / filename).string();
  IM_CHECK(full.size() < IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
  ImStrncpy(ctx->CaptureArgs->InOutputFile, full.c_str(), IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
}

static void CaptureScreenshotTo(ImGuiTestContext* ctx, const fs::path& dir, const char* filename) {
  SetCaptureOutputFile(ctx, dir, filename);
  ctx->CaptureScreenshot(0);
}

void EnsureProjectCreatedFromLauncher(ImGuiTestContext* ctx, LauncherUiHarness* state) {
  IM_CHECK(ctx != nullptr);
  IM_CHECK(state != nullptr);

  if (state->shell.HasActiveProject())
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
  IM_CHECK(state->shell.HasActiveProject());
  CaptureScreenshotTo(ctx, state->uiCaptureOutputDir, "launcher_ui__create_project_from_launcher__expect_project_created.png");
}

ImGuiTest* RegisterLauncherSmokeTest(ImGuiTestEngine* engine, LauncherUiHarness* harness) {
  REQUIRE(engine != nullptr);
  REQUIRE(harness != nullptr);

  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "create_project_from_launcher");
  test->UserData = harness;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* state = static_cast<LauncherUiHarness*>(ctx->Test->UserData);
    IM_CHECK(state != nullptr);

    EnsureProjectCreatedFromLauncher(ctx, state);
    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Configure"));
    IM_CHECK(ctx->ItemExists("Build"));
    IM_CHECK(ctx->ItemExists("Run Game"));
  };

  return test;
}

ImGuiTest* RegisterLauncherBackToHomeTest(ImGuiTestEngine* engine, LauncherUiHarness* harness) {
  REQUIRE(engine != nullptr);
  REQUIRE(harness != nullptr);

  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "back_to_home_returns_launcher");
  test->UserData = harness;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* state = static_cast<LauncherUiHarness*>(ctx->Test->UserData);
    IM_CHECK(state != nullptr);

    EnsureProjectCreatedFromLauncher(ctx, state);

    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Back To Home"));
    ctx->ItemClick("Back To Home");
    ctx->Yield(2);
    CaptureScreenshotTo(ctx, state->uiCaptureOutputDir, "launcher_ui__back_to_home_returns_launcher__expect_launcher_home_visible.png");

    IM_CHECK(!state->shell.HasActiveProject());
    ctx->SetRef("Horo Launcher");
    IM_CHECK(ctx->ItemExists("Create New Project"));
    ImGuiWindow* recentProjectsList = FindWindowContaining("RecentProjectsList");
    IM_CHECK(recentProjectsList != nullptr);
    ctx->SetRef(recentProjectsList);
    IM_CHECK(ctx->ItemExists("UiSmokeGame"));
    CaptureScreenshotTo(ctx, state->uiCaptureOutputDir, "launcher_ui__back_to_home_returns_launcher__expect_recent_project_listed.png");
  };

  return test;
}

ImGuiTest* RegisterLauncherRecentProjectsTest(ImGuiTestEngine* engine, LauncherUiHarness* harness) {
  REQUIRE(engine != nullptr);
  REQUIRE(harness != nullptr);

  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "open_project_from_recent_projects");
  test->UserData = harness;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* state = static_cast<LauncherUiHarness*>(ctx->Test->UserData);
    IM_CHECK(state != nullptr);

    EnsureProjectCreatedFromLauncher(ctx, state);

    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Back To Home"));
    ctx->ItemClick("Back To Home");
    ctx->Yield(2);
    IM_CHECK(!state->shell.HasActiveProject());

    ctx->SetRef("Horo Launcher");
    ImGuiWindow* recentProjectsList = FindWindowContaining("RecentProjectsList");
    IM_CHECK(recentProjectsList != nullptr);
    ctx->SetRef(recentProjectsList);
    IM_CHECK(ctx->ItemExists("UiSmokeGame"));
    ctx->ItemClick("UiSmokeGame");
    ctx->Yield(3);

    IM_CHECK(state->shell.HasActiveProject());
    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Back To Home"));
    CaptureScreenshotTo(ctx, state->uiCaptureOutputDir, "launcher_ui__open_project_from_recent_projects__expect_project_reopened.png");
  };

  return test;
}

}  // namespace

TEST_CASE("Editor launcher smoke flow works through imgui test engine", "[launcher][ui]") {
  fs::path uiOut = RepoRootFromTestSource() / "ui_test_output";
  const std::string envOut = ReadEnvString("MONOLITH_UI_TEST_OUTPUT_DIR");
  if (!envOut.empty())
    uiOut = envOut;
  {
    std::error_code ec;
    fs::create_directories(uiOut, ec);
  }

  LauncherUiHarness harness(uiOut);
  ImGuiContextGuard imgui;

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  REQUIRE(engine != nullptr);

  ImGuiTestEngineIO& testIo = ImGuiTestEngine_GetIO(engine);
  testIo.ConfigCaptureEnabled = true;
  testIo.ScreenCaptureFunc = LauncherUiScreenCaptureFunc;
  testIo.ConfigFixedDeltaTime = 1.0f / 60.0f;
  testIo.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  testIo.ConfigVerboseLevel = ImGuiTestVerboseLevel_Error;
  testIo.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;

  ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
  ImGuiTest* smokeTest = RegisterLauncherSmokeTest(engine, &harness);
  ImGuiTest* backToHomeTest = RegisterLauncherBackToHomeTest(engine, &harness);
  ImGuiTest* recentProjectsTest = RegisterLauncherRecentProjectsTest(engine, &harness);
  REQUIRE(smokeTest != nullptr);
  REQUIRE(backToHomeTest != nullptr);
  REQUIRE(recentProjectsTest != nullptr);
  ImGuiTestEngine_QueueTest(engine, smokeTest);
  ImGuiTestEngine_QueueTest(engine, backToHomeTest);
  ImGuiTestEngine_QueueTest(engine, recentProjectsTest);

  int frameCount = 0;
  while ((testIo.IsRunningTests || !ImGuiTestEngine_IsTestQueueEmpty(engine)) && frameCount < 600) {
    RenderLauncherShellFrame(&harness.shell);
    ImGuiTestEngine_PostSwap(engine);
    ++frameCount;
  }

  if (testIo.IsRunningTests || !ImGuiTestEngine_IsTestQueueEmpty(engine))
    ImGuiTestEngine_TryAbortEngine(engine);

  int testsRun = 0;
  int testsSucceeded = 0;
  ImGuiTestEngine_GetResult(engine, testsRun, testsSucceeded);
  const int smokeStatus = static_cast<int>(smokeTest->Output.Status);
  const int backToHomeStatus = static_cast<int>(backToHomeTest->Output.Status);
  const int recentProjectsStatus = static_cast<int>(recentProjectsTest->Output.Status);
  const std::string smokeLog = smokeTest->Output.Log.Buffer.c_str();
  const std::string backToHomeLog = backToHomeTest->Output.Log.Buffer.c_str();
  const std::string recentProjectsLog = recentProjectsTest->Output.Log.Buffer.c_str();

  INFO(std::string("Smoke test status: ") + std::to_string(smokeStatus));
  INFO(std::string("Smoke test log:\n") + smokeLog);
  INFO(std::string("Back To Home test status: ") + std::to_string(backToHomeStatus));
  INFO(std::string("Back To Home test log:\n") + backToHomeLog);
  INFO(std::string("Recent Projects test status: ") + std::to_string(recentProjectsStatus));
  INFO(std::string("Recent Projects test log:\n") + recentProjectsLog);
  ImGuiTestEngine_Stop(engine);
  imgui.Release();
  ImGuiTestEngine_DestroyContext(engine);

  REQUIRE(frameCount < 600);
  REQUIRE(testsRun == 3);
  REQUIRE(testsSucceeded == 3);
  REQUIRE(smokeStatus == ImGuiTestStatus_Success);
  REQUIRE(backToHomeStatus == ImGuiTestStatus_Success);
  REQUIRE(recentProjectsStatus == ImGuiTestStatus_Success);
  REQUIRE(harness.shell.HasActiveProject());
  REQUIRE(fs::exists(harness.projectRoot / ".horo" / "project.json"));
  REQUIRE(fs::exists(harness.projectRoot / "src" / "main.cpp"));
  REQUIRE(fs::exists(harness.projectRoot / "assets" / "scenes" / "level.json"));
}
