#pragma once

#include <memory>

namespace Monolith {
class UiAutomationRunner {
 public:
  static void PrepareEnvironmentBeforeAppStart(bool runUiAutomation);

  UiAutomationRunner();
  ~UiAutomationRunner();

  UiAutomationRunner(const UiAutomationRunner&) = delete;
  UiAutomationRunner& operator=(const UiAutomationRunner&) = delete;

  void StartIfRequested(bool runUiAutomation, void* shellContext);
  void PostRenderFrame(void* nativeWindowHandle);
  void Shutdown();
  void DestroyContext();

  bool DidPass() const;
  bool IsActive() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace Monolith

