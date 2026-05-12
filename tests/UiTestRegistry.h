#pragma once

#ifdef HORO_STANDALONE_UI_AUTOMATION

#include <functional>
#include <string>

#include <imgui_test_engine/imgui_te_engine.h>

#include "ui/launcher/UiTestHarness.h"

namespace Horo {
using UiScenarioRegisterFn =
    std::function<ImGuiTest *(ImGuiTestEngine *, UiAutomationRunState *)>;

void RegisterUiScenario(const char *fullName, UiScenarioRegisterFn fn);

void InitializeUiScenarioRegistry();

bool QueueRegisteredUiScenarios(ImGuiTestEngine *engine,
                                UiAutomationRunState *state,
                                const std::string &filter, int *outQueuedCount);
} // namespace Horo

#endif
