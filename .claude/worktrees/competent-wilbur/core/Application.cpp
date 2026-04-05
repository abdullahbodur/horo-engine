#include "core/Application.h"

#include <glad/glad.h>

#include "core/Logger.h"
#include "core/Time.h"
#include "input/Input.h"

namespace Monolith {

Application::Application(const AppSpec& spec) {
  WindowSpec ws;
  ws.title = spec.name;
  ws.width = spec.width;
  ws.height = spec.height;
  ws.vsync = spec.vsync;

  m_window = std::make_unique<Window>(ws);
  m_window->SetCloseCallback([this]() { m_running = false; });

  Input::Init(m_window->GetNativeHandle());
}

Application::~Application() = default;

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

    m_window->SwapBuffers();
  }

  OnShutdown();
  LOG_INFO("Application shut down cleanly.");
}

}  // namespace Monolith
