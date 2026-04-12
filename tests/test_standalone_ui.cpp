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
#include "standalone/StandaloneEditorShell.h"

using namespace Monolith;
using namespace Monolith::Standalone;

namespace {

namespace fs = std::filesystem;

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

struct StandaloneUiHarness {
  fs::path tempRoot;
  fs::path projectRoot;
  fs::path buildRoot;
  fs::path homeRoot;
  CurrentPathGuard currentPathGuard;
  ProjectPathGuard projectPathGuard;
  HomeDirGuard homeDirGuard;
  StandaloneEditorShell shell;

  StandaloneUiHarness()
      : tempRoot(fs::temp_directory_path() / "horo_standalone_ui_tests"),
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

  ~StandaloneUiHarness() {
    shell.Shutdown();
  }
};

void RenderStandaloneFrame(StandaloneEditorShell* shell) {
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

ImGuiTest* RegisterStandaloneLauncherSmokeTest(ImGuiTestEngine* engine, StandaloneUiHarness* harness) {
  REQUIRE(engine != nullptr);
  REQUIRE(harness != nullptr);

  ImGuiTest* test = IM_REGISTER_TEST(engine, "standalone_ui", "create_project_from_launcher");
  test->UserData = harness;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* state = static_cast<StandaloneUiHarness*>(ctx->Test->UserData);
    IM_CHECK(state != nullptr);

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
    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Configure"));
    IM_CHECK(ctx->ItemExists("Build"));
    IM_CHECK(ctx->ItemExists("Run Game"));
  };

  return test;
}

}  // namespace

TEST_CASE("Standalone launcher smoke flow works through imgui test engine", "[standalone][ui]") {
  StandaloneUiHarness harness;
  ImGuiContextGuard imgui;

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  REQUIRE(engine != nullptr);

  ImGuiTestEngineIO& testIo = ImGuiTestEngine_GetIO(engine);
  testIo.ConfigCaptureEnabled = false;
  testIo.ConfigFixedDeltaTime = 1.0f / 60.0f;
  testIo.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  testIo.ConfigVerboseLevel = ImGuiTestVerboseLevel_Error;
  testIo.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;

  ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
  ImGuiTest* smokeTest = RegisterStandaloneLauncherSmokeTest(engine, &harness);
  REQUIRE(smokeTest != nullptr);
  ImGuiTestEngine_QueueTest(engine, smokeTest);

  int frameCount = 0;
  while ((testIo.IsRunningTests || !ImGuiTestEngine_IsTestQueueEmpty(engine)) && frameCount < 600) {
    RenderStandaloneFrame(&harness.shell);
    ImGuiTestEngine_PostSwap(engine);
    ++frameCount;
  }

  if (testIo.IsRunningTests || !ImGuiTestEngine_IsTestQueueEmpty(engine))
    ImGuiTestEngine_TryAbortEngine(engine);

  int testsRun = 0;
  int testsSucceeded = 0;
  ImGuiTestEngine_GetResult(engine, testsRun, testsSucceeded);
  const int smokeStatus = static_cast<int>(smokeTest->Output.Status);
  const std::string smokeLog = smokeTest->Output.Log.Buffer.c_str();

  INFO(std::string("Smoke test status: ") + std::to_string(smokeStatus));
  INFO(std::string("Smoke test log:\n") + smokeLog);
  ImGuiTestEngine_Stop(engine);
  imgui.Release();
  ImGuiTestEngine_DestroyContext(engine);

  REQUIRE(frameCount < 600);
  REQUIRE(testsRun == 1);
  REQUIRE(testsSucceeded == 1);
  REQUIRE(smokeStatus == ImGuiTestStatus_Success);
  REQUIRE(harness.shell.HasActiveProject());
  REQUIRE(fs::exists(harness.projectRoot / ".horo" / "project.json"));
  REQUIRE(fs::exists(harness.projectRoot / "src" / "main.cpp"));
  REQUIRE(fs::exists(harness.projectRoot / "assets" / "scenes" / "level.json"));
}
