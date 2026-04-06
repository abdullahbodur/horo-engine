#include "mcp/McpController.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>

namespace Monolith {
namespace Mcp {

namespace {

std::string FormatNowTime() {
  using clock = std::chrono::system_clock;
  const std::time_t t = clock::to_time_t(clock::now());
  std::tm tmBuf{};
#ifdef _WIN32
  localtime_s(&tmBuf, &t);
#else
  localtime_r(&t, &tmBuf);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmBuf);
  return buf;
}

}  // namespace

McpController::McpController() = default;

McpController::~McpController() {
  Shutdown();
}

void McpController::Initialize() {
  if (m_initialized)
    return;

  m_settingsDocument = LoadMcpSettings();
  if (m_settingsDocument.settings.enabled && m_settingsDocument.settings.authToken.empty()) {
    m_settingsDocument.settings.authToken = GenerateMcpAuthToken();
    std::string ignoredError;
    SaveMcpSettings(&m_settingsDocument, &ignoredError);
  }

  m_protocol = std::make_unique<McpProtocol>(McpProtocolContext{
      [this]() {
        std::scoped_lock lock(m_snapshotMutex);
        return m_snapshot;
      },
      [this](const std::string& toolName, const nlohmann::json& arguments) {
        return InvokeCommand(toolName, arguments);
      },
      [this](const std::string& target, bool ok, const std::string& detail) {
        PushActivity(target, ok, detail);
      },
      [this]() { return m_settingsDocument.settings.authToken; },
  });

  std::scoped_lock lock(m_statusMutex);
  m_status.enabled = m_settingsDocument.settings.enabled;
  m_status.endpointUrl = EndpointUrl();
  m_status.toolCount = m_protocol->ToolCount();
  m_status.resourceCount = m_protocol->ResourceCount();
  if (m_settingsDocument.parseError)
    m_status.lastError = m_settingsDocument.error;

  m_initialized = true;
}

void McpController::Shutdown() {
  StopServer();
  m_protocol.reset();
  m_initialized = false;
}

bool McpController::ApplySettings(McpSettings settings, std::string* outError) {
  if (outError)
    outError->clear();
  if (!m_initialized)
    Initialize();

  if (settings.enabled && settings.authToken.empty())
    settings.authToken = GenerateMcpAuthToken();

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

void McpController::PublishSnapshot(const McpEditorSnapshot& snapshot) {
  std::scoped_lock lock(m_snapshotMutex);
  m_snapshot = CloneSnapshot(snapshot);
}

size_t McpController::DrainCommands(
    const std::function<McpCommandResult(const std::string&, const nlohmann::json&)>& executor,
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

  for (QueuedCommand& command : local) {
    McpCommandResult result;
    try {
      result = executor ? executor(command.toolName, command.arguments)
                        : McpCommandResult{false, nlohmann::json::object(),
                                           "No editor executor registered."};
    } catch (const std::exception& e) {
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
         "\",\n"
         "      \"headers\": {\n"
         "        \"Authorization\": \"Bearer " +
         m_settingsDocument.settings.authToken +
         "\"\n"
         "      }\n"
         "    }\n"
         "  }\n"
         "}";
}

std::string McpController::BuildCodexConfigSnippet() const {
  return "[mcp_servers.horo_engine]\n"
         "url = \"" +
         EndpointUrl() +
         "\"\n\n"
         "[mcp_servers.horo_engine.http_headers]\n"
         "Authorization = \"Bearer " +
         m_settingsDocument.settings.authToken + "\"\n";
}

void McpController::RefreshServerState(std::string* outError) {
  if (!m_initialized)
    return;

  const bool shouldRun =
      m_editorActive && m_settingsDocument.settings.enabled && m_settingsDocument.settings.autoStart;
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

void McpController::StartServer(std::string* outError) {
  const McpServerStartResult result = m_server.Start(
      m_settingsDocument.settings.host, m_settingsDocument.settings.port,
      [this](const McpHttpRequest& request) { return m_protocol->HandleHttp(request); },
      [this]() {
        std::scoped_lock lock(m_statusMutex);
        ++m_status.activeConnections;
        ++m_status.totalRequests;
      },
      [this]() {
        std::scoped_lock lock(m_statusMutex);
        m_status.activeConnections = std::max(0, m_status.activeConnections - 1);
      });

  std::scoped_lock lock(m_statusMutex);
  m_status.running = result.ok;
  if (!result.ok) {
    m_status.lastError = "Failed to bind MCP server on " + EndpointUrl() + ": " + result.error;
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
}

McpCommandResult McpController::InvokeCommand(const std::string& toolName,
                                              const nlohmann::json& arguments) {
  std::promise<McpCommandResult> promise;
  std::future<McpCommandResult> future = promise.get_future();
  {
    std::scoped_lock lock(m_commandMutex);
    QueuedCommand command;
    command.toolName = toolName;
    command.arguments = arguments;
    command.promise = std::move(promise);
    m_commands.push_back(std::move(command));
  }

  if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
    return McpCommandResult{false, nlohmann::json::object(), "Editor command timed out."};
  return future.get();
}

void McpController::PushActivity(const std::string& target, bool ok, const std::string& detail) {
  std::scoped_lock lock(m_statusMutex);
  McpActivityEntry entry;
  entry.timeText = FormatNowTime();
  entry.target = target;
  entry.ok = ok;
  entry.detail = detail;
  m_status.recentActivity.insert(m_status.recentActivity.begin(), std::move(entry));
  if (m_status.recentActivity.size() > 12)
    m_status.recentActivity.resize(12);
}

std::string McpController::EndpointUrl() const {
  std::ostringstream out;
  out << "http://" << m_settingsDocument.settings.host << ":" << m_settingsDocument.settings.port
      << "/mcp";
  return out.str();
}

}  // namespace Mcp
}  // namespace Monolith
