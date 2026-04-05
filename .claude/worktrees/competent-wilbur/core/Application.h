#pragma once
#include <memory>
#include <string>

#include "core/Window.h"

namespace Monolith {

struct AppSpec {
  std::string name = "Monolith App";
  int width = 1280;
  int height = 720;
  bool vsync = true;
};

class Application {
 public:
  explicit Application(const AppSpec& spec);
  virtual ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  void Run();

  Window& GetWindow() { return *m_window; }

 protected:
  // Subclasses override these
  virtual void OnInit() {}
  virtual void OnUpdate(float /*dt*/) {}       // variable-rate update (rendering, input)
  virtual void OnFixedUpdate(float /*dt*/) {}  // fixed-rate update (physics)
  virtual void OnRender(float /*alpha*/) {}    // alpha = interpolation factor [0,1]
  virtual void OnShutdown() {}

 private:
  std::unique_ptr<Window> m_window;
  bool m_running = true;
};

}  // namespace Monolith
