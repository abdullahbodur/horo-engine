#include "mcp/McpController.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <sstream>
#include <unordered_map>

#include "core/StringHash.h"

namespace Monolith::Mcp {
namespace {
std::string FormatNowTime() {
  using clock = std::chrono::system_clock;
  const auto now = std::chrono::floor<std::chrono::seconds>(clock::now());
  return std::format("{:%H:%M:%S}", now);
}

std::string FormatNowTimestamp() {
  using clock = std::chrono::system_clock;
  const auto now = std::chrono::floor<std::chrono::seconds>(clock::now());
  return std::format("{:%Y-%m-%d %H:%M:%S}", now);
}

std::string TruncatePreview(const std::string &text, size_t maxChars = 240) {
  if (text.size() <= maxChars)
    return text;
  if (maxChars < 4)
    return text.substr(0, maxChars);
  return text.substr(0, maxChars - 3) + "...";
}
} // namespace

McpController::McpController() = default;

McpController::~McpController() { Shutdown(); }

void McpController::Initialize() {
  if (m_initialized)
    return;

  m_settingsDocument = LoadMcpSettings();

  m_protocol = std::make_unique<McpProtocol>(McpProtocolContext{
      [this]() {
        std::scoped_lock lock(m_snapshotMutex);
        return m_snapshot;
      },
      [this](const std::string &toolName, const nlohmann::json &arguments) {
        return InvokeCommand(toolName, arguments);
      },
      [this](const McpActivityRecord &activity) { PushActivity(activity); },
  });

  std::scoped_lock lock(m_statusMutex);
  m_status.enabled = m_settingsDocument.settings.enabled;
  m_status.endpointUrl = EndpointUrl();
  m_status.toolCount = m_protocol->ToolCount();
  m_status.resourceCount = m_protocol->ResourceCount();
  m_status.toolCatalog = m_protocol->ToolCatalog();
  m_status.resourceCatalog = m_protocol->ResourceCatalog();
  if (m_settingsDocument.parseError)
    m_status.lastError = m_settingsDocument.error;

  m_initialized = true;
}

void McpController::Shutdown() {
  StopServer();
  m_protocol.reset();
  m_initialized = false;
}

bool McpController::ApplySettings(McpSettings settings, std::string *outError) {
  if (outError)
    outError->clear();
  if (!m_initialized)
    Initialize();

  m_settingsDocument.settings = std::move(settings);
  if (!SaveMcpSettings(&m_settingsDocument, outError)) {
    std::scoped_lock lock(m_statusMutex);
    if (outError)
      m_status.lastError = *outError;
    return false;
  }

  {
    std::scoped_lock lock(m_statusMutex);
    m_status.enabled = m_settingsDocument.settings.enabled;
    m_status.endpointUrl = EndpointUrl();
    m_status.lastError.clear();
  }
  RefreshServerState(outError);
  return true;
}

void McpController::SetEditorActive(bool active) {
  m_editorActive = active;
  RefreshServerState(nullptr);
}

void McpController::PublishSnapshot(const McpEditorSnapshot &snapshot) {
  std::scoped_lock lock(m_snapshotMutex);
  m_snapshot = CloneSnapshot(snapshot);
}

size_t McpController::DrainCommands(
    const std::function<McpCommandResult(std::string_view,
                                         const nlohmann::json &)> &executor,
    size_t maxCommands) {
  std::deque<QueuedCommand> local;
  {
    std::scoped_lock lock(m_commandMutex);
    const size_t count = std::min(maxCommands, m_commands.size());
    for (size_t i = 0; i < count; ++i) {
      local.push_back(std::move(m_commands.front()));
      m_commands.pop_front();
    }
  }

  for (QueuedCommand &command : local) {
    McpCommandResult result;
    try {
      result = executor ? executor(command.toolName, command.arguments)
                        : McpCommandResult{false, nlohmann::json::object(),
                                           "No editor executor registered."};
    } catch (const nlohmann::json::exception &e) {
      result.ok = false;
      result.error = e.what();
    } catch (const std::future_error &e) {
      result.ok = false;
      result.error = e.what();
    }
    command.promise.set_value(std::move(result));
  }
  return local.size();
}

McpStatusSnapshot McpController::GetStatusSnapshot() const {
  std::scoped_lock lock(m_statusMutex);
  return m_status;
}

std::string McpController::BuildClaudeConfigSnippet() const {
  return "{\n"
         "  \"mcpServers\": {\n"
         "    \"horo-engine\": {\n"
         "      \"type\": \"http\",\n"
         "      \"url\": \"" +
         EndpointUrl() +
         "\"\n"
         "    }\n"
         "  }\n"
         "}";
}

std::string McpController::BuildCodexConfigSnippet() const {
  return "[mcp_servers.horo_engine]\n"
         "url = \"" +
         EndpointUrl() + "\"\n";
}

std::string McpController::BuildVsCodeConfigSnippet() const {
  return "{\n"
         "  \"servers\": {\n"
         "    \"horoEngine\": {\n"
         "      \"type\": \"http\",\n"
         "      \"url\": \"" +
         EndpointUrl() +
         "\"\n"
         "    }\n"
         "  }\n"
         "}";
}

void McpController::ClearActivityLog() {
  std::scoped_lock lock(m_statusMutex);
  m_status.recentActivity.clear();
  m_status.successCount = 0;
  m_status.failureCount = 0;
  m_status.totalRequests = 0;
  m_status.lastRequestTime.clear();
  m_status.topTool.clear();
  m_status.topResource.clear();
}

void McpController::RefreshServerState(std::string *outError) {
  if (!m_initialized)
    return;

  const bool shouldRun = m_editorActive &&
                         m_settingsDocument.settings.enabled &&
                         m_settingsDocument.settings.autoStart;
  {
    std::scoped_lock lock(m_statusMutex);
    m_status.enabled = m_settingsDocument.settings.enabled;
    m_status.desiredRunning = shouldRun;
    m_status.endpointUrl = EndpointUrl();
  }

  if (!shouldRun) {
    StopServer();
    return;
  }

  if (m_server.IsRunning())
    StopServer();
  StartServer(outError);
}

void McpController::StartServer(std::string *outError) {
  const McpServerStartResult result = m_server.Start(
      m_settingsDocument.settings.host, m_settingsDocument.settings.port,
      [this](const McpHttpRequest &request) {
        return m_protocol->HandleHttp(request);
      },
      [this]() {
        std::scoped_lock lock(m_statusMutex);
        ++m_status.activeConnections;
        ++m_status.activeRequests;
        ++m_status.totalRequests;
      },
      [this]() {
        std::scoped_lock lock(m_statusMutex);
        m_status.activeConnections =
            std::max(0, m_status.activeConnections - 1);
        m_status.activeRequests = std::max(0, m_status.activeRequests - 1);
      });

  std::scoped_lock lock(m_statusMutex);
  m_status.running = result.ok;
  if (!result.ok) {
    m_status.lastError =
        "Failed to bind MCP server on " + EndpointUrl() + ": " + result.error;
    if (outError)
      *outError = m_status.lastError;
  } else {
    m_status.lastError.clear();
  }
}

void McpController::StopServer() {
  m_server.Stop();
  std::scoped_lock lock(m_statusMutex);
  m_status.running = false;
  m_status.activeConnections = 0;
  m_status.activeRequests = 0;
}

McpCommandResult McpController::InvokeCommand(std::string_view toolName,
                                              const nlohmann::json &arguments) {
  std::promise<McpCommandResult> promise;
  std::future<McpCommandResult> future = promise.get_future();
  {
    std::scoped_lock lock(m_commandMutex);
    QueuedCommand command;
    command.toolName = std::string(toolName);
    command.arguments = arguments;
    command.promise = std::move(promise);
    m_commands.push_back(std::move(command));
  }

  if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
    return McpCommandResult{false, nlohmann::json::object(),
                            "Editor command timed out."};
  return future.get();
}

void McpController::PushActivity(const McpActivityRecord &activity) {
  std::scoped_lock lock(m_statusMutex);
  McpActivityEntry entry;
  entry.timeText = FormatNowTime();
  entry.timestampText = FormatNowTimestamp();
  entry.requestId = activity.requestId;
  entry.transportMethod = activity.transportMethod;
  entry.mcpMethod = activity.mcpMethod;
  entry.target = activity.target;
  entry.operation = activity.operation;
  entry.ok = activity.ok;
  entry.httpStatus = activity.httpStatus;
  entry.durationMs = activity.durationMs;
  entry.requestPreview = TruncatePreview(activity.requestPreview);
  entry.responsePreview = TruncatePreview(activity.responsePreview, 320);
  entry.error = TruncatePreview(activity.error);
  m_status.lastRequestTime = entry.timestampText;
  if (entry.ok)
    ++m_status.successCount;
  else
    ++m_status.failureCount;

  m_status.recentActivity.insert(m_status.recentActivity.begin(),
                                 std::move(entry));
  if (m_status.recentActivity.size() > 40)
    m_status.recentActivity.resize(40);

  std::unordered_map<std::string, uint64_t, StringHash, std::equal_to<>>
      toolCounts;
  std::unordered_map<std::string, uint64_t, StringHash, std::equal_to<>>
      resourceCounts;
  for (const McpActivityEntry &item : m_status.recentActivity) {
    if (item.operation == "tool" && !item.target.empty())
      ++toolCounts[item.target];
    else if (item.operation == "resource" && !item.target.empty())
      ++resourceCounts[item.target];
  }

  uint64_t bestToolCount = 0;
  m_status.topTool.clear();
  for (const auto &[tool, count] : toolCounts) {
    if (count > bestToolCount) {
      bestToolCount = count;
      m_status.topTool = tool;
    }
  }

  uint64_t bestResourceCount = 0;
  m_status.topResource.clear();
  for (const auto &[resource, count] : resourceCounts) {
    if (count > bestResourceCount) {
      bestResourceCount = count;
      m_status.topResource = resource;
    }
  }
}

std::string McpController::EndpointUrl() const {
  std::ostringstream out;
  out << kMcpUrlScheme << "://" << m_settingsDocument.settings.host << ":"
      << m_settingsDocument.settings.port << "/mcp";
  return out.str();
}
} // namespace Monolith::Mcp
