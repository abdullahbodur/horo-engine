#pragma once

#include <filesystem>
#include <string>

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
#include <imgui_test_engine/imgui_te_engine.h>
#endif

namespace Monolith {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
struct UiAutomationRunState {
  std::filesystem::path tempRoot;
  std::filesystem::path projectRoot;
  std::filesystem::path uiCaptureOutputDir;

  bool captureEnabled = false;
  bool videoEnabled = false;
  bool videoCaptureOpen = false;

  void* shellContext = nullptr;
};

bool QueueRegisteredUiScenarios(ImGuiTestEngine* engine,
                                UiAutomationRunState* state,
                                const std::string& filter,
                                int* outQueuedCount = nullptr);
#else
struct UiAutomationRunState {};
inline bool QueueRegisteredUiScenarios(void*, UiAutomationRunState*, const std::string&, int* = nullptr) {
  return false;
}
#endif

}  // namespace Monolith

