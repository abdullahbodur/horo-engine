#include "input/Input.h"

#include <GLFW/glfw3.h>

namespace Horo {
    GLFWwindow *Input::s_window = nullptr;
    std::array<bool, Input::MAX_KEYS> Input::s_keys = {};
    std::array<bool, Input::MAX_KEYS> Input::s_keysLast = {};
    std::array<bool, Input::MAX_BUTTONS> Input::s_buttons = {};
    std::array<bool, Input::MAX_BUTTONS> Input::s_buttonsLast = {};
    Vec2 Input::s_mousePos = {};
    Vec2 Input::s_mouseDelta = {};
    float Input::s_scrollDelta = 0.0f;
    Vec2 Input::s_mousePosLast = {};

    void Input::Init(GLFWwindow *window) {
        s_window = window;
        glfwSetScrollCallback(window, ScrollCallback);
    }

    void Input::Poll() {
        // Snapshot key state
        s_keysLast = s_keys;
        for (int i = 0; i < MAX_KEYS; i++)
            s_keys[static_cast<size_t>(i)] = glfwGetKey(s_window, i) == GLFW_PRESS;

        // Snapshot mouse buttons
        s_buttonsLast = s_buttons;
        for (int i = 0; i < MAX_BUTTONS; i++)
            s_buttons[static_cast<size_t>(i)] =
                    glfwGetMouseButton(s_window, i) == GLFW_PRESS;

        // Mouse position
        s_mousePosLast = s_mousePos;
        double mx = 0.0;
        double my = 0.0;
        glfwGetCursorPos(s_window, &mx, &my);
        s_mousePos = {static_cast<float>(mx), static_cast<float>(my)};
        s_mouseDelta = s_mousePos - s_mousePosLast;
    }

    bool Input::IsKeyDown(Key key) { return s_keys[static_cast<size_t>(key)]; }

    bool Input::IsKeyPressed(Key key) {
        return s_keys[static_cast<size_t>(key)] &&
               !s_keysLast[static_cast<size_t>(key)];
    }

    bool Input::IsKeyReleased(Key key) {
        return !s_keys[static_cast<size_t>(key)] &&
               s_keysLast[static_cast<size_t>(key)];
    }

    bool Input::IsMouseButtonDown(MouseButton b) {
        return s_buttons[static_cast<size_t>(b)];
    }

    bool Input::IsMouseButtonPressed(MouseButton b) {
        return s_buttons[static_cast<size_t>(b)] &&
               !s_buttonsLast[static_cast<size_t>(b)];
    }

    bool Input::IsMouseButtonReleased(MouseButton b) {
        return !s_buttons[static_cast<size_t>(b)] &&
               s_buttonsLast[static_cast<size_t>(b)];
    }

    Vec2 Input::GetMousePosition() { return s_mousePos; }
    Vec2 Input::GetMouseDelta() { return s_mouseDelta; }

    float Input::GetScrollDelta() {
        float d = s_scrollDelta;
        s_scrollDelta = 0;
        return d;
    }

    void Input::ScrollCallback(GLFWwindow *, double, double yOffset) {
        s_scrollDelta += static_cast<float>(yOffset);
    }
} // namespace Horo
