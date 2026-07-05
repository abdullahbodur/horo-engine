# Input Module

`input/` exposes frame-based keyboard/mouse input state over GLFW.

## Responsibilities

- Keyboard state queries: down / pressed / released
- Mouse button state queries: down / pressed / released
- Mouse position and per-frame delta tracking
- Scroll wheel delta tracking
- Key and mouse code enums (`KeyCodes.h`, `MouseCodes.h`)

## Main API

- `Input::Init(GLFWwindow* window)`
- `Input::Poll()` (call once per frame after event polling)
- `Input::IsKeyDown/IsKeyPressed/IsKeyReleased`
- `Input::IsMouseButtonDown/Pressed/Released`
- `Input::GetMousePosition`, `GetMouseDelta`, `GetScrollDelta`

## Frame Semantics

- **Down**: key/button is currently held.
- **Pressed**: true only on transition up→down (first frame).
- **Released**: true only on transition down→up.

This enables deterministic gameplay input without ad-hoc edge detection in game code.
