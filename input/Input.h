#pragma once
#include "input/KeyCodes.h"
#include "input/MouseCodes.h"
#include "math/Vec2.h"
#include <array>

struct GLFWwindow;

namespace Monolith {
class Input {
public:
  static void Init(GLFWwindow *window);

  // Call once per frame after glfwPollEvents()
  static void Poll();

  static bool IsKeyDown(Key key);

  static bool
  IsKeyPressed(Key key); // true only on the first frame the key is down
  static bool
  IsKeyReleased(Key key); // true only on the frame the key was released

  static bool IsMouseButtonDown(MouseButton btn);

  static bool IsMouseButtonPressed(MouseButton btn);

  static bool IsMouseButtonReleased(MouseButton btn);

  static Vec2 GetMousePosition();

  static Vec2 GetMouseDelta();   // pixels moved since last frame
  static float GetScrollDelta(); // vertical scroll delta this frame

private:
  static GLFWwindow *s_window;

  static constexpr int MAX_KEYS = 512;
  static constexpr int MAX_BUTTONS = 8;

  static std::array<bool, MAX_KEYS> s_keys;
  static std::array<bool, MAX_KEYS> s_keysLast;
  static std::array<bool, MAX_BUTTONS> s_buttons;
  static std::array<bool, MAX_BUTTONS> s_buttonsLast;
  static Vec2 s_mousePos;
  static Vec2 s_mouseDelta;
  static float s_scrollDelta;
  static Vec2 s_mousePosLast;

  static void ScrollCallback(GLFWwindow *, double, double yOffset);
};
} // namespace Monolith
