#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "mcp/McpProtocol.h"

namespace Monolith {
namespace Mcp {

struct McpServerStartResult {
  bool ok = false;
  std::string error;
};

class McpHttpServer {
 public:
  McpHttpServer();
  ~McpHttpServer();

  McpServerStartResult Start(const std::string& host,
                             int port,
                             std::function<McpHttpResponse(const McpHttpRequest&)> handler,
                             std::function<void()> onRequestBegin,
                             std::function<void()> onRequestEnd);
  void Stop();
  bool IsRunning() const { return m_running.load(); }

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
  std::atomic<bool> m_running{false};
};

}  // namespace Mcp
}  // namespace Monolith
