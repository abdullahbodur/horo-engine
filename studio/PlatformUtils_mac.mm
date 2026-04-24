// PlatformUtils_mac.mm
//
// macOS-specific utilities for the Horo Engine backend process.

#import <AppKit/AppKit.h>

namespace Monolith {

// Prevent the backend process from appearing in the macOS dock and app
// switcher. GLFW calls [NSApp finishLaunching] during window creation which
// registers the process as a foreground application. Calling this after the
// window is created overrides that to a background (prohibited) policy.
void SuppressMacOSDockIcon() {
  [[NSApplication sharedApplication]
      setActivationPolicy:NSApplicationActivationPolicyProhibited];
}

}  // namespace Monolith
