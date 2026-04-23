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
#include "studio/EditorLayer.h"
#include "studio/SceneSerializer.h"

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

McpSchemaFieldSnapshot MakeSchemaField(std::string key,
                                       std::string label,
                                       std::string widget,
                                       std::string defaultValue,
                                       std::vector<std::string> options = {},
                                       bool hasMin = false,
                                       float minVal = 0.0f,
                                       bool hasMax = false,
                                       float maxVal = 0.0f,
                                       std::string description = {}) {
  McpSchemaFieldSnapshot field;
  field.key = std::move(key);
  field.label = std::move(label);
  field.description = std::move(description);
  field.widget = std::move(widget);
  field.hasDefault = true;
  field.defaultValue = std::move(defaultValue);
  field.options = std::move(options);
  field.hasMin = hasMin;
  field.minVal = minVal;
  field.hasMax = hasMax;
  field.maxVal = maxVal;
  return field;
}

void PopulateSchemaSnapshot(McpEditorSnapshot* snapshot) {
  REQUIRE(snapshot != nullptr);

  McpSchemaEntrySnapshot panel;
  panel.kind = "object";
  panel.name = "Panel";
  panel.label = "Panel";
  panel.fields.push_back(MakeSchemaField("material", "Material", "enum", "stone",
                                         {"stone", "metal", "wood"}));
  snapshot->schema.objectTypes.push_back(std::move(panel));

  McpSchemaEntrySnapshot prop;
  prop.kind = "object";
  prop.name = "Prop";
  prop.label = "Prop";
  prop.fields.push_back(
      MakeSchemaField("mesh", "Mesh", "enum", "box", {"box", "sphere", "cylinder", "pyramid"}));
  snapshot->schema.objectTypes.push_back(std::move(prop));

  McpSchemaEntrySnapshot lightObject;
  lightObject.kind = "object";
  lightObject.name = "Light";
  lightObject.label = "Light";
  lightObject.fields.push_back(MakeSchemaField("lightType", "Type", "enum", "point",
                                               {"point", "directional"}));
  lightObject.fields.push_back(
      MakeSchemaField("radius", "Radius", "float", "10.0", {}, true, 0.5f, true, 50.0f));
  lightObject.fields.push_back(
      MakeSchemaField("intensity", "Intensity", "float", "1.0", {}, true, 0.1f, true, 20.0f));
  lightObject.fields.push_back(
      MakeSchemaField("color", "Color RGB", "color3", "1.0000,1.0000,1.0000"));
  snapshot->schema.objectTypes.push_back(std::move(lightObject));

  McpSchemaEntrySnapshot lightComponent;
  lightComponent.kind = "component";
  lightComponent.name = "light";
  lightComponent.label = "Light";
  lightComponent.appliesTo = {"Panel", "Prop"};
  lightComponent.fields.push_back(MakeSchemaField("lightType", "Type", "enum", "point",
                                                  {"point", "directional"}));
  lightComponent.fields.push_back(
      MakeSchemaField("radius", "Radius", "float", "5.0", {}, true, 0.5f, true, 50.0f));
  lightComponent.fields.push_back(
      MakeSchemaField("intensity", "Intensity", "float", "1.0", {}, true, 0.1f, true, 20.0f));
  lightComponent.fields.push_back(
      MakeSchemaField("color", "Color RGB", "color3", "1.0000,1.0000,1.0000"));
  snapshot->schema.components.push_back(std::move(lightComponent));

  McpSchemaEntrySnapshot rigidbody;
  rigidbody.kind = "component";
  rigidbody.name = "rigidbody";
  rigidbody.label = "RigidBody";
  rigidbody.appliesTo = {"Prop"};
  rigidbody.fields.push_back(
      MakeSchemaField("mass", "Mass", "float", "1.0", {}, true, 0.0f, true, 500.0f));
  rigidbody.fields.push_back(MakeSchemaField("isKinematic", "Kinematic", "bool", "false"));
  rigidbody.fields.push_back(MakeSchemaField("useGravity", "Use Gravity", "bool", "true"));
  snapshot->schema.components.push_back(std::move(rigidbody));

  McpSchemaEntrySnapshot script;
  script.kind = "component";
  script.name = "script";
  script.label = "Script";
  script.appliesTo = {"Panel", "Prop", "Light", "Camera"};
  script.fields.push_back(MakeSchemaField("behaviorTag", "Behavior", "string", "",
                                          {}, false, 0.0f, false, 0.0f,
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
  PopulateSchemaSnapshot(&snapshot);
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

std::vector<json> ReadJsonLines(const std::filesystem::path& path) {
  std::vector<json> lines;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty())
      lines.push_back(json::parse(line));
  }
  return lines;
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

  const json catalogCaseInsensitive = BuildAssetsCatalogJson(snapshot, 8, "HERO");
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
  REQUIRE(pagedConsole["lines"][0]["message"] == "Missing optional collider for obj_light");
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
  REQUIRE(NearlyEqualJsonFloat(componentSchema["fields"][1]["numeric"]["min"], 0.5f));

  const json objectSchema = BuildSchemaJson(snapshot, "Light", "object");
  REQUIRE(objectSchema["name"] == "Light");
  REQUIRE(objectSchema["fields"][0]["options"][1] == "directional");
  REQUIRE(objectSchema["fields"][2]["widget"] == "float");

  const json objectList = BuildObjectListJson(snapshot, 8, "Prop", "root", false);
  REQUIRE(objectList["matchedObjects"] == 2);
  REQUIRE(objectList["objects"].size() == 2);

  const json objectListCaseInsensitive = BuildObjectListJson(snapshot, 8, "prop", "ROOT", false);
  REQUIRE(objectListCaseInsensitive["matchedObjects"] == 2);

  const json selectedOnly = BuildObjectListJson(snapshot, 8, "", "", true);
  REQUIRE(selectedOnly["matchedObjects"] == 2);
  REQUIRE(selectedOnly["objects"].size() == 2);

  const json pagedObjects = BuildObjectListJson(snapshot, 1, "Prop", "", false, 1);
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
  REQUIRE(toolList["result"]["tools"].size() == 33);
  REQUIRE(toolList["result"]["tools"][0]["name"] == "editor_search");
  const json* createObjectTool = nullptr;
  const json* listSchemaTool = nullptr;
  const json* getSchemaTool = nullptr;
  for (const json& tool : toolList["result"]["tools"]) {
    if (tool.value("name", std::string()) == "editor_create_object") {
      createObjectTool = &tool;
    }
    if (tool.value("name", std::string()) == "editor_list_schema_types")
      listSchemaTool = &tool;
    if (tool.value("name", std::string()) == "editor_get_schema")
      getSchemaTool = &tool;
  }
  REQUIRE(createObjectTool != nullptr);
  REQUIRE((*createObjectTool)["inputSchema"]["properties"]["position"]["items"]["type"] == "number");
  REQUIRE((*createObjectTool)["inputSchema"]["properties"]["position"]["minItems"] == 3);
  REQUIRE((*createObjectTool)["inputSchema"]["properties"]["mode"]["enum"][0] == "preview");
  REQUIRE(listSchemaTool != nullptr);
  REQUIRE((*listSchemaTool)["inputSchema"]["properties"]["kind"]["enum"].size() == 3);
  REQUIRE(getSchemaTool != nullptr);
  REQUIRE((*getSchemaTool)["inputSchema"]["required"][0] == "name");
  const json* deleteTool = nullptr;
  for (const json& tool : toolList["result"]["tools"]) {
    if (tool.value("name", std::string()) == "editor_delete") {
      deleteTool = &tool;
      break;
    }
  }
  REQUIRE(deleteTool != nullptr);
  REQUIRE((*deleteTool)["inputSchema"]["properties"]["previewToken"]["type"] == "string");

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

  const json resourceObjects =
      ReadResource(protocol, "scene://objects", json{{"limit", 1}, {"offset", 1}, {"type", "Prop"}}, 30);
  const json resourceObjectsPayload =
      json::parse(resourceObjects["result"]["contents"][0]["text"].get<std::string>());
  REQUIRE(resourceObjectsPayload["offset"] == 1);
  REQUIRE(resourceObjectsPayload["returned"] == 1);
  REQUIRE(resourceObjectsPayload["objects"][0]["id"] == "obj_child");

  const json resourceConsole =
      ReadResource(protocol, "console://recent", json{{"limit", 2}, {"offset", 1}}, 31);
  const json resourceConsolePayload =
      json::parse(resourceConsole["result"]["contents"][0]["text"].get<std::string>());
  REQUIRE(resourceConsolePayload["offset"] == 1);
  REQUIRE(resourceConsolePayload["returned"] == 2);
  REQUIRE(resourceConsolePayload["hasMore"].get<bool>());

  const json resourceHierarchy =
      ReadResource(protocol, "scene://hierarchy", json{{"limit", 2}, {"offset", 1}}, 32);
  const json resourceHierarchyPayload =
      json::parse(resourceHierarchy["result"]["contents"][0]["text"].get<std::string>());
  REQUIRE(resourceHierarchyPayload["offset"] == 1);
  REQUIRE(resourceHierarchyPayload["returned"] == 2);
  REQUIRE(resourceHierarchyPayload["hasMore"].get<bool>());

  const json resourceCatalog =
      ReadResource(protocol, "assets://catalog", json{{"limit", 1}, {"offset", 1}}, 33);
  const json resourceCatalogPayload =
      json::parse(resourceCatalog["result"]["contents"][0]["text"].get<std::string>());
  REQUIRE(resourceCatalogPayload["offset"] == 1);
  REQUIRE(resourceCatalogPayload["returned"] == 1);
  REQUIRE(resourceCatalogPayload["hasMore"].get<bool>());

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

  const json listSchema = CallTool(protocol, "editor.list_schema_types", json{{"kind", "component"}}, 116);
  REQUIRE(listSchema["result"]["structuredContent"]["kind"] == "component");
  REQUIRE(listSchema["result"]["structuredContent"]["entryCount"] == 3);

  const json getSchema = CallTool(protocol, "editor_get_schema", json{{"name", "light"}, {"kind", "component"}}, 117);
  REQUIRE(getSchema["result"]["structuredContent"]["name"] == "light");
  REQUIRE(getSchema["result"]["structuredContent"]["fields"].size() == 4);
  REQUIRE(getSchema["result"]["structuredContent"]["fields"][0]["options"][1] == "directional");

  REQUIRE(activity.size() >= 22);
  REQUIRE(activity.back().target == "editor.get_schema");
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

TEST_CASE("McpProtocol dispatches write tools and gates destructive apply behind previews", "[mcp][protocol]") {
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

  const std::vector<std::pair<std::string, json>> applyOnlyTools = {
      {"editor.select", json{{"id", "obj_root"}}},
      {"editor.clear_selection", json::object()},
      {"editor.create_object", json{{"type", "Prop"}, {"id", "spawned"}}},
      {"editor.create_object_from_asset", json{{"assetId", "crate"}, {"id", "spawned_from_asset"}}},
      {"editor.update_object", json{{"id", "obj_root"}, {"props", json{{"tag", "updated"}}}}},
      {"editor.transform", json{{"id", "obj_root"}, {"position", json::array({4, 5, 6})}}},
      {"editor.rename_object", json{{"id", "obj_root"}, {"newId", "obj_root_renamed"}}},
      {"editor.reparent_object", json{{"id", "obj_child"}, {"parentId", "obj_camera"}}},
      {"editor.duplicate", json{{"id", "obj_root"}, {"count", 2}}},
      {"editor.select_asset", json{{"id", "crate"}}},
      {"editor.update_asset", json{{"id", "crate"}, {"mesh", "assets/models/crate_v2.obj"}}},
      {"editor.save_scene", json::object()},
  };

  for (size_t i = 0; i < applyOnlyTools.size(); ++i) {
    const json response =
        CallTool(protocol, applyOnlyTools[i].first, applyOnlyTools[i].second, 300 + static_cast<int>(i));
    REQUIRE(response["result"]["structuredContent"]["handledTool"] == applyOnlyTools[i].first);
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
    const json preview =
        CallTool(protocol, destructiveTools[i].first, previewArguments, 400 + static_cast<int>(i));
    REQUIRE(preview["result"]["structuredContent"]["mode"] == "preview");
    REQUIRE(preview["result"]["structuredContent"]["previewToken"].is_string());

    json applyArguments = destructiveTools[i].second;
    applyArguments["mode"] = "apply";
    applyArguments["previewToken"] = preview["result"]["structuredContent"]["previewToken"];
    const json response =
        CallTool(protocol, destructiveTools[i].first, applyArguments, 500 + static_cast<int>(i));
    if (destructiveTools[i].first == "editor.delete_asset") {
      REQUIRE(response["error"]["message"] == "delete rejected");
      continue;
    }
    REQUIRE(response["result"]["structuredContent"]["handledTool"] == destructiveTools[i].first);
    REQUIRE(response["result"]["structuredContent"]["echo"]["mode"] == "apply");
    REQUIRE(response["result"]["structuredContent"]["echo"]["previewToken"].is_string());
  }

  REQUIRE(invoked.size() == applyOnlyTools.size() + destructiveTools.size());
  REQUIRE(invoked.front() == "editor.select");
  REQUIRE(invoked.back() == "editor.reload_scene");
}

TEST_CASE("McpProtocol validates schema-backed mutations and preview tokens before queueing",
          "[mcp][protocol]") {
  McpEditorSnapshot snapshot = MakeSnapshot();
  std::vector<std::string> invoked;
  McpProtocol protocol(McpProtocolContext{
      [&snapshot]() { return CloneSnapshot(snapshot); },
      [&invoked](const std::string& toolName, const json&) {
        invoked.push_back(toolName);
        return McpCommandResult{true, json{{"handledTool", toolName}}, {}};
      },
      {},
  });

  const json invalidEnum = CallTool(
      protocol, "editor.create_object",
      json{{"type", "Light"}, {"props", json{{"lightType", "spot"}}}}, 600);
  REQUIRE(invalidEnum["error"]["message"] == "props.lightType must be one of: point, directional.");

  const json invalidComponentRange =
      CallTool(protocol, "editor.update_object",
               json{{"id", "obj_root"},
                    {"components", json::array({json{{"type", "light"},
                                                     {"props", json{{"radius", 0.1}}}}})}},
               601);
  REQUIRE(invalidComponentRange["error"]["message"] ==
          "components[0].props.radius must be >= 0.5000.");

  const json deleteWithoutPreview =
      CallTool(protocol, "editor.delete", json{{"ids", json::array({"obj_light"})}}, 602);
  REQUIRE(deleteWithoutPreview["error"]["message"] == "previewToken is required for apply mode.");

  const json deletePreview =
      CallTool(protocol, "editor.delete", json{{"ids", json::array({"obj_light"})}, {"mode", "preview"}}, 603);
  REQUIRE(deletePreview["result"]["structuredContent"]["preview"]["deletedCount"] == 1);
  const std::string previewToken =
      deletePreview["result"]["structuredContent"]["previewToken"].get<std::string>();

  const json deleteMismatchedApply = CallTool(
      protocol, "editor.delete",
      json{{"ids", json::array({"obj_root"})}, {"mode", "apply"}, {"previewToken", previewToken}}, 604);
  REQUIRE(deleteMismatchedApply["error"]["message"] ==
          "previewToken does not match the current scene state or arguments.");

  REQUIRE(invoked.empty());
}

TEST_CASE("McpProtocol writes apply audit records to project root and fallback settings path",
          "[mcp][protocol][audit]") {
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
      [&invoked](const std::string& toolName, const json&) {
        invoked.push_back(toolName);
        if (toolName == "editor.delete_asset")
          return McpCommandResult{false, json::object(), "delete rejected"};
        return McpCommandResult{true, json{{"handledTool", toolName}}, {}};
      },
      {},
  });

  const json deletePreview =
      CallTool(protocol, "editor.delete", json{{"ids", json::array({"obj_light"})}, {"mode", "preview"}}, 700);
  const std::string deleteToken =
      deletePreview["result"]["structuredContent"]["previewToken"].get<std::string>();
  const json deleteApply =
      CallTool(protocol, "editor.delete",
               json{{"ids", json::array({"obj_light"})}, {"mode", "apply"}, {"previewToken", deleteToken}}, 701);
  REQUIRE(deleteApply["result"]["structuredContent"]["handledTool"] == "editor.delete");

  const json deleteAssetPreview =
      CallTool(protocol, "editor.delete_asset", json{{"id", "hero"}, {"mode", "preview"}}, 702);
  const std::string deleteAssetToken =
      deleteAssetPreview["result"]["structuredContent"]["previewToken"].get<std::string>();
  const json deleteAssetApply =
      CallTool(protocol, "editor.delete_asset",
               json{{"id", "hero"}, {"mode", "apply"}, {"previewToken", deleteAssetToken}}, 703);
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
  const json selectApply = CallTool(protocol, "editor.select", json{{"id", "obj_root"}}, 704);
  REQUIRE(selectApply["result"]["structuredContent"]["handledTool"] == "editor.select");

  const fs::path fallbackAuditPath = ResolveMcpSettingsDirectory() / "mcp-audit.jsonl";
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
      [](const std::string& toolName, const json& arguments) {
        if (toolName == "editor.delete") {
          return McpCommandResult{
              true,
              json{{"deletedCount", arguments.contains("ids") ? arguments["ids"].size() : 0}},
              {}};
        }
        return McpCommandResult{true, json{{"handledTool", toolName}}, {}};
      },
      {},
  });

  const json sceneSummary = ReadResource(protocol, "scene://summary", json{{"limit", 2}}, 800);
  const json sceneSummaryPayload =
      json::parse(sceneSummary["result"]["contents"][0]["text"].get<std::string>());
  REQUIRE(sceneSummaryPayload["objectCount"] == 4);

  const json search = CallTool(protocol, "editor.search", json{{"query", "hero"}, {"limit", 3}}, 801);
  REQUIRE(search["result"]["structuredContent"]["assets"].size() == 1);

  const json schema = CallTool(protocol, "editor.get_schema", json{{"name", "light"}, {"kind", "component"}}, 802);
  REQUIRE(schema["result"]["structuredContent"]["fields"].size() == 4);

  const json preview =
      CallTool(protocol, "editor.delete", json{{"ids", json::array({"obj_light"})}, {"mode", "preview"}}, 803);
  REQUIRE(preview["result"]["structuredContent"]["preview"]["deletedCount"] == 1);
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

  const json missingSchema = CallTool(protocolWithSnapshot, "editor.get_schema", json{{"name", "missing"}}, 4021);
  REQUIRE(missingSchema["error"]["message"] == "Schema not found.");

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
  REQUIRE(status.toolCount == 33);
  REQUIRE(status.resourceCount == 11);
  REQUIRE(status.totalRequests >= 3);

  controller.SetEditorActive(false);
}
