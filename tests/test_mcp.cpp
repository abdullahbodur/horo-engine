#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/ProjectPath.h"
#include "editor/EditorLayer.h"
#include "editor/SceneSerializer.h"

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
using namespace Monolith::Editor;

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

std::filesystem::path NormalizePathForComparison(const std::filesystem::path& path) {
  if (path.empty())
    return path;
  std::error_code ec;
  const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
  if (!ec)
    return normalized;
  ec.clear();
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    const std::filesystem::path normalizedParent = std::filesystem::weakly_canonical(parent, ec);
    if (!ec)
      return (normalizedParent / path.filename()).lexically_normal();
  }
  return path.lexically_normal();
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

struct ProjectRootGuard {
  std::filesystem::path previousRoot;

  explicit ProjectRootGuard(const std::filesystem::path& nextRoot) : previousRoot(Monolith::ProjectPath::Root()) {
    Monolith::ProjectPath::Init(nextRoot);
  }

  ~ProjectRootGuard() {
    Monolith::ProjectPath::Init(previousRoot);
  }
};

struct HttpResponse {
  int statusCode = 0;
  std::string body;
};

HttpResponse SendHttpPost(int port, const json& payload) {
  EnsureSocketsReady();

  const std::string body = payload.dump();
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
  const std::string responseBody = raw.substr(headerEnd + 4);
  const size_t firstSpace = head.find(' ');
  REQUIRE(firstSpace != std::string::npos);
  return HttpResponse{std::stoi(head.substr(firstSpace + 1, 3)), responseBody};
}

bool NearlyEqualJsonFloat(const json& value, float expected, float eps = 0.001f) {
  return std::fabs(value.get<float>() - expected) <= eps;
}

McpEditorSnapshot MakeSnapshot() {
  McpEditorSnapshot snapshot;
  snapshot.editorActive = true;
  snapshot.playMode = true;
  snapshot.dirty = true;
  snapshot.reloadPending = true;
  snapshot.sceneId = "scene_main";
  snapshot.sceneName = "Main Scene";
  snapshot.sceneFilePath = "assets/scenes/main_scene.json";
  snapshot.selectedAssetId = "crate";
  snapshot.selectedObjectIds = {"obj_root", "obj_child"};

  McpObjectSnapshot root;
  root.id = "obj_root";
  root.type = "Prop";
  root.position = Monolith::Vec3(1.0f, 2.0f, 3.0f);
  root.scale = Monolith::Vec3(2.0f, 2.0f, 2.0f);
  root.yaw = 45.0f;
  root.assetId = "crate";
  root.props["mesh"] = "crate.obj";
  root.props["tag"] = "root";
  root.components.push_back({"MeshRenderer", {{"castShadows", "true"}}});
  snapshot.objects.push_back(root);

  McpObjectSnapshot child;
  child.id = "obj_child";
  child.type = "Prop";
  child.assetId = "hero";
  child.props["parentId"] = "obj_root";
  child.props["socket"] = "hand_r";
  child.components.push_back({"Attachment", {{"socket", "hand_r"}}});
  snapshot.objects.push_back(child);

  McpObjectSnapshot camera;
  camera.id = "obj_camera";
  camera.type = "Camera";
  camera.position = Monolith::Vec3(0.0f, 5.0f, -10.0f);
  camera.components.push_back({"Camera", {{"fov", "60"}}});
  snapshot.objects.push_back(camera);

  McpObjectSnapshot light;
  light.id = "obj_light";
  light.type = "Light";
  light.props["parentId"] = "obj_root";
  light.props["color"] = "white";
  snapshot.objects.push_back(light);

  snapshot.assets.push_back({"crate", "assets/models/crate.obj", "1,1,1", "assets/textures/crate.png"});
  snapshot.assets.push_back({"hero", "assets/models/hero.glb", "1,2,1", "assets/textures/hero.png"});
  snapshot.assets.push_back({"floor", "assets/models/floor.obj", "8,1,8", ""});

  snapshot.consoleEntries.push_back({"12:00:00", "INFO", "Loaded assets successfully"});
  snapshot.consoleEntries.push_back({"12:00:01", "WARN", "Missing optional collider for obj_light"});
  snapshot.consoleEntries.push_back({"12:00:02", "ERROR", "Renderer exploded briefly"});
  snapshot.consoleEntries.push_back({"12:00:03", "INFO", "Hero animation warmed"});
  snapshot.build.available = true;
  snapshot.build.source = "scene_project_runtime";
  snapshot.build.status = "warning";
  snapshot.build.assetCount = 3;
  snapshot.build.nodeCount = 4;
  snapshot.build.sceneValidationWarnings = 1;
  snapshot.build.runtimeBuildWarnings = 1;
  snapshot.build.roomCount = 1;
  snapshot.build.propCount = 4;
  snapshot.build.lightCount = 1;
  snapshot.build.hasSceneCamera = true;
  snapshot.build.issues.push_back({"validation", "warning", "scene.nodes[1].parentId",
                                   "parentId does not resolve to a declared scene node."});
  snapshot.build.issues.push_back({"runtime", "warning", "scene.nodes[3].light",
                                   "Light node is missing typed light properties; using defaults."});
  return snapshot;
}

McpEditorSnapshot MakeSnapshotFromDocument(const SceneDocument& doc,
                                          const std::vector<std::string>& selectedObjectIds,
                                          const std::string& selectedAssetId = {}) {
  McpEditorSnapshot snapshot;
  snapshot.editorActive = true;
  snapshot.sceneId = doc.sceneId;
  snapshot.sceneName = doc.sceneName;
  snapshot.sceneFilePath = doc.filePath;
  snapshot.dirty = doc.dirty;
  snapshot.selectedObjectIds = selectedObjectIds;
  snapshot.selectedAssetId = selectedAssetId;
  for (const SceneObject& object : doc.objects) {
    McpObjectSnapshot entry;
    entry.id = object.id;
    switch (object.type) {
      case SceneObjectType::Panel:
        entry.type = "Panel";
        break;
      case SceneObjectType::Prop:
        entry.type = "Prop";
        break;
      case SceneObjectType::Light:
        entry.type = "Light";
        break;
      case SceneObjectType::Camera:
        entry.type = "Camera";
        break;
    }
    entry.position = object.position;
    entry.scale = object.scale;
    entry.yaw = object.yaw;
    entry.pitch = object.pitch;
    entry.roll = object.roll;
    entry.assetId = object.assetId;
    entry.props = object.props;
    snapshot.objects.push_back(std::move(entry));
  }
  for (const auto& assetEntry : doc.assets)
    snapshot.assets.push_back({assetEntry.first, assetEntry.second.mesh, assetEntry.second.renderScale,
                               assetEntry.second.albedoMap});
  return snapshot;
}

SceneDocument MakeEditorSceneDocument(const std::filesystem::path& filePath) {
  SceneDocument doc;
  doc.sceneId = "scene";
  doc.sceneName = "Scene";
  doc.filePath = filePath.string();
  doc.dirty = false;
  doc.assets["stone"] = AssetDef{"assets/models/stone/stone.obj", "1.0000,1.0000,1.0000", ""};

  SceneObject panel;
  panel.id = "panel_000";
  panel.type = SceneObjectType::Panel;
  doc.objects.push_back(panel);

  SceneObject camera;
  camera.id = "cam_000";
  camera.type = SceneObjectType::Camera;
  camera.props["followTargetId"] = "";
  camera.props["parentId"] = "obj_000";
  doc.objects.push_back(camera);

  return doc;
}

SceneObject* FindSceneObject(SceneDocument* doc, const std::string& id) {
  if (!doc)
    return nullptr;
  for (SceneObject& object : doc->objects) {
    if (object.id == id)
      return &object;
  }
  return nullptr;
}

const SceneObject* FindSceneObject(const SceneDocument* doc, const std::string& id) {
  if (!doc)
    return nullptr;
  for (const SceneObject& object : doc->objects) {
    if (object.id == id)
      return &object;
  }
  return nullptr;
}

json ParseBody(const McpHttpResponse& response) {
  return json::parse(response.body);
}

json ProtocolRequest(McpProtocol& protocol, const std::string& method, const json& params = json::object(), int id = 1) {
  const McpHttpResponse response = protocol.HandleHttp(
      McpHttpRequest{"POST", "/mcp", {}, json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}}.dump()});
  REQUIRE(response.statusCode == 200);
  return ParseBody(response);
}

json ReadResource(McpProtocol& protocol, const std::string& uri, const json& extraParams = json::object(), int id = 100) {
  json params = extraParams;
  params["uri"] = uri;
  return ProtocolRequest(protocol, "resources/read", params, id);
}

json CallTool(McpProtocol& protocol, const std::string& name, const json& arguments = json::object(), int id = 200) {
  return ProtocolRequest(protocol, "tools/call", json{{"name", name}, {"arguments", arguments}}, id);
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

TEST_CASE("McpSnapshot builders cover compact MCP resources", "[mcp][snapshot]") {
  McpEditorSnapshot snapshot = MakeSnapshot();

  const json sceneSummary = BuildSceneSummaryJson(snapshot, 2);
  REQUIRE(sceneSummary["sceneId"] == "scene_main");
  REQUIRE(sceneSummary["objectCount"] == 4);
  REQUIRE(sceneSummary["objects"].size() == 2);
  REQUIRE(sceneSummary["moreObjects"] == 2);

  const json sceneStatus = BuildSceneStatusJson(snapshot);
  REQUIRE(sceneStatus["playMode"].get<bool>());
  REQUIRE(sceneStatus["dirty"].get<bool>());
  REQUIRE(sceneStatus["reloadPending"].get<bool>());

  const json selection = BuildSelectionJson(snapshot);
  REQUIRE(selection["selectedObjectIds"].size() == 2);
  REQUIRE(selection["objects"].size() == 2);
  REQUIRE(selection["asset"]["id"] == "crate");

  const json assets = BuildAssetsJson(snapshot, 2);
  REQUIRE(assets["assetCount"] == 3);
  REQUIRE(assets["assets"].size() == 2);
  REQUIRE(assets["moreAssets"] == 1);

  const json assetSelection = BuildAssetsSelectionJson(snapshot);
  REQUIRE(assetSelection["selectedAssetId"] == "crate");
  REQUIRE(assetSelection["hasSelection"].get<bool>());
  REQUIRE(assetSelection["asset"]["objectReferenceCount"] == 1);

  const json catalog = BuildAssetsCatalogJson(snapshot, 8, "hero");
  REQUIRE(catalog["matchedAssets"] == 1);
  REQUIRE(catalog["assets"][0]["id"] == "hero");

  const json recentConsole = BuildConsoleJson(snapshot, 2);
  REQUIRE(recentConsole["lineCount"] == 4);
  REQUIRE(recentConsole["lines"].size() == 2);
  REQUIRE(recentConsole["lines"][1]["message"] == "Hero animation warmed");

  const json consoleSummary = BuildConsoleSummaryJson(snapshot, 2);
  REQUIRE(consoleSummary["infoCount"] == 2);
  REQUIRE(consoleSummary["warnCount"] == 1);
  REQUIRE(consoleSummary["errorCount"] == 1);
  REQUIRE(consoleSummary["recent"].size() == 2);

  const json buildStatus = BuildBuildStatusJson(snapshot, 1);
  REQUIRE(buildStatus["status"] == "warning");
  REQUIRE(buildStatus["sceneValidationWarnings"] == 1);
  REQUIRE(buildStatus["runtimeBuildWarnings"] == 1);
  REQUIRE(buildStatus["issueCount"] == 2);
  REQUIRE(buildStatus["issues"].size() == 1);
  REQUIRE(buildStatus["moreIssues"] == 1);

  const json objectList = BuildObjectListJson(snapshot, 8, "Prop", "root", false);
  REQUIRE(objectList["matchedObjects"] == 2);
  REQUIRE(objectList["objects"].size() == 2);

  const json selectedOnly = BuildObjectListJson(snapshot, 8, "", "", true);
  REQUIRE(selectedOnly["matchedObjects"] == 2);
  REQUIRE(selectedOnly["objects"].size() == 2);

  const json hierarchy = BuildHierarchyJson(snapshot, 8);
  REQUIRE(hierarchy["objectCount"] == 4);
  REQUIRE(hierarchy["roots"] == 2);
  REQUIRE(hierarchy["entries"].size() == 4);
  REQUIRE(hierarchy["entries"][0]["depth"] == 0);

  const json searchAll = SearchSnapshot(snapshot, "hero", 5, "all");
  REQUIRE(searchAll["assets"].size() == 1);
  REQUIRE(searchAll["objects"].size() == 1);

  const json searchAssets = SearchAssetsSnapshot(snapshot, "crate", 5);
  REQUIRE(searchAssets["matchedAssets"] == 1);
  REQUIRE(searchAssets["assets"][0]["id"] == "crate");

  const json searchConsole = SearchConsoleSnapshot(snapshot, "exploded", 5);
  REQUIRE(searchConsole["matchedLines"] == 1);
  REQUIRE(searchConsole["lines"][0]["level"] == "ERROR");
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
}

TEST_CASE("McpProtocol serves initialize, lists, all resources, and all read tools", "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  std::vector<McpActivityRecord> activity;
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [](const std::string&, const json&) { return McpCommandResult{}; },
      [&activity](const McpActivityRecord& entry) { activity.push_back(entry); },
  });

  const json initialize = ProtocolRequest(protocol, "initialize", json::object(), 1);
  REQUIRE(initialize["result"]["serverInfo"]["name"] == "horo-engine");

  const json ping = ProtocolRequest(protocol, "ping", json::object(), 2);
  REQUIRE(ping["result"].empty());

  const json toolList = ProtocolRequest(protocol, "tools/list", json::object(), 3);
  REQUIRE(toolList["result"]["tools"].size() == 31);
  REQUIRE(toolList["result"]["tools"][0]["name"] == "editor_search");
  const json* createObjectTool = nullptr;
  for (const json& tool : toolList["result"]["tools"]) {
    if (tool.value("name", std::string()) == "editor_create_object") {
      createObjectTool = &tool;
      break;
    }
  }
  REQUIRE(createObjectTool != nullptr);
  REQUIRE((*createObjectTool)["inputSchema"]["properties"]["position"]["items"]["type"] == "number");
  REQUIRE((*createObjectTool)["inputSchema"]["properties"]["position"]["minItems"] == 3);

  const json resourceList = ProtocolRequest(protocol, "resources/list", json::object(), 4);
  REQUIRE(resourceList["result"]["resources"].size() == 11);

  const std::vector<std::string> resourceUris = {
      "scene://summary",   "scene://selection",   "scene://assets",      "scene://hierarchy",
      "scene://objects",   "scene://scene_status","assets://selection",  "assets://catalog",
      "console://recent",  "console://summary",   "build://status",
  };
  for (size_t i = 0; i < resourceUris.size(); ++i) {
    const json response =
        ReadResource(protocol, resourceUris[i], json{{"limit", 2}, {"query", "hero"}}, 10 + static_cast<int>(i));
    REQUIRE(response["result"]["contents"][0]["uri"] == resourceUris[i]);
    REQUIRE_FALSE(response["result"]["contents"][0]["text"].get<std::string>().empty());
  }

  const json getObject = CallTool(protocol, "editor.get_object", json{{"id", "obj_root"}}, 100);
  REQUIRE(getObject["result"]["structuredContent"]["id"] == "obj_root");

  const json getObjectSanitized = CallTool(protocol, "editor_get_object", json{{"id", "obj_root"}}, 101);
  REQUIRE(getObjectSanitized["result"]["structuredContent"]["id"] == "obj_root");

  const json getObjectEdges = CallTool(protocol, "editor.get_object_edges", json{{"id", "obj_root"}}, 102);
  REQUIRE(getObjectEdges["result"]["structuredContent"]["worldCorners"].size() == 8);

  const json listObjects =
      CallTool(protocol, "editor.list_objects", json{{"type", "Prop"}, {"query", "obj"}, {"selectedOnly", false}}, 103);
  REQUIRE(listObjects["result"]["structuredContent"]["matchedObjects"] == 2);

  const json getObjects =
      CallTool(protocol, "editor.get_objects", json{{"ids", json::array({"obj_root", "obj_child", "missing"})}}, 104);
  REQUIRE(getObjects["result"]["structuredContent"]["objects"].size() == 2);

  const json getChildren = CallTool(protocol, "editor.get_object_children", json{{"id", "obj_root"}}, 105);
  REQUIRE(getChildren["result"]["structuredContent"]["childCount"] == 2);

  const json getParent = CallTool(protocol, "editor.get_object_parent", json{{"id", "obj_child"}}, 106);
  REQUIRE(getParent["result"]["structuredContent"]["parentId"] == "obj_root");
  REQUIRE(getParent["result"]["structuredContent"]["parent"]["id"] == "obj_root");

  const json countObjects = CallTool(protocol, "editor.count_objects", json{{"type", "Prop"}, {"query", "obj"}}, 107);
  REQUIRE(countObjects["result"]["structuredContent"]["count"] == 2);

  const json search = CallTool(protocol, "editor_search", json{{"query", "hero"}, {"limit", 5}, {"scope", "all"}}, 108);
  REQUIRE(search["result"]["structuredContent"]["assets"].size() == 1);

  const json listAssets = CallTool(protocol, "editor.list_assets", json{{"query", ".obj"}, {"limit", 5}}, 109);
  REQUIRE(listAssets["result"]["structuredContent"]["matchedAssets"] == 2);

  const json getAsset = CallTool(protocol, "editor.get_asset", json{{"id", "crate"}}, 110);
  REQUIRE(getAsset["result"]["structuredContent"]["id"] == "crate");
  REQUIRE(getAsset["result"]["structuredContent"]["objectReferenceCount"] == 1);

  const json searchAssets = CallTool(protocol, "editor.search_assets", json{{"query", "hero"}, {"limit", 5}}, 111);
  REQUIRE(searchAssets["result"]["structuredContent"]["matchedAssets"] == 1);

  const json countAssets = CallTool(protocol, "editor.count_assets", json{{"query", ".png"}}, 112);
  REQUIRE(countAssets["result"]["structuredContent"]["count"] == 2);

  const json sceneStatus = CallTool(protocol, "editor.scene_status", json::object(), 113);
  REQUIRE(sceneStatus["result"]["structuredContent"]["sceneId"] == "scene_main");

  const json sceneFile = CallTool(protocol, "editor.get_scene_file", json::object(), 114);
  REQUIRE(sceneFile["result"]["structuredContent"]["filePath"] == "assets/scenes/main_scene.json");
  REQUIRE(sceneFile["result"]["structuredContent"]["dirty"].get<bool>());

  const json searchConsole = CallTool(protocol, "editor.search_console", json{{"query", "exploded"}, {"limit", 5}}, 115);
  REQUIRE(searchConsole["result"]["structuredContent"]["matchedLines"] == 1);

  REQUIRE(activity.size() >= 20);
  REQUIRE(activity.back().target == "editor.search_console");
  REQUIRE(activity.back().ok);
}

TEST_CASE("Editor MCP commands preserve reserved ids and reload from disk", "[mcp][editor]") {
  EnvGuard env("horo_editor_mcp_consistency");
  const std::filesystem::path scenePath =
      std::filesystem::temp_directory_path() / "horo_editor_mcp_consistency" / "world.json";
  std::filesystem::create_directories(scenePath.parent_path());

  const SceneDocument initialDoc = MakeEditorSceneDocument(scenePath);
  SceneSerializer::SaveToFile(initialDoc, scenePath.string());

  EditorLayer editor;
  editor.LoadDocument(initialDoc);

  const McpCommandResult stringVecFail = editor.ExecuteMcpCommand(
      "editor.create_object",
      json{{"id", "codex_probe_panel"},
           {"type", "Panel"},
           {"position", json::array({"12", "1", "12"})},
           {"scale", json::array({"1", "1", "1"})},
           {"props", json{{"material", "default"}}}});
  REQUIRE_FALSE(stringVecFail.ok);
  REQUIRE(stringVecFail.error == "position must be [x,y,z].");

  const McpCommandResult createPanel = editor.ExecuteMcpCommand(
      "editor.create_object",
      json{{"id", "codex_probe_panel"},
           {"type", "Panel"},
           {"position", json::array({12, 1, 12})},
           {"scale", json::array({1, 1, 1})},
           {"props", json{{"material", "default"}}}});
  REQUIRE(createPanel.ok);

  const McpCommandResult createProp = editor.ExecuteMcpCommand(
      "editor.create_object_from_asset",
      json{{"id", "codex_probe_prop"}, {"assetId", "stone"}, {"position", json::array({13, 1, 13})}});
  REQUIRE(createProp.ok);

  const McpCommandResult reparent =
      editor.ExecuteMcpCommand("editor.reparent_object",
                               json{{"id", "codex_probe_prop"}, {"parentId", "codex_probe_panel"}});
  REQUIRE(reparent.ok);

  const McpCommandResult duplicate =
      editor.ExecuteMcpCommand("editor.duplicate", json{{"id", "codex_probe_prop"}, {"count", 1}});
  REQUIRE(duplicate.ok);
  REQUIRE(duplicate.data["duplicates"].size() == 1);
  REQUIRE(duplicate.data["duplicates"][0]["id"] != "obj_000");

  const std::string duplicateId = duplicate.data["duplicates"][0]["id"].get<std::string>();

  const McpCommandResult renameReserved =
      editor.ExecuteMcpCommand("editor.rename_object", json{{"id", duplicateId}, {"newId", "obj_000"}});
  REQUIRE_FALSE(renameReserved.ok);
  REQUIRE(renameReserved.error == "Object id already exists.");

  const McpCommandResult renameOk = editor.ExecuteMcpCommand(
      "editor.rename_object", json{{"id", duplicateId}, {"newId", "codex_probe_dup"}});
  REQUIRE(renameOk.ok);

  const SceneDocument& afterRenameDoc = editor.GetDocument();
  const SceneObject* camera = FindSceneObject(&afterRenameDoc, "cam_000");
  REQUIRE(camera != nullptr);
  REQUIRE(camera->props.at("parentId") == "obj_000");

  const McpCommandResult select = editor.ExecuteMcpCommand(
      "editor.select", json{{"ids", json::array({"codex_probe_panel", "codex_probe_prop"})}});
  REQUIRE(select.ok);
  REQUIRE(select.data["selectedObjectIds"].size() == 2);

  const McpEditorSnapshot selectedSnapshot =
      MakeSnapshotFromDocument(editor.GetDocument(), editor.GetSelectedObjectIds(), editor.GetSelectedAssetId());
  const json selectedOnly = BuildObjectListJson(selectedSnapshot, 16, "", "", true);
  REQUIRE(selectedOnly["matchedObjects"] == 2);

  const McpCommandResult reload = editor.ExecuteMcpCommand("editor.reload_scene", json::object());
  REQUIRE(reload.ok);
  const SceneDocument& reloadedDoc = editor.GetDocument();
  REQUIRE_FALSE(reloadedDoc.dirty);
  REQUIRE(reloadedDoc.objects.size() == initialDoc.objects.size());
  REQUIRE(FindSceneObject(&reloadedDoc, "codex_probe_panel") == nullptr);
  REQUIRE(FindSceneObject(&reloadedDoc, "codex_probe_prop") == nullptr);
  REQUIRE(FindSceneObject(&reloadedDoc, "codex_probe_dup") == nullptr);

  camera = FindSceneObject(&reloadedDoc, "cam_000");
  REQUIRE(camera != nullptr);
  REQUIRE(camera->props.at("parentId") == "obj_000");
}

TEST_CASE("Editor MCP delete_asset reports managed file deletion details", "[mcp][editor]") {
  namespace fs = std::filesystem;

  EnvGuard env("horo_editor_mcp_delete_asset");
  const fs::path projectRoot = env.tempHome / "project";
  fs::create_directories(projectRoot / "assets" / "models" / "stone");
  {
    std::ofstream mesh(projectRoot / "assets" / "models" / "stone" / "stone.obj");
    mesh << "o stone\n";
  }
  {
    std::ofstream albedo(projectRoot / "assets" / "models" / "stone" / "stone.png");
    albedo << "png";
  }
  ProjectRootGuard projectRootGuard(projectRoot);

  const fs::path scenePath = projectRoot / "assets" / "scenes" / "world.json";
  fs::create_directories(scenePath.parent_path());

  SceneDocument doc;
  doc.filePath = scenePath.string();
  doc.assets["stone"] = AssetDef{"assets/models/stone/stone.obj", "1,1,1", "assets/models/stone/stone.png"};
  SceneObject object;
  object.id = "prop_stone";
  object.type = SceneObjectType::Prop;
  object.assetId = "stone";
  doc.objects.push_back(object);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult removeAsset = editor.ExecuteMcpCommand("editor.delete_asset", json{{"id", "stone"}});
  REQUIRE(removeAsset.ok);
  REQUIRE(removeAsset.data["deletedAssetId"] == "stone");
  REQUIRE(removeAsset.data["clearedObjectReferences"] == 1);
  REQUIRE(removeAsset.data["deletedManagedFiles"].get<bool>());
  REQUIRE(NormalizePathForComparison(removeAsset.data["deletedAssetDirectory"].get<std::string>()) ==
          NormalizePathForComparison(projectRoot / "assets" / "models" / "stone"));
  REQUIRE(editor.GetDocument().assets.find("stone") == editor.GetDocument().assets.end());
  REQUIRE(editor.GetDocument().objects[0].assetId.empty());
  REQUIRE_FALSE(fs::exists(projectRoot / "assets" / "models" / "stone"));
}

TEST_CASE("McpProtocol dispatches every write tool and serializes success and failure", "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  std::vector<std::string> invoked;
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [&invoked](const std::string& toolName, const json& arguments) {
        invoked.push_back(toolName);
        if (toolName == "editor.delete_asset")
          return McpCommandResult{false, json::object(), "delete rejected"};
        return McpCommandResult{true, json{{"handledTool", toolName}, {"echo", arguments}}, {}};
      },
      {},
  });

  const std::vector<std::pair<std::string, json>> writeTools = {
      {"editor.select", json{{"id", "obj_root"}}},
      {"editor.clear_selection", json::object()},
      {"editor.create_object", json{{"type", "Prop"}, {"id", "spawned"}}},
      {"editor.create_object_from_asset", json{{"assetId", "crate"}, {"id", "spawned_from_asset"}}},
      {"editor.update_object", json{{"id", "obj_root"}, {"props", json{{"tag", "updated"}}}}},
      {"editor.transform", json{{"id", "obj_root"}, {"position", json::array({4, 5, 6})}}},
      {"editor.rename_object", json{{"id", "obj_root"}, {"newId", "obj_root_renamed"}}},
      {"editor.reparent_object", json{{"id", "obj_child"}, {"parentId", "obj_camera"}}},
      {"editor.duplicate", json{{"id", "obj_root"}, {"count", 2}}},
      {"editor.delete", json{{"ids", json::array({"obj_light"})}}},
      {"editor.select_asset", json{{"id", "crate"}}},
      {"editor.update_asset", json{{"id", "crate"}, {"mesh", "assets/models/crate_v2.obj"}}},
      {"editor.delete_asset", json{{"id", "hero"}}},
      {"editor.new_scene", json{{"sceneId", "fresh"}, {"sceneName", "Fresh"}}},
      {"editor.save_scene", json::object()},
      {"editor.reload_scene", json::object()},
  };

  for (size_t i = 0; i < writeTools.size(); ++i) {
    const json response = CallTool(protocol, writeTools[i].first, writeTools[i].second, 300 + static_cast<int>(i));
    if (writeTools[i].first == "editor.delete_asset") {
      REQUIRE(response["error"]["message"] == "delete rejected");
      continue;
    }
    REQUIRE(response["result"]["structuredContent"]["handledTool"] == writeTools[i].first);
    REQUIRE(response["result"]["structuredContent"]["echo"] == writeTools[i].second);
  }

  REQUIRE(invoked.size() == writeTools.size());
  REQUIRE(invoked.front() == "editor.select");
  REQUIRE(invoked.back() == "editor.reload_scene");
}

TEST_CASE("McpProtocol returns expected errors for unsupported or unavailable paths", "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  McpProtocol protocolWithSnapshot(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      nullptr,
      {},
  });

  const McpHttpResponse unknownEndpoint =
      protocolWithSnapshot.HandleHttp(McpHttpRequest{"POST", "/nope", {}, "{}"});
  REQUIRE(unknownEndpoint.statusCode == 404);

  const McpHttpResponse getInfo = protocolWithSnapshot.HandleHttp(McpHttpRequest{"GET", "/mcp", {}, {}});
  REQUIRE(getInfo.statusCode == 200);

  const McpHttpResponse badMethod = protocolWithSnapshot.HandleHttp(McpHttpRequest{"PUT", "/mcp", {}, {}});
  REQUIRE(badMethod.statusCode == 405);

  const McpHttpResponse badJson = protocolWithSnapshot.HandleHttp(McpHttpRequest{"POST", "/mcp", {}, "{"});
  REQUIRE(badJson.statusCode == 400);

  const json unknownMethod = ProtocolRequest(protocolWithSnapshot, "bogus/method", json::object(), 400);
  REQUIRE(unknownMethod["error"]["message"] == "Method not found.");

  const json unknownResource = ReadResource(protocolWithSnapshot, "scene://missing", json::object(), 401);
  REQUIRE(unknownResource["error"]["message"] == "Unknown resource URI.");

  const json unknownTool = CallTool(protocolWithSnapshot, "editor.missing", json::object(), 402);
  REQUIRE(unknownTool["error"]["message"] == "Unknown tool.");

  const json objectNotFound = CallTool(protocolWithSnapshot, "editor.get_object", json{{"id", "missing"}}, 403);
  REQUIRE(objectNotFound["error"]["message"] == "Object not found.");

  const json assetNotFound = CallTool(protocolWithSnapshot, "editor.get_asset", json{{"id", "missing"}}, 404);
  REQUIRE(assetNotFound["error"]["message"] == "Asset not found.");

  McpProtocol noSnapshot(McpProtocolContext{});
  const McpHttpResponse noSnapshotResponse = noSnapshot.HandleHttp(
      McpHttpRequest{"POST", "/mcp", {}, json{{"jsonrpc", "2.0"}, {"id", 405}, {"method", "tools/call"}, {"params", {{"name", "editor.search"}, {"arguments", json::object()}}}}.dump()});
  REQUIRE(noSnapshotResponse.statusCode == 503);

  const McpHttpResponse queueUnavailable = protocolWithSnapshot.HandleHttp(
      McpHttpRequest{"POST", "/mcp", {}, json{{"jsonrpc", "2.0"}, {"id", 406}, {"method", "tools/call"}, {"params", {{"name", "editor.select"}, {"arguments", json{{"id", "obj_root"}}}}}}.dump()});
  REQUIRE(queueUnavailable.statusCode == 503);
}

TEST_CASE("McpController localhost server serves reads, writes, and status snapshots", "[mcp][integration]") {
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

  const HttpResponse ping = SendHttpPost(settings.port, json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ping"}});
  REQUIRE(ping.statusCode == 200);

  const HttpResponse sceneStatus = SendHttpPost(
      settings.port,
      json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/call"}, {"params", {{"name", "editor.scene_status"}, {"arguments", json::object()}}}});
  REQUIRE(sceneStatus.statusCode == 200);
  REQUIRE(json::parse(sceneStatus.body)["result"]["structuredContent"]["objectCount"] == 4);

  std::string selectedId;
  auto future = std::async(std::launch::async, [&]() {
    return SendHttpPost(
        settings.port,
        json{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"}, {"params", {{"name", "editor.select"}, {"arguments", json{{"id", "obj_root"}}}}}});
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const size_t drained = controller.DrainCommands([&](const std::string& toolName, const json& arguments) {
    REQUIRE(toolName == "editor.select");
    selectedId = arguments["id"].get<std::string>();
    return McpCommandResult{true, json{{"selectedObjectIds", json::array({selectedId})}}, {}};
  });
  REQUIRE(drained == 1);

  const HttpResponse selectResponse = future.get();
  REQUIRE(selectResponse.statusCode == 200);
  REQUIRE(json::parse(selectResponse.body)["result"]["structuredContent"]["selectedObjectIds"][0] == "obj_root");

  const McpStatusSnapshot status = controller.GetStatusSnapshot();
  REQUIRE(status.running);
  REQUIRE(status.toolCount == 31);
  REQUIRE(status.resourceCount == 11);
  REQUIRE(status.totalRequests >= 3);

  controller.SetEditorActive(false);
}
