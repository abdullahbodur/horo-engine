#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "mcp/McpProtocol.h"
#include "mcp/McpServer.h"
#include "mcp/McpSettings.h"

namespace Monolith::Mcp {
struct McpActivityEntry {
  std::string timeText;
  std::string timestampText;
  std::string requestId;
  std::string transportMethod;
  std::string mcpMethod;
  std::string target;
  std::string operation;
  bool ok = true;
  int httpStatus = 200;
  double durationMs = 0.0;
  std::string requestPreview;
  std::string responsePreview;
  std::string error;
};

struct McpStatusSnapshot {
  bool enabled = false;
  bool desiredRunning = false;
  bool running = false;
  std::string endpointUrl;
  int activeConnections = 0;
  int activeRequests = 0;
  uint64_t totalRequests = 0;
  uint64_t successCount = 0;
  uint64_t failureCount = 0;
  size_t toolCount = 0;
  size_t resourceCount = 0;
  std::string lastRequestTime;
  std::string topTool;
  std::string topResource;
  std::string lastError;
  std::vector<McpActivityEntry> recentActivity;
  std::vector<McpCatalogEntry> toolCatalog;
  std::vector<McpCatalogEntry> resourceCatalog;
};

class McpController {
public:
  McpController();

  ~McpController();

  void Initialize();

  void Shutdown();

  const McpSettingsDocument &SettingsDocument() const {
    return m_settingsDocument;
  }

  McpSettings GetSettings() const { return m_settingsDocument.settings; }

  bool ApplySettings(McpSettings settings, std::string *outError);

  void SetEditorActive(bool active);

  void PublishSnapshot(const McpEditorSnapshot &snapshot);

  size_t DrainCommands(const std::function<McpCommandResult(
                           std::string_view, const nlohmann::json &)> &executor,
                       size_t maxCommands = 16);

  McpStatusSnapshot GetStatusSnapshot() const;

  std::string BuildClaudeConfigSnippet() const;

  std::string BuildCodexConfigSnippet() const;

  std::string BuildVsCodeConfigSnippet() const;

  void ClearActivityLog();

private:
  struct QueuedCommand {
    std::string toolName;
    nlohmann::json arguments;
    std::promise<McpCommandResult> promise;
  };

  void RefreshServerState(std::string *outError);

  void StartServer(std::string *outError);

  void StopServer();

  McpCommandResult InvokeCommand(std::string_view toolName,
                                 const nlohmann::json &arguments);

  void PushActivity(const McpActivityRecord &activity);

  std::string EndpointUrl() const;

  McpSettingsDocument m_settingsDocument;
  bool m_initialized = false;
  bool m_editorActive = false;

  mutable std::mutex m_snapshotMutex;
  std::shared_ptr<const McpEditorSnapshot> m_snapshot;

  mutable std::mutex m_commandMutex;
  std::deque<QueuedCommand> m_commands;

  mutable std::mutex m_statusMutex;
  McpStatusSnapshot m_status;

  std::unique_ptr<McpProtocol> m_protocol;
  McpHttpServer m_server;
};
} // namespace Monolith::Mcp
