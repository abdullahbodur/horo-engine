#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "mcp/McpController.h"
#include "mcp/McpProtocol.h"
#include "mcp/McpSettings.h"
#include "mcp/McpSnapshot.h"

using json = nlohmann::json;
using namespace Monolith::Mcp;

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

void CloseSocket(SocketHandle socketHandle) {
  if (socketHandle == kInvalidSocket)
    return;
#ifdef _WIN32
  closesocket(socketHandle);
#else
  close(socketHandle);
#endif
}

void EnsureSocketsReady() {
#ifdef _WIN32
  static bool ready = false;
  if (!ready) {
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
    ready = true;
  }
#endif
}

struct EnvGuard {
  std::filesystem::path tempHome;
  std::string oldHome;

  explicit EnvGuard(const std::string& name) {
    tempHome = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(tempHome);
    std::filesystem::create_directories(tempHome);
#ifdef _WIN32
    const char* key = "USERPROFILE";
#else
    const char* key = "HOME";
#endif
#ifdef _WIN32
    char* existing = nullptr;
    size_t len = 0;
    if (_dupenv_s(&existing, &len, key) == 0 && existing) {
      oldHome = existing;
      free(existing);
    }
    _putenv_s("USERPROFILE", tempHome.string().c_str());
#else
    const char* existing = std::getenv(key);
    if (existing)
      oldHome = existing;
    setenv("HOME", tempHome.string().c_str(), 1);
#endif
  }

  ~EnvGuard() {
#ifdef _WIN32
    _putenv_s("USERPROFILE", oldHome.c_str());
#else
    if (oldHome.empty())
      unsetenv("HOME");
    else
      setenv("HOME", oldHome.c_str(), 1);
#endif
    std::filesystem::remove_all(tempHome);
  }
};

struct HttpResponse {
  int statusCode = 0;
  std::string body;
};

HttpResponse SendHttpPost(int port, const std::string& body) {
  EnsureSocketsReady();

  SocketHandle socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(socketHandle != kInvalidSocket);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  REQUIRE(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
  REQUIRE(connect(socketHandle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

  std::string request =
      "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n";
  request += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
  REQUIRE(send(socketHandle, request.data(), static_cast<int>(request.size()), 0) > 0);

  std::string raw;
  char buffer[4096];
  while (true) {
    const int rc = recv(socketHandle, buffer, sizeof(buffer), 0);
    if (rc <= 0)
      break;
    raw.append(buffer, static_cast<size_t>(rc));
  }
  CloseSocket(socketHandle);

  const size_t headerEnd = raw.find("\r\n\r\n");
  REQUIRE(headerEnd != std::string::npos);
  const std::string head = raw.substr(0, headerEnd);
  const std::string payload = raw.substr(headerEnd + 4);
  const size_t firstSpace = head.find(' ');
  REQUIRE(firstSpace != std::string::npos);
  HttpResponse response;
  response.statusCode = std::stoi(head.substr(firstSpace + 1, 3));
  response.body = payload;
  return response;
}

bool NearlyEqualJsonFloat(const json& value, float expected, float eps = 0.001f) {
  return std::fabs(value.get<float>() - expected) <= eps;
}

McpEditorSnapshot MakeSnapshot() {
  McpEditorSnapshot snapshot;
  snapshot.editorActive = true;
  snapshot.sceneId = "scene";
  snapshot.sceneName = "Scene";
  snapshot.sceneFilePath = "assets/scenes/scene.json";
  snapshot.selectedObjectIds = {"obj_001"};

  McpObjectSnapshot object;
  object.id = "obj_001";
  object.type = "Prop";
  object.assetId = "crate";
  object.props["mesh"] = "crate.obj";
  snapshot.objects.push_back(object);

  McpAssetSnapshot asset;
  asset.id = "crate";
  asset.mesh = "assets/models/crate.obj";
  asset.renderScale = "1,1,1";
  snapshot.assets.push_back(asset);
  return snapshot;
}

}  // namespace

TEST_CASE("McpSettings preserves unknown keys and uses home settings path", "[mcp][settings]") {
  EnvGuard env("horo_mcp_settings");
  const std::filesystem::path settingsPath = ResolveMcpSettingsPath();
  std::filesystem::create_directories(settingsPath.parent_path());

  {
    std::ofstream out(settingsPath);
    out << R"({
      "theme": "ice",
      "mcp": {
        "enabled": true,
        "port": 40123,
        "custom": "keep-me"
      }
    })";
  }

  McpSettingsDocument doc = LoadMcpSettings();
  REQUIRE(doc.loadedFromDisk);
  REQUIRE(doc.settings.enabled);
  REQUIRE(doc.settings.port == 40123);
  REQUIRE(doc.rootJson["theme"] == "ice");

  doc.settings.port = 40124;
  std::string err;
  REQUIRE(SaveMcpSettings(&doc, &err));

  std::ifstream in(settingsPath);
  json saved = json::parse(in);
  REQUIRE(saved["theme"] == "ice");
  REQUIRE(saved["mcp"]["custom"] == "keep-me");
  REQUIRE(saved["mcp"]["port"] == 40124);
}

TEST_CASE("McpSnapshot compacts summary and console resources", "[mcp][snapshot]") {
  McpEditorSnapshot snapshot;
  snapshot.sceneId = "scene";
  snapshot.sceneName = "Scene";
  snapshot.sceneFilePath = "assets/scenes/world.json";
  for (int i = 0; i < 15; ++i) {
    McpObjectSnapshot object;
    object.id = "obj_" + std::to_string(i);
    object.type = "Prop";
    object.props["mesh"] = "crate.obj";
    snapshot.objects.push_back(object);
  }
  for (int i = 0; i < 25; ++i) {
    snapshot.consoleEntries.push_back(McpConsoleEntry{"12:00:00", "INFO", "line_" + std::to_string(i)});
  }

  const json summary = BuildSceneSummaryJson(snapshot);
  REQUIRE(summary["objects"].size() == 12);
  REQUIRE(summary["moreObjects"] == 3);
  REQUIRE_FALSE(summary["objects"][0].contains("props"));

  const json console = BuildConsoleJson(snapshot);
  REQUIRE(console["lines"].size() == 20);
  REQUIRE(console["truncated"].get<bool>());

  const json search = SearchSnapshot(snapshot, "obj_1", 5, "objects");
  REQUIRE_FALSE(search["objects"].empty());
}

TEST_CASE("McpSnapshot builds world-space edges for rotated objects", "[mcp][snapshot]") {
  McpObjectSnapshot object;
  object.id = "box";
  object.type = "Prop";
  object.position = Monolith::Vec3(10.0f, 2.0f, 3.0f);
  object.scale = Monolith::Vec3(4.0f, 2.0f, 6.0f);
  object.yaw = 90.0f;

  const json edges = BuildObjectEdgesJson(object);
  REQUIRE(edges["id"] == "box");
  REQUIRE(edges["basis"] == "object_transform_box");
  REQUIRE(edges["worldCorners"].size() == 8);
  REQUIRE(edges["worldEdges"].size() == 12);
  REQUIRE(NearlyEqualJsonFloat(edges["center"][0], 10.0f));
  REQUIRE(NearlyEqualJsonFloat(edges["center"][1], 2.0f));
  REQUIRE(NearlyEqualJsonFloat(edges["center"][2], 3.0f));
  REQUIRE(NearlyEqualJsonFloat(edges["halfExtents"][0], 2.0f));
  REQUIRE(NearlyEqualJsonFloat(edges["halfExtents"][1], 1.0f));
  REQUIRE(NearlyEqualJsonFloat(edges["halfExtents"][2], 3.0f));

  const json& firstCorner = edges["worldCorners"][0];
  REQUIRE(NearlyEqualJsonFloat(firstCorner[0], 11.0f));
  REQUIRE(NearlyEqualJsonFloat(firstCorner[1], 0.0f));
  REQUIRE(NearlyEqualJsonFloat(firstCorner[2], 0.0f));

  const json& firstEdge = edges["worldEdges"][0];
  REQUIRE(firstEdge["from"] == firstCorner);
  REQUIRE(firstEdge["to"].size() == 3);
}

TEST_CASE("McpProtocol serves resources and tools without auth", "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  std::vector<McpActivityRecord> activity;
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [](const std::string&, const json&) { return McpCommandResult{}; },
      [&activity](const McpActivityRecord& entry) { activity.push_back(entry); },
  });

  const McpHttpResponse tools = protocol.HandleHttp(
      McpHttpRequest{"POST", "/mcp", {}, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"});
  REQUIRE(tools.statusCode == 200);
  REQUIRE(json::parse(tools.body)["result"]["tools"].size() == 31);

  const McpHttpResponse object = protocol.HandleHttp(
      McpHttpRequest{"POST", "/mcp", {},
                     R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"editor.get_object","arguments":{"id":"obj_001"}}})"});
  REQUIRE(object.statusCode == 200);
  REQUIRE(json::parse(object.body)["result"]["structuredContent"]["id"] == "obj_001");

  const McpHttpResponse resource = protocol.HandleHttp(
      McpHttpRequest{"POST", "/mcp", {},
                     R"({"jsonrpc":"2.0","id":3,"method":"resources/read","params":{"uri":"scene://summary"}})"});
  REQUIRE(resource.statusCode == 200);

  const McpHttpResponse edges = protocol.HandleHttp(
      McpHttpRequest{"POST", "/mcp", {},
                     R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"editor.get_object_edges","arguments":{"id":"obj_001"}}})"});
  REQUIRE(edges.statusCode == 200);
  const json edgesBody = json::parse(edges.body);
  REQUIRE(edgesBody["result"]["structuredContent"]["id"] == "obj_001");
  REQUIRE(edgesBody["result"]["structuredContent"]["worldCorners"].size() == 8);
  REQUIRE(edgesBody["result"]["structuredContent"]["worldEdges"].size() == 12);

  REQUIRE(activity.size() >= 3);
  REQUIRE(activity.back().operation == "tool");
  REQUIRE(activity.back().target == "editor.get_object_edges");
  REQUIRE(activity.back().ok);
}

TEST_CASE("McpController localhost server serves reads and queued writes", "[mcp][integration]") {
  EnvGuard env("horo_mcp_controller");
  McpController controller;
  controller.Initialize();
  controller.PublishSnapshot(MakeSnapshot());

  McpSettings settings = controller.GetSettings();
  settings.enabled = true;
  settings.autoStart = true;
  settings.host = kDefaultMcpHost;
  settings.port = 39881;

  std::string err;
  REQUIRE(controller.ApplySettings(settings, &err));
  controller.SetEditorActive(true);

  const HttpResponse ping = SendHttpPost(settings.port, R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
  REQUIRE(ping.statusCode == 200);

  std::string selectedId;
  auto future = std::async(std::launch::async, [&]() {
    return SendHttpPost(
        settings.port,
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"editor.select","arguments":{"id":"obj_001"}}})");
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const size_t drained = controller.DrainCommands([&](const std::string& toolName, const json& arguments) {
    REQUIRE(toolName == "editor.select");
    selectedId = arguments["id"].get<std::string>();
    return McpCommandResult{true, json{{"selectedObjectIds", json::array({selectedId})}}, std::string()};
  });
  REQUIRE(drained == 1);

  const HttpResponse selectResponse = future.get();
  REQUIRE(selectResponse.statusCode == 200);
  REQUIRE(selectedId == "obj_001");
  REQUIRE(json::parse(selectResponse.body)["result"]["structuredContent"]["selectedObjectIds"][0] ==
          "obj_001");

  controller.SetEditorActive(false);
}
