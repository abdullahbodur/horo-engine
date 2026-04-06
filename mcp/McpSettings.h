#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace Monolith {
namespace Mcp {

inline constexpr int kDefaultMcpPort = 39281;
inline constexpr const char* kDefaultMcpHost = "127.0.0.1";
inline constexpr const char* kDefaultMcpTransport = "http";

struct McpSettings {
  bool enabled = false;
  std::string transport = kDefaultMcpTransport;
  std::string host = kDefaultMcpHost;
  int port = kDefaultMcpPort;
  bool autoStart = true;
};

struct McpSettingsDocument {
  McpSettings settings;
  nlohmann::json rootJson = nlohmann::json::object();
  bool loadedFromDisk = false;
  bool parseError = false;
  std::string error;
};

McpSettings DefaultMcpSettings();
std::filesystem::path ResolveMcpHomeDirectory();
std::filesystem::path ResolveMcpSettingsDirectory();
std::filesystem::path ResolveMcpSettingsPath();
McpSettingsDocument LoadMcpSettings();
bool SaveMcpSettings(McpSettingsDocument* doc, std::string* outError);

}  // namespace Mcp
}  // namespace Monolith
