#include "input/Input.h"

#include <GLFW/glfw3.h>

#include <cstring>

namespace Horo {

GLFWwindow* Input::s_window = nullptr;
bool Input::s_keys[MAX_KEYS] = {};
bool Input::s_keysLast[MAX_KEYS] = {};
bool Input::s_buttons[MAX_BUTTONS] = {};
bool Input::s_buttonsLast[MAX_BUTTONS] = {};
Vec2 Input::s_mousePos = {};
Vec2 Input::s_mouseDelta = {};
float Input::s_scrollDelta = 0.0f;
Vec2 Input::s_mousePosLast = {};

void Input::Init(GLFWwindow* window) {
  s_window = window;
  glfwSetScrollCallback(window, ScrollCallback);
}

void Input::Poll() {
  // Snapshot key state
  std::memcpy(s_keysLast, s_keys, sizeof(s_keys));
  for (int i = 0; i < MAX_KEYS; i++)
    s_keys[i] = glfwGetKey(s_window, i) == GLFW_PRESS;

  // Snapshot mouse buttons
  std::memcpy(s_buttonsLast, s_buttons, sizeof(s_buttons));
  for (int i = 0; i < MAX_BUTTONS; i++)
    s_buttons[i] = glfwGetMouseButton(s_window, i) == GLFW_PRESS;

  // Mouse position
  s_mousePosLast = s_mousePos;
  double mx, my;
  glfwGetCursorPos(s_window, &mx, &my);
  s_mousePos = {static_cast<float>(mx), static_cast<float>(my)};
  s_mouseDelta = s_mousePos - s_mousePosLast;
}

bool Input::IsKeyDown(Key key) {
  return s_keys[static_cast<int>(key)];
}
bool Input::IsKeyPressed(Key key) {
  return s_keys[static_cast<int>(key)] && !s_keysLast[static_cast<int>(key)];
}
bool Input::IsKeyReleased(Key key) {
  return !s_keys[static_cast<int>(key)] && s_keysLast[static_cast<int>(key)];
}

bool Input::IsMouseButtonDown(MouseButton b) {
  return s_buttons[static_cast<int>(b)];
}
bool Input::IsMouseButtonPressed(MouseButton b) {
  return s_buttons[static_cast<int>(b)] && !s_buttonsLast[static_cast<int>(b)];
}
bool Input::IsMouseButtonReleased(MouseButton b) {
  return !s_buttons[static_cast<int>(b)] && s_buttonsLast[static_cast<int>(b)];
}

Vec2 Input::GetMousePosition() {
  return s_mousePos;
}
Vec2 Input::GetMouseDelta() {
  return s_mouseDelta;
}
float Input::GetScrollDelta() {
  float d = s_scrollDelta;
  s_scrollDelta = 0;
  return d;
}

void Input::ScrollCallback(GLFWwindow*, double, double yOffset) {
  s_scrollDelta += static_cast<float>(yOffset);
}

}  // namespace Horo
