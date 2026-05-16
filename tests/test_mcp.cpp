#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/ProjectPath.h"
#include "ui/editor/EditorLayer.h"
#include "ui/editor/SceneSerializer.h"

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
#include "mcp/McpServer.h"
#include "mcp/McpSettings.h"
#include "mcp/McpSnapshot.h"
#include "tests/TestTempPaths.h"

using json = nlohmann::json;
using namespace Horo::Mcp;
using namespace Horo::Editor;

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

std::filesystem::path
NormalizePathForComparison(const std::filesystem::path &path) {
  if (path.empty())
    return path;
  std::error_code ec;
  if (const auto normalized = std::filesystem::weakly_canonical(path, ec); !ec)
    return normalized;
  ec.clear();
  if (const auto parent = path.parent_path(); !parent.empty()) {
    if (const auto normalizedParent =
            std::filesystem::weakly_canonical(parent, ec);
        !ec)
      return (normalizedParent / path.filename()).lexically_normal();
  }
  return path.lexically_normal();
}

struct EnvGuard {
  EnvGuard(const EnvGuard &) = delete;

  EnvGuard &operator=(const EnvGuard &) = delete;

  std::filesystem::path tempHome;
  std::string oldHome;

  explicit EnvGuard(std::string_view name) {
    tempHome = Horo::Tests::SecureTempBase() / name;
    std::filesystem::remove_all(tempHome);
    std::filesystem::create_directories(tempHome);
#ifdef _WIN32
    const char *key = "USERPROFILE";
#else
    const char *key = "HOME";
#endif
#ifdef _WIN32
    if (size_t len = 0; getenv_s(&len, nullptr, 0, key) == 0 && len > 1) {
      std::vector<char> existing(len);
      if (getenv_s(&len, existing.data(), existing.size(), key) == 0 &&
          len > 1) {
        oldHome.assign(existing.data());
      }
    }
    _putenv_s("USERPROFILE", tempHome.string().c_str());
#else
    if (const char *existing = std::getenv(key))
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
  ProjectRootGuard(const ProjectRootGuard &) = delete;

  ProjectRootGuard &operator=(const ProjectRootGuard &) = delete;

  std::filesystem::path previousRoot = Horo::ProjectPath::Root();

  explicit ProjectRootGuard(const std::filesystem::path &nextRoot) {
    Horo::ProjectPath::Init(nextRoot);
  }

  ~ProjectRootGuard() { Horo::ProjectPath::Init(previousRoot); }
};

struct HttpResponse {
  int statusCode = 0;
  std::string body;
};

HttpResponse SendHttpPost(int port, const json &payload) {
  EnsureSocketsReady();

  const std::string body = payload.dump();
  SocketHandle socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(socketHandle != kInvalidSocket);
  if (socketHandle == kInvalidSocket)
    return {};

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  REQUIRE(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
  REQUIRE(connect(socketHandle, reinterpret_cast<sockaddr *>(&addr),
                  sizeof(addr)) == 0);

  const std::string request =
      std::format("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: "
                  "application/json\r\nContent-Length: {}\r\nConnection: "
                  "close\r\n\r\n{}",
                  body.size(), body);
  REQUIRE(send(socketHandle, request.data(), static_cast<int>(request.size()),
               0) > 0);

  std::string raw;
  std::string buffer(4096, '\0');
  while (true) {
    const auto rc =
        recv(socketHandle, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (rc <= 0)
      break;
    raw.append(buffer.data(), static_cast<size_t>(rc));
  }
  CloseSocket(socketHandle);

  const size_t headerEnd = raw.find("\r\n\r\n");
  REQUIRE(headerEnd != std::string::npos);
  const std::string head = raw.substr(0, headerEnd);
  const std::string responseBody = raw.substr(headerEnd + 4);
  const size_t firstSpace = head.find(' ');
  REQUIRE(firstSpace != std::string::npos);
  return HttpResponse{.statusCode = std::stoi(head.substr(firstSpace + 1, 3)),
                      .body = responseBody};
}

HttpResponse SendRawHttpRequest(int port, const std::string &request) {
  EnsureSocketsReady();

  SocketHandle socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(socketHandle != kInvalidSocket);
  if (socketHandle == kInvalidSocket)
    return {};

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  REQUIRE(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
  REQUIRE(connect(socketHandle, reinterpret_cast<sockaddr *>(&addr),
                  sizeof(addr)) == 0);
  REQUIRE(send(socketHandle, request.data(), static_cast<int>(request.size()),
               0) > 0);

  std::string raw;
  std::string buffer(4096, '\0');
  while (true) {
    const auto rc =
        recv(socketHandle, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (rc <= 0)
      break;
    raw.append(buffer.data(), static_cast<size_t>(rc));
  }
  CloseSocket(socketHandle);

  const size_t headerEnd = raw.find("\r\n\r\n");
  REQUIRE(headerEnd != std::string::npos);
  const std::string head = raw.substr(0, headerEnd);
  const size_t firstSpace = head.find(' ');
  REQUIRE(firstSpace != std::string::npos);
  return HttpResponse{.statusCode = std::stoi(head.substr(firstSpace + 1, 3)),
                      .body = raw.substr(headerEnd + 4)};
}

bool NearlyEqualJsonFloat(const json &value, float expected,
                          float eps = 0.001f) {
  return std::abs(value.get<float>() - expected) <= eps;
}

struct FieldRange {
  bool hasMin = false;
  float minVal = 0.0f;
  bool hasMax = false;
  float maxVal = 0.0f;
};

McpSchemaFieldSnapshot
MakeSchemaField(std::string key, std::string label, std::string widget,
                std::string defaultValue, std::vector<std::string> options = {},
                FieldRange range = {}, std::string description = {}) {
  McpSchemaFieldSnapshot field;
  field.key = std::move(key);
  field.label = std::move(label);
  field.description = std::move(description);
  field.widget = std::move(widget);
  field.hasDefault = true;
  field.defaultValue = std::move(defaultValue);
  field.options = std::move(options);
  field.hasMin = range.hasMin;
  field.minVal = range.minVal;
  field.hasMax = range.hasMax;
  field.maxVal = range.maxVal;
  return field;
}

void PopulateSchemaSnapshot(McpEditorSnapshot *snapshot) {
  REQUIRE(snapshot != nullptr);

  McpSchemaEntrySnapshot panel;
  panel.kind = "object";
  panel.name = "Panel";
  panel.label = "Panel";
  panel.fields.push_back(MakeSchemaField("material", "Material", "enum",
                                         "stone", {"stone", "metal", "wood"}));
  snapshot->schema.objectTypes.push_back(std::move(panel));

  McpSchemaEntrySnapshot prop;
  prop.kind = "object";
  prop.name = "Prop";
  prop.label = "Prop";
  prop.fields.push_back(MakeSchemaField(
      "mesh", "Mesh", "enum", "box", {"box", "sphere", "cylinder", "pyramid"}));
  snapshot->schema.objectTypes.push_back(std::move(prop));

  McpSchemaEntrySnapshot lightObject;
  lightObject.kind = "object";
  lightObject.name = "Light";
  lightObject.label = "Light";
  lightObject.fields.push_back(MakeSchemaField(
      "lightType", "Type", "enum", "point", {"point", "directional"}));
  lightObject.fields.push_back(MakeSchemaField(
      "radius", "Radius", "float", "10.0", {}, {true, 0.5f, true, 50.0f}));
  lightObject.fields.push_back(MakeSchemaField(
      "intensity", "Intensity", "float", "1.0", {}, {true, 0.1f, true, 20.0f}));
  lightObject.fields.push_back(
      MakeSchemaField("color", "Color RGB", "color3", "1.0000,1.0000,1.0000"));
  snapshot->schema.objectTypes.push_back(std::move(lightObject));

  McpSchemaEntrySnapshot lightComponent;
  lightComponent.kind = "component";
  lightComponent.name = "light";
  lightComponent.label = "Light";
  lightComponent.appliesTo = {"Panel", "Prop"};
  lightComponent.fields.push_back(MakeSchemaField(
      "lightType", "Type", "enum", "point", {"point", "directional"}));
  lightComponent.fields.push_back(MakeSchemaField(
      "radius", "Radius", "float", "5.0", {}, {true, 0.5f, true, 50.0f}));
  lightComponent.fields.push_back(MakeSchemaField(
      "intensity", "Intensity", "float", "1.0", {}, {true, 0.1f, true, 20.0f}));
  lightComponent.fields.push_back(
      MakeSchemaField("color", "Color RGB", "color3", "1.0000,1.0000,1.0000"));
  snapshot->schema.components.push_back(std::move(lightComponent));

  McpSchemaEntrySnapshot rigidbody;
  rigidbody.kind = "component";
  rigidbody.name = "rigidbody";
  rigidbody.label = "RigidBody";
  rigidbody.appliesTo = {"Prop"};
  rigidbody.fields.push_back(MakeSchemaField("mass", "Mass", "float", "1.0", {},
                                             {true, 0.0f, true, 500.0f}));
  rigidbody.fields.push_back(
      MakeSchemaField("isKinematic", "Kinematic", "bool", "false"));
  rigidbody.fields.push_back(
      MakeSchemaField("useGravity", "Use Gravity", "bool", "true"));
  snapshot->schema.components.push_back(std::move(rigidbody));

  McpSchemaEntrySnapshot script;
  script.kind = "component";
  script.name = "script";
  script.label = "Script";
  script.appliesTo = {"Panel", "Prop", "Light", "Camera"};
  script.fields.push_back(MakeSchemaField(
      "behaviorTag", "Behavior", "string", "", {}, {false, 0.0f, false, 0.0f},
      "Runtime behavior id resolved by the game."));
  snapshot->schema.components.push_back(std::move(script));
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
  root.position = Horo::Vec3(1.0f, 2.0f, 3.0f);
  root.scale = Horo::Vec3(2.0f, 2.0f, 2.0f);
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
  camera.position = Horo::Vec3(0.0f, 5.0f, -10.0f);
  camera.components.push_back({"Camera", {{"fov", "60"}}});
  snapshot.objects.push_back(camera);

  McpObjectSnapshot light;
  light.id = "obj_light";
  light.type = "Light";
  light.props["parentId"] = "obj_root";
  light.props["color"] = "white";
  snapshot.objects.push_back(light);

  snapshot.assets.emplace_back("crate", "assets/models/crate.obj", "1,1,1",
                               "assets/textures/crate.png");
  snapshot.assets.back().normalMap = "assets/textures/crate_normal.png";
  snapshot.assets.back().metallicRoughnessMap = "assets/textures/crate_mr.png";
  snapshot.assets.back().emissiveMap = "assets/textures/crate_emissive.png";
  snapshot.assets.back().occlusionMap = "assets/textures/crate_occlusion.png";
  snapshot.assets.emplace_back("hero", "assets/models/hero.glb", "1,2,1",
                               "assets/textures/hero.png");
  snapshot.assets.back().normalMap = "assets/textures/hero_normal.png";
  snapshot.assets.back().metallicRoughnessMap = "assets/textures/hero_mr.png";
  snapshot.assets.back().emissiveMap = "assets/textures/hero_emissive.png";
  snapshot.assets.back().occlusionMap = "assets/textures/hero_occlusion.png";
  snapshot.assets.emplace_back("floor", "assets/models/floor.obj", "8,1,8", "");

  snapshot.consoleEntries.emplace_back("12:00:00", "INFO",
                                       "Loaded assets successfully");
  snapshot.consoleEntries.emplace_back(
      "12:00:01", "WARN", "Missing optional collider for obj_light");
  snapshot.consoleEntries.emplace_back("12:00:02", "ERROR",
                                       "Renderer exploded briefly");
  snapshot.consoleEntries.emplace_back("12:00:03", "INFO",
                                       "Hero animation warmed");
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
  snapshot.build.issues.emplace_back(
      "validation", "warning", "scene.nodes[1].parentId",
      "parentId does not resolve to a declared scene node.");
  snapshot.build.issues.emplace_back(
      "runtime", "warning", "scene.nodes[3].light",
      "Light node is missing typed light properties; using defaults.");
  PopulateSchemaSnapshot(&snapshot);
  return snapshot;
}

McpEditorSnapshot
MakeSnapshotFromDocument(const SceneDocument &doc,
                         const std::vector<std::string> &selectedObjectIds,
                         std::string_view selectedAssetId = {}) {
  McpEditorSnapshot snapshot;
  snapshot.editorActive = true;
  snapshot.sceneId = doc.sceneId;
  snapshot.sceneName = doc.sceneName;
  snapshot.sceneFilePath = doc.filePath;
  snapshot.dirty = doc.dirty;
  snapshot.selectedObjectIds = selectedObjectIds;
  snapshot.selectedAssetId = std::string(selectedAssetId);
  using enum SceneObjectType;
  for (const SceneObject &object : doc.objects) {
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
    snapshot.objects.emplace_back(std::move(entry));
  }
  for (const auto &[assetId, assetDef] : doc.assets) {
    snapshot.assets.emplace_back(assetId, assetDef.mesh, assetDef.renderScale,
                                 assetDef.albedoMap, assetDef.normalMap,
                                 assetDef.metallicRoughnessMap,
                                 assetDef.emissiveMap, assetDef.occlusionMap);
  }
  return snapshot;
}

SceneDocument MakeEditorSceneDocument(const std::filesystem::path &filePath) {
  SceneDocument doc;
  doc.sceneId = "scene";
  doc.sceneName = "Scene";
  doc.filePath = filePath.string();
  doc.dirty = false;
  doc.assets["stone"] =
      AssetDef{"assets/models/stone/stone.obj", "1.0000,1.0000,1.0000", ""};

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

SceneObject *FindSceneObject(SceneDocument *doc, std::string_view id) {
  if (!doc)
    return nullptr;
  for (SceneObject &object : doc->objects) {
    if (object.id == id)
      return &object;
  }
  return nullptr;
}

const SceneObject *FindSceneObject(const SceneDocument *doc,
                                   std::string_view id) {
  if (!doc)
    return nullptr;
  for (const SceneObject &object : doc->objects) {
    if (object.id == id)
      return &object;
  }
  return nullptr;
}

json ParseBody(const McpHttpResponse &response) {
  return json::parse(response.body);
}

std::vector<json> ReadJsonLines(const std::filesystem::path &path) {
  std::vector<json> lines;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty())
      lines.push_back(json::parse(line));
  }
  return lines;
}

json ProtocolRequest(const McpProtocol &protocol, std::string_view method,
                     const json &params = json::object(), int id = 1) {
  const McpHttpResponse response =
      protocol.HandleHttp(McpHttpRequest{"POST",
                                         "/mcp",
                                         {},
                                         json{{"jsonrpc", "2.0"},
                                              {"id", id},
                                              {"method", std::string(method)},
                                              {"params", params}}
                                             .dump()});
  REQUIRE(response.statusCode == 200);
  return ParseBody(response);
}

json ReadResource(const McpProtocol &protocol, std::string_view uri,
                  const json &extraParams = json::object(), int id = 100) {
  json params = extraParams;
  params["uri"] = std::string(uri);
  return ProtocolRequest(protocol, "resources/read", params, id);
}

json CallTool(const McpProtocol &protocol, std::string_view name,
              const json &arguments = json::object(), int id = 200) {
  return ProtocolRequest(
      protocol, "tools/call",
      json{{"name", std::string(name)}, {"arguments", arguments}}, id);
}

} // namespace

TEST_CASE("McpSettings loads missing values with defaults and keeps extras", "[mcp][settings][coverage]") {
  EnvGuard env("horo_mcp_settings_coverage");
  const std::filesystem::path settingsPath = ResolveMcpSettingsPath();
  std::filesystem::create_directories(settingsPath.parent_path());

  {
    std::ofstream out(settingsPath);
    out << R"({
      "theme": "ice",
      "mcp": {
        "enabled": true,
        "custom": "keep-me"
      }
    })";
  }

  const McpSettingsDocument doc = LoadMcpSettings();
  REQUIRE(doc.loadedFromDisk);
  REQUIRE_FALSE(doc.parseError);
  REQUIRE(doc.rootJson["theme"] == "ice");
  REQUIRE(doc.rootJson["mcp"]["custom"] == "keep-me");
  REQUIRE(doc.settings.enabled);
  REQUIRE(doc.settings.transport == std::string(kDefaultMcpTransport));
  REQUIRE(doc.settings.host == std::string(kDefaultMcpHost));
  REQUIRE(doc.settings.port == kDefaultMcpPort);
  REQUIRE(doc.settings.autoStart == true);
}

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
  REQUIRE(selection["asset"]["albedoMap"] == "assets/textures/crate.png");
  REQUIRE(selection["asset"]["normalMap"] == "assets/textures/crate_normal.png");
  REQUIRE(selection["asset"]["metallicRoughnessMap"] ==
          "assets/textures/crate_mr.png");
  REQUIRE(selection["asset"]["emissiveMap"] ==
          "assets/textures/crate_emissive.png");
  REQUIRE(selection["asset"]["occlusionMap"] ==
          "assets/textures/crate_occlusion.png");

  const json assets = BuildAssetsJson(snapshot, 2);
  REQUIRE(assets["assetCount"] == 3);
  REQUIRE(assets["assets"].size() == 2);
  REQUIRE(assets["moreAssets"] == 1);

  const json assetSelection = BuildAssetsSelectionJson(snapshot);
  REQUIRE(assetSelection["selectedAssetId"] == "crate");
  REQUIRE(assetSelection["hasSelection"].get<bool>());
  REQUIRE(assetSelection["asset"]["objectReferenceCount"] == 1);
  REQUIRE(assetSelection["asset"]["normalMap"] ==
          "assets/textures/crate_normal.png");

  const json catalog = BuildAssetsCatalogJson(snapshot, 8, "hero");
  REQUIRE(catalog["matchedAssets"] == 1);
  REQUIRE(catalog["assets"][0]["id"] == "hero");

  const json catalogCaseInsensitive =
      BuildAssetsCatalogJson(snapshot, 8, "HERO");
  REQUIRE(catalogCaseInsensitive["matchedAssets"] == 1);

  const json pagedCatalog = BuildAssetsCatalogJson(snapshot, 1, "", 1);
  REQUIRE(pagedCatalog["offset"] == 1);
  REQUIRE(pagedCatalog["limit"] == 1);
  REQUIRE(pagedCatalog["returned"] == 1);
  REQUIRE(pagedCatalog["hasMore"].get<bool>());
  REQUIRE(pagedCatalog["assets"][0]["id"] == "hero");

  const json recentConsole = BuildConsoleJson(snapshot, 2);
  REQUIRE(recentConsole["lineCount"] == 4);
  REQUIRE(recentConsole["lines"].size() == 2);
  REQUIRE(recentConsole["lines"][1]["message"] == "Hero animation warmed");

  const json pagedConsole = BuildConsoleJson(snapshot, 2, 1);
  REQUIRE(pagedConsole["offset"] == 1);
  REQUIRE(pagedConsole["returned"] == 2);
  REQUIRE(pagedConsole["hasMore"].get<bool>());
  REQUIRE(pagedConsole["lines"][0]["message"] ==
          "Missing optional collider for obj_light");
  REQUIRE(pagedConsole["lines"][1]["message"] == "Renderer exploded briefly");

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

  const json schemaCatalog = BuildSchemaCatalogJson(snapshot);
  REQUIRE(schemaCatalog["objectTypeCount"] == 3);
  REQUIRE(schemaCatalog["componentCount"] == 3);
  REQUIRE(schemaCatalog["entryCount"] == 6);

  const json componentCatalog = BuildSchemaCatalogJson(snapshot, "component");
  REQUIRE(componentCatalog["kind"] == "component");
  REQUIRE(componentCatalog["entryCount"] == 3);
  REQUIRE(componentCatalog["entries"][0]["kind"] == "component");

  const json componentSchema = BuildSchemaJson(snapshot, "light", "component");
  REQUIRE(componentSchema["name"] == "light");
  REQUIRE(componentSchema["kind"] == "component");
  REQUIRE(componentSchema["appliesTo"].size() == 2);
  REQUIRE(componentSchema["fields"].size() == 4);
  REQUIRE(NearlyEqualJsonFloat(componentSchema["fields"][1]["numeric"]["min"],
                               0.5f));

  const json objectSchema = BuildSchemaJson(snapshot, "Light", "object");
  REQUIRE(objectSchema["name"] == "Light");
  REQUIRE(objectSchema["fields"][0]["options"][1] == "directional");
  REQUIRE(objectSchema["fields"][2]["widget"] == "float");

  const json objectList =
      BuildObjectListJson(snapshot, 8, "Prop", "root", false);
  REQUIRE(objectList["matchedObjects"] == 2);
  REQUIRE(objectList["objects"].size() == 2);

  const json objectListCaseInsensitive =
      BuildObjectListJson(snapshot, 8, "prop", "ROOT", false);
  REQUIRE(objectListCaseInsensitive["matchedObjects"] == 2);

  const json selectedOnly = BuildObjectListJson(snapshot, 8, "", "", true);
  REQUIRE(selectedOnly["matchedObjects"] == 2);
  REQUIRE(selectedOnly["objects"].size() == 2);

  const json pagedObjects =
      BuildObjectListJson(snapshot, 1, "Prop", "", false, 1);
  REQUIRE(pagedObjects["offset"] == 1);
  REQUIRE(pagedObjects["returned"] == 1);
  REQUIRE_FALSE(pagedObjects["hasMore"].get<bool>());
  REQUIRE(pagedObjects["objects"][0]["id"] == "obj_child");

  const json hierarchy = BuildHierarchyJson(snapshot, 8);
  REQUIRE(hierarchy["objectCount"] == 4);
  REQUIRE(hierarchy["roots"] == 2);
  REQUIRE(hierarchy["entries"].size() == 4);
  REQUIRE(hierarchy["entries"][0]["depth"] == 0);

  const json pagedHierarchy = BuildHierarchyJson(snapshot, 2, 1);
  REQUIRE(pagedHierarchy["offset"] == 1);
  REQUIRE(pagedHierarchy["limit"] == 2);
  REQUIRE(pagedHierarchy["returned"] == 2);
  REQUIRE(pagedHierarchy["hasMore"].get<bool>());

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
  object.position = Horo::Vec3(10.0f, 2.0f, 3.0f);
  object.scale = Horo::Vec3(4.0f, 2.0f, 6.0f);
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

  const json &firstCorner = edges["worldCorners"][0];
  // Box local corner (-0.5, -0.5, -0.5) scaled by (4, 2, 6) = (-2, -1, -3),
  // rotated 90° around Y (yaw) maps (x,y,z) -> (z, y, -x) -> (-3, -1, 2),
  // then translated by center (10, 2, 3) = (7, 1, 5).
  REQUIRE(NearlyEqualJsonFloat(firstCorner[0], 7.0f));
  REQUIRE(NearlyEqualJsonFloat(firstCorner[1], 1.0f));
  REQUIRE(NearlyEqualJsonFloat(firstCorner[2], 5.0f));
}

TEST_CASE("McpProtocol serves initialize, lists, all resources, and all read tools", "[mcp][protocol][coverage]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  std::vector<McpActivityRecord> activity;
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [](const std::string &, const json &) { return McpCommandResult{}; },
      [&activity](const McpActivityRecord &entry) {
        activity.push_back(entry);
      },
  });

  const json initialize =
      ProtocolRequest(protocol, "initialize", json::object(), 1);
  REQUIRE(initialize["result"]["serverInfo"]["name"] == "horo-engine");

  const json ping = ProtocolRequest(protocol, "ping", json::object(), 2);
  REQUIRE(ping["result"].empty());

  const json toolList =
      ProtocolRequest(protocol, "tools/list", json::object(), 3);
  REQUIRE(toolList["result"]["tools"].size() == 33);
  REQUIRE(toolList["result"]["tools"][0]["name"] == "editor_search");
  const json *createObjectTool = nullptr;
  const json *listSchemaTool = nullptr;
  const json *getSchemaTool = nullptr;
  for (const json &tool : toolList["result"]["tools"]) {
    if (tool.value("name", std::string()) == "editor_create_object") {
      createObjectTool = &tool;
    }
    if (tool.value("name", std::string()) == "editor_list_schema_types")
      listSchemaTool = &tool;
    if (tool.value("name", std::string()) == "editor_get_schema")
      getSchemaTool = &tool;
  }
  REQUIRE(createObjectTool != nullptr);
  REQUIRE((*createObjectTool)["inputSchema"]["properties"]["position"]["items"]
                             ["type"] == "number");
  REQUIRE((*createObjectTool)["inputSchema"]["properties"]["position"]
                             ["minItems"] == 3);
  REQUIRE((*createObjectTool)["inputSchema"]["properties"]["mode"]["enum"][0] ==
          "preview");
  REQUIRE(listSchemaTool != nullptr);
  REQUIRE(
      (*listSchemaTool)["inputSchema"]["properties"]["kind"]["enum"].size() ==
      3);
  REQUIRE(getSchemaTool != nullptr);
  REQUIRE((*getSchemaTool)["inputSchema"]["required"][0] == "name");
  const json *deleteTool = nullptr;
  for (const json &tool : toolList["result"]["tools"]) {
    if (tool.value("name", std::string()) == "editor_delete") {
      deleteTool = &tool;
      break;
    }
  }
  REQUIRE(deleteTool != nullptr);
  REQUIRE((*deleteTool)["inputSchema"]["properties"]["previewToken"]["type"] ==
          "string");

  const json resourceList =
      ProtocolRequest(protocol, "resources/list", json::object(), 4);
  REQUIRE(resourceList["result"]["resources"].size() == 11);

  const std::vector<std::string> resourceUris = {
      "scene://summary",    "scene://selection", "scene://assets",
      "scene://hierarchy",  "scene://objects",   "scene://scene_status",
      "assets://selection", "assets://catalog",  "console://recent",
      "console://summary",  "build://status",
  };
  for (size_t i = 0; i < resourceUris.size(); ++i) {
    const json response = ReadResource(protocol, resourceUris[i],
                                       json{{"limit", 2}, {"query", "hero"}},
                                       10 + static_cast<int>(i));
    REQUIRE(response["result"]["contents"][0]["uri"] == resourceUris[i]);
    REQUIRE_FALSE(
        response["result"]["contents"][0]["text"].get<std::string>().empty());
  }

  const json resourceObjects =
      ReadResource(protocol, "scene://objects",
                   json{{"limit", 1}, {"offset", 1}, {"type", "Prop"}}, 30);
  const json resourceObjectsPayload = json::parse(
      resourceObjects["result"]["contents"][0]["text"].get<std::string>());
  REQUIRE(resourceObjectsPayload["offset"] == 1);
  REQUIRE(resourceObjectsPayload["returned"] == 1);
  REQUIRE(resourceObjectsPayload["objects"][0]["id"] == "obj_child");

  const json resourceConsole = ReadResource(
      protocol, "console://recent", json{{"limit", 2}, {"offset", 1}}, 31);
  const json resourceConsolePayload = json::parse(
      resourceConsole["result"]["contents"][0]["text"].get<std::string>());
  REQUIRE(resourceConsolePayload["offset"] == 1);
  REQUIRE(resourceConsolePayload["returned"] == 2);
  REQUIRE(resourceConsolePayload["hasMore"].get<bool>());

  const json resourceHierarchy = ReadResource(
      protocol, "scene://hierarchy", json{{"limit", 2}, {"offset", 1}}, 32);
  const json resourceHierarchyPayload = json::parse(
      resourceHierarchy["result"]["contents"][0]["text"].get<std::string>());
  REQUIRE(resourceHierarchyPayload["offset"] == 1);
  REQUIRE(resourceHierarchyPayload["returned"] == 2);
  REQUIRE(resourceHierarchyPayload["hasMore"].get<bool>());

  const json resourceCatalog = ReadResource(
      protocol, "assets://catalog", json{{"limit", 1}, {"offset", 1}}, 33);
  const json resourceCatalogPayload = json::parse(
      resourceCatalog["result"]["contents"][0]["text"].get<std::string>());
  REQUIRE(resourceCatalogPayload["offset"] == 1);
  REQUIRE(resourceCatalogPayload["returned"] == 1);
  REQUIRE(resourceCatalogPayload["hasMore"].get<bool>());

  const json getObject =
      CallTool(protocol, "editor.get_object", json{{"id", "obj_root"}}, 100);
  REQUIRE(getObject["result"]["structuredContent"]["id"] == "obj_root");

  const json getObjectSanitized =
      CallTool(protocol, "editor_get_object", json{{"id", "obj_root"}}, 101);
  REQUIRE(getObjectSanitized["result"]["structuredContent"]["id"] ==
          "obj_root");

  const json getObjectEdges = CallTool(protocol, "editor.get_object_edges",
                                       json{{"id", "obj_root"}}, 102);
  REQUIRE(
      getObjectEdges["result"]["structuredContent"]["worldCorners"].size() ==
      8);

  const json listObjects = CallTool(
      protocol, "editor.list_objects",
      json{{"type", "Prop"}, {"query", "obj"}, {"selectedOnly", false}}, 103);
  REQUIRE(listObjects["result"]["structuredContent"]["matchedObjects"] == 2);

  const json getObjects = CallTool(
      protocol, "editor.get_objects",
      json{{"ids", json::array({"obj_root", "obj_child", "missing"})}}, 104);
  REQUIRE(getObjects["result"]["structuredContent"]["objects"].size() == 2);

  const json getChildren = CallTool(protocol, "editor.get_object_children",
                                    json{{"id", "obj_root"}}, 105);
  REQUIRE(getChildren["result"]["structuredContent"]["childCount"] == 2);

  const json getParent = CallTool(protocol, "editor.get_object_parent",
                                  json{{"id", "obj_child"}}, 106);
  REQUIRE(getParent["result"]["structuredContent"]["parentId"] == "obj_root");
  REQUIRE(getParent["result"]["structuredContent"]["parent"]["id"] ==
          "obj_root");

  const json countObjects =
      CallTool(protocol, "editor.count_objects",
               json{{"type", "Prop"}, {"query", "obj"}}, 107);
  REQUIRE(countObjects["result"]["structuredContent"]["count"] == 2);

  const json search =
      CallTool(protocol, "editor_search",
               json{{"query", "hero"}, {"limit", 5}, {"scope", "all"}}, 108);
  REQUIRE(search["result"]["structuredContent"]["assets"].size() == 1);

  const json listAssets = CallTool(protocol, "editor.list_assets",
                                   json{{"query", ".obj"}, {"limit", 5}}, 109);
  REQUIRE(listAssets["result"]["structuredContent"]["matchedAssets"] == 2);

  const json getAsset =
      CallTool(protocol, "editor.get_asset", json{{"id", "crate"}}, 110);
  REQUIRE(getAsset["result"]["structuredContent"]["id"] == "crate");
  REQUIRE(getAsset["result"]["structuredContent"]["objectReferenceCount"] == 1);

  const json searchAssets =
      CallTool(protocol, "editor.search_assets",
               json{{"query", "hero"}, {"limit", 5}}, 111);
  REQUIRE(searchAssets["result"]["structuredContent"]["matchedAssets"] == 1);

  const json countAssets =
      CallTool(protocol, "editor.count_assets", json{{"query", ".png"}}, 112);
  REQUIRE(countAssets["result"]["structuredContent"]["count"] == 2);

  const json sceneStatus =
      CallTool(protocol, "editor.scene_status", json::object(), 113);
  REQUIRE(sceneStatus["result"]["structuredContent"]["sceneId"] ==
          "scene_main");

  const json sceneFile =
      CallTool(protocol, "editor.get_scene_file", json::object(), 114);
  REQUIRE(sceneFile["result"]["structuredContent"]["filePath"] ==
          "assets/scenes/main_scene.json");
  REQUIRE(sceneFile["result"]["structuredContent"]["dirty"].get<bool>());

  const json searchConsole =
      CallTool(protocol, "editor.search_console",
               json{{"query", "exploded"}, {"limit", 5}}, 115);
  REQUIRE(searchConsole["result"]["structuredContent"]["matchedLines"] == 1);

  const json listSchema = CallTool(protocol, "editor.list_schema_types",
                                   json{{"kind", "component"}}, 116);
  REQUIRE(listSchema["result"]["structuredContent"]["kind"] == "component");
  REQUIRE(listSchema["result"]["structuredContent"]["entryCount"] == 3);

  const json getSchema =
      CallTool(protocol, "editor_get_schema",
               json{{"name", "light"}, {"kind", "component"}}, 117);
  REQUIRE(getSchema["result"]["structuredContent"]["name"] == "light");
  REQUIRE(getSchema["result"]["structuredContent"]["fields"].size() == 4);
  REQUIRE(getSchema["result"]["structuredContent"]["fields"][0]["options"][1] ==
          "directional");

  REQUIRE(activity.size() >= 22);
  REQUIRE(activity.back().target == "editor.get_schema");
  REQUIRE(activity.back().ok);
}

TEST_CASE("Editor MCP commands preserve reserved ids and reload from disk", "[mcp][editor]") {
  EnvGuard env("horo_editor_mcp_consistency");
  const std::filesystem::path scenePath = Horo::Tests::SecureTempBase() /
                                          "horo_editor_mcp_consistency" /
                                          "world.json";
  std::filesystem::create_directories(scenePath.parent_path());

  const SceneDocument initialDoc = MakeEditorSceneDocument(scenePath);
  SceneSerializer::SaveToFile(initialDoc, scenePath.string());

  EditorLayer editor;
  editor.LoadDocument(initialDoc);

  const McpCommandResult stringVecFail = editor.ExecuteMcpCommand(
      "editor.create_object", json{{"id", "codex_probe_panel"},
                                   {"type", "Panel"},
                                   {"position", json::array({"12", "1", "12"})},
                                   {"scale", json::array({"1", "1", "1"})},
                                   {"props", json{{"material", "default"}}}});
  REQUIRE_FALSE(stringVecFail.ok);
  REQUIRE(stringVecFail.error == "position must be [x,y,z].");

  const McpCommandResult createPanel = editor.ExecuteMcpCommand(
      "editor.create_object", json{{"id", "codex_probe_panel"},
                                   {"type", "Panel"},
                                   {"position", json::array({12, 1, 12})},
                                   {"scale", json::array({1, 1, 1})},
                                   {"props", json{{"material", "default"}}}});
  REQUIRE(createPanel.ok);

  const McpCommandResult createProp =
      editor.ExecuteMcpCommand("editor.create_object_from_asset",
                               json{{"id", "codex_probe_prop"},
                                    {"assetId", "stone"},
                                    {"position", json::array({13, 1, 13})}});
  REQUIRE(createProp.ok);

  const McpCommandResult reparent = editor.ExecuteMcpCommand(
      "editor.reparent_object",
      json{{"id", "codex_probe_prop"}, {"parentId", "codex_probe_panel"}});
  REQUIRE(reparent.ok);

  const McpCommandResult duplicate = editor.ExecuteMcpCommand(
      "editor.duplicate", json{{"id", "codex_probe_prop"}, {"count", 1}});
  REQUIRE(duplicate.ok);
  REQUIRE(duplicate.data["duplicates"].size() == 1);
  REQUIRE(duplicate.data["duplicates"][0]["id"] != "obj_000");

  const std::string duplicateId =
      duplicate.data["duplicates"][0]["id"].get<std::string>();

  const McpCommandResult renameReserved = editor.ExecuteMcpCommand(
      "editor.rename_object", json{{"id", duplicateId}, {"newId", "obj_000"}});
  REQUIRE_FALSE(renameReserved.ok);
  REQUIRE(renameReserved.error == "Object id already exists.");

  const McpCommandResult renameOk = editor.ExecuteMcpCommand(
      "editor.rename_object",
      json{{"id", duplicateId}, {"newId", "codex_probe_dup"}});
  REQUIRE(renameOk.ok);

  const SceneDocument &afterRenameDoc = editor.GetDocument();
  const SceneObject *camera = FindSceneObject(&afterRenameDoc, "cam_000");
  REQUIRE(camera != nullptr);
  REQUIRE(camera->props.at("parentId") == "obj_000");

  const McpCommandResult select = editor.ExecuteMcpCommand(
      "editor.select",
      json{{"ids", json::array({"codex_probe_panel", "codex_probe_prop"})}});
  REQUIRE(select.ok);
  REQUIRE(select.data["selectedObjectIds"].size() == 2);

  const McpEditorSnapshot selectedSnapshot = MakeSnapshotFromDocument(
      editor.GetDocument(), editor.GetSelectedObjectIds(),
      editor.GetSelectedAssetId());
  const json selectedOnly =
      BuildObjectListJson(selectedSnapshot, 16, "", "", true);
  REQUIRE(selectedOnly["matchedObjects"] == 2);

  const McpCommandResult reload =
      editor.ExecuteMcpCommand("editor.reload_scene", json::object());
  REQUIRE(reload.ok);
  const SceneDocument &reloadedDoc = editor.GetDocument();
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
    std::ofstream mesh(projectRoot / "assets" / "models" / "stone" /
                       "stone.obj");
    mesh << "o stone\n";
  }
  {
    std::ofstream albedo(projectRoot / "assets" / "models" / "stone" /
                         "stone.png");
    albedo << "png";
  }
  ProjectRootGuard projectRootGuard(projectRoot);

  const fs::path scenePath = projectRoot / "assets" / "scenes" / "world.json";
  fs::create_directories(scenePath.parent_path());

  SceneDocument doc;
  doc.filePath = scenePath.string();
  doc.assets["stone"] = AssetDef{"assets/models/stone/stone.obj", "1,1,1",
                                 "assets/models/stone/stone.png"};
  SceneObject object;
  object.id = "prop_stone";
  object.type = SceneObjectType::Prop;
  object.assetId = "stone";
  doc.objects.push_back(object);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult removeAsset =
      editor.ExecuteMcpCommand("editor.delete_asset", json{{"id", "stone"}});
  REQUIRE(removeAsset.ok);
  REQUIRE(removeAsset.data["deletedAssetId"] == "stone");
  REQUIRE(removeAsset.data["clearedObjectReferences"] == 1);
  REQUIRE(removeAsset.data["deletedManagedFiles"].get<bool>());
  REQUIRE(
      NormalizePathForComparison(
          removeAsset.data["deletedAssetDirectory"].get<std::string>()) ==
      NormalizePathForComparison(projectRoot / "assets" / "models" / "stone"));
  REQUIRE(editor.GetDocument().assets.find("stone") ==
          editor.GetDocument().assets.end());
  REQUIRE(editor.GetDocument().objects[0].assetId.empty());
  REQUIRE_FALSE(fs::exists(projectRoot / "assets" / "models" / "stone"));
}

TEST_CASE("McpProtocol dispatches write tools and gates destructive apply behind previews", "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  std::vector<std::string> invoked;
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [&invoked](std::string_view toolName, const json &arguments) {
        invoked.emplace_back(toolName);
        if (toolName == "editor.delete_asset")
          return McpCommandResult{false, json::object(), "delete rejected"};
        return McpCommandResult{
            true, json{{"handledTool", toolName}, {"echo", arguments}}, {}};
      },
      {},
  });

  const std::vector<std::pair<std::string, json>> applyOnlyTools = {
      {"editor.select", json{{"id", "obj_root"}}},
      {"editor.clear_selection", json::object()},
      {"editor.create_object", json{{"type", "Prop"}, {"id", "spawned"}}},
      {"editor.create_object_from_asset",
       json{{"assetId", "crate"}, {"id", "spawned_from_asset"}}},
      {"editor.update_object",
       json{{"id", "obj_root"}, {"props", json{{"tag", "updated"}}}}},
      {"editor.transform",
       json{{"id", "obj_root"}, {"position", json::array({4, 5, 6})}}},
      {"editor.rename_object",
       json{{"id", "obj_root"}, {"newId", "obj_root_renamed"}}},
      {"editor.reparent_object",
       json{{"id", "obj_child"}, {"parentId", "obj_camera"}}},
      {"editor.duplicate", json{{"id", "obj_root"}, {"count", 2}}},
      {"editor.select_asset", json{{"id", "crate"}}},
      {"editor.update_asset",
       json{{"id", "crate"}, {"mesh", "assets/models/crate_v2.obj"}}},
      {"editor.save_scene", json::object()},
  };

  for (size_t i = 0; i < applyOnlyTools.size(); ++i) {
    const json response =
        CallTool(protocol, applyOnlyTools[i].first, applyOnlyTools[i].second,
                 300 + static_cast<int>(i));
    REQUIRE(response["result"]["structuredContent"]["handledTool"] ==
            applyOnlyTools[i].first);
    REQUIRE(response["result"]["structuredContent"]["echo"]["mode"] == "apply");
  }

  const std::vector<std::pair<std::string, json>> destructiveTools = {
      {"editor.delete", json{{"ids", json::array({"obj_light"})}}},
      {"editor.delete_asset", json{{"id", "hero"}}},
      {"editor.new_scene", json{{"sceneId", "fresh"}, {"sceneName", "Fresh"}}},
      {"editor.reload_scene", json::object()},
  };

  for (size_t i = 0; i < destructiveTools.size(); ++i) {
    json previewArguments = destructiveTools[i].second;
    previewArguments["mode"] = "preview";
    const json preview = CallTool(protocol, destructiveTools[i].first,
                                  previewArguments, 400 + static_cast<int>(i));
    REQUIRE(preview["result"]["structuredContent"]["mode"] == "preview");
    REQUIRE(preview["result"]["structuredContent"]["previewToken"].is_string());

    json applyArguments = destructiveTools[i].second;
    applyArguments["mode"] = "apply";
    applyArguments["previewToken"] =
        preview["result"]["structuredContent"]["previewToken"];
    const json response = CallTool(protocol, destructiveTools[i].first,
                                   applyArguments, 500 + static_cast<int>(i));
    if (destructiveTools[i].first == "editor.delete_asset") {
      REQUIRE(response["error"]["message"] == "delete rejected");
      continue;
    }
    REQUIRE(response["result"]["structuredContent"]["handledTool"] ==
            destructiveTools[i].first);
    REQUIRE(response["result"]["structuredContent"]["echo"]["mode"] == "apply");
    REQUIRE(response["result"]["structuredContent"]["echo"]["previewToken"]
                .is_string());
  }

  REQUIRE(invoked.size() == applyOnlyTools.size() + destructiveTools.size());
  REQUIRE(invoked.front() == "editor.select");
  REQUIRE(invoked.back() == "editor.reload_scene");
}

TEST_CASE("McpProtocol validates schema-backed mutations and preview tokens before queueing", "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  std::vector<std::string> invoked;
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [&invoked](const std::string &toolName, const json &) {
        invoked.push_back(toolName);
        return McpCommandResult{true, json{{"handledTool", toolName}}, {}};
      },
      {},
  });

  const json invalidEnum = CallTool(
      protocol, "editor.create_object",
      json{{"type", "Light"}, {"props", json{{"lightType", "spot"}}}}, 600);
  REQUIRE(invalidEnum["error"]["message"] ==
          "props.lightType must be one of: point, directional.");

  const json invalidComponentRange =
      CallTool(protocol, "editor.update_object",
               json{{"id", "obj_root"},
                    {"components",
                     json::array({json{{"type", "light"},
                                       {"props", json{{"radius", 0.1}}}}})}},
               601);
  REQUIRE(invalidComponentRange["error"]["message"] ==
          "components[0].props.radius must be >= 0.5000.");

  const json deleteWithoutPreview =
      CallTool(protocol, "editor.delete",
               json{{"ids", json::array({"obj_light"})}}, 602);
  REQUIRE(deleteWithoutPreview["error"]["message"] ==
          "previewToken is required for apply mode.");

  const json deletePreview = CallTool(
      protocol, "editor.delete",
      json{{"ids", json::array({"obj_light"})}, {"mode", "preview"}}, 603);
  REQUIRE(
      deletePreview["result"]["structuredContent"]["preview"]["deletedCount"] ==
      1);
  const std::string previewToken =
      deletePreview["result"]["structuredContent"]["previewToken"]
          .get<std::string>();

  const json deleteMismatchedApply =
      CallTool(protocol, "editor.delete",
               json{{"ids", json::array({"obj_root"})},
                    {"mode", "apply"},
                    {"previewToken", previewToken}},
               604);
  REQUIRE(deleteMismatchedApply["error"]["message"] ==
          "previewToken does not match the current scene state or arguments.");

  REQUIRE(invoked.empty());
}

TEST_CASE("McpProtocol writes apply audit records to project root and fallback settings path", "[mcp][protocol][audit]") {
  namespace fs = std::filesystem;

  EnvGuard env("horo_mcp_audit");
  const fs::path projectRoot = env.tempHome / "project";
  fs::create_directories(projectRoot / "assets");
  {
    std::ofstream presets(projectRoot / "CMakePresets.json");
    presets << "{}";
  }

  McpEditorSnapshot snapshot = MakeSnapshot();
  std::vector<std::string> invoked;
  ProjectRootGuard projectRootGuard(projectRoot);
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [&invoked](const std::string &toolName, const json &) {
        invoked.push_back(toolName);
        if (toolName == "editor.delete_asset")
          return McpCommandResult{false, json::object(), "delete rejected"};
        return McpCommandResult{true, json{{"handledTool", toolName}}, {}};
      },
      {},
  });

  const json deletePreview = CallTool(
      protocol, "editor.delete",
      json{{"ids", json::array({"obj_light"})}, {"mode", "preview"}}, 700);
  const std::string deleteToken =
      deletePreview["result"]["structuredContent"]["previewToken"]
          .get<std::string>();
  const json deleteApply = CallTool(protocol, "editor.delete",
                                    json{{"ids", json::array({"obj_light"})},
                                         {"mode", "apply"},
                                         {"previewToken", deleteToken}},
                                    701);
  REQUIRE(deleteApply["result"]["structuredContent"]["handledTool"] ==
          "editor.delete");

  const json deleteAssetPreview =
      CallTool(protocol, "editor.delete_asset",
               json{{"id", "hero"}, {"mode", "preview"}}, 702);
  const std::string deleteAssetToken =
      deleteAssetPreview["result"]["structuredContent"]["previewToken"]
          .get<std::string>();
  const json deleteAssetApply =
      CallTool(protocol, "editor.delete_asset",
               json{{"id", "hero"},
                    {"mode", "apply"},
                    {"previewToken", deleteAssetToken}},
               703);
  REQUIRE(deleteAssetApply["error"]["message"] == "delete rejected");

  const fs::path auditPath = projectRoot / ".horo" / "mcp-audit.jsonl";
  REQUIRE(fs::exists(auditPath));
  const std::vector<json> records = ReadJsonLines(auditPath);
  REQUIRE(records.size() == 2);
  REQUIRE(records[0]["tool"] == "editor.delete");
  REQUIRE(records[0]["mode"] == "apply");
  REQUIRE(records[0]["result"] == "success");
  REQUIRE(records[0]["previewToken"] == deleteToken);
  REQUIRE(records[0]["changedIds"][0] == "obj_light");
  REQUIRE(records[1]["tool"] == "editor.delete_asset");
  REQUIRE(records[1]["result"] == "error");
  REQUIRE(records[1]["error"] == "delete rejected");
  REQUIRE(records[1]["previewToken"] == deleteAssetToken);

  invoked.clear();
  ProjectRootGuard clearProjectRoot{std::filesystem::path()};
  const json selectApply =
      CallTool(protocol, "editor.select", json{{"id", "obj_root"}}, 704);
  REQUIRE(selectApply["result"]["structuredContent"]["handledTool"] ==
          "editor.select");

  const fs::path fallbackAuditPath =
      ResolveMcpSettingsDirectory() / "mcp-audit.jsonl";
  REQUIRE(fs::exists(fallbackAuditPath));
  const std::vector<json> fallbackRecords = ReadJsonLines(fallbackAuditPath);
  REQUIRE(fallbackRecords.size() == 1);
  REQUIRE(fallbackRecords[0]["tool"] == "editor.select");
  REQUIRE(fallbackRecords[0]["result"] == "success");
}

TEST_CASE("McpProtocol supports the recommended inspect to audit workflow", "[mcp][protocol][workflow]") {
  namespace fs = std::filesystem;

  EnvGuard env("horo_mcp_workflow");
  const fs::path projectRoot = env.tempHome / "project";
  fs::create_directories(projectRoot / "assets");
  {
    std::ofstream presets(projectRoot / "CMakePresets.json");
    presets << "{}";
  }

  McpEditorSnapshot snapshot = MakeSnapshot();
  ProjectRootGuard projectRootGuard(projectRoot);
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [](std::string_view toolName, const json &arguments) {
        if (toolName == "editor.delete") {
          return McpCommandResult{
              true,
              json{{"deletedCount",
                    arguments.contains("ids") ? arguments["ids"].size() : 0}},
              {}};
        }
        return McpCommandResult{true, json{{"handledTool", toolName}}, {}};
      },
      {},
  });

  const json sceneSummary =
      ReadResource(protocol, "scene://summary", json{{"limit", 2}}, 800);
  const json sceneSummaryPayload = json::parse(
      sceneSummary["result"]["contents"][0]["text"].get<std::string>());
  REQUIRE(sceneSummaryPayload["objectCount"] == 4);

  const json search = CallTool(protocol, "editor.search",
                               json{{"query", "hero"}, {"limit", 3}}, 801);
  REQUIRE(search["result"]["structuredContent"]["assets"].size() == 1);

  const json schema =
      CallTool(protocol, "editor.get_schema",
               json{{"name", "light"}, {"kind", "component"}}, 802);
  REQUIRE(schema["result"]["structuredContent"]["fields"].size() == 4);

  const json preview = CallTool(
      protocol, "editor.delete",
      json{{"ids", json::array({"obj_light"})}, {"mode", "preview"}}, 803);
  REQUIRE(preview["result"]["structuredContent"]["preview"]["deletedCount"] ==
          1);
  const std::string previewToken =
      preview["result"]["structuredContent"]["previewToken"].get<std::string>();

  const json apply = CallTool(protocol, "editor.delete",
                              json{{"ids", json::array({"obj_light"})},
                                   {"mode", "apply"},
                                   {"previewToken", previewToken}},
                              804);
  REQUIRE(apply["result"]["structuredContent"]["deletedCount"] == 1);

  const fs::path auditPath = projectRoot / ".horo" / "mcp-audit.jsonl";
  REQUIRE(fs::exists(auditPath));
  const std::vector<json> records = ReadJsonLines(auditPath);
  REQUIRE(records.size() == 1);
  REQUIRE(records[0]["tool"] == "editor.delete");
  REQUIRE(records[0]["previewToken"] == previewToken);
  REQUIRE(records[0]["result"] == "success");
}

TEST_CASE("McpProtocol returns expected errors for unsupported or unavailable paths", "[mcp][protocol][coverage]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  McpProtocol protocolWithSnapshot(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      nullptr,
      {},
  });

  const McpHttpResponse unknownEndpoint = protocolWithSnapshot.HandleHttp(
      McpHttpRequest{"POST", "/nope", {}, "{}"});
  REQUIRE(unknownEndpoint.statusCode == 404);

  const McpHttpResponse getInfo =
      protocolWithSnapshot.HandleHttp(McpHttpRequest{"GET", "/mcp", {}, {}});
  REQUIRE(getInfo.statusCode == 200);

  const McpHttpResponse badMethod =
      protocolWithSnapshot.HandleHttp(McpHttpRequest{"PUT", "/mcp", {}, {}});
  REQUIRE(badMethod.statusCode == 405);

  const McpHttpResponse badJson =
      protocolWithSnapshot.HandleHttp(McpHttpRequest{"POST", "/mcp", {}, "{"});
  REQUIRE(badJson.statusCode == 400);

  const json unknownMethod = ProtocolRequest(
      protocolWithSnapshot, "bogus/method", json::object(), 400);
  REQUIRE(unknownMethod["id"] == 400);
  REQUIRE(unknownMethod["error"].is_object());
  REQUIRE(unknownMethod["error"]["code"] == -32601);

  const json unknownResource = ReadResource(
      protocolWithSnapshot, "scene://missing", json::object(), 401);
  REQUIRE(unknownResource["error"]["message"] == "Unknown resource URI.");

  const json unknownTool =
      CallTool(protocolWithSnapshot, "editor.missing", json::object(), 402);
  REQUIRE(unknownTool["error"]["message"] == "Unknown tool.");

  const json missingSchema = CallTool(protocolWithSnapshot, "editor.get_schema",
                                      json{{"name", "missing"}}, 4021);
  REQUIRE(missingSchema["error"]["message"] == "Schema not found.");

  const json objectNotFound = CallTool(
      protocolWithSnapshot, "editor.get_object", json{{"id", "missing"}}, 403);
  REQUIRE(objectNotFound["error"]["message"] == "Object not found.");

  const json assetNotFound = CallTool(protocolWithSnapshot, "editor.get_asset",
                                      json{{"id", "missing"}}, 404);
  REQUIRE(assetNotFound["error"]["message"] == "Asset not found.");

  McpProtocol noSnapshot(McpProtocolContext{});
  const McpHttpResponse noSnapshotResponse =
      noSnapshot.HandleHttp(McpHttpRequest{
          "POST",
          "/mcp",
          {},
          json{{"jsonrpc", "2.0"},
               {"id", 405},
               {"method", "tools/call"},
               {"params",
                {{"name", "editor.search"}, {"arguments", json::object()}}}}
              .dump()});
  REQUIRE(noSnapshotResponse.statusCode == 503);

  const McpHttpResponse queueUnavailable = protocolWithSnapshot.HandleHttp(
      McpHttpRequest{"POST",
                     "/mcp",
                     {},
                     json{{"jsonrpc", "2.0"},
                          {"id", 406},
                          {"method", "tools/call"},
                          {"params",
                           {{"name", "editor.select"},
                            {"arguments", json{{"id", "obj_root"}}}}}}
                         .dump()});
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

  const HttpResponse ping = SendHttpPost(
      settings.port, json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ping"}});
  REQUIRE(ping.statusCode == 200);

  const HttpResponse sceneStatus = SendHttpPost(
      settings.port,
      json{{"jsonrpc", "2.0"},
           {"id", 2},
           {"method", "tools/call"},
           {"params",
            {{"name", "editor.scene_status"}, {"arguments", json::object()}}}});
  REQUIRE(sceneStatus.statusCode == 200);
  REQUIRE(json::parse(
              sceneStatus.body)["result"]["structuredContent"]["objectCount"] ==
          4);

  std::string selectedId;
  std::promise<HttpResponse> selectPromise;
  std::future<HttpResponse> selectFuture = selectPromise.get_future();
  std::thread selectThread(
      [&settings, promise = std::move(selectPromise)]() mutable {
        promise.set_value(SendHttpPost(
            settings.port, json{{"jsonrpc", "2.0"},
                                {"id", 3},
                                {"method", "tools/call"},
                                {"params",
                                 {{"name", "editor.select"},
                                  {"arguments", json{{"id", "obj_root"}}}}}}));
      });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const size_t drained = controller.DrainCommands(
      [&selectedId](std::string_view toolName, const json &arguments) {
        REQUIRE(toolName == "editor.select");
        selectedId = arguments["id"].get<std::string>();
        return McpCommandResult{
            true, json{{"selectedObjectIds", json::array({selectedId})}}, {}};
      });
  REQUIRE(drained == 1);

  const HttpResponse selectResponse = selectFuture.get();
  REQUIRE(selectResponse.statusCode == 200);
  REQUIRE(json::parse(selectResponse.body)["result"]["structuredContent"]
                                          ["selectedObjectIds"][0] ==
          "obj_root");
  if (selectThread.joinable())
    selectThread.join();

  const McpStatusSnapshot status = controller.GetStatusSnapshot();
  REQUIRE(status.running);
  REQUIRE(status.toolCount == 33);
  REQUIRE(status.resourceCount == 11);
  REQUIRE(status.totalRequests >= 3);

  controller.SetEditorActive(false);
}

TEST_CASE("McpProtocol accepts underscore aliases for tool names", "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      nullptr,
      {},
  });

  const json canonical =
      CallTool(protocol, "editor.search", json{{"query", "obj"}}, 910);
  const json alias =
      CallTool(protocol, "editor_search", json{{"query", "obj"}}, 911);

  REQUIRE(canonical.contains("result"));
  REQUIRE(alias.contains("result"));
  REQUIRE(alias["result"]["structuredContent"] ==
          canonical["result"]["structuredContent"]);
}

// ===========================================================================
// McpSettings — edge cases: default values, sanitization, null save, home dir
// ===========================================================================

TEST_CASE("McpSettings: DefaultMcpSettings returns expected defaults", "[mcp][settings]") {
  const McpSettings s = DefaultMcpSettings();
  REQUIRE(s.enabled == false);
  REQUIRE(s.transport == std::string(kDefaultMcpTransport));
  REQUIRE(s.host == std::string(kDefaultMcpHost));
  REQUIRE(s.port == kDefaultMcpPort);
  REQUIRE(s.autoStart == true);
}

TEST_CASE("McpSettings: SanitizePort clamps out-of-range values via SaveMcpSettings", "[mcp][settings]") {
  EnvGuard env("horo_mcp_sanitize_port");
  McpSettingsDocument doc;
  doc.settings = DefaultMcpSettings();

  // Out-of-range port — SaveMcpSettings should clamp it
  doc.settings.port = 0;
  std::string err;
  SaveMcpSettings(&doc, &err);
  REQUIRE(doc.settings.port == kDefaultMcpPort);

  doc.settings.port = 99999;
  SaveMcpSettings(&doc, &err);
  REQUIRE(doc.settings.port == kDefaultMcpPort);

  // Valid port should be preserved
  doc.settings.port = 3000;
  SaveMcpSettings(&doc, &err);
  REQUIRE(doc.settings.port == 3000);
}

TEST_CASE("McpSettings: SanitizeHost enforces localhost via SaveMcpSettings", "[mcp][settings]") {
  EnvGuard env("horo_mcp_sanitize_host");
  McpSettingsDocument doc;
  doc.settings = DefaultMcpSettings();

  doc.settings.host = "evil.host.com";
  std::string err;
  SaveMcpSettings(&doc, &err);
  REQUIRE(doc.settings.host == std::string(kDefaultMcpHost));
}

TEST_CASE("McpSettings: LoadMcpSettings handles bad JSON gracefully", "[mcp][settings]") {
  EnvGuard env("horo_mcp_bad_json");
  const auto settingsDir = env.tempHome / ".horo";
  std::filesystem::create_directories(settingsDir);
  std::ofstream(settingsDir / "settings.json") << "{not valid json!!!";

  const McpSettingsDocument doc = LoadMcpSettings();
  REQUIRE(doc.loadedFromDisk);
  REQUIRE(doc.parseError);
  REQUIRE_FALSE(doc.error.empty());
}

TEST_CASE("McpSettings: LoadMcpSettings handles non-object JSON root", "[mcp][settings]") {
  EnvGuard env("horo_mcp_nonobj_root");
  const auto settingsDir = env.tempHome / ".horo";
  std::filesystem::create_directories(settingsDir);
  std::ofstream(settingsDir / "settings.json") << "[1,2,3]";

  const McpSettingsDocument doc = LoadMcpSettings();
  REQUIRE(doc.loadedFromDisk);
  REQUIRE(doc.parseError);
}

TEST_CASE("McpSettings: SaveMcpSettings nullptr returns false with error", "[mcp][settings]") {
  std::string err;
  REQUIRE_FALSE(SaveMcpSettings(nullptr, &err));
  REQUIRE_FALSE(err.empty());
}

TEST_CASE("McpSettings: SaveMcpSettings nullptr outError does not crash", "[mcp][settings]") {
  REQUIRE_FALSE(SaveMcpSettings(nullptr, nullptr));
}

TEST_CASE("McpSettings: ResolveMcpHomeDirectory returns a non-empty path", "[mcp][settings]") {
  const auto p = ResolveMcpHomeDirectory();
  REQUIRE_FALSE(p.empty());
}

TEST_CASE("McpSettings: ResolveMcpHomeDirectory uses current_path when HOME unset", "[mcp][settings]") {
  EnvGuard env("horo_mcp_nohome");
  // Unset HOME / USERPROFILE so the fallback branch is taken
#ifdef _WIN32
  _putenv_s("USERPROFILE", "");
  _putenv_s("HOMEDRIVE", "");
  _putenv_s("HOMEPATH", "");
#else
  unsetenv("HOME");
#endif
  const auto p = ResolveMcpHomeDirectory();
  REQUIRE_FALSE(p.empty());
  // Restore via EnvGuard destructor
}

// ===========================================================================
// McpSnapshot — CloneSnapshot, Find helpers, Build helpers
// ===========================================================================

TEST_CASE("McpSnapshot: CloneSnapshot produces independent copy", "[mcp][snapshot]") {
  McpEditorSnapshot original = MakeSnapshot();
  const auto clonePtr = CloneSnapshot(original);
  REQUIRE(clonePtr != nullptr);
  REQUIRE(clonePtr->objects.size() == original.objects.size());
  REQUIRE(clonePtr->assets.size() == original.assets.size());

  // Mutating the original must not affect the clone
  const size_t origObjCount = original.objects.size();
  original.objects.clear();
  REQUIRE(clonePtr->objects.size() == origObjCount);
}

TEST_CASE("McpSnapshot: FindObjectById returns nullptr for unknown id", "[mcp][snapshot]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  REQUIRE(FindObjectById(snapshot, "does_not_exist") == nullptr);
}

TEST_CASE("McpSnapshot: FindObjectById returns correct object", "[mcp][snapshot]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  REQUIRE_FALSE(snapshot.objects.empty());
  const std::string firstId = snapshot.objects[0].id;
  const McpObjectSnapshot *obj = FindObjectById(snapshot, firstId);
  REQUIRE(obj != nullptr);
  REQUIRE(obj->id == firstId);
}

TEST_CASE("McpSnapshot: FindAssetById returns nullptr for unknown id", "[mcp][snapshot]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  REQUIRE(FindAssetById(snapshot, "ghost_asset") == nullptr);
}

TEST_CASE("McpSnapshot: FindAssetById returns correct asset", "[mcp][snapshot]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  REQUIRE_FALSE(snapshot.assets.empty());
  const std::string firstId = snapshot.assets[0].id;
  const McpAssetSnapshot *asset = FindAssetById(snapshot, firstId);
  REQUIRE(asset != nullptr);
  REQUIRE(asset->id == firstId);
}

TEST_CASE("McpSnapshot: BuildBuildStatusJson with no issues", "[mcp][snapshot]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  snapshot.build.issues.clear();
  snapshot.build.status = "idle";
  const json status = BuildBuildStatusJson(snapshot);
  REQUIRE(status.contains("status"));
}

TEST_CASE("McpSnapshot: BuildConsoleJson with non-zero offset trims lines", "[mcp][snapshot]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  // Add several console entries
  snapshot.consoleEntries.clear();
  for (int i = 0; i < 5; ++i) {
    McpConsoleEntry entry;
    entry.level = "info";
    entry.message = std::format("msg{}", i);
    snapshot.consoleEntries.emplace_back(std::move(entry));
  }
  const json full = BuildConsoleJson(snapshot, 10, 0);
  const json offset = BuildConsoleJson(snapshot, 10, 3);
  REQUIRE(offset["lines"].size() < full["lines"].size());
}

TEST_CASE("McpSnapshot: SearchConsoleSnapshot returns matching lines", "[mcp][snapshot]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  snapshot.consoleEntries.clear();
  McpConsoleEntry err;
  err.level = "error";
  err.message = "shader compile failed";
  McpConsoleEntry info;
  info.level = "info";
  info.message = "scene loaded";
  snapshot.consoleEntries.push_back(err);
  snapshot.consoleEntries.push_back(info);
  const json result = SearchConsoleSnapshot(snapshot, "shader", 5);
  REQUIRE(result["matchedLines"] >= 1);
}

TEST_CASE("McpSnapshot: SearchAssetsSnapshot returns matching assets", "[mcp][snapshot]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  const json result = SearchAssetsSnapshot(snapshot, "crate", 5);
  REQUIRE(result["matchedAssets"] >= 1);
  REQUIRE(result["assets"][0]["id"] == "crate");
}

TEST_CASE("McpSnapshot: BuildSelectionJson with empty selection has no asset key", "[mcp][snapshot]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  snapshot.selectedObjectIds.clear();
  snapshot.selectedAssetId.clear();
  const json sel = BuildSelectionJson(snapshot);
  REQUIRE(sel["objects"].empty());
  REQUIRE_FALSE(sel.contains("asset"));
}

// ===========================================================================
// McpController — config snippets, ClearActivityLog, SettingsDocument
// ===========================================================================

TEST_CASE("McpController: BuildClaudeConfigSnippet returns non-empty JSON", "[mcp][controller]") {
  EnvGuard env("horo_mcp_claude_snippet");
  McpController controller;
  const std::string snippet = controller.BuildClaudeConfigSnippet();
  REQUIRE_FALSE(snippet.empty());
  const json j = json::parse(snippet);
  REQUIRE(j.contains("mcpServers"));
}

TEST_CASE("McpController: BuildCodexConfigSnippet returns non-empty config", "[mcp][controller]") {
  EnvGuard env("horo_mcp_codex_snippet");
  McpController controller;
  const std::string snippet = controller.BuildCodexConfigSnippet();
  REQUIRE_FALSE(snippet.empty());
  // Codex uses TOML format, not JSON
  REQUIRE(snippet.find("horo_engine") != std::string::npos);
  REQUIRE(snippet.find("url") != std::string::npos);
}

TEST_CASE("McpController: BuildVsCodeConfigSnippet returns non-empty JSON", "[mcp][controller]") {
  EnvGuard env("horo_mcp_vscode_snippet");
  McpController controller;
  const std::string snippet = controller.BuildVsCodeConfigSnippet();
  REQUIRE_FALSE(snippet.empty());
  const json j = json::parse(snippet);
  REQUIRE(j.is_object());
}

TEST_CASE("McpController: ClearActivityLog does not crash", "[mcp][controller]") {
  EnvGuard env("horo_mcp_clear_log");
  McpController controller;
  REQUIRE_NOTHROW(controller.ClearActivityLog());
  REQUIRE_NOTHROW(controller.ClearActivityLog()); // idempotent
}

TEST_CASE("McpController: SettingsDocument reflects defaults before Initialize", "[mcp][controller]") {
  EnvGuard env("horo_mcp_settings_doc");
  McpController controller;
  const McpSettingsDocument &doc = controller.SettingsDocument();
  // Before Initialize, settings should hold default values (enabled=false)
  REQUIRE_FALSE(doc.settings.enabled);
}

TEST_CASE("McpController: Initialize is idempotent when not started", "[mcp][controller]") {
  EnvGuard env("horo_mcp_idempotent_init");
  McpController controller;
  REQUIRE_NOTHROW(controller.Initialize());
  REQUIRE_NOTHROW(controller.Initialize()); // second call must not crash
}

// ===========================================================================
// McpProtocol — additional field-validation and protocol paths
// ===========================================================================

TEST_CASE("McpProtocol: notifications are accepted with 202 and no body", "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); }, nullptr, {}});

  const McpHttpResponse resp = protocol.HandleHttp(McpHttpRequest{
      "POST",
      "/mcp",
      {},
      json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}
          .dump()});
  REQUIRE(resp.statusCode == 202);
  REQUIRE(resp.body.empty());
}

TEST_CASE("McpProtocol: field validation rejects bad enum, float, bool and color values", "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [](const std::string &toolName, const json &) {
        return McpCommandResult{true, json{{"handledTool", toolName}}, {}};
      },
      {}});

  // NormalizeEnumField: value is a number, not a string
  const json enumNotString = CallTool(
      protocol, "editor.create_object",
      json{{"type", "Light"}, {"props", json{{"lightType", 99}}}}, 700);
  REQUIRE(enumNotString["error"]["message"] ==
          "props.lightType must be a string.");

  // NormalizeFloatField: value is an array (neither number nor string)
  const json floatBadType =
      CallTool(protocol, "editor.create_object",
               json{{"type", "Light"},
                    {"props", json{{"lightType", "point"},
                                   {"radius", json::array({1, 2, 3})}}}},
               701);
  REQUIRE(floatBadType["error"]["message"] == "props.radius must be a number.");

  // NormalizeFloatField: value string that cannot be parsed as float
  const json floatBadString = CallTool(
      protocol, "editor.create_object",
      json{{"type", "Light"},
           {"props", json{{"lightType", "point"}, {"radius", "notanumber"}}}},
      702);
  REQUIRE(floatBadString["error"]["message"] ==
          "props.radius must be a number.");

  // NormalizeFloatField: value exceeds maximum (radius > 50)
  const json floatOverMax =
      CallTool(protocol, "editor.create_object",
               json{{"type", "Light"},
                    {"props", json{{"lightType", "point"}, {"radius", 200}}}},
               703);
  REQUIRE(floatOverMax["error"]["message"].get<std::string>().find(
              "must be <=") != std::string::npos);

  // NormalizeFloatField: string value that IS a valid float
  const json floatFromString =
      CallTool(protocol, "editor.create_object",
               json{{"type", "Light"},
                    {"id", "new_light_str"},
                    {"props", json{{"lightType", "point"}, {"radius", "5.0"}}}},
               704);
  REQUIRE(floatFromString["result"]["structuredContent"]["handledTool"] ==
          "editor.create_object");

  // NormalizeBoolField: string "true" (string path)
  const json boolFromString = CallTool(
      protocol, "editor.update_object",
      json{{"id", "obj_root"},
           {"components",
            json::array({json{{"type", "rigidbody"},
                              {"props", json{{"isKinematic", "true"}}}}})}},
      705);
  REQUIRE(boolFromString["result"]["structuredContent"]["handledTool"] ==
          "editor.update_object");

  // NormalizeBoolField: numeric (neither bool nor string)
  const json boolBadType = CallTool(
      protocol, "editor.update_object",
      json{{"id", "obj_root"},
           {"components",
            json::array({json{{"type", "rigidbody"},
                              {"props", json{{"isKinematic", 123}}}}})}},
      706);
  REQUIRE(boolBadType["error"]["message"] ==
          "components[0].props.isKinematic must be a boolean.");

  // NormalizeColor3Value: valid [r,g,b] array
  const json colorArray =
      CallTool(protocol, "editor.create_object",
               json{{"type", "Light"},
                    {"id", "new_light_col"},
                    {"props", json{{"lightType", "point"},
                                   {"color", json::array({1.0, 0.5, 0.0})}}}},
               707);
  REQUIRE(colorArray["result"]["structuredContent"]["handledTool"] ==
          "editor.create_object");

  // NormalizeColor3Value: string that cannot be parsed as color
  const json colorBadString = CallTool(
      protocol, "editor.create_object",
      json{{"type", "Light"},
           {"props", json{{"lightType", "point"}, {"color", "notacolor"}}}},
      708);
  REQUIRE(colorBadString["error"]["message"].get<std::string>().find(
              "color3") != std::string::npos);
}

TEST_CASE("McpProtocol: ParseMutationMode rejects non-string and unknown mode values", "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [](const std::string &toolName, const json &) {
        return McpCommandResult{true, json{{"handledTool", toolName}}, {}};
      },
      {}});

  // mode is a number (not a string)
  const json modeNumber = CallTool(
      protocol, "editor.select", json{{"id", "obj_root"}, {"mode", 123}}, 710);
  REQUIRE(modeNumber["error"]["message"] ==
          "mode must be \"preview\" or \"apply\".");

  // mode is an unrecognised string
  const json modeInvalid =
      CallTool(protocol, "editor.select",
               json{{"id", "obj_root"}, {"mode", "execute"}}, 711);
  REQUIRE(modeInvalid["error"]["message"] ==
          "mode must be \"preview\" or \"apply\".");
}

TEST_CASE("McpProtocol: reparent_object rejects self-parent, cycles, and invalid parentId types", "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [](const std::string &toolName, const json &) {
        return McpCommandResult{true, json{{"handledTool", toolName}}, {}};
      },
      {}});

  // parentId is an integer (not string or null)
  const json badParentType =
      CallTool(protocol, "editor.reparent_object",
               json{{"id", "obj_root"}, {"parentId", 42}}, 720);
  REQUIRE(badParentType["error"]["message"] ==
          "parentId must be a string or null.");

  // parentId is the object itself
  const json selfParent =
      CallTool(protocol, "editor.reparent_object",
               json{{"id", "obj_root"}, {"parentId", "obj_root"}}, 721);
  REQUIRE(selfParent["error"]["message"] == "Object cannot parent itself.");

  // parentId creates a cycle: obj_root → obj_child (child already has
  // obj_root as parentId in props), so reparenting obj_root under obj_child
  // would create obj_child → obj_root → obj_child.
  const json cycle =
      CallTool(protocol, "editor.reparent_object",
               json{{"id", "obj_root"}, {"parentId", "obj_light"}}, 722);
  // obj_light has parentId=obj_root, so obj_root→obj_light would create cycle
  REQUIRE(cycle["error"]["message"] == "Parent would create a cycle.");
}

TEST_CASE("McpProtocol: freeform props accepted for types without a schema entry", "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [](const std::string &toolName, const json &) {
        return McpCommandResult{true, json{{"handledTool", toolName}}, {}};
      },
      {}});

  // Camera has no schema; freeform props should be accepted as scalars.
  const json cameraCreate = CallTool(
      protocol, "editor.create_object",
      json{{"type", "Camera"},
           {"id", "new_cam"},
           {"props", json{{"fov", "90"}, {"followTargetId", "obj_root"}}}},
      730);
  REQUIRE(cameraCreate["result"]["structuredContent"]["handledTool"] ==
          "editor.create_object");
}

TEST_CASE("McpProtocol: NormalizeComponentList rejects items without type field", "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [](const std::string &toolName, const json &) {
        return McpCommandResult{true, json{{"handledTool", toolName}}, {}};
      },
      {}});

  // Component entry missing "type" key
  const json noType = CallTool(
      protocol, "editor.update_object",
      json{{"id", "obj_root"},
           {"components", json::array({json{{"props", json::object()}}})}},
      740);
  REQUIRE(noType["error"]["message"].get<std::string>().find(
              "must include a string type") != std::string::npos);

  // Component entry with unknown component type
  const json unknownType = CallTool(
      protocol, "editor.update_object",
      json{{"id", "obj_root"},
           {"components", json::array({json{{"type", "nonexistent"},
                                            {"props", json::object()}}})}},
      741);
  REQUIRE(unknownType["error"]["message"] ==
          "Unknown component type: nonexistent.");
}

// ===========================================================================
// EditorLayer::ExecuteMcpCommand — direct handler coverage
// These tests call ExecuteMcpCommand directly so the real handler logic runs
// (as opposed to protocol tests which use mock command callbacks).
// ===========================================================================

TEST_CASE("EditorLayer MCP: clear_selection clears state and returns cleared=true", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  doc.sceneName = "ClearSel";
  SceneObject obj;
  obj.id = "sel_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  // First select an object so we have something to clear
  const McpCommandResult sel =
      editor.ExecuteMcpCommand("editor.select", json{{"id", "sel_obj"}});
  REQUIRE(sel.ok);
  REQUIRE(sel.data["selectedObjectIds"].size() == 1);

  // Now clear
  const McpCommandResult result =
      editor.ExecuteMcpCommand("editor.clear_selection", json::object());
  REQUIRE(result.ok);
  REQUIRE(result.data["cleared"].get<bool>());
  REQUIRE(editor.GetSelectedObjectIds().empty());
}

TEST_CASE("EditorLayer MCP: undo with no history returns ok with undone=false", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  doc.sceneName = "UndoTest";

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result =
      editor.ExecuteMcpCommand("editor.undo", json::object());
  REQUIRE(result.ok);
  REQUIRE(result.data.contains("undone"));
  REQUIRE_FALSE(result.data["undone"].get<bool>());
  REQUIRE(result.data.contains("dirty"));
}

TEST_CASE("EditorLayer MCP: undo/redo after create_object toggle document state", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  doc.sceneName = "UndoRedoTest";

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Create something to undo
  const McpCommandResult createResult = editor.ExecuteMcpCommand(
      "editor.create_object", json{{"type", "Prop"}, {"id", "undo_target"}});
  REQUIRE(createResult.ok);
  REQUIRE(editor.GetDocument().objects.size() == 1);

  // Undo the creation
  const McpCommandResult undoResult =
      editor.ExecuteMcpCommand("editor.undo", json::object());
  REQUIRE(undoResult.ok);
  REQUIRE(undoResult.data["undone"].get<bool>());

  // Redo restores it
  const McpCommandResult redoResult =
      editor.ExecuteMcpCommand("editor.redo", json::object());
  REQUIRE(redoResult.ok);
  REQUIRE(redoResult.data.contains("redone"));
  REQUIRE(redoResult.data.contains("dirty"));
}

TEST_CASE("EditorLayer MCP: redo with nothing to redo returns ok with redone=false", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result =
      editor.ExecuteMcpCommand("editor.redo", json::object());
  REQUIRE(result.ok);
  REQUIRE_FALSE(result.data["redone"].get<bool>());
}

TEST_CASE("EditorLayer MCP: update_object sets pitch, roll, and clears assetId with null", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  doc.assets["box"] = AssetDef{"assets/box.obj", "1,1,1", ""};
  SceneObject obj;
  obj.id = "pitch_roll_obj";
  obj.type = SceneObjectType::Prop;
  obj.assetId = "box";
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.update_object", json{{"id", "pitch_roll_obj"},
                                   {"pitch", 15.0f},
                                   {"roll", 30.0f},
                                   {"assetId", nullptr}});
  REQUIRE(result.ok);
  const json &updated = result.data["updated"];
  REQUIRE(NearlyEqualJsonFloat(updated["pitch"], 15.0f));
  REQUIRE(NearlyEqualJsonFloat(updated["roll"], 30.0f));
  REQUIRE(updated["assetId"] == "");
}

TEST_CASE("EditorLayer MCP: update_object with assetId string updates asset reference", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  doc.assets["box"] = AssetDef{"assets/box.obj", "1,1,1", ""};
  doc.assets["sphere"] = AssetDef{"assets/sphere.obj", "1,1,1", ""};
  SceneObject obj;
  obj.id = "asset_switch_obj";
  obj.type = SceneObjectType::Prop;
  obj.assetId = "box";
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.update_object",
      json{{"id", "asset_switch_obj"},
           {"assetId", "sphere"},
           {"props", json{{"material", "metal"}, {"count", 5}}}});
  REQUIRE(result.ok);
  REQUIRE(result.data["updated"]["assetId"] == "sphere");
}

TEST_CASE("EditorLayer MCP: update_object with props containing non-string values", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  SceneObject obj;
  obj.id = "props_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Props with a numeric value (should be serialized via .dump())
  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.update_object",
      json{{"id", "props_obj"},
           {"props", json{{"label", "hello"}, {"count", 42}}}});
  REQUIRE(result.ok);

  // Verify the non-string prop was serialized
  const SceneDocument &d = editor.GetDocument();
  bool found = false;
  for (const auto &o : d.objects) {
    if (o.id == "props_obj") {
      found = true;
      REQUIRE(o.props.at("label") == "hello");
      REQUIRE(o.props.at("count") == "42");
    }
  }
  REQUIRE(found);
}

TEST_CASE("EditorLayer MCP: update_object with valid components list updates components", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  SceneObject obj;
  obj.id = "comp_update_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.update_object",
      json{{"id", "comp_update_obj"},
           {"components",
            json::array({json{{"type", "light"},
                              {"props", json{{"lightType", "point"}}}}})}});
  REQUIRE(result.ok);

  const SceneDocument &d = editor.GetDocument();
  for (const auto &o : d.objects) {
    if (o.id == "comp_update_obj") {
      REQUIRE(o.components.size() == 1);
      REQUIRE(o.components[0].type == "light");
    }
  }
}

TEST_CASE("EditorLayer MCP: update_object rejects invalid components array", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  SceneObject obj;
  obj.id = "bad_comp_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Non-object item in components
  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.update_object",
      json{{"id", "bad_comp_obj"},
           {"components", json::array({"not_an_object"})}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error == "components must be an array of objects.");
}

TEST_CASE("EditorLayer MCP: update_object rejects invalid scale", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  SceneObject obj;
  obj.id = "bad_scale_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.update_object",
      json{{"id", "bad_scale_obj"}, {"scale", json::array({"x", "y", "z"})}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error == "scale must be [x,y,z].");
}

TEST_CASE("EditorLayer MCP: update_object with transform callback fires callback", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  SceneObject obj;
  obj.id = "cb_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  bool callbackFired = false;
  editor.SetTransformCallback(
      [&callbackFired](const SceneObject &) { callbackFired = true; });

  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.update_object", json{{"id", "cb_obj"}, {"yaw", 45.0f}});
  REQUIRE(result.ok);
  REQUIRE(callbackFired);
}

TEST_CASE("EditorLayer MCP: delete by single id removes the object", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  SceneObject a;
  a.id = "del_single";
  a.type = SceneObjectType::Prop;
  doc.objects.push_back(a);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result =
      editor.ExecuteMcpCommand("editor.delete", json{{"id", "del_single"}});
  REQUIRE(result.ok);
  REQUIRE(result.data["deletedCount"] == 1);
  REQUIRE(editor.GetDocument().objects.empty());
}

TEST_CASE("EditorLayer MCP: delete by ids array removes multiple objects", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  for (int i = 0; i < 3; ++i) {
    SceneObject o;
    o.id = std::format("multi_del_{}", i);
    o.type = SceneObjectType::Prop;
    doc.objects.push_back(o);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.delete",
      json{{"ids", json::array({"multi_del_0", "multi_del_2"})}});
  REQUIRE(result.ok);
  REQUIRE(result.data["deletedCount"] == 2);
  REQUIRE(editor.GetDocument().objects.size() == 1);
  REQUIRE(editor.GetDocument().objects[0].id == "multi_del_1");
}

TEST_CASE("EditorLayer MCP: delete skips non-string items in ids array", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  SceneObject o;
  o.id = "skip_del";
  o.type = SceneObjectType::Prop;
  doc.objects.push_back(o);

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Mix of non-string and valid string ids — non-string should be skipped
  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.delete", json{{"ids", json::array({42, "skip_del", nullptr})}});
  REQUIRE(result.ok);
  REQUIRE(result.data["deletedCount"] == 1);
}

TEST_CASE("EditorLayer MCP: delete with no matching objects returns error", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result =
      editor.ExecuteMcpCommand("editor.delete", json{{"id", "ghost"}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error == "No matching objects to delete.");
}

TEST_CASE("EditorLayer MCP: select_asset rejects unknown asset id", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.select_asset", json{{"id", "ghost_asset"}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error == "Asset not found.");
}

TEST_CASE("EditorLayer MCP: update_asset updates fields and returns asset summary", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  doc.assets["prop_asset"] = AssetDef{"assets/prop.obj", "1,1,1", ""};
  SceneObject o;
  o.id = "ref_obj";
  o.type = SceneObjectType::Prop;
  o.assetId = "prop_asset";
  doc.objects.push_back(o);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.update_asset", json{{"id", "prop_asset"},
                                  {"mesh", "assets/prop_v2.obj"},
                                  {"renderScale", "2,2,2"},
                                  {"albedoMap", "assets/prop_albedo.png"},
                                  {"displayName", "Prop V2"}});
  REQUIRE(result.ok);
  REQUIRE(result.data["asset"]["id"] == "prop_asset");
  REQUIRE(result.data["asset"]["mesh"] == "assets/prop_v2.obj");
  REQUIRE(result.data["asset"]["objectReferenceCount"] == 1);
}

TEST_CASE("EditorLayer MCP: update_asset clears fields with null values", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  doc.assets["clearable"] =
      AssetDef{"assets/c.obj", "1,1,1", "assets/c.png", "", "Clearable"};

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.update_asset", json{{"id", "clearable"},
                                  {"mesh", nullptr},
                                  {"renderScale", nullptr},
                                  {"albedoMap", nullptr},
                                  {"displayName", nullptr}});
  REQUIRE(result.ok);
  REQUIRE(result.data["asset"]["mesh"] == "");
}

TEST_CASE("EditorLayer MCP: update_asset rejects unknown asset id", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result =
      editor.ExecuteMcpCommand("editor.update_asset", json{{"id", "missing"}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error == "Asset not found.");
}

TEST_CASE("EditorLayer MCP: new_scene resets doc with custom sceneId and sceneName", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "old_scene";
  doc.sceneName = "Old Scene";
  SceneObject o;
  o.id = "old_obj";
  o.type = SceneObjectType::Prop;
  doc.objects.push_back(o);

  EditorLayer editor;
  editor.LoadDocument(doc);
  REQUIRE_FALSE(editor.GetDocument().objects.empty());

  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.new_scene",
      json{{"sceneId", "fresh_id"}, {"sceneName", "Fresh Scene"}});
  REQUIRE(result.ok);
  REQUIRE(result.data["sceneId"] == "fresh_id");
  REQUIRE(result.data["sceneName"] == "Fresh Scene");
  REQUIRE(result.data.contains("dirty"));
  // Document should be reset — old_obj no longer present
  for (const auto &object : editor.GetDocument().objects) {
    REQUIRE(object.id != "old_obj");
  }
}

TEST_CASE("EditorLayer MCP: new_scene without args uses defaults", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "old";

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result =
      editor.ExecuteMcpCommand("editor.new_scene", json::object());
  REQUIRE(result.ok);
  REQUIRE(result.data.contains("sceneId"));
  REQUIRE(result.data.contains("sceneName"));
  REQUIRE(result.data.contains("dirty"));
}

TEST_CASE("EditorLayer MCP: save_scene saves document and returns filePath", "[mcp][handler]") {
  namespace fs = std::filesystem;

  EnvGuard env("horo_mcp_save_scene_handler");
  const fs::path scenePath = Horo::Tests::SecureTempBase() /
                             "horo_mcp_save_scene_handler" / "scene.json";
  fs::create_directories(scenePath.parent_path());

  SceneDocument doc;
  doc.sceneId = "save_test";
  doc.sceneName = "Save Test";
  doc.filePath = scenePath.string();
  SceneObject obj;
  obj.id = "save_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result =
      editor.ExecuteMcpCommand("editor.save_scene", json::object());
  REQUIRE(result.ok);
  REQUIRE(result.data["saved"].get<bool>());
  REQUIRE_FALSE(result.data["filePath"].get<std::string>().empty());
  REQUIRE(fs::exists(scenePath));
}

TEST_CASE("EditorLayer MCP: save_scene handles valid path", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "valid_scene";
  std::filesystem::path testPath = std::filesystem::temp_directory_path() /
                                   "test_scene_valid_12345/scene.json";
  doc.filePath = testPath.string();

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result =
      editor.ExecuteMcpCommand("editor.save_scene", json::object());
  REQUIRE(result.ok);
  REQUIRE(result.data["saved"].get<bool>());
}

TEST_CASE("EditorLayer MCP: reload_scene works with saved scene", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "reload_scene";
  std::filesystem::path testPath = std::filesystem::temp_directory_path() /
                                   "test_scene_reload_12345/scene.json";
  doc.filePath = testPath.string();

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult saveResult =
      editor.ExecuteMcpCommand("editor.save_scene", json::object());
  REQUIRE(saveResult.ok);

  const McpCommandResult reloadResult =
      editor.ExecuteMcpCommand("editor.reload_scene", json::object());
  REQUIRE(reloadResult.ok);
}

TEST_CASE("EditorLayer MCP: duplicate with ids array creates one copy per source", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  SceneObject a;
  a.id = "dup_a";
  a.type = SceneObjectType::Prop;
  SceneObject b;
  b.id = "dup_b";
  b.type = SceneObjectType::Prop;
  doc.objects.push_back(a);
  doc.objects.push_back(b);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.duplicate", json{{"ids", json::array({"dup_a", "dup_b"})}});
  REQUIRE(result.ok);
  REQUIRE(result.data["duplicates"].size() == 2);
}

TEST_CASE("EditorLayer MCP: duplicate ids array skips non-string items", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  SceneObject a;
  a.id = "skip_str_a";
  a.type = SceneObjectType::Prop;
  doc.objects.push_back(a);

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Mix of non-string and valid string ids — non-string items should be skipped
  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.duplicate",
      json{{"ids", json::array({42, nullptr, "skip_str_a"})}});
  REQUIRE(result.ok);
  REQUIRE(result.data["duplicates"].size() == 1);
}

TEST_CASE("EditorLayer MCP: duplicate with no matching id returns error", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result =
      editor.ExecuteMcpCommand("editor.duplicate", json{{"id", "no_such_obj"}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error == "Object not found.");
}

TEST_CASE("EditorLayer MCP: reparent_object rejects unknown parent id", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  SceneObject obj;
  obj.id = "orphan_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.reparent_object",
      json{{"id", "orphan_obj"}, {"parentId", "ghost_parent"}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error == "Parent object not found.");
}

TEST_CASE("EditorLayer MCP: reparent_object with empty parentId removes parent", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  SceneObject parent;
  parent.id = "parent_x";
  parent.type = SceneObjectType::Prop;
  SceneObject child;
  child.id = "child_x";
  child.type = SceneObjectType::Prop;
  child.props["parentId"] = "parent_x";
  doc.objects.push_back(parent);
  doc.objects.push_back(child);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.reparent_object", json{{"id", "child_x"}, {"parentId", ""}});
  REQUIRE(result.ok);
  REQUIRE(result.data["parentId"] == "");
}

TEST_CASE("EditorLayer MCP: create_object with non-string prop values uses dump()", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Numeric and boolean prop values should be serialized via .dump()
  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.create_object",
      json{{"type", "Prop"},
           {"id", "dump_prop_obj"},
           {"props", json{{"count", 7}, {"active", true}}}});
  REQUIRE(result.ok);

  bool found = false;
  for (const auto &o : editor.GetDocument().objects) {
    if (o.id == "dump_prop_obj") {
      found = true;
      REQUIRE(o.props.at("count") == "7");
    }
  }
  REQUIRE(found);
}

TEST_CASE("EditorLayer MCP: create_object with props null value triggers early return", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Pass props as an array (not an object) — McpParseProps should return empty
  // map
  const McpCommandResult result = editor.ExecuteMcpCommand(
      "editor.create_object", json{{"type", "Prop"},
                                   {"id", "noobj_props"},
                                   {"props", json::array({1, 2, 3})}});
  REQUIRE(result.ok);
  // The props should be empty since the non-object was discarded
  for (const auto &o : editor.GetDocument().objects) {
    if (o.id == "noobj_props") {
      REQUIRE(o.props.empty());
    }
  }
}

TEST_CASE("EditorLayer MCP: create_object_from_asset fires transform callback", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";
  doc.assets["box"] = AssetDef{"assets/box.obj", "1,1,1", ""};

  EditorLayer editor;
  editor.LoadDocument(doc);

  bool callbackFired = false;
  editor.SetTransformCallback(
      [&callbackFired](const SceneObject &) { callbackFired = true; });

  const McpCommandResult result =
      editor.ExecuteMcpCommand("editor.create_object_from_asset",
                               json{{"assetId", "box"}, {"yaw", 90.0f}});
  REQUIRE(result.ok);
  REQUIRE(callbackFired);
}

TEST_CASE("EditorLayer MCP: ExecuteMcpCommand returns error for unknown tool", "[mcp][handler]") {
  SceneDocument doc;
  doc.sceneId = "scene";

  EditorLayer editor;
  editor.LoadDocument(doc);

  const McpCommandResult result =
      editor.ExecuteMcpCommand("editor.nonexistent_tool", json::object());
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error == "Unsupported MCP command.");
}

// ===========================================================================
// McpServer.cpp error-path tests
// ===========================================================================

TEST_CASE("McpHttpServer: Start with invalid host triggers inet_pton failure", "[mcp][server]") {
  // Covers McpServer.cpp lines 278-286: inet_pton returns != 1 for a
  // syntactically invalid IPv4 address, setting result.error and not binding.
  using namespace Horo::Mcp;
  McpHttpServer server;
  const McpServerStartResult result = server.Start(
      "999.999.999.999", 9999,
      [](const McpHttpRequest &) {
        return McpHttpResponse{200, "OK", "application/json", {}, ""};
      },
      [] {}, [] {});
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.error.empty());
  REQUIRE_FALSE(server.IsRunning());
}

TEST_CASE("McpHttpServer: Start with non-numeric host triggers inet_pton failure", "[mcp][server]") {
  // Second variant — a hostname (not an IP) also fails inet_pton.
  using namespace Horo::Mcp;
  McpHttpServer server;
  const McpServerStartResult result = server.Start(
      "not-an-ip-address", 9999,
      [](const McpHttpRequest &) {
        return McpHttpResponse{200, "OK", "application/json", {}, ""};
      },
      [] {}, [] {});
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.error.empty());
  REQUIRE_FALSE(server.IsRunning());
}

TEST_CASE("McpHttpServer: parses request headers and dispatches one complete body", "[mcp][server]") {
  McpHttpServer server;
  int beginCount = 0;
  int endCount = 0;
  McpHttpRequest captured;

  const int port = 39882;
  const McpServerStartResult started = server.Start(
      "127.0.0.1", port,
      [&captured](const McpHttpRequest &request) {
        captured = request;
        McpHttpResponse response;
        response.statusCode = 200;
        response.statusText = "OK";
        response.contentType = "application/json";
        response.body = R"({"ok":true})";
        return response;
      },
      [&beginCount] { ++beginCount; }, [&endCount] { ++endCount; });
  REQUIRE(started.ok);

  const std::string body = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
  const std::string request =
      std::format("POST /mcp HTTP/1.1\r\nHost: LOCALHOST\r\nContent-Type: "
                  "application/json\r\nCoNtEnT-LeNgTh: {}\r\nX-Trace:  abc  "
                  "\r\nConnection: close\r\n\r\n{}",
                  body.size(), body);

  const HttpResponse response = SendRawHttpRequest(port, request);
  server.Stop();

  REQUIRE(response.statusCode == 200);
  REQUIRE(json::parse(response.body)["ok"].get<bool>());
  REQUIRE(beginCount == 1);
  REQUIRE(endCount == 1);
  REQUIRE(captured.method == "POST");
  REQUIRE(captured.path == "/mcp");
  REQUIRE(captured.body == body);
  REQUIRE(captured.headers.at("content-type") == "application/json");
  REQUIRE(captured.headers.at("x-trace") == "abc");
}

TEST_CASE("McpHttpServer: malformed request returns bad request without handler", "[mcp][server]") {
  McpHttpServer server;
  bool handlerCalled = false;
  int beginCount = 0;
  int endCount = 0;

  const int port = 39883;
  const McpServerStartResult started = server.Start(
      "127.0.0.1", port,
      [&handlerCalled](const McpHttpRequest &) {
        handlerCalled = true;
        McpHttpResponse response;
        response.statusCode = 200;
        response.statusText = "OK";
        response.contentType = "application/json";
        response.body = "{}";
        return response;
      },
      [&beginCount] { ++beginCount; }, [&endCount] { ++endCount; });
  REQUIRE(started.ok);

  const HttpResponse response = SendRawHttpRequest(
      port, "\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n");
  server.Stop();

  REQUIRE(response.statusCode == 400);
  REQUIRE(json::parse(response.body)["error"] == "Invalid HTTP request.");
  REQUIRE_FALSE(handlerCalled);
  REQUIRE(beginCount == 1);
  REQUIRE(endCount == 1);
}

// ===========================================================================
// McpSettings.cpp error-path tests
// ===========================================================================

TEST_CASE("McpSettings: SaveMcpSettings returns error when directory cannot be created (HOME points inside a file)", "[mcp][settings]") {
  // Covers McpSettings.cpp lines 151-155: create_directories sets an error_code
  // when the parent path is not a real directory (e.g. /dev/null on
  // macOS/Linux).
#ifdef _WIN32
  SKIP("Test relies on /dev/null being a non-directory; skipped on Windows.");
#else
  const char *key = "HOME";
  std::string oldHome;
  if (const char *existing = std::getenv(key))
    oldHome = existing;
  setenv(key, "/dev/null",
         1); // ResolveMcpSettingsDirectory() => /dev/null/.horo

  McpSettingsDocument doc;
  std::string error;
  const bool ok = SaveMcpSettings(&doc, &error);

  if (oldHome.empty())
    unsetenv(key);
  else
    setenv(key, oldHome.c_str(), 1);

  REQUIRE_FALSE(ok);
  REQUIRE_FALSE(error.empty());
#endif
}

TEST_CASE("McpSettings: SaveMcpSettings returns error when settings file path is occupied by a directory", "[mcp][settings]") {
  // Covers McpSettings.cpp lines 156-161: ofstream::is_open() returns false
  // when the target path is an existing directory rather than a regular file.
  EnvGuard env("horo_mcp_ofstream_fail");

  // Create <tempHome>/.horo/settings.json as a *directory* so the ofstream open
  // fails.
  const std::filesystem::path settingsPath = ResolveMcpSettingsPath();
  std::error_code ec;
  std::filesystem::create_directories(settingsPath,
                                      ec); // makes settings.json a dir

  McpSettingsDocument doc;
  std::string error;
  const bool ok = SaveMcpSettings(&doc, &error);
  REQUIRE_FALSE(ok);
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("McpProtocol: preview mode for create/update/rename/reparent/duplicate tools", "[mcp][protocol][preview]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  std::vector<std::string> invoked;
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [&invoked](const std::string &toolName, const json &) {
        invoked.push_back(toolName);
        return McpCommandResult{true, json{{"handledTool", toolName}}, {}};
      },
      {},
  });

  // editor.create_object with mode="preview"
  const json createPreview = CallTool(
      protocol, "editor.create_object",
      json{{"type", "Prop"}, {"id", "preview_obj"}, {"mode", "preview"}}, 1);
  REQUIRE(createPreview["result"]["structuredContent"]["mode"] == "preview");
  REQUIRE(
      createPreview["result"]["structuredContent"]["previewToken"].is_string());
  REQUIRE(invoked.empty());

  // editor.create_object preview with position, scale, assetId
  const json createWithDetails =
      CallTool(protocol, "editor.create_object",
               json{{"type", "Prop"},
                    {"id", "detailed_obj"},
                    {"position", json::array({1.0, 2.0, 3.0})},
                    {"scale", json::array({1.0, 1.0, 1.0})},
                    {"assetId", "crate"},
                    {"mode", "preview"}},
               2);
  REQUIRE(createWithDetails["result"]["structuredContent"]["mode"] ==
          "preview");
  REQUIRE(invoked.empty());

  // editor.create_object_from_asset with mode="preview"
  const json createFromAssetPreview =
      CallTool(protocol, "editor.create_object_from_asset",
               json{{"assetId", "crate"},
                    {"id", "from_asset_preview"},
                    {"mode", "preview"}},
               3);
  REQUIRE(createFromAssetPreview["result"]["structuredContent"]["mode"] ==
          "preview");
  REQUIRE(invoked.empty());

  // editor.update_object with mode="preview"
  const json updatePreview = CallTool(protocol, "editor.update_object",
                                      json{{"id", "obj_root"},
                                           {"props", json{{"tag", "updated"}}},
                                           {"mode", "preview"}},
                                      4);
  REQUIRE(updatePreview["result"]["structuredContent"]["mode"] == "preview");
  REQUIRE(
      updatePreview["result"]["structuredContent"]["previewToken"].is_string());
  REQUIRE(invoked.empty());

  // editor.transform with mode="preview"
  const json transformPreview =
      CallTool(protocol, "editor.transform",
               json{{"id", "obj_root"},
                    {"position", json::array({4.0, 5.0, 6.0})},
                    {"mode", "preview"}},
               5);
  REQUIRE(transformPreview["result"]["structuredContent"]["mode"] == "preview");
  REQUIRE(invoked.empty());

  // editor.rename_object with mode="preview"
  const json renamePreview =
      CallTool(protocol, "editor.rename_object",
               json{{"id", "obj_root"},
                    {"newId", "obj_root_preview_renamed"},
                    {"mode", "preview"}},
               6);
  REQUIRE(renamePreview["result"]["structuredContent"]["mode"] == "preview");
  REQUIRE(invoked.empty());

  // editor.reparent_object with mode="preview"
  const json reparentPreview = CallTool(protocol, "editor.reparent_object",
                                        json{{"id", "obj_child"},
                                             {"parentId", "obj_camera"},
                                             {"mode", "preview"}},
                                        7);
  REQUIRE(reparentPreview["result"]["structuredContent"]["mode"] == "preview");
  REQUIRE(invoked.empty());

  // editor.duplicate with mode="preview"
  const json duplicatePreview =
      CallTool(protocol, "editor.duplicate",
               json{{"id", "obj_root"}, {"count", 2}, {"mode", "preview"}}, 8);
  REQUIRE(duplicatePreview["result"]["structuredContent"]["mode"] == "preview");

  // None of the above should have invoked the command
  REQUIRE(invoked.empty());

  // Apply round-trip: preview then apply for editor.create_object
  const std::string createToken =
      createPreview["result"]["structuredContent"]["previewToken"]
          .get<std::string>();
  const json createApply = CallTool(protocol, "editor.create_object",
                                    json{{"type", "Prop"},
                                         {"id", "preview_obj"},
                                         {"mode", "apply"},
                                         {"previewToken", createToken}},
                                    9);
  REQUIRE(createApply["result"]["structuredContent"]["handledTool"] ==
          "editor.create_object");
  REQUIRE(invoked.size() == 1);
  REQUIRE(invoked[0] == "editor.create_object");
}

TEST_CASE("McpProtocol: editor.update_asset and editor.delete_asset preview mode", "[mcp][protocol][preview]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  std::vector<std::string> invoked;
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [&invoked](const std::string &toolName, const json &) {
        invoked.push_back(toolName);
        return McpCommandResult{true, json{{"handledTool", toolName}}, {}};
      },
      {},
  });

  // editor.update_asset with mode="preview"
  const json updateAsset = CallTool(protocol, "editor.update_asset",
                                    json{{"id", "crate"},
                                         {"mesh", "assets/models/crate_v2.obj"},
                                         {"albedoMap", "assets/models/crate_albedo.png"},
                                         {"normalMap", "assets/models/crate_normal.png"},
                                         {"metallicRoughnessMap", "assets/models/crate_mr.png"},
                                         {"emissiveMap", "assets/models/crate_emissive.png"},
                                         {"occlusionMap", "assets/models/crate_occlusion.png"},
                                         {"mode", "preview"}},
                                    1);
  REQUIRE(updateAsset["result"]["structuredContent"]["mode"] == "preview");
  REQUIRE(
      updateAsset["result"]["structuredContent"]["previewToken"].is_string());
  REQUIRE(updateAsset["result"]["structuredContent"]["preview"]["after"]["normalMap"] ==
          "assets/models/crate_normal.png");
  REQUIRE(invoked.empty());

  // editor.delete_asset with mode="preview" on an existing asset
  const json deleteAsset =
      CallTool(protocol, "editor.delete_asset",
               json{{"id", "crate"}, {"mode", "preview"}}, 2);
  REQUIRE(deleteAsset["result"]["structuredContent"]["mode"] == "preview");
  REQUIRE(invoked.empty());

  // editor.delete (objects) with a single id in preview
  const json deleteObjects =
      CallTool(protocol, "editor.delete",
               json{{"id", "obj_root"}, {"mode", "preview"}}, 3);
  REQUIRE(deleteObjects["result"]["structuredContent"]["mode"] == "preview");
  REQUIRE(invoked.empty());

  // editor.delete with both id and ids list in preview (cascading delete
  // preview)
  const json deleteMultiple =
      CallTool(protocol, "editor.delete",
               json{{"id", "obj_root"},
                    {"ids", json::array({"obj_child", "obj_light"})},
                    {"mode", "preview"}},
               4);
  REQUIRE(deleteMultiple["result"]["structuredContent"]["mode"] == "preview");
  REQUIRE(invoked.empty());
}

TEST_CASE("McpProtocol: create_object preview with parentId and position set", "[mcp][protocol][preview]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [](const std::string &, const json &) { return McpCommandResult{}; },
      {},
  });

  // With parentId → exercises parentId branch in BuildCreateObjectPreview
  const json withParent = CallTool(
      protocol, "editor.create_object",
      json{{"type", "Panel"}, {"parentId", "obj_root"}, {"mode", "preview"}},
      1);
  REQUIRE(withParent["result"]["structuredContent"]["mode"] == "preview");
  const json &preview = withParent["result"]["structuredContent"]["preview"];
  // BuildObjectJson hoists parentId to the top-level of the object JSON
  REQUIRE(preview["created"]["parentId"] == "obj_root");

  // With assetId via create_object_from_asset → exercises assetId lookup
  const json withAsset =
      CallTool(protocol, "editor.create_object_from_asset",
               json{{"assetId", "crate"},
                    {"position", json::array({1.0, 2.0, 3.0})},
                    {"mode", "preview"}},
               2);
  REQUIRE(withAsset["result"]["structuredContent"]["mode"] == "preview");
  const json &assetPreview =
      withAsset["result"]["structuredContent"]["preview"];
  REQUIRE(assetPreview["assetId"] == "crate");
  REQUIRE(assetPreview["position"].is_array());

  // update_object on existing id → exercises full before/after path in
  // BuildUpdateObjectPreview
  const json updateExisting = CallTool(protocol, "editor.update_object",
                                       json{{"id", "obj_root"},
                                            {"props", json{{"tag", "x"}}},
                                            {"mode", "preview"}},
                                       3);
  REQUIRE(updateExisting["result"]["structuredContent"]["mode"] == "preview");
  const json &updatePreview =
      updateExisting["result"]["structuredContent"]["preview"];
  REQUIRE(updatePreview["before"].is_object());
  REQUIRE(updatePreview["after"].is_object());
}

// ===========================================================================
// McpController — additional branch coverage
// ===========================================================================

TEST_CASE("McpController: Initialize and Shutdown are idempotent", "[mcp][controller]") {
  ProjectRootGuard guard("McpControllerInitShutdown");

  McpController ctrl;
  ctrl.Initialize();
  ctrl.Shutdown();
  ctrl.Shutdown(); // second call should not crash
  ctrl.Initialize();
  ctrl.Initialize(); // second call must be no-op
  ctrl.Shutdown();
}

TEST_CASE("McpController: DrainCommands with null executor returns 0", "[mcp][controller]") {
  ProjectRootGuard guard("McpControllerDrainNull");

  McpController ctrl;
  ctrl.Initialize();
  const size_t drained = ctrl.DrainCommands(nullptr, 8);
  CHECK(drained == 0);
  ctrl.Shutdown();
}

TEST_CASE("McpController: DrainCommands with executor returns 0 when no commands queued", "[mcp][controller]") {
  ProjectRootGuard guard("McpControllerDrainEmpty");

  McpController ctrl;
  ctrl.Initialize();
  const size_t drained = ctrl.DrainCommands(
      [](std::string_view, const json &) -> McpCommandResult {
        return {true, json::object(), ""};
      });
  CHECK(drained == 0);
  ctrl.Shutdown();
}

TEST_CASE("McpController: BuildClaudeConfigSnippet contains url key", "[mcp][controller]") {
  ProjectRootGuard guard("McpControllerClaudeSnippet");

  McpController ctrl;
  ctrl.Initialize();
  const std::string snippet = ctrl.BuildClaudeConfigSnippet();
  CHECK(snippet.find("url") != std::string::npos);
  ctrl.Shutdown();
}

TEST_CASE("McpController: BuildCodexConfigSnippet contains url key", "[mcp][controller]") {
  ProjectRootGuard guard("McpControllerCodexSnippet");

  McpController ctrl;
  ctrl.Initialize();
  const std::string snippet = ctrl.BuildCodexConfigSnippet();
  CHECK(snippet.find("url") != std::string::npos);
  ctrl.Shutdown();
}

TEST_CASE("McpController: BuildVsCodeConfigSnippet contains url key", "[mcp][controller]") {
  ProjectRootGuard guard("McpControllerVsCodeSnippet");

  McpController ctrl;
  ctrl.Initialize();
  const std::string snippet = ctrl.BuildVsCodeConfigSnippet();
  CHECK(snippet.find("url") != std::string::npos);
  ctrl.Shutdown();
}

TEST_CASE("McpController: GetStatusSnapshot toolCatalog populated on init", "[mcp][controller]") {
  EnvGuard env("horo_mcp_controller_status_default");
  ProjectRootGuard guard("McpControllerStatusDefault");

  McpController ctrl;
  ctrl.Initialize();
  const McpStatusSnapshot status = ctrl.GetStatusSnapshot();
  CHECK_FALSE(status.enabled);
  CHECK(!status.toolCatalog.empty());
  ctrl.Shutdown();
}

TEST_CASE("McpController: ClearActivityLog is idempotent", "[mcp][controller]") {
  ProjectRootGuard guard("McpControllerClearLog");

  McpController ctrl;
  ctrl.Initialize();
  ctrl.ClearActivityLog();
  ctrl.ClearActivityLog();
  ctrl.Shutdown();
}

TEST_CASE("McpController: SetEditorActive does not crash", "[mcp][controller]") {
  ProjectRootGuard guard("McpControllerSetEditorActive");

  McpController ctrl;
  ctrl.Initialize();
  ctrl.SetEditorActive(true);
  ctrl.SetEditorActive(false);
  ctrl.Shutdown();
}

TEST_CASE("McpController: PublishSnapshot with empty snapshot does not crash", "[mcp][controller]") {
  ProjectRootGuard guard("McpControllerPublishEmpty");

  McpController ctrl;
  ctrl.Initialize();
  McpEditorSnapshot snap;
  ctrl.PublishSnapshot(snap);
  ctrl.Shutdown();
}

TEST_CASE("McpController captures executor JSON exceptions as tool errors", "[mcp][controller]") {
  EnvGuard env("horo_mcp_controller_executor_exception");
  McpController controller;
  controller.Initialize();
  controller.PublishSnapshot(MakeSnapshot());

  McpSettings settings = controller.GetSettings();
  settings.enabled = true;
  settings.autoStart = true;
  settings.host = kDefaultMcpHost;
  settings.port = 39882;

  std::string err;
  REQUIRE(controller.ApplySettings(settings, &err));
  controller.SetEditorActive(true);

  std::promise<HttpResponse> responsePromise;
  std::future<HttpResponse> responseFuture = responsePromise.get_future();
  std::thread worker([&settings, promise = std::move(responsePromise)]() mutable {
    promise.set_value(SendHttpPost(
        settings.port,
        json{{"jsonrpc", "2.0"},
             {"id", 901},
             {"method", "tools/call"},
             {"params",
              {{"name", "editor.select"},
               {"arguments", json{{"id", "obj_root"}}}}}}));
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const size_t drained = controller.DrainCommands(
      [](std::string_view, const json &arguments) -> McpCommandResult {
        (void)arguments.at("missing_key").get<std::string>();
        return {true, json::object(), {}};
      });
  REQUIRE(drained == 1);

  const HttpResponse response = responseFuture.get();
  if (worker.joinable())
    worker.join();
  REQUIRE(response.statusCode == 200);
  const json payload = json::parse(response.body);
  REQUIRE(payload["error"]["message"].is_string());
  REQUIRE_FALSE(payload["error"]["message"].get<std::string>().empty());

  controller.SetEditorActive(false);
}

TEST_CASE("McpController activity stats track top targets and clear cleanly", "[mcp][controller]") {
  EnvGuard env("horo_mcp_controller_activity_stats");
  McpController controller;
  controller.Initialize();
  controller.PublishSnapshot(MakeSnapshot());

  McpSettings settings = controller.GetSettings();
  settings.enabled = true;
  settings.autoStart = true;
  settings.host = kDefaultMcpHost;
  settings.port = 39883;

  std::string err;
  REQUIRE(controller.ApplySettings(settings, &err));
  controller.SetEditorActive(true);

  const HttpResponse toolCallA =
      SendHttpPost(settings.port,
                   json{{"jsonrpc", "2.0"},
                        {"id", 910},
                        {"method", "tools/call"},
                        {"params",
                         {{"name", "editor.scene_status"},
                          {"arguments", json::object()}}}});
  REQUIRE(toolCallA.statusCode == 200);

  const HttpResponse toolCallB =
      SendHttpPost(settings.port,
                   json{{"jsonrpc", "2.0"},
                        {"id", 911},
                        {"method", "tools/call"},
                        {"params",
                         {{"name", "editor.scene_status"},
                          {"arguments", json::object()}}}});
  REQUIRE(toolCallB.statusCode == 200);

  const HttpResponse resourceReadA = SendHttpPost(
      settings.port, json{{"jsonrpc", "2.0"},
                          {"id", 912},
                          {"method", "resources/read"},
                          {"params", {{"uri", "scene://summary"}}}});
  REQUIRE(resourceReadA.statusCode == 200);

  const HttpResponse resourceReadB = SendHttpPost(
      settings.port, json{{"jsonrpc", "2.0"},
                          {"id", 913},
                          {"method", "resources/read"},
                          {"params", {{"uri", "scene://summary"}}}});
  REQUIRE(resourceReadB.statusCode == 200);

  const HttpResponse failingMethod =
      SendHttpPost(settings.port, json{{"jsonrpc", "2.0"},
                                       {"id", 914},
                                       {"method", "bogus/method"},
                                       {"params", json::object()}});
  REQUIRE(failingMethod.statusCode == 200);

  const McpStatusSnapshot beforeClear = controller.GetStatusSnapshot();
  REQUIRE(beforeClear.successCount >= 4);
  REQUIRE(beforeClear.failureCount >= 1);
  REQUIRE(beforeClear.totalRequests >= 5);
  REQUIRE(beforeClear.topTool == "editor.scene_status");
  REQUIRE(beforeClear.topResource == "scene://summary");
  REQUIRE_FALSE(beforeClear.lastRequestTime.empty());
  REQUIRE_FALSE(beforeClear.recentActivity.empty());

  controller.ClearActivityLog();
  const McpStatusSnapshot afterClear = controller.GetStatusSnapshot();
  REQUIRE(afterClear.successCount == 0);
  REQUIRE(afterClear.failureCount == 0);
  REQUIRE(afterClear.totalRequests == 0);
  REQUIRE(afterClear.topTool.empty());
  REQUIRE(afterClear.topResource.empty());
  REQUIRE(afterClear.lastRequestTime.empty());
  REQUIRE(afterClear.recentActivity.empty());

  controller.SetEditorActive(false);
}
