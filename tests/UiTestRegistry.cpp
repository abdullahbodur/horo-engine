#include "tests/UiTestRegistry.h"

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace Monolith {
void RegisterLauncherUiScenarioSet();

namespace {
struct UiScenarioRegistration {
  std::string fullName;
  UiScenarioRegisterFn fn = nullptr;
};

std::vector<UiScenarioRegistration>& Registry() {
  static std::vector<UiScenarioRegistration> scenarios;
  return scenarios;
}

std::string Trim(std::string value) {
  auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
                return !isSpace(static_cast<unsigned char>(ch));
              }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) {
                return !isSpace(static_cast<unsigned char>(ch));
              }).base(),
              value.end());
  return value;
}

bool MatchesPattern(const std::string& value, const std::string& pattern) {
  if (pattern.empty() || pattern == "*")
    return true;
  const size_t wildcard = pattern.find('*');
  if (wildcard == std::string::npos)
    return value == pattern;

  const std::string prefix = pattern.substr(0, wildcard);
  const std::string suffix = pattern.substr(wildcard + 1);
  if (!prefix.empty() && value.rfind(prefix, 0) != 0)
    return false;
  if (!suffix.empty() && (value.size() < suffix.size() ||
                          value.compare(value.size() - suffix.size(), suffix.size(), suffix) != 0)) {
    return false;
  }
  return value.size() >= prefix.size() + suffix.size();
}

bool MatchesFilter(const std::string& scenarioName, const std::string& rawFilter) {
  const std::string filter = Trim(rawFilter);
  if (filter.empty())
    return true;

  size_t start = 0;
  while (start <= filter.size()) {
    const size_t end = filter.find(',', start);
    const std::string token = Trim(filter.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (!token.empty() && MatchesPattern(scenarioName, token))
      return true;
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return false;
}

}  // namespace

void RegisterUiScenario(const char* fullName, UiScenarioRegisterFn fn) {
  if (!fullName || !*fullName || fn == nullptr)
    return;
  Registry().push_back({fullName, fn});
}

void InitializeUiScenarioRegistry() {
  static bool initialized = false;
  if (initialized)
    return;
  initialized = true;
  RegisterLauncherUiScenarioSet();
}

bool QueueRegisteredUiScenarios(ImGuiTestEngine* engine,
                                UiAutomationRunState* state,
                                const std::string& filter,
                                int* outQueuedCount) {
  if (!engine || !state)
    return false;
  InitializeUiScenarioRegistry();

  int queued = 0;
  for (const UiScenarioRegistration& entry : Registry()) {
    if (!entry.fn || !MatchesFilter(entry.fullName, filter))
      continue;
    ImGuiTest* test = entry.fn(engine, state);
    if (test) {
      ImGuiTestEngine_QueueTest(engine, test);
      ++queued;
    }
  }
  if (outQueuedCount)
    *outQueuedCount = queued;
  return queued > 0;
}

}  // namespace Monolith

#endif

