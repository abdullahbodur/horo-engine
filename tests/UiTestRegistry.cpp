#include "tests/UiTestRegistry.h"

#ifdef HORO_STANDALONE_UI_AUTOMATION

#include <algorithm>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Logger.h"

namespace Horo {
void RegisterLauncherUiScenarioSet();
void RegisterEditorUiScenarioSet();

namespace {
struct UiScenarioRegistration {
  std::string fullName;
  UiScenarioRegisterFn fn = nullptr;
};

std::vector<UiScenarioRegistration> &Registry() {
  static std::vector<UiScenarioRegistration> scenarios;
  return scenarios;
}

std::string Trim(std::string value) {
  auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
  value.erase(value.begin(), std::ranges::find_if(value, [&](char ch) {
                return !isSpace(static_cast<unsigned char>(ch));
              }));
  value.erase(
      std::ranges::find_if(
          std::views::reverse(value),
          [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); })
          .base(),
      value.end());
  return value;
}

bool MatchesPattern(std::string_view value, std::string_view pattern) {
  if (pattern.empty() || pattern == "*")
    return true;
  const size_t wildcard = pattern.find('*');
  if (wildcard == std::string::npos)
    return value == pattern;

  const std::string_view prefix = pattern.substr(0, wildcard);
  const std::string_view suffix = pattern.substr(wildcard + 1);
  if (!prefix.empty() && value.rfind(prefix, 0) != 0)
    return false;
  if (!suffix.empty() && (value.size() < suffix.size() ||
                          value.compare(value.size() - suffix.size(),
                                        suffix.size(), suffix) != 0)) {
    return false;
  }
  return value.size() >= prefix.size() + suffix.size();
}

bool MatchesFilter(std::string_view scenarioName, std::string_view rawFilter) {
  const std::string filter = Trim(std::string(rawFilter));
  if (filter.empty())
    return true;

  size_t start = 0;
  while (start <= filter.size()) {
    const size_t end = filter.find(',', start);
    if (const std::string token = Trim(filter.substr(
            start, end == std::string::npos ? std::string::npos : end - start));
        !token.empty() && MatchesPattern(scenarioName, token)) {
      return true;
    }
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return false;
}
} // namespace

void RegisterUiScenario(const char *fullName, UiScenarioRegisterFn fn) {
  if (!fullName || !*fullName || !fn)
    return;
  Registry().emplace_back(fullName, std::move(fn));
}

void InitializeUiScenarioRegistry() {
  static bool initialized = false;
  if (initialized)
    return;
  initialized = true;
  RegisterLauncherUiScenarioSet();
  RegisterEditorUiScenarioSet();
  LogDebug("UI scenario registry initialized with {} scenario(s).",
           Registry().size());
}

bool QueueRegisteredUiScenarios(ImGuiTestEngine *engine,
                                UiAutomationRunState *state,
                                const std::string &filter,
                                int *outQueuedCount) {
  if (!engine || !state)
    return false;
  InitializeUiScenarioRegistry();

  int queued = 0;
  for (const UiScenarioRegistration &entry : Registry()) {
    if (!entry.fn)
      continue;
    if (!MatchesFilter(entry.fullName, filter)) {
      LogDebug("UI scenario skipped by filter: '{}' (filter='{}')",
               entry.fullName, filter);
      continue;
    }
    LogDebug("UI scenario queued: '{}'", entry.fullName);
    ImGuiTest *test = entry.fn(engine, state);
    if (test) {
      ImGuiTestEngine_QueueTest(engine, test);
      ++queued;
    }
  }
  LogInfo(
      "UI scenario queue summary: queued={}, total_registered={}, filter='{}'",
      queued, Registry().size(), filter);
  if (outQueuedCount)
    *outQueuedCount = queued;
  return queued > 0;
}
} // namespace Horo

#endif
