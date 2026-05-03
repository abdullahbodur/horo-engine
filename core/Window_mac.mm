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

  // Force dark appearance base.
  window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];

  // Make the title bar transparent so the window's backgroundColor shows
  // through it, giving the exact dark navy color used by the launcher
  // (RGB 1, 7, 17 = #01070F) instead of the generic system dark chrome.
  window.titlebarAppearsTransparent = YES;
  window.backgroundColor =
      [NSColor colorWithSRGBRed:1.0 / 255.0
                          green:7.0 / 255.0
                           blue:17.0 / 255.0
                          alpha:1.0];
}

extern "C" void ApplyAppIconMac(const char *iconPath) {
  if (!iconPath || !*iconPath)
    return;

  NSString *path = [NSString stringWithUTF8String:iconPath];
  NSImage *icon = [NSImage imageWithContentsOfFile:path];
  if (icon)
    [NSApp setApplicationIconImage:icon];
}
