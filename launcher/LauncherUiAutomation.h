#pragma once

#include <memory>

namespace Monolith::Standalone {
class StandaloneEditorShell;

class LauncherUiAutomationRunner {
 public:
  static void PrepareEnvironmentBeforeAppStart(bool runUiAutomation);

  LauncherUiAutomationRunner();
  ~LauncherUiAutomationRunner();

  LauncherUiAutomationRunner(const LauncherUiAutomationRunner&) = delete;
  LauncherUiAutomationRunner& operator=(const LauncherUiAutomationRunner&) = delete;

  void StartIfRequested(bool runUiAutomation, StandaloneEditorShell* shell);
  void PostRenderFrame(void* nativeWindowHandle);
  void Shutdown();
  void DestroyContext();

  bool DidPass() const;
  bool IsActive() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace Monolith::Standalone
