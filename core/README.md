# Core Module

`core/` contains engine bootstrap and platform-facing runtime primitives.

## Responsibilities

- App lifecycle (`Application`): init, update, fixed update, render, shutdown
- Window + OpenGL context management (`Window`, GLFW-backed)
- Frame timing and fixed-step scheduling (`Time`)
- Logging and diagnostics (`Logger`, `Assert`)
- Screenshot utility and project-relative path resolution (`Screenshot`, `ProjectPath`)

## Main Types

- `AppSpec` (`Application.h`): app/window startup configuration
- `Application` (`Application.h/.cpp`):
  - Fixed-step game loop using `Time::FIXED_DT`
  - Optional CLI mode (`--editor`) parsed with `ParseArgs`
  - Virtual hooks: `OnInit`, `OnUpdate`, `OnFixedUpdate`, `OnRender`, `OnShutdown`
- `Window` (`Window.h/.cpp`):
  - Resize, close, and file-drop callbacks
  - Swap chain + event polling integration

## Typical Usage

```cpp
#include "core/Application.h"

class MyGame final : public Monolith::Application {
 public:
  MyGame() : Application({.name = "MyGame", .width = 1280, .height = 720}) {}

 private:
  void OnInit() override {}
  void OnFixedUpdate(float dt) override {}
  void OnUpdate(float dt) override {}
  void OnRender(float alpha) override {}
};
```

## Notes

- `Application` owns `Window` via `std::unique_ptr` (RAII lifecycle).
- `Time` exposes interpolation alpha for smooth render between fixed physics ticks.
- Keep heavy gameplay state out of `core/`; this module should stay minimal and stable.
