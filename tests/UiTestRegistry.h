#pragma once

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION

#include <functional>
#include <string>

#include <imgui_test_engine/imgui_te_engine.h>

#include "launcher/UiTestHarness.h"

namespace Monolith {
using UiScenarioRegisterFn =
    std::function<ImGuiTest *(ImGuiTestEngine *, UiAutomationRunState *)>;

void RegisterUiScenario(const char *fullName, UiScenarioRegisterFn fn);

void InitializeUiScenarioRegistry();

bool QueueRegisteredUiScenarios(ImGuiTestEngine *engine,
                                UiAutomationRunState *state,
                                const std::string &filter, int *outQueuedCount);
} // namespace Monolith

#endif
