#include "mcp/McpSettings.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <random>
#include <system_error>

namespace Monolith {
namespace Mcp {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string GetEnvVar(const char* name) {
  if (!name || !*name)
    return {};
#ifdef _WIN32
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || !value)
    return {};
  std::string out(value);
  free(value);
  return out;
#else
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string();
#endif
}

std::string NormalizeTransport(std::string transport) {
  if (transport.empty())
    return kDefaultMcpTransport;
  for (char& c : transport)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return transport == "http" ? transport : std::string(kDefaultMcpTransport);
}

int SanitizePort(int port) {
  return (port >= 1 && port <= 65535) ? port : kDefaultMcpPort;
}

std::string SanitizeHost(const std::string& host) {
  return host == kDefaultMcpHost ? host : std::string(kDefaultMcpHost);
}

}  // namespace

McpSettings DefaultMcpSettings() {
  McpSettings out;
  out.enabled = false;
  out.transport = kDefaultMcpTransport;
  out.host = kDefaultMcpHost;
  out.port = kDefaultMcpPort;
  out.autoStart = true;
  return out;
}

fs::path ResolveMcpHomeDirectory() {
#ifdef _WIN32
  std::string home = GetEnvVar("USERPROFILE");
  if (!home.empty())
    return fs::path(home);
  const std::string drive = GetEnvVar("HOMEDRIVE");
  const std::string path = GetEnvVar("HOMEPATH");
  if (!drive.empty() && !path.empty())
    return fs::path(drive + path);
#else
  std::string home = GetEnvVar("HOME");
  if (!home.empty())
    return fs::path(home);
#endif
  std::error_code ec;
  return fs::current_path(ec);
}

fs::path ResolveMcpSettingsDirectory() {
  return ResolveMcpHomeDirectory() / ".horo";
}

fs::path ResolveMcpSettingsPath() {
  return ResolveMcpSettingsDirectory() / "settings.json";
}

std::string GenerateMcpAuthToken(size_t bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::random_device rd;
  std::uniform_int_distribution<int> dist(0, 255);
  std::string token;
  token.reserve(bytes * 2);
  for (size_t i = 0; i < bytes; ++i) {
    const int value = dist(rd);
    token.push_back(kHex[(value >> 4) & 0xF]);
    token.push_back(kHex[value & 0xF]);
  }
  return token;
}

McpSettingsDocument LoadMcpSettings() {
  McpSettingsDocument out;
  out.settings = DefaultMcpSettings();

  const fs::path path = ResolveMcpSettingsPath();
  std::ifstream in(path);
  if (!in.is_open())
    return out;

  out.loadedFromDisk = true;
  try {
    in >> out.rootJson;
  } catch (const json::exception& e) {
    out.rootJson = json::object();
    out.parseError = true;
    out.error = e.what();
    return out;
  }

  if (!out.rootJson.is_object()) {
    out.rootJson = json::object();
    out.parseError = true;
    out.error = "Settings root must be an object.";
    return out;
  }

  const json mcpJson = out.rootJson.value("mcp", json::object());
  if (!mcpJson.is_object())
    return out;

  out.settings.enabled = mcpJson.value("enabled", out.settings.enabled);
  out.settings.transport = NormalizeTransport(mcpJson.value("transport", out.settings.transport));
  out.settings.host = SanitizeHost(mcpJson.value("host", out.settings.host));
  out.settings.port = SanitizePort(mcpJson.value("port", out.settings.port));
  out.settings.authToken = mcpJson.value("authToken", std::string());
  out.settings.autoStart = mcpJson.value("autoStart", out.settings.autoStart);
  return out;
}

bool SaveMcpSettings(McpSettingsDocument* doc, std::string* outError) {
  if (outError)
    outError->clear();
  if (!doc) {
    if (outError)
      *outError = "Settings document is null.";
    return false;
  }

  doc->settings.transport = NormalizeTransport(doc->settings.transport);
  doc->settings.host = SanitizeHost(doc->settings.host);
  doc->settings.port = SanitizePort(doc->settings.port);

  json root = doc->rootJson.is_object() ? doc->rootJson : json::object();
  json mcpJson = root.value("mcp", json::object());
  if (!mcpJson.is_object())
    mcpJson = json::object();

  mcpJson["enabled"] = doc->settings.enabled;
  mcpJson["transport"] = doc->settings.transport;
  mcpJson["host"] = doc->settings.host;
  mcpJson["port"] = doc->settings.port;
  mcpJson["authToken"] = doc->settings.authToken;
  mcpJson["autoStart"] = doc->settings.autoStart;
  root["mcp"] = std::move(mcpJson);

  const fs::path dir = ResolveMcpSettingsDirectory();
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    if (outError)
      *outError = ec.message();
    return false;
  }

  std::ofstream outFile(ResolveMcpSettingsPath());
  if (!outFile.is_open()) {
    if (outError)
      *outError = "Failed to open settings file for writing.";
    return false;
  }

  try {
    outFile << root.dump(2);
  } catch (const std::exception& e) {
    if (outError)
      *outError = e.what();
    return false;
  }

  doc->rootJson = std::move(root);
  doc->loadedFromDisk = true;
  doc->parseError = false;
  doc->error.clear();
  return true;
}

}  // namespace Mcp
}  // namespace Monolith
