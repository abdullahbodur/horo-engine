#include "tests/UiTestRegistry.h"

#ifdef HORO_STANDALONE_UI_AUTOMATION

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui_test_engine/imgui_te_context.h>

#include "core/Logger.h"

namespace Horo {
void RegisterLauncherUiScenarioSet();
void RegisterEditorUiScenarioSet();

namespace {
struct UiScenarioRegistration {
  std::string fullName;
  UiScenarioRegisterFn fn = nullptr;
};

using UiTestFunc = decltype(ImGuiTest::TestFunc);

std::unordered_map<ImGuiTest *, UiTestFunc> &WrappedTestFuncs() {
  static std::unordered_map<ImGuiTest *, UiTestFunc> funcs;
  return funcs;
}

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

std::string SanitizeCapturePathComponent(std::string_view value) {
  std::string sanitized;
  sanitized.reserve(value.size());
  for (const char ch : value) {
    const auto uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) || ch == '-' || ch == '_') {
      sanitized.push_back(ch);
    } else {
      sanitized.push_back('_');
    }
  }
  return sanitized.empty() ? std::string("unnamed") : sanitized;
}

std::string BuildScenarioVideoFilename(const ImGuiTest &test) {
  return std::format("{}__{}__run.mp4",
                     SanitizeCapturePathComponent(test.Category ? test.Category
                                                                 : "unknown"),
                     SanitizeCapturePathComponent(test.Name ? test.Name
                                                             : "unknown"));
}

bool BeginScenarioVideoCapture(ImGuiTestContext *ctx,
                               UiAutomationRunState *state,
                               const ImGuiTest &test) {
  if (!ctx || !state || !state->videoEnabled || state->videoCaptureOpen ||
      state->uiCaptureOutputDir.empty() || !ctx->CaptureArgs) {
    return false;
  }

  const std::string filename = BuildScenarioVideoFilename(test);
  const std::string full = (state->uiCaptureOutputDir / filename).string();
  if (full.size() >= IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile)) {
    LogWarn("UI scenario video path is too long, skipping recording: {}", full);
    return false;
  }

  ctx->CaptureReset();
  ImStrncpy(ctx->CaptureArgs->InOutputFile, full.c_str(),
            IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
  const bool started = ctx->CaptureBeginVideo();
  state->videoCaptureOpen = started;
  state->videoCaptureOwnedByRegistry = started;
  if (started) {
    LogInfo("UI scenario recording start: {}/{} -> {}",
            test.Category ? test.Category : "unknown",
            test.Name ? test.Name : "unknown", full);
  } else {
    LogWarn("UI scenario recording failed to start: {}/{}",
            test.Category ? test.Category : "unknown",
            test.Name ? test.Name : "unknown");
  }
  return started;
}

void EndScenarioVideoCapture(ImGuiTestContext *ctx, UiAutomationRunState *state,
                             const bool started) {
  if (!started || !ctx || !state || !state->videoCaptureOpen)
    return;
  ctx->CaptureEndVideo();
  ctx->CaptureReset();
  state->videoCaptureOpen = false;
  state->videoCaptureOwnedByRegistry = false;
  LogInfo("UI scenario recording end.");
}

struct ScenarioVideoCaptureScope {
  ImGuiTestContext *ctx = nullptr;
  UiAutomationRunState *state = nullptr;
  bool started = false;

  ScenarioVideoCaptureScope(ImGuiTestContext *inCtx,
                            UiAutomationRunState *inState,
                            const ImGuiTest &test)
      : ctx(inCtx), state(inState),
        started(BeginScenarioVideoCapture(inCtx, inState, test)) {}

  ~ScenarioVideoCaptureScope() {
    EndScenarioVideoCapture(ctx, state, started);
  }

  ScenarioVideoCaptureScope(const ScenarioVideoCaptureScope &) = delete;
  ScenarioVideoCaptureScope &
  operator=(const ScenarioVideoCaptureScope &) = delete;
};

void RunScenarioWithVideoCapture(ImGuiTestContext *ctx) {
  if (!ctx || !ctx->Test)
    return;

  const auto funcIt = WrappedTestFuncs().find(ctx->Test);
  if (funcIt == WrappedTestFuncs().end() || !funcIt->second)
    return;

  UiAutomationRunState *state =
      static_cast<UiAutomationRunState *>(ctx->Test->UserData);
  ScenarioVideoCaptureScope captureScope(ctx, state, *ctx->Test);
  funcIt->second(ctx);
}

std::vector<std::string> SplitFilterTokens(std::string_view rawFilter) {
  std::vector<std::string> tokens;
  const std::string filter = Trim(std::string(rawFilter));
  size_t start = 0;
  while (start <= filter.size()) {
    const size_t end = filter.find(',', start);
    std::string token = Trim(filter.substr(
        start, end == std::string::npos ? std::string::npos : end - start));
    if (!token.empty())
      tokens.push_back(std::move(token));
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return tokens;
}

bool QueueScenario(ImGuiTestEngine *engine, UiAutomationRunState *state,
                   const UiScenarioRegistration &entry, int *queued) {
  if (!entry.fn)
    return false;
  LogDebug("UI scenario queued: '{}'", entry.fullName);
  ImGuiTest *test = entry.fn(engine, state);
  if (!test)
    return false;
  if (test->TestFunc) {
    WrappedTestFuncs()[test] = test->TestFunc;
    test->TestFunc = &RunScenarioWithVideoCapture;
  }
  ImGuiTestEngine_QueueTest(engine, test);
  ++(*queued);
  return true;
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
  WrappedTestFuncs().clear();

  int queued = 0;
  const std::vector<std::string> filterTokens = SplitFilterTokens(filter);
  if (filterTokens.empty()) {
    for (const UiScenarioRegistration &entry : Registry())
      QueueScenario(engine, state, entry, &queued);
  } else {
    std::unordered_set<std::string> queuedNames;
    for (const std::string &token : filterTokens) {
      for (const UiScenarioRegistration &entry : Registry()) {
        if (queuedNames.contains(entry.fullName) ||
            !MatchesPattern(entry.fullName, token)) {
          continue;
        }
        if (QueueScenario(engine, state, entry, &queued))
          queuedNames.insert(entry.fullName);
      }
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
