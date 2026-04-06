#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "mcp/McpSnapshot.h"

namespace Monolith {
namespace Mcp {

struct McpHttpRequest {
  std::string method;
  std::string path;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};

struct McpHttpResponse {
  int statusCode = 200;
  std::string statusText = "OK";
  std::string contentType = "application/json";
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};

struct McpCommandResult {
  bool ok = false;
  nlohmann::json data = nlohmann::json::object();
  std::string error;
};

struct McpCatalogEntry {
  std::string name;
  std::string target;
  std::string description;
};

struct McpActivityRecord {
  std::string requestId;
  std::string transportMethod;
  std::string mcpMethod;
  std::string target;
  std::string operation;
  bool ok = false;
  int httpStatus = 200;
  double durationMs = 0.0;
  std::string requestPreview;
  std::string responsePreview;
  std::string error;
};

struct McpProtocolContext {
  std::function<std::shared_ptr<const McpEditorSnapshot>()> snapshotProvider;
  std::function<McpCommandResult(const std::string&, const nlohmann::json&)> commandInvoker;
  std::function<void(const McpActivityRecord&)> activitySink;
};

class McpProtocol {
 public:
  explicit McpProtocol(McpProtocolContext context);

  McpHttpResponse HandleHttp(const McpHttpRequest& request) const;
  size_t ToolCount() const;
  size_t ResourceCount() const;
  const std::vector<McpCatalogEntry>& ToolCatalog() const;
  const std::vector<McpCatalogEntry>& ResourceCatalog() const;

 private:
  McpProtocolContext m_context;
};

}  // namespace Mcp
}  // namespace Monolith
