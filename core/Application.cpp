#include "core/Application.h"

#include <filesystem>
#include <string_view>
#include <system_error>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "core/Time.h"
#include "input/Input.h"

namespace Monolith {

namespace fs = std::filesystem;

static fs::path GetExePath() {
#ifdef __APPLE__
  char buf[4096];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0)
    return fs::canonical(buf);
  return {};
#elif defined(_WIN32)
  wchar_t buf[MAX_PATH];
  DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (len > 0)
    return fs::path(buf);
  return {};
#else
  char buf[4096];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len > 0) {
    buf[len] = '\0';
    return fs::path(buf);
  }
  return {};
#endif
}

Application::Application(const AppSpec& spec) {
  // Normalize CWD to the exe directory so relative asset paths always resolve.
  auto exePath = GetExePath();
  auto exeDir  = exePath.empty() ? fs::current_path() : exePath.parent_path();
  {
    std::error_code ec;
    fs::current_path(exeDir, ec);
    if (ec)
      LOG_WARN("Application: could not chdir to exe dir (%s): %s",
               exeDir.string().c_str(), ec.message().c_str());
  }

  ProjectPath::Init(exeDir);

  if (!spec.defaultSceneFile.empty())
    m_defaultSceneFilePath = ProjectPath::Resolve(spec.defaultSceneFile).string();

  WindowSpec ws;
  ws.title = spec.name;
  ws.width = spec.width;
  ws.height = spec.height;
  ws.vsync = spec.vsync;
  ws.graphicsApi = spec.graphicsApi;

  m_window = std::make_unique<Window>(ws);
  m_window->SetCloseCallback([this]() { m_running = false; });

  Input::Init(m_window->GetNativeHandle());
}

Application::~Application() = default;

void Application::ParseArgs(int argc, char** argv) {
  m_editorStartupCli = ParseEditorStartupCli(argc, argv);
}

bool Application::ShouldStartWithEditor() const {
#ifdef NDEBUG
  constexpr bool kReleaseDefaults = true;
#else
  constexpr bool kReleaseDefaults = false;
#endif
  return Monolith::ShouldStartWithEditor(m_editorStartupCli, kReleaseDefaults);
}

void Application::Run() {
  OnInit();

  // Initialise GLFW timer
  // glfwGetTime() starts at construction, so first Tick() will give a tiny delta — fine.

  while (m_running && !m_window->ShouldClose()) {
    Time::Tick();
    m_window->PollEvents();
    Input::Poll();

    // Fixed-step loop
    while (Time::ConsumeFixedStep())
      OnFixedUpdate(Time::FIXED_DT);

    float alpha = Time::GetInterpolationAlpha();
    OnUpdate(Time::DeltaTime());
    OnRender(alpha);

    if (m_window->OwnsPresentation())
      m_window->SwapBuffers();
  }

  OnShutdown();
  LOG_INFO("Application shut down cleanly.");
}

}  // namespace Monolith
