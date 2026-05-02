// Objective-C++ bridge for macOS window appearance customization.
// Window.cpp calls the C-linkage function below; this file must stay isolated
// so no other translation unit pulls in AppKit headers.

// clang-format off
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
// clang-format on

#import <AppKit/AppKit.h>

extern "C" void ApplyDarkTitleBarMac(GLFWwindow *glfwWindow) {
  if (!glfwWindow)
    return;

  NSWindow *window = glfwGetCocoaWindow(glfwWindow);
  if (!window)
    return;

  // Force the dark appearance so the title bar matches the engine's dark navy
  // launcher background instead of inheriting the system appearance.
  window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
}
