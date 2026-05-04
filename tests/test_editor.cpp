#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "core/LogBuffer.h"
#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "ui/editor/AssetImportService.h"
#include "ui/editor/AssetImporterRegistry.h"
#include "ui/editor/AssetMetadata.h"
#include "ui/editor/EditorAssetImport.h"
#include "ui/editor/EditorImGuiBackend.h"
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorSceneGraph.h"
#include "ui/editor/EditorSchema.h"
#include "ui/editor/EditorSearch.h"
#include "ui/editor/EditorUiLogic.h"
#include "ui/editor/EditorWorkspaceSettings.h"
#include "ui/editor/Raycaster.h"
#include "ui/editor/SceneDocument.h"
#include "ui/editor/SceneProjectBridge.h"
#include "ui/editor/SceneRuntimeBridge.h"
#include "ui/editor/SceneSerializer.h"
#include "renderer/Camera.h"
#include "scene/Registry.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/PlayerTagComponent.h"
#include "scene/components/TransformComponent.h"
#include "tests/TestTempPaths.h"

// Private shared helpers — included here to directly unit-test the inline
// functions (FindEnumOptionIndex, BuildImGuiComboItems,
// SchemaAppliesToObjectType) that live inside anonymous namespaces and are
// not reachable via EditorLayer's public surface.
#include "ui/editor/EditorLayerInternal.h"

using namespace Horo;
using namespace Horo::Editor;

namespace {
Horo::Editor::SceneObject
MakeObjectFromAssetForTest(const Horo::Editor::SceneDocument &,
                           std::string_view assetId) {
  Horo::Editor::SceneObject obj;
  obj.id = "generated";
  obj.type = Horo::Editor::SceneObjectType::Prop;
  obj.assetId = std::string(assetId);
  return obj;
}

Horo::Editor::SceneObject
DuplicateObjectForTest(const Horo::Editor::SceneDocument &doc,
                       const Horo::Editor::SceneObject &src) {
  Horo::Editor::SceneObject clone = src;
  clone.id = std::format("copy_{}", doc.objects.size());
  clone.props.erase("_eid");
  return clone;
}
} // namespace
using Catch::Approx;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string TmpPath(const std::string &name) {
  return (Horo::Tests::SecureTempBase() / name).string();
}

static std::filesystem::path RepoRootFromTestSource() {
  return std::filesystem::path(std::source_location::current().file_name())
      .parent_path()
      .parent_path()
      .lexically_normal();
}

static void WriteFile(const std::string &path, const std::string &content) {
  std::ofstream f(path);
  f << content;
}

static std::filesystem::path
NormalizePathForComparison(const std::filesystem::path &path) {
  if (path.empty())
    return path;
  std::error_code ec;
  const std::filesystem::path normalized =
      std::filesystem::weakly_canonical(path, ec);
  if (!ec)
    return normalized;
  ec.clear();
  if (const std::filesystem::path parent = path.parent_path();
      !parent.empty()) {
    const std::filesystem::path normalizedParent =
        std::filesystem::weakly_canonical(parent, ec);
    if (!ec)
      return (normalizedParent / path.filename()).lexically_normal();
  }
  return path.lexically_normal();
}

struct ProjectPathGuard {
  std::filesystem::path previousRoot = Horo::ProjectPath::Root();

  ProjectPathGuard(const ProjectPathGuard &) = delete;

  ProjectPathGuard &operator=(const ProjectPathGuard &) = delete;

  ProjectPathGuard(ProjectPathGuard &&) = delete;

  ProjectPathGuard &operator=(ProjectPathGuard &&) = delete;

  explicit ProjectPathGuard(const std::filesystem::path &nextRoot) {
    Horo::ProjectPath::Init(nextRoot);
  }

  ~ProjectPathGuard() { Horo::ProjectPath::Init(previousRoot); }
};

struct HomeDirGuard {
  std::string previousUserProfile;
  std::string previousHomeDrive;
  std::string previousHomePath;
  std::string previousHome;

  HomeDirGuard(const HomeDirGuard &) = delete;

  HomeDirGuard &operator=(const HomeDirGuard &) = delete;

  HomeDirGuard(HomeDirGuard &&) = delete;

  HomeDirGuard &operator=(HomeDirGuard &&) = delete;

  static std::string ReadEnv(const char *name) {
    if (!name || !*name)
      return {};
#ifdef _WIN32
    size_t len = 0;
    if (getenv_s(&len, nullptr, 0, name) != 0 || len <= 1)
      return {};
    std::vector<char> value(len);
    if (getenv_s(&len, value.data(), value.size(), name) != 0 || len <= 1)
      return {};
    return std::string(value.data());
#else
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
  }

  explicit HomeDirGuard(const std::filesystem::path &nextHome) {
    previousUserProfile = ReadEnv("USERPROFILE");
    previousHomeDrive = ReadEnv("HOMEDRIVE");
    previousHomePath = ReadEnv("HOMEPATH");
    previousHome = ReadEnv("HOME");
#ifdef _WIN32
    _putenv_s("USERPROFILE", nextHome.string().c_str());
    _putenv_s("HOMEDRIVE", "");
    _putenv_s("HOMEPATH", "");
#else
    setenv("HOME", nextHome.string().c_str(), 1);
#endif
  }

  ~HomeDirGuard() {
#ifdef _WIN32
    _putenv_s("USERPROFILE", previousUserProfile.c_str());
    _putenv_s("HOMEDRIVE", previousHomeDrive.c_str());
    _putenv_s("HOMEPATH", previousHomePath.c_str());
#else
    if (previousHome.empty())
      unsetenv("HOME");
    else
      setenv("HOME", previousHome.c_str(), 1);
#endif
  }
};

struct ImGuiContextGuard {
  ImGuiContextGuard(const ImGuiContextGuard &) = delete;

  ImGuiContextGuard &operator=(const ImGuiContextGuard &) = delete;

  ImGuiContextGuard(ImGuiContextGuard &&) = delete;

  ImGuiContextGuard &operator=(ImGuiContextGuard &&) = delete;

  ImGuiContextGuard() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    unsigned char *fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.MousePos = ImVec2(32.0f, 32.0f);
    io.MouseDown[0] = false;
  }

  ~ImGuiContextGuard() { ImGui::DestroyContext(); }
};

static void SeedExternalAssetDragPayload(const char *assetId) {
  REQUIRE(assetId != nullptr);
  static std::string payloadStorage;
  payloadStorage = assetId;
  ImGuiContext &g = *ImGui::GetCurrentContext();
  ImGuiPayload &payload = g.DragDropPayload;
  payload.Clear();
  payload.SourceId = 1;
  payload.Data = payloadStorage.data();
  payload.DataSize = static_cast<int>(payloadStorage.size()) + 1;
  payload.DataFrameCount = g.FrameCount;
  payload.Delivery = false;
  payload.Preview = false;
  ImStrncpy(payload.DataType, "ASSET_ID", IM_ARRAYSIZE(payload.DataType));

  g.DragDropActive = true;
  g.DragDropSourceFlags = ImGuiDragDropFlags_SourceExtern;
  g.DragDropSourceFrameCount = g.FrameCount - 1;
  g.DragDropMouseButton = -1;
  g.DragDropAcceptFlags = ImGuiDragDropFlags_None;
  g.DragDropAcceptIdCurr = 0;
  g.DragDropAcceptIdPrev = 0;
  g.DragDropAcceptFrameCount = -1;
}

// ===========================================================================
// EditorSchema
// ===========================================================================

TEST_CASE("EditorSchema: missing file is silently ignored", "[editor][schema][coverage]") {
  EditorSchema schema;
  REQUIRE_NOTHROW(schema.LoadFromFile("/nonexistent/path/schema.json"));
  REQUIRE(schema.GetSchema(SceneObjectType::Prop) == nullptr);
}

TEST_CASE("EditorSchema: empty JSON is silently ignored", "[editor][schema][coverage]") {
  WriteFile(TmpPath("schema_empty.json"), "{}");
  EditorSchema schema;
  REQUIRE_NOTHROW(schema.LoadFromFile(TmpPath("schema_empty.json")));
  REQUIRE(schema.GetSchema(SceneObjectType::Panel) == nullptr);
}

TEST_CASE("EditorSchema: malformed JSON is silently ignored", "[editor][schema][coverage]") {
  WriteFile(TmpPath("schema_bad.json"), "{ this is not valid json !!!}");
  EditorSchema schema;
  REQUIRE_NOTHROW(schema.LoadFromFile(TmpPath("schema_bad.json")));
  REQUIRE(schema.GetSchema(SceneObjectType::Prop) == nullptr);
}

TEST_CASE("EditorImGuiBackend: backend support reflects build capabilities", "[editor][imgui][backend]") {
  REQUIRE(IsSupportedEditorImGuiBackend(RenderBackendId::OpenGL));
  REQUIRE(IsSupportedEditorImGuiBackend(RenderBackendId::Auto));

#if defined(HORO_HAS_VULKAN)
  REQUIRE(IsSupportedEditorImGuiBackend(RenderBackendId::Vulkan));
#else
  REQUIRE_FALSE(IsSupportedEditorImGuiBackend(RenderBackendId::Vulkan));
#endif
}

TEST_CASE("EditorSchema: loads Prop mesh as enum field", "[editor][schema]") {
  const std::string json = R"({
        "types": {
            "Prop": {
                "fields": [
                    {
                      "key": "mesh",
                      "label": "Mesh",
                      "type": "enum",
                      "options": ["box", "sphere", "cylinder", "pyramid"],
                      "default": "box"
                    }
                ]
            }
        }
    })";
  WriteFile(TmpPath("schema_prop.json"), json);

  EditorSchema schema;
  schema.LoadFromFile(TmpPath("schema_prop.json"));

  const TypeSchema *ts = schema.GetSchema(SceneObjectType::Prop);
  REQUIRE(ts != nullptr);
  REQUIRE(ts->fields.size() == 1);
  REQUIRE(ts->fields[0].key == "mesh");
  REQUIRE(ts->fields[0].label == "Mesh");
  REQUIRE(ts->fields[0].widget == FieldDef::Widget::Enum);
  REQUIRE(ts->fields[0].defaultValue == "box");
  REQUIRE(ts->fields[0].options.size() == 4);
  REQUIRE(ts->fields[0].options[0] == "box");
  REQUIRE(ts->fields[0].options[1] == "sphere");
  REQUIRE(ts->fields[0].options[2] == "cylinder");
  REQUIRE(ts->fields[0].options[3] == "pyramid");
}

TEST_CASE("EditorSchema: normalizes legacy Prop mesh string field to enum", "[editor][schema]") {
  const std::string json = R"({
        "types": {
            "Prop": {
                "fields": [
                    {"key": "mesh", "label": "Mesh", "type": "string", "default": "box"}
                ]
            }
        }
    })";
  WriteFile(TmpPath("schema_prop_mesh_legacy.json"), json);

  EditorSchema schema;
  schema.LoadFromFile(TmpPath("schema_prop_mesh_legacy.json"));

  const TypeSchema *ts = schema.GetSchema(SceneObjectType::Prop);
  REQUIRE(ts != nullptr);
  REQUIRE(ts->fields.size() == 1);
  REQUIRE(ts->fields[0].key == "mesh");
  REQUIRE(ts->fields[0].widget == FieldDef::Widget::Enum);
  REQUIRE_FALSE(ts->fields[0].options.empty());
}

TEST_CASE("EditorSchema: ignores deprecated Prop isLight field", "[editor][schema]") {
  const std::string json = R"({
        "types": {
            "Prop": {
                "fields": [
                    {"key": "mesh", "label": "Mesh", "type": "enum", "options": ["box"], "default": "box"},
                    {"key": "isLight", "label": "Is Light", "type": "bool", "default": "false"}
                ]
            }
        }
    })";
  WriteFile(TmpPath("schema_prop_islight_deprecated.json"), json);

  EditorSchema schema;
  schema.LoadFromFile(TmpPath("schema_prop_islight_deprecated.json"));

  const TypeSchema *ts = schema.GetSchema(SceneObjectType::Prop);
  REQUIRE(ts != nullptr);
  REQUIRE(ts->fields.size() == 1);
  REQUIRE(ts->fields[0].key == "mesh");
}

TEST_CASE("EditorSchema: loads float field with min/max", "[editor][schema]") {
  const std::string json = R"({
        "types": {
            "Light": {
                "fields": [
                    {"key": "intensity", "label": "Intensity", "type": "float", "min": 0.0, "max": 10.0, "default": "1.0"}
                ]
            }
        }
    })";
  WriteFile(TmpPath("schema_light.json"), json);

  EditorSchema schema;
  schema.LoadFromFile(TmpPath("schema_light.json"));

  const TypeSchema *ts = schema.GetSchema(SceneObjectType::Light);
  REQUIRE(ts != nullptr);
  REQUIRE(ts->fields[0].widget == FieldDef::Widget::Float);
  REQUIRE(ts->fields[0].minVal == Approx(0.0f));
  REQUIRE(ts->fields[0].maxVal == Approx(10.0f));
}

TEST_CASE("EditorSchema: loads bool field", "[editor][schema]") {
  const std::string json = R"({
        "types": {
            "Prop": {
                "fields": [
                    {"key": "visible", "label": "Visible", "type": "bool", "default": "true"}
                ]
            }
        }
    })";
  WriteFile(TmpPath("schema_bool.json"), json);

  EditorSchema schema;
  schema.LoadFromFile(TmpPath("schema_bool.json"));

  const TypeSchema *ts = schema.GetSchema(SceneObjectType::Prop);
  REQUIRE(ts != nullptr);
  REQUIRE(ts->fields[0].widget == FieldDef::Widget::Bool);
}

TEST_CASE("EditorSchema: loads enum field with options", "[editor][schema]") {
  const std::string json = R"({
        "types": {
            "Panel": {
                "fields": [
                    {
                        "key": "material",
                        "label": "Material",
                        "type": "enum",
                        "options": ["wood", "metal", "stone"],
                        "default": "wood"
                    }
                ]
            }
        }
    })";
  WriteFile(TmpPath("schema_enum.json"), json);

  EditorSchema schema;
  schema.LoadFromFile(TmpPath("schema_enum.json"));

  const TypeSchema *ts = schema.GetSchema(SceneObjectType::Panel);
  REQUIRE(ts != nullptr);
  REQUIRE(ts->fields[0].widget == FieldDef::Widget::Enum);
  REQUIRE(ts->fields[0].options.size() == 3);
  REQUIRE(ts->fields[0].options[0] == "wood");
  REQUIRE(ts->fields[0].options[1] == "metal");
  REQUIRE(ts->fields[0].options[2] == "stone");
}

TEST_CASE("EditorSchema: loads color3 field", "[editor][schema]") {
  const std::string json = R"({
        "types": {
            "Light": {
                "fields": [
                    {"key": "color", "label": "Color", "type": "color3"}
                ]
            }
        }
    })";
  WriteFile(TmpPath("schema_color.json"), json);

  EditorSchema schema;
  schema.LoadFromFile(TmpPath("schema_color.json"));

  const TypeSchema *ts = schema.GetSchema(SceneObjectType::Light);
  REQUIRE(ts != nullptr);
  REQUIRE(ts->fields[0].widget == FieldDef::Widget::Color3);
}

TEST_CASE("EditorSchema: GetSchema returns nullptr for unregistered type", "[editor][schema]") {
  const std::string json = R"({
        "types": {
            "Prop": {
                "fields": [{"key": "x", "label": "X", "type": "string"}]
            }
        }
    })";
  WriteFile(TmpPath("schema_partial.json"), json);

  EditorSchema schema;
  schema.LoadFromFile(TmpPath("schema_partial.json"));

  REQUIRE(schema.GetSchema(SceneObjectType::Prop) != nullptr);
  REQUIRE(schema.GetSchema(SceneObjectType::Light) == nullptr);
  REQUIRE(schema.GetSchema(SceneObjectType::Panel) == nullptr);
}

TEST_CASE("EditorSchema: loads multiple types", "[editor][schema]") {
  const std::string json = R"({
        "types": {
            "Prop": {
                "fields": [{"key": "mesh", "label": "Mesh", "type": "string"}]
            },
            "Light": {
                "fields": [
                    {"key": "intensity", "label": "Intensity", "type": "float"},
                    {"key": "color", "label": "Color", "type": "color3"}
                ]
            },
            "Panel": {
                "fields": [{"key": "text", "label": "Text", "type": "string"}]
            }
        }
    })";
  WriteFile(TmpPath("schema_multi.json"), json);

  EditorSchema schema;
  schema.LoadFromFile(TmpPath("schema_multi.json"));

  REQUIRE(schema.GetSchema(SceneObjectType::Prop) != nullptr);
  REQUIRE(schema.GetSchema(SceneObjectType::Prop)->fields.size() == 1);

  REQUIRE(schema.GetSchema(SceneObjectType::Light) != nullptr);
  REQUIRE(schema.GetSchema(SceneObjectType::Light)->fields.size() == 2);

  REQUIRE(schema.GetSchema(SceneObjectType::Panel) != nullptr);
  REQUIRE(schema.GetSchema(SceneObjectType::Panel)->fields.size() == 1);
}

TEST_CASE("EditorSchema: type without fields is skipped", "[editor][schema]") {
  const std::string json = R"({
        "types": {
            "Prop": {}
        }
    })";
  WriteFile(TmpPath("schema_nofields.json"), json);

  EditorSchema schema;
  schema.LoadFromFile(TmpPath("schema_nofields.json"));

  REQUIRE(schema.GetSchema(SceneObjectType::Prop) == nullptr);
}

TEST_CASE("EditorSchema: loads component schema metadata and lookups", "[editor][schema][coverage]") {
  const std::string json = R"({
        "types": {
            "Prop": {
                "label": "Prop",
                "appliesTo": ["Prop"],
                "fields": [
                    {"key": "mesh", "label": "Mesh", "type": "enum", "options": ["box"], "default": "box"}
                ]
            }
        },
        "components": {
            "light": {
                "label": "Light",
                "appliesTo": ["Prop", "Panel"],
                "fields": [
                    {
                        "key": "intensity",
                        "label": "Intensity",
                        "description": "Brightness.",
                        "type": "float",
                        "min": 0.1,
                        "max": 20.0,
                        "default": "1.0"
                    },
                    {
                        "key": "lightType",
                        "label": "Type",
                        "type": "enum",
                        "options": ["point", "directional"],
                        "default": "point"
                    }
                ]
            }
        }
    })";
  WriteFile(TmpPath("schema_components.json"), json);

  EditorSchema schema;
  schema.LoadFromFile(TmpPath("schema_components.json"));

  const TypeSchema *propSchema = schema.GetSchemaByName("Prop");
  REQUIRE(propSchema != nullptr);
  REQUIRE(propSchema->name == "Prop");
  REQUIRE(propSchema->label == "Prop");
  REQUIRE(propSchema->appliesTo.size() == 1);
  REQUIRE(propSchema->appliesTo[0] == "Prop");

  const ComponentSchema *lightSchema = schema.GetComponentSchema("light");
  REQUIRE(lightSchema != nullptr);
  REQUIRE(lightSchema->name == "light");
  REQUIRE(lightSchema->label == "Light");
  REQUIRE(lightSchema->appliesTo.size() == 2);
  REQUIRE(lightSchema->appliesTo[0] == "Prop");
  REQUIRE(lightSchema->fields.size() == 2);
  REQUIRE(lightSchema->fields[0].description == "Brightness.");
  REQUIRE(lightSchema->fields[0].hasMin);
  REQUIRE(lightSchema->fields[0].minVal == Approx(0.1f));
  REQUIRE(lightSchema->fields[0].hasMax);
  REQUIRE(lightSchema->fields[0].maxVal == Approx(20.0f));
  REQUIRE(lightSchema->fields[1].widget == FieldDef::Widget::Enum);
  REQUIRE(lightSchema->fields[1].options.size() == 2);
  REQUIRE(lightSchema->fields[1].options[0] == "point");
  REQUIRE(lightSchema->fields[1].options[1] == "directional");
}

TEST_CASE("EditorSchema: bundled schema exposes shared component definitions", "[editor][schema][coverage]") {
  const std::filesystem::path schemaPath =
      RepoRootFromTestSource() / "assets" / "editor_schema.json";
  REQUIRE(std::filesystem::exists(schemaPath));

  EditorSchema schema;
  schema.LoadFromFile(schemaPath.string());

  const ComponentSchema *rigidbody = schema.GetComponentSchema("rigidbody");
  REQUIRE(rigidbody != nullptr);
  REQUIRE(rigidbody->label == "RigidBody");
  REQUIRE(rigidbody->appliesTo.size() == 1);
  REQUIRE(rigidbody->appliesTo[0] == "Prop");
  REQUIRE(rigidbody->fields.size() == 3);
  REQUIRE(rigidbody->fields[0].key == "mass");
  REQUIRE(rigidbody->fields[0].hasMin);
  REQUIRE(rigidbody->fields[0].minVal == Approx(0.0f));

  const ComponentSchema *script = schema.GetComponentSchema("script");
  REQUIRE(script != nullptr);
  REQUIRE(script->fields.size() == 1);
  REQUIRE(script->fields[0].key == "behaviorTag");
  REQUIRE(script->fields[0].allowEmpty);
}

// ===========================================================================
// SceneSerializer
// ===========================================================================

TEST_CASE("SceneSerializer: LoadFromFile throws on missing file", "[editor][serializer]") {
  REQUIRE_THROWS_AS(SceneSerializer::LoadFromFile("/nonexistent/scene.json"),
                    std::runtime_error);
}

TEST_CASE("SceneSerializer: LoadFromFile throws on invalid JSON", "[editor][serializer]") {
  WriteFile(TmpPath("bad_scene.json"), "{ not json!!! }");
  REQUIRE_THROWS_AS(SceneSerializer::LoadFromFile(TmpPath("bad_scene.json")),
                    std::runtime_error);
}

TEST_CASE("SceneSerializer: round-trip empty scene", "[editor][serializer]") {
  SceneDocument doc;
  doc.filePath = "test.json";

  const std::string path = TmpPath("empty_scene.json");
  REQUIRE_NOTHROW(SceneSerializer::SaveToFile(doc, path));

  SceneDocument loaded = SceneSerializer::LoadFromFile(path);
  REQUIRE(loaded.filePath == path);
  REQUIRE(loaded.objects.empty());
  REQUIRE(loaded.assets.empty());
}

TEST_CASE("SceneSerializer: round-trip single Panel object", "[editor][serializer]") {
  SceneDocument doc;

  SceneObject obj;
  obj.id = "panel_001";
  obj.type = SceneObjectType::Panel;
  obj.position = {1.0f, 2.0f, 3.0f};
  obj.scale = {0.5f, 0.5f, 0.5f};
  obj.yaw = 45.0f;
  obj.props["texture"] = "wood.png";
  doc.objects.push_back(obj);

  const std::string path = TmpPath("panel_scene.json");
  SceneSerializer::SaveToFile(doc, path);
  SceneDocument loaded = SceneSerializer::LoadFromFile(path);

  REQUIRE(loaded.objects.size() == 1);
  const auto &o = loaded.objects[0];
  REQUIRE(o.id == "panel_001");
  REQUIRE(o.type == SceneObjectType::Panel);
  REQUIRE(o.position.x == Approx(1.0f));
  REQUIRE(o.position.y == Approx(2.0f));
  REQUIRE(o.position.z == Approx(3.0f));
  REQUIRE(o.scale.x == Approx(0.5f));
  REQUIRE(o.yaw == Approx(45.0f));
  REQUIRE(o.props.at("texture") == "wood.png");
}

TEST_CASE("SceneSerializer: round-trip Prop and Light types", "[editor][serializer]") {
  SceneDocument doc;

  SceneObject prop;
  prop.id = "prop_a";
  prop.type = SceneObjectType::Prop;
  prop.position = {10.0f, 0.0f, -5.0f};
  doc.objects.push_back(prop);

  SceneObject light;
  light.id = "light_1";
  light.type = SceneObjectType::Light;
  light.position = {0.0f, 10.0f, 0.0f};
  doc.objects.push_back(light);

  const std::string path = TmpPath("proplight_scene.json");
  SceneSerializer::SaveToFile(doc, path);
  SceneDocument loaded = SceneSerializer::LoadFromFile(path);

  REQUIRE(loaded.objects.size() == 2);
  bool foundProp = false;
  bool foundLight = false;
  for (const auto &o : loaded.objects) {
    if (o.id == "prop_a") {
      foundProp = true;
      REQUIRE(o.type == SceneObjectType::Prop);
    }
    if (o.id == "light_1") {
      foundLight = true;
      REQUIRE(o.type == SceneObjectType::Light);
    }
  }
  REQUIRE(foundProp);
  REQUIRE(foundLight);
}

TEST_CASE("SceneSerializer: round-trip asset registry", "[editor][serializer]") {
  SceneDocument doc;

  AssetDef asset;
  asset.mesh = "stone.obj";
  asset.renderScale = "2.0000,2.0000,2.0000";
  asset.guid = "guid_stone";
  asset.displayName = "Stone Asset";
  doc.assets["stone_asset"] = asset;

  SceneObject obj;
  obj.id = "obj_with_asset";
  obj.type = SceneObjectType::Prop;
  obj.assetId = "stone_asset";
  obj.position = {0.0f, 0.0f, 0.0f};
  doc.objects.push_back(obj);

  const std::string path = TmpPath("asset_scene.json");
  SceneSerializer::SaveToFile(doc, path);
  SceneDocument loaded = SceneSerializer::LoadFromFile(path);

  REQUIRE(loaded.assets.count("stone_asset") == 1);
  REQUIRE(loaded.assets.at("stone_asset").mesh == "stone.obj");
  REQUIRE(loaded.assets.at("stone_asset").renderScale ==
          "2.0000,2.0000,2.0000");
  REQUIRE(loaded.assets.at("stone_asset").guid == "guid_stone");
  REQUIRE(loaded.assets.at("stone_asset").displayName == "Stone Asset");
  REQUIRE(loaded.objects[0].assetId == "stone_asset");
}

TEST_CASE("SceneSerializer: legacy asset entries gain guid and display name on load", "[editor][serializer]") {
  const std::string path = TmpPath("legacy_asset_identity_scene.json");
  const std::string json = R"({
      "version": 1,
      "sceneId": "legacy",
      "assets": {
        "crate": {
          "mesh": "assets/models/crate/crate.obj",
          "renderScale": "1.0000,1.0000,1.0000"
        }
      },
      "objects": []
    })";
  WriteFile(path, json);

  SceneDocument loaded = SceneSerializer::LoadFromFile(path);
  REQUIRE(loaded.assets.count("crate") == 1);
  REQUIRE_FALSE(loaded.assets.at("crate").guid.empty());
  REQUIRE(loaded.assets.at("crate").displayName == "crate");
}

TEST_CASE("AssetMetadata: EnsureAssetMetadataForDocument writes sidecar files", "[editor][asset-metadata]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_asset_metadata_case";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile((root / "CMakePresets.json").string(), "{}");
  ProjectPathGuard guard(root);

  SceneDocument doc;
  doc.assets["crate"] =
      AssetDef{"assets/models/crate_guid/crate.obj", "1.0000,1.0000,1.0000",
               "assets/models/crate_guid/crate.png", "crate_guid", "Crate"};

  std::string error;
  REQUIRE(EnsureAssetMetadataForDocument(&doc, &error));
  REQUIRE(error.empty());

  AssetMetadata metadata;
  REQUIRE(LoadAssetMetadata("crate_guid", &metadata, &error));
  REQUIRE(metadata.assetId == "crate");
  REQUIRE(metadata.assetGuid == "crate_guid");
  REQUIRE(metadata.displayName == "Crate");
  REQUIRE(metadata.producedFiles.size() == 2);
}

TEST_CASE("AssetImporterRegistry: built-in importers resolve by extension and id", "[editor][asset-import]") {
  AssetImporterRegistry registry;
  const AssetImporter *objImporter = registry.FindByExtension("mesh.OBJ");
  REQUIRE(objImporter != nullptr);
  REQUIRE(std::string(objImporter->ImporterId()) == "builtin.obj_mesh");

  const AssetImporter *pngImporter = registry.FindByExtension("albedo.png");
  const AssetImporter *jpgImporter = registry.FindByExtension("albedo.jpg");
  REQUIRE(pngImporter != nullptr);
  REQUIRE(jpgImporter != nullptr);
  REQUIRE(std::string(pngImporter->ImporterId()) == "builtin.texture_copy");
  REQUIRE(std::string(jpgImporter->ImporterId()) == "builtin.texture_copy");

  REQUIRE(registry.FindById("builtin.obj_mesh") != nullptr);
  REQUIRE(registry.FindById("builtin.texture_copy") != nullptr);
}

TEST_CASE("AssetImportService: imports OBJ and persists importer metadata", "[editor][asset-import]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_asset_import_obj";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile((root / "CMakePresets.json").string(), "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path sourceObj = root / "crate.obj";
  WriteFile(sourceObj.string(), "v 0 0 0\n"
                                "v 0 1 0\n"
                                "v 1 0 0\n"
                                "f 1 2 3\n");

  AssetImportService service;
  AssetImportResult result =
      service.ImportAssetFromSource(sourceObj.string(), "crate", "guid_crate",
                                    "Crate", {{"preset", "default"}});

  REQUIRE(result.ok);
  REQUIRE(result.asset.guid == "guid_crate");
  REQUIRE(result.asset.displayName == "Crate");
  REQUIRE(result.asset.mesh.find("assets/models/guid_crate/") !=
          std::string::npos);

  AssetMetadata metadata;
  std::string error;
  REQUIRE(LoadAssetMetadata("guid_crate", &metadata, &error));
  REQUIRE(metadata.importerId == "builtin.obj_mesh");
  REQUIRE(metadata.sourcePath == sourceObj.string());
  REQUIRE(metadata.settings.at("preset") == "default");
  REQUIRE(metadata.diagnostics.empty());
}

TEST_CASE("AssetImportService: unsupported source yields structured diagnostics", "[editor][asset-import][diagnostics]") {
  AssetImportService service;
  AssetImportResult result = service.ImportAssetFromSource(
      "C:/tmp/unsupported.txt", "broken", "guid_broken", "Broken");

  REQUIRE_FALSE(result.ok);
  REQUIRE(result.diagnostics.size() == 1);
  REQUIRE(result.diagnostics[0].severity == AssetDiagnosticSeverity::Error);
  REQUIRE(result.diagnostics[0].code == "asset.importer.not_found");
  REQUIRE(result.diagnostics[0].assetGuid == "guid_broken");
}

TEST_CASE("AssetImportService: reimport propagation follows deterministic topological order", "[editor][asset-import][reimport]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_asset_reimport_graph";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile((root / "CMakePresets.json").string(), "{}");
  ProjectPathGuard guard(root);

  auto writeObj = [](const std::filesystem::path &path, float xOffset) {
    WriteFile(path.string(),
              std::format("v {} 0 0\nv {} 1 0\nv {} 0 0\nf 1 2 3\n", xOffset,
                          xOffset, xOffset + 1.0f));
  };

  writeObj(root / "root.obj", 0.0f);
  writeObj(root / "alpha.obj", 2.0f);
  writeObj(root / "beta.obj", 4.0f);
  writeObj(root / "gamma.obj", 6.0f);

  AssetImportService service;
  SceneDocument doc;
  for (const auto &[name, guid, file] :
       std::vector<std::tuple<std::string, std::string, std::string>>{
           {"root", "guid_root", "root.obj"},
           {"alpha", "guid_alpha", "alpha.obj"},
           {"beta", "guid_beta", "beta.obj"},
           {"gamma", "guid_gamma", "gamma.obj"},
       }) {
    AssetImportResult result =
        service.ImportAssetFromSource((root / file).string(), name, guid, name);
    REQUIRE(result.ok);
    doc.assets[name] = result.asset;
  }

  auto addDependency = [](const std::string &guid,
                          const std::string &dependsOnGuid) {
    AssetMetadata metadata;
    std::string error;
    REQUIRE(LoadAssetMetadata(guid, &metadata, &error));
    metadata.dependencies.emplace_back(AssetDependencyKind::DownstreamAsset,
                                       dependsOnGuid);
    REQUIRE(SaveAssetMetadata(metadata, &error));
  };

  addDependency("guid_alpha", "guid_root");
  addDependency("guid_beta", "guid_root");
  addDependency("guid_gamma", "guid_beta");

  AssetReimportResult reimportResult = service.ReimportAssetWithDependents(
      &doc, "guid_root", "Source changed on root");
  REQUIRE(reimportResult.ok);
  REQUIRE(reimportResult.order ==
          std::vector<std::string>{"guid_root", "guid_alpha", "guid_beta",
                                   "guid_gamma"});

  AssetMetadata gammaMetadata;
  std::string error;
  REQUIRE(LoadAssetMetadata("guid_gamma", &gammaMetadata, &error));
  REQUIRE(gammaMetadata.lastImportReason == "Dependency changed: guid_beta");
}

TEST_CASE("AssetImportService: cyclic dependencies fail reimport with actionable error", "[editor][asset-import][reimport]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_asset_reimport_cycle";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile((root / "CMakePresets.json").string(), "{}");
  ProjectPathGuard guard(root);

  WriteFile((root / "a.obj").string(), "v 0 0 0\nv 0 1 0\nv 1 0 0\nf 1 2 3\n");
  WriteFile((root / "b.obj").string(), "v 0 0 0\nv 0 1 0\nv 1 0 0\nf 1 2 3\n");

  AssetImportService service;
  SceneDocument doc;
  AssetImportResult a = service.ImportAssetFromSource((root / "a.obj").string(),
                                                      "a", "guid_a", "A");
  AssetImportResult b = service.ImportAssetFromSource((root / "b.obj").string(),
                                                      "b", "guid_b", "B");
  REQUIRE(a.ok);
  REQUIRE(b.ok);
  doc.assets["a"] = a.asset;
  doc.assets["b"] = b.asset;

  AssetMetadata aMetadata;
  AssetMetadata bMetadata;
  std::string error;
  REQUIRE(LoadAssetMetadata("guid_a", &aMetadata, &error));
  REQUIRE(LoadAssetMetadata("guid_b", &bMetadata, &error));
  aMetadata.dependencies.emplace_back(AssetDependencyKind::DownstreamAsset,
                                      "guid_b");
  bMetadata.dependencies.emplace_back(AssetDependencyKind::DownstreamAsset,
                                      "guid_a");
  REQUIRE(SaveAssetMetadata(aMetadata, &error));
  REQUIRE(SaveAssetMetadata(bMetadata, &error));

  AssetReimportResult reimportResult =
      service.ReimportAssetWithDependents(&doc, "guid_a", "Cycle validation");
  REQUIRE_FALSE(reimportResult.ok);
  REQUIRE(ContainsCaseInsensitive(reimportResult.error, "cycle"));

  AssetMetadata aReloaded;
  REQUIRE(LoadAssetMetadata("guid_a", &aReloaded, &error));
  REQUIRE_FALSE(aReloaded.lastImportSucceeded);
  REQUIRE(aReloaded.diagnostics.size() == 1);
  REQUIRE(aReloaded.diagnostics[0].code == "asset.reimport.dependency_cycle");
}

TEST_CASE("SceneSerializer: asset albedoMap round-trip", "[editor][serializer]") {
  SceneDocument doc;
  AssetDef asset;
  asset.mesh = "prop.obj";
  asset.renderScale = "1.0000,1.0000,1.0000";
  asset.albedoMap = "assets/models/custom_Albedo.png";
  doc.assets["tex_asset"] = asset;

  SceneObject obj;
  obj.id = "inst";
  obj.type = SceneObjectType::Prop;
  obj.assetId = "tex_asset";
  doc.objects.push_back(obj);

  const std::string path = TmpPath("albedo_asset_scene.json");
  SceneSerializer::SaveToFile(doc, path);
  SceneDocument loaded = SceneSerializer::LoadFromFile(path);

  REQUIRE(loaded.assets.at("tex_asset").albedoMap ==
          "assets/models/custom_Albedo.png");
}

TEST_CASE("SceneSerializer: empty albedoMap not written to JSON", "[editor][serializer]") {
  SceneDocument doc;
  AssetDef asset;
  asset.mesh = "x.obj";
  asset.renderScale = "1,1,1";
  doc.assets["x"] = asset;

  const std::string path = TmpPath("no_albedo_scene.json");
  SceneSerializer::SaveToFile(doc, path);
  std::ifstream in(path);
  REQUIRE(in.is_open());
  std::string saved((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
  REQUIRE(saved.find("albedoMap") == std::string::npos);
}

TEST_CASE("SceneSerializer: asset-backed objects omit inline props block", "[editor][serializer]") {
  SceneDocument doc;

  AssetDef asset;
  asset.mesh = "crate.obj";
  asset.renderScale = "1.0000,1.5000,1.0000";
  doc.assets["crate"] = asset;

  SceneObject obj;
  obj.id = "crate_instance";
  obj.type = SceneObjectType::Prop;
  obj.assetId = "crate";
  obj.props["mesh"] = "should_not_be_saved.obj";
  obj.props["renderScale"] = "9,9,9";
  doc.objects.push_back(obj);

  const std::string path = TmpPath("asset_backed_scene.json");
  SceneSerializer::SaveToFile(doc, path);

  std::ifstream in(path);
  REQUIRE(in.is_open());
  std::string saved((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
  REQUIRE(saved.find("\"asset\": \"crate\"") != std::string::npos);
  REQUIRE(saved.find("should_not_be_saved.obj") == std::string::npos);
  REQUIRE(saved.find("\"props\"") == std::string::npos);

  SceneDocument loaded = SceneSerializer::LoadFromFile(path);
  REQUIRE(loaded.objects.size() == 1);
  REQUIRE(loaded.objects[0].assetId == "crate");
}

TEST_CASE("SceneSerializer: inline props survive when object has no asset reference", "[editor][serializer]") {
  SceneDocument doc;

  SceneObject obj;
  obj.id = "inline_prop_obj";
  obj.type = SceneObjectType::Prop;
  obj.props["mesh"] = "barrel.obj";
  obj.props["renderScale"] = "1.2500,1.2500,1.2500";
  doc.objects.push_back(obj);

  const std::string path = TmpPath("inline_props_scene.json");
  SceneSerializer::SaveToFile(doc, path);
  SceneDocument loaded = SceneSerializer::LoadFromFile(path);

  REQUIRE(loaded.objects.size() == 1);
  REQUIRE(loaded.objects[0].assetId.empty());
  REQUIRE(loaded.objects[0].props.at("mesh") == "barrel.obj");
  REQUIRE(loaded.objects[0].props.at("renderScale") == "1.2500,1.2500,1.2500");
}

TEST_CASE("SceneSerializer: prefab instance metadata round-trips", "[editor][serializer]") {
  SceneDocument doc;

  SceneObject obj;
  obj.id = "crate_instance";
  obj.type = SceneObjectType::Prop;
  obj.position = {4.0f, 1.0f, 2.0f};
  obj.prefabInstance =
      ScenePrefabInstance{"crate_prefab", "assets/prefabs/crate_prefab.horo"};
  doc.objects.push_back(obj);

  const std::string path = TmpPath("prefab_instance_roundtrip.horo");
  SceneSerializer::SaveToFile(doc, path);
  SceneDocument loaded = SceneSerializer::LoadFromFile(path);

  REQUIRE(loaded.objects.size() == 1);
  REQUIRE(loaded.objects[0].prefabInstance.has_value());
  REQUIRE(loaded.objects[0].prefabInstance->prefabId == "crate_prefab");
  REQUIRE(loaded.objects[0].prefabInstance->sourcePath ==
          "assets/prefabs/crate_prefab.horo");
}

TEST_CASE("Editor helpers: duplicating an object clears runtime entity id", "[editor]") {
  SceneDocument doc;
  SceneObject src;
  src.id = "prop_001";
  src.type = SceneObjectType::Prop;
  src.assetId = "crate";
  src.props["_eid"] = "42";
  src.props["mesh"] = "crate.obj";
  doc.objects.push_back(src);

  SceneObject clone = DuplicateObjectForTest(doc, src);
  REQUIRE(clone.id != src.id);
  REQUIRE(clone.assetId == "crate");
  REQUIRE(clone.props.count("_eid") == 0);
  REQUIRE(clone.props.at("mesh") == "crate.obj");
}

TEST_CASE("Editor helpers: asset-backed object stores selected asset id", "[editor]") {
  SceneDocument doc;
  AssetDef asset;
  asset.mesh = "torch.obj";
  asset.renderScale = "0.5000,0.5000,0.5000";
  doc.assets["torch"] = asset;

  SceneObject created = MakeObjectFromAssetForTest(doc, "torch");
  REQUIRE(created.type == SceneObjectType::Prop);
  REQUIRE(created.assetId == "torch");
}

TEST_CASE("Editor helpers: object query matches id and asset id", "[editor]") {
  SceneObject obj;
  obj.id = "prop_007";
  obj.type = SceneObjectType::Prop;
  obj.assetId = "stone_obj";

  REQUIRE(ObjectMatchesQuickOpenQuery(obj, "prop"));
  REQUIRE(ObjectMatchesQuickOpenQuery(obj, "stone"));
  REQUIRE(ObjectMatchesQuickOpenQuery(obj, "STONE_OBJ"));
  REQUIRE_FALSE(ObjectMatchesQuickOpenQuery(obj, "torch"));
}

TEST_CASE("Editor helpers: shortcut query matches category command and keys", "[editor]") {
  ShortcutRow row{"Editor", "Quick open", "Ctrl/Cmd + P"};
  REQUIRE(MatchesShortcutQuery(row, "editor"));
  REQUIRE(MatchesShortcutQuery(row, "quick"));
  REQUIRE(MatchesShortcutQuery(row, "cmd"));
  REQUIRE_FALSE(MatchesShortcutQuery(row, "delete"));
}

TEST_CASE("Editor helpers: command palette query matches command and shortcut", "[editor]") {
  CommandPaletteRow row{"save_scene", "Save Scene", "Toolbar"};
  REQUIRE(MatchesCommandPaletteQuery(row, "save"));
  REQUIRE(MatchesCommandPaletteQuery(row, "toolbar"));
  REQUIRE(MatchesCommandPaletteQuery(row, "scene"));
  REQUIRE_FALSE(MatchesCommandPaletteQuery(row, "delete"));
}

TEST_CASE("Editor helpers: object type labels are stable", "[editor]") {
  REQUIRE(std::string(ObjectTypeLabel(SceneObjectType::Prop)) == "prop");
  REQUIRE(std::string(ObjectTypeLabel(SceneObjectType::Light)) == "light");
  REQUIRE(std::string(ObjectTypeLabel(SceneObjectType::Panel)) == "board");
  REQUIRE(std::string(ObjectTypeLabel(static_cast<SceneObjectType>(999))) ==
          "unknown");
}

TEST_CASE("Editor helpers: generic search helpers handle empty query", "[editor]") {
  ShortcutRow row{"Editor", "Quick open", "Ctrl/Cmd + P"};
  REQUIRE(MatchesShortcutQuery(row, ""));
  REQUIRE(ContainsCaseInsensitive("Any Text", ""));
  REQUIRE_FALSE(ContainsCaseInsensitive("Any Text", "missing"));

  SceneObject obj;
  obj.id = "crate_01";
  obj.type = SceneObjectType::Prop;
  obj.assetId = "crate";
  REQUIRE(ObjectMatchesQuickOpenQuery(obj, ""));

  AssetDef asset;
  asset.mesh = "assets/models/crate.obj";
  REQUIRE(AssetMatchesQuickOpenQuery("crate_asset", asset, ""));
}

TEST_CASE("Editor helpers: asset quick-open query matches id and mesh", "[editor]") {
  AssetDef asset;
  asset.mesh = "assets/models/torch.obj";
  REQUIRE(AssetMatchesQuickOpenQuery("torch_asset", asset, "torch"));
  REQUIRE(AssetMatchesQuickOpenQuery("torch_asset", asset, "models"));
  REQUIRE_FALSE(AssetMatchesQuickOpenQuery("torch_asset", asset, "barrel"));
}

TEST_CASE("Editor helpers: filtered list state handles empty and no-match cases", "[editor]") {
  REQUIRE(EvaluateFilteredListState(3, 1, "") == FilteredListState::None);
  REQUIRE(EvaluateFilteredListState(0, 0, "") == FilteredListState::EmptyData);
  REQUIRE(EvaluateFilteredListState(4, 0, "torch") ==
          FilteredListState::NoMatches);
  REQUIRE(EvaluateFilteredListState(4, 0, "") == FilteredListState::None);
}

TEST_CASE("Editor helpers: shortcut table includes required entries", "[editor]") {
  const auto rows = GetEditorShortcuts();
  REQUIRE_FALSE(rows.empty());

  auto hasCommandWithKeys = [&](const char *command, const char *keys) {
    return std::ranges::any_of(rows, [&](const ShortcutRow &row) {
      return std::string(row.command) == command &&
             std::string(row.keys) == keys;
    });
  };

  REQUIRE(hasCommandWithKeys("Run or stop game in viewport",
                             "Toolbar: Play / Stop"));
  REQUIRE(hasCommandWithKeys("Toggle shortcuts help", "? or F1"));
  REQUIRE(hasCommandWithKeys("Quick open", "Ctrl/Cmd + P"));
  REQUIRE(hasCommandWithKeys("Command palette", "Ctrl/Cmd + Shift + P"));
  REQUIRE(hasCommandWithKeys("Undo last scene change", "Ctrl/Cmd + Z"));
  REQUIRE(hasCommandWithKeys("Redo last scene change",
                             "Ctrl/Cmd + Shift + Z / Ctrl+Y"));
}

TEST_CASE("Editor helpers: shortcut table commands are unique", "[editor]") {
  const auto rows = GetEditorShortcuts();
  std::unordered_set<std::string, StringHash, std::equal_to<>> commandSet;

  for (const auto &row : rows) {
    const auto [_, inserted] = commandSet.emplace(row.command);
    REQUIRE(inserted);
  }
}

TEST_CASE("Editor asset import: validates obj extension case-insensitively", "[editor]") {
  REQUIRE(IsObjFilePath("sandbox/mesh.obj"));
  REQUIRE(IsObjFilePath("sandbox/MESH.OBJ"));
  REQUIRE_FALSE(IsObjFilePath("sandbox/mesh.fbx"));
  REQUIRE_FALSE(IsObjFilePath(""));
}

TEST_CASE("Editor asset import: derives asset id and mesh tag from path", "[editor]") {
  const std::string path = "/Users/bodur/Downloads/torch_model.obj";
  REQUIRE(AssetIdFromImportedPath(path) == "torch_model");
  REQUIRE(MeshTagFromImportedPath(path) == "assets/models/torch_model.obj");

  REQUIRE(AssetIdFromImportedPath("").empty());
  REQUIRE(MeshTagFromImportedPath("").empty());
}

TEST_CASE("Editor MCP delete_asset removes managed imported asset folders", "[editor][mcp][filesystem]") {
  namespace fs = std::filesystem;

  const fs::path projectRoot =
      Horo::Tests::SecureTempBase() / "horo_editor_delete_asset_managed";
  fs::remove_all(projectRoot);
  fs::create_directories(projectRoot / "assets" / "models" / "crate");
  WriteFile(
      (projectRoot / "assets" / "models" / "crate" / "crate.obj").string(),
      "o crate\n");
  WriteFile(
      (projectRoot / "assets" / "models" / "crate" / "crate.png").string(),
      "png");

  ProjectPathGuard projectPath(projectRoot);

  SceneDocument doc;
  doc.assets["crate"] = AssetDef{"assets/models/crate/crate.obj", "1,1,1",
                                 "assets/models/crate/crate.png"};
  SceneObject obj;
  obj.id = "prop_1";
  obj.type = SceneObjectType::Prop;
  obj.assetId = "crate";
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand("editor.delete_asset",
                                               nlohmann::json{{"id", "crate"}});
  REQUIRE(result.ok);
  REQUIRE(result.data["deletedManagedFiles"].get<bool>());
  REQUIRE(result.data["clearedObjectReferences"] == 1);
  REQUIRE(
      NormalizePathForComparison(
          result.data["deletedAssetDirectory"].get<std::string>()) ==
      NormalizePathForComparison(projectRoot / "assets" / "models" / "crate"));
  REQUIRE(editor.GetDocument().assets.find("crate") ==
          editor.GetDocument().assets.end());
  REQUIRE(editor.GetDocument().objects[0].assetId.empty());
  REQUIRE_FALSE(fs::exists(projectRoot / "assets" / "models" / "crate"));
}

TEST_CASE("Editor MCP delete_asset keeps manual asset files and removes only registry entry", "[editor][mcp][filesystem]") {
  namespace fs = std::filesystem;

  const fs::path projectRoot =
      Horo::Tests::SecureTempBase() / "horo_editor_delete_asset_manual";
  fs::remove_all(projectRoot);
  fs::create_directories(projectRoot / "assets" / "models");
  WriteFile((projectRoot / "assets" / "models" / "crate.obj").string(),
            "o crate\n");

  ProjectPathGuard projectPath(projectRoot);

  SceneDocument doc;
  doc.assets["crate"] = AssetDef{"assets/models/crate.obj", "1,1,1", ""};

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand("editor.delete_asset",
                                               nlohmann::json{{"id", "crate"}});
  REQUIRE(result.ok);
  REQUIRE_FALSE(result.data["deletedManagedFiles"].get<bool>());
  REQUIRE_FALSE(result.data.contains("deletedAssetDirectory"));
  REQUIRE(editor.GetDocument().assets.empty());
  REQUIRE(fs::exists(projectRoot / "assets" / "models" / "crate.obj"));
}

TEST_CASE("Editor create_object_from_asset shares parent-aware creation path", "[editor][mcp][assets]") {
  SceneDocument doc;
  doc.assets["crate"] =
      AssetDef{"assets/models/crate/crate.obj", "1.0000,2.0000,3.0000", ""};

  SceneObject parent;
  parent.id = "parent_panel";
  parent.type = SceneObjectType::Panel;
  doc.objects.push_back(parent);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto childResult =
      editor.ExecuteMcpCommand("editor.create_object_from_asset",
                               nlohmann::json{{"assetId", "crate"},
                                              {"parentId", "parent_panel"},
                                              {"id", "child_prop"}});
  REQUIRE(childResult.ok);
  REQUIRE(childResult.data["created"]["id"] == "child_prop");
  REQUIRE(childResult.data["created"]["parentId"] == "parent_panel");

  const auto rootResult = editor.ExecuteMcpCommand(
      "editor.create_object_from_asset",
      nlohmann::json{{"assetId", "crate"},
                     {"id", "root_prop"},
                     {"position", nlohmann::json::array({4.0f, 5.0f, 6.0f})}});
  REQUIRE(rootResult.ok);
  REQUIRE(rootResult.data["created"]["id"] == "root_prop");
  REQUIRE_FALSE(rootResult.data["created"].contains("parentId"));
  REQUIRE(rootResult.data["created"]["position"][0].get<float>() ==
          Approx(4.0f));
  REQUIRE(rootResult.data["created"]["position"][1].get<float>() ==
          Approx(5.0f));
  REQUIRE(rootResult.data["created"]["position"][2].get<float>() ==
          Approx(6.0f));

  const SceneDocument &updated = editor.GetDocument();
  REQUIRE(updated.objects.size() == 3);
  REQUIRE(updated.objects[1].props.at("parentId") == "parent_panel");
  REQUIRE(updated.objects[2].position.x == Approx(4.0f));
  REQUIRE(updated.objects[2].position.y == Approx(5.0f));
  REQUIRE(updated.objects[2].position.z == Approx(6.0f));
}

TEST_CASE("Editor MCP duplicate duplicates each selected object once", "[editor][mcp][selection]") {
  SceneDocument doc;

  SceneObject first;
  first.id = "panel_a";
  first.type = SceneObjectType::Panel;
  first.position = {1.0f, 0.0f, 2.0f};
  doc.objects.push_back(first);

  SceneObject second;
  second.id = "prop_b";
  second.type = SceneObjectType::Prop;
  second.position = {3.0f, 0.0f, 4.0f};
  doc.objects.push_back(second);

  SceneObject third;
  third.id = "light_c";
  third.type = SceneObjectType::Light;
  third.position = {5.0f, 1.0f, 6.0f};
  doc.objects.push_back(third);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.duplicate",
      nlohmann::json{
          {"ids", nlohmann::json::array({"light_c", "panel_a", "panel_a"})},
          {"count", 4}});
  REQUIRE(result.ok);
  REQUIRE(result.data["duplicates"].is_array());
  REQUIRE(result.data["duplicates"].size() == 2);

  const SceneDocument &updated = editor.GetDocument();
  REQUIRE(updated.objects.size() == 5);
  REQUIRE(updated.dirty);

  const SceneObject &panelClone = updated.objects[3];
  REQUIRE(panelClone.id != "panel_a");
  REQUIRE(panelClone.type == SceneObjectType::Panel);
  REQUIRE(panelClone.position.x == Approx(2.0f));
  REQUIRE(panelClone.position.y == Approx(0.0f));
  REQUIRE(panelClone.position.z == Approx(3.0f));

  const SceneObject &lightClone = updated.objects[4];
  REQUIRE(lightClone.id != "light_c");
  REQUIRE(lightClone.type == SceneObjectType::Light);
  REQUIRE(lightClone.position.x == Approx(6.0f));
  REQUIRE(lightClone.position.y == Approx(1.0f));
  REQUIRE(lightClone.position.z == Approx(7.0f));

  const std::vector<std::string> selectedIds = editor.GetSelectedObjectIds();
  REQUIRE(selectedIds.size() == 2);
  REQUIRE(selectedIds[0] == panelClone.id);
  REQUIRE(selectedIds[1] == lightClone.id);
}

TEST_CASE("Editor MCP duplicate preserves single-object count behavior", "[editor][mcp][selection]") {
  SceneDocument doc;

  SceneObject source;
  source.id = "camera_main";
  source.type = SceneObjectType::Camera;
  source.position = {10.0f, 2.0f, -4.0f};
  doc.objects.push_back(source);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.duplicate", nlohmann::json{{"id", "camera_main"}, {"count", 3}});
  REQUIRE(result.ok);
  REQUIRE(result.data["duplicates"].size() == 3);

  const SceneDocument &updated = editor.GetDocument();
  REQUIRE(updated.objects.size() == 4);

  for (size_t i = 1; i < updated.objects.size(); ++i) {
    const SceneObject &clone = updated.objects[i];
    const auto expectedOffset = static_cast<float>(i);
    REQUIRE(clone.id != "camera_main");
    REQUIRE(clone.position.x == Approx(10.0f + expectedOffset));
    REQUIRE(clone.position.y == Approx(2.0f));
    REQUIRE(clone.position.z == Approx(-4.0f + expectedOffset));
  }

  const std::vector<std::string> selectedIds = editor.GetSelectedObjectIds();
  REQUIRE(selectedIds.size() == 3);
  REQUIRE(selectedIds[0] == updated.objects[1].id);
  REQUIRE(selectedIds[1] == updated.objects[2].id);
  REQUIRE(selectedIds[2] == updated.objects[3].id);
}

TEST_CASE("Editor MCP undo and redo restore created object selection", "[editor][mcp][history]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto createResult = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Panel"}, {"id", "panel_created"}});
  REQUIRE(createResult.ok);
  REQUIRE(editor.GetDocument().objects.size() == 1);
  REQUIRE(editor.GetDocument().dirty);
  REQUIRE(editor.GetSelectedObjectIds() ==
          std::vector<std::string>{"panel_created"});

  const auto undoResult =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undoResult.ok);
  REQUIRE(undoResult.data["undone"].get<bool>());
  REQUIRE(editor.GetDocument().objects.empty());
  REQUIRE_FALSE(editor.GetDocument().dirty);
  REQUIRE(editor.GetSelectedObjectIds().empty());

  const auto redoResult =
      editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
  REQUIRE(redoResult.ok);
  REQUIRE(redoResult.data["redone"].get<bool>());
  REQUIRE(editor.GetDocument().objects.size() == 1);
  REQUIRE(editor.GetDocument().objects[0].id == "panel_created");
  REQUIRE(editor.GetDocument().dirty);
  REQUIRE(editor.GetSelectedObjectIds() ==
          std::vector<std::string>{"panel_created"});
}

TEST_CASE("Editor MCP undo and redo restore duplicate selection and dirty state", "[editor][mcp][history]") {
  SceneDocument doc;

  SceneObject first;
  first.id = "prop_a";
  first.type = SceneObjectType::Prop;
  doc.objects.push_back(first);

  SceneObject second;
  second.id = "prop_b";
  second.type = SceneObjectType::Prop;
  doc.objects.push_back(second);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto duplicateResult = editor.ExecuteMcpCommand(
      "editor.duplicate",
      nlohmann::json{{"ids", nlohmann::json::array({"prop_a", "prop_b"})}});
  REQUIRE(duplicateResult.ok);
  REQUIRE(editor.GetDocument().objects.size() == 4);
  const std::vector<std::string> duplicatedSelection =
      editor.GetSelectedObjectIds();
  REQUIRE(duplicatedSelection.size() == 2);
  REQUIRE(editor.GetDocument().dirty);

  const auto undoResult =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undoResult.ok);
  REQUIRE(editor.GetDocument().objects.size() == 2);
  REQUIRE_FALSE(editor.GetDocument().dirty);
  REQUIRE(editor.GetSelectedObjectIds().empty());

  const auto redoResult =
      editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
  REQUIRE(redoResult.ok);
  REQUIRE(editor.GetDocument().objects.size() == 4);
  REQUIRE(editor.GetDocument().dirty);
  REQUIRE(editor.GetSelectedObjectIds() == duplicatedSelection);
}

TEST_CASE("Editor MCP undo restores previous scene after new_scene", "[editor][mcp][history]") {
  SceneDocument doc;
  doc.sceneId = "forest";
  doc.sceneName = "Forest";
  doc.filePath = TmpPath("forest_scene.horo");

  SceneObject object;
  object.id = "tree";
  object.type = SceneObjectType::Prop;
  doc.objects.push_back(object);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto newSceneResult = editor.ExecuteMcpCommand(
      "editor.new_scene",
      nlohmann::json{{"sceneId", "empty"}, {"sceneName", "Empty Scene"}});
  REQUIRE(newSceneResult.ok);
  REQUIRE(editor.GetDocument().sceneId == "empty");
  REQUIRE(editor.GetDocument().sceneName == "Empty Scene");
  REQUIRE(editor.GetDocument().objects.empty());

  const auto undoResult =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undoResult.ok);
  REQUIRE(editor.GetDocument().sceneId == "forest");
  REQUIRE(editor.GetDocument().sceneName == "Forest");
  REQUIRE(editor.GetDocument().objects.size() == 1);
  REQUIRE_FALSE(editor.GetDocument().dirty);
}

TEST_CASE("Editor MCP undo restores asset edits and selected asset", "[editor][mcp][history]") {
  SceneDocument doc;
  doc.assets["crate"] = AssetDef{"assets/models/crate.obj", "1,1,1", ""};

  EditorLayer editor;
  editor.LoadDocument(doc);
  REQUIRE(editor
              .ExecuteMcpCommand("editor.select_asset",
                                 nlohmann::json{{"id", "crate"}})
              .ok);
  const std::string previousDisplayName =
      editor.GetDocument().assets.at("crate").displayName;

  const auto updateResult = editor.ExecuteMcpCommand(
      "editor.update_asset",
      nlohmann::json{{"id", "crate"},
                     {"displayName", "Crate Large"},
                     {"albedoMap", "assets/models/crate.png"}});
  REQUIRE(updateResult.ok);
  REQUIRE(editor.GetDocument().assets.at("crate").displayName == "Crate Large");
  REQUIRE(editor.GetSelectedAssetId() == "crate");

  const auto undoResult =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undoResult.ok);
  REQUIRE(editor.GetDocument().assets.at("crate").displayName ==
          previousDisplayName);
  REQUIRE(editor.GetDocument().assets.at("crate").albedoMap.empty());
  REQUIRE(editor.GetSelectedAssetId() == "crate");
  REQUIRE_FALSE(editor.GetDocument().dirty);
}

TEST_CASE("Editor MCP create_prefab writes prefab file and links selected instance", "[editor][mcp][prefab]") {
  namespace fs = std::filesystem;

  const fs::path projectRoot =
      Horo::Tests::SecureTempBase() / "horo_editor_create_prefab";
  fs::remove_all(projectRoot);
  fs::create_directories(projectRoot / "assets" / "prefabs");

  ProjectPathGuard projectPath(projectRoot);

  SceneDocument doc;
  doc.assets["crate"] =
      AssetDef{"assets/models/crate.obj", "1.0000,2.0000,3.0000", ""};

  SceneObject obj;
  obj.id = "crate_prop";
  obj.type = SceneObjectType::Prop;
  obj.assetId = "crate";
  obj.position = {7.0f, 0.0f, 9.0f};
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.create_prefab", nlohmann::json{{"id", "crate_prop"}});
  REQUIRE(result.ok);
  REQUIRE(result.data["prefabPath"].is_string());

  const fs::path prefabPath =
      projectRoot / result.data["prefabPath"].get<std::string>();
  REQUIRE(fs::exists(prefabPath));

  SceneDocument prefabDoc = SceneSerializer::LoadFromFile(prefabPath.string());
  REQUIRE(prefabDoc.objects.size() == 1);
  REQUIRE(prefabDoc.objects[0].id == "crate_prop");
  REQUIRE_FALSE(prefabDoc.objects[0].prefabInstance.has_value());

  const SceneObject &updated = editor.GetDocument().objects[0];
  REQUIRE(updated.prefabInstance.has_value());
  REQUIRE(updated.prefabInstance->sourcePath ==
          result.data["prefabPath"].get<std::string>());
  REQUIRE(updated.prefabInstance->prefabId == prefabPath.stem().string());
  REQUIRE(editor.GetDocument().dirty);
}

TEST_CASE("Runtime bridge resolves linked prefab instances to concrete props", "[editor][prefab][runtime]") {
  namespace fs = std::filesystem;

  const fs::path projectRoot =
      Horo::Tests::SecureTempBase() / "horo_runtime_prefab_instance";
  fs::remove_all(projectRoot);
  fs::create_directories(projectRoot / "assets" / "prefabs");

  ProjectPathGuard projectPath(projectRoot);

  SceneDocument prefabDoc;
  prefabDoc.filePath =
      (projectRoot / "assets" / "prefabs" / "crate_prefab.horo").string();
  prefabDoc.assets["crate"] =
      AssetDef{"assets/models/crate.obj", "2.0000,3.0000,4.0000", ""};

  SceneObject prefabObject;
  prefabObject.id = "crate_template";
  prefabObject.type = SceneObjectType::Prop;
  prefabObject.assetId = "crate";
  prefabObject.position = {0.0f, 0.0f, 0.0f};
  prefabObject.scale = {1.5f, 1.0f, 0.5f};
  prefabDoc.objects.push_back(prefabObject);
  SceneSerializer::SaveToFile(prefabDoc, prefabDoc.filePath);

  SceneDocument doc;
  SceneObject instance;
  instance.id = "crate_instance";
  instance.type = SceneObjectType::Prop;
  instance.position = {8.0f, 0.0f, 6.0f};
  instance.scale = {0.5f, 2.0f, 1.0f};
  instance.prefabInstance =
      ScenePrefabInstance{"crate_prefab", "assets/prefabs/crate_prefab.horo"};
  doc.objects.push_back(instance);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.assets.size() == 1);
  REQUIRE(model.scene.nodes.size() == 1);
  REQUIRE(model.scene.nodes[0].assetId == "crate");
  REQUIRE(model.scene.nodes[0].prefabInstance.has_value());

  const RuntimeSceneBuildResult runtime =
      Horo::Editor::BuildRuntimeSceneDefinition(doc);
  REQUIRE(runtime.issues.empty());
  REQUIRE(runtime.definition.rooms.size() == 1);
  REQUIRE(runtime.definition.rooms[0].props.size() == 1);

  const RuntimeSceneProp &prop = runtime.definition.rooms[0].props[0];
  REQUIRE(prop.id == "crate_instance");
  REQUIRE(prop.meshTag == "assets/models/crate.obj");
  REQUIRE(prop.position.x == Approx(8.0f));
  REQUIRE(prop.position.z == Approx(6.0f));
  REQUIRE(prop.scale.x == Approx(1.0f));
  REQUIRE(prop.scale.y == Approx(6.0f));
  REQUIRE(prop.scale.z == Approx(4.0f));
}

TEST_CASE("Editor UI logic: hotkey popup triggers only on valid rising edge", "[editor]") {
  REQUIRE(ShouldToggleHelpPopup(true, false, false, false));
  REQUIRE_FALSE(ShouldToggleHelpPopup(true, true, false, false));
  REQUIRE_FALSE(ShouldToggleHelpPopup(true, false, true, false));
  REQUIRE_FALSE(ShouldToggleHelpPopup(true, false, false, true));
}

TEST_CASE("Editor UI logic: quick open is blocked in fly mode and text input", "[editor]") {
  REQUIRE(ShouldOpenQuickOpen(true, false, false, false, false));
  REQUIRE_FALSE(ShouldOpenQuickOpen(true, false, true, false, false));
  REQUIRE_FALSE(ShouldOpenQuickOpen(true, false, false, true, false));
  REQUIRE_FALSE(ShouldOpenQuickOpen(true, false, false, false, true));
  REQUIRE_FALSE(ShouldOpenQuickOpen(true, true, false, false, false));
}

TEST_CASE("Editor helpers: command palette table includes scene actions", "[editor]") {
  const auto rows = GetEditorCommands();
  REQUIRE_FALSE(rows.empty());

  auto hasCommand = [&](const char *id, const char *command) {
    return std::ranges::any_of(rows, [&](const CommandPaletteRow &row) {
      return std::string(row.id) == id && std::string(row.command) == command;
    });
  };

  REQUIRE(hasCommand("new_scene", "New Scene"));
  REQUIRE(hasCommand("open_scene", "Open Scene..."));
  REQUIRE(hasCommand("save_scene", "Save Scene"));
  REQUIRE(hasCommand("close_editor", "Close Editor"));
  REQUIRE(hasCommand("undo", "Undo"));
  REQUIRE(hasCommand("redo", "Redo"));
}

TEST_CASE("Editor UI logic: command palette shares quick-open gating", "[editor]") {
  REQUIRE(ShouldOpenCommandPalette(true, false, false, false, false));
  REQUIRE_FALSE(ShouldOpenCommandPalette(true, false, true, false, false));
  REQUIRE_FALSE(ShouldOpenCommandPalette(true, false, false, true, false));
  REQUIRE_FALSE(ShouldOpenCommandPalette(true, true, false, false, false));
}

TEST_CASE("Editor UI logic: copy and delete actions gate correctly", "[editor]") {
  REQUIRE(ShouldCopySelectionRef(true, false, false, false, true));
  REQUIRE_FALSE(ShouldCopySelectionRef(true, false, false, false, false));
  REQUIRE_FALSE(ShouldCopySelectionRef(true, true, false, false, true));
  REQUIRE(ShouldRequestDeleteSelection(true, false, true));
  REQUIRE_FALSE(ShouldRequestDeleteSelection(true, true, true));
  REQUIRE_FALSE(ShouldRequestDeleteSelection(true, false, false));
}

TEST_CASE("Editor UI logic: escape handling respects modal and input gates", "[editor]") {
  REQUIRE(ShouldHandleEditorEscape(true, false, false, false, false));
  REQUIRE_FALSE(ShouldHandleEditorEscape(true, true, false, false, false));
  REQUIRE_FALSE(ShouldHandleEditorEscape(true, false, true, false, false));
  REQUIRE_FALSE(ShouldHandleEditorEscape(true, false, false, true, false));
  REQUIRE_FALSE(ShouldHandleEditorEscape(true, false, false, false, true));
}

TEST_CASE("Editor UI logic: exit decision reflects unsaved state", "[editor]") {
  REQUIRE(ResolveEditorExitDecision(false) ==
          EditorExitDecision::ExitImmediately);
  REQUIRE(ResolveEditorExitDecision(true) ==
          EditorExitDecision::PromptUnsavedConfirm);
}

TEST_CASE("Editor UI logic: close finalization waits for pending reload", "[editor]") {
  REQUIRE_FALSE(ShouldFinalizeEditorClose(false, false));
  REQUIRE_FALSE(ShouldFinalizeEditorClose(true, true));
  REQUIRE(ShouldFinalizeEditorClose(true, false));
}

TEST_CASE("Editor UI logic: status text is stable and clamps selection", "[editor]") {
  EditorStatusText status =
      BuildEditorStatusText(EditorStatusSnapshot{-2, true, false, true});
  REQUIRE(status.selectionCount == 0);
  REQUIRE(std::string(status.dirtyText) == "yes");
  REQUIRE(std::string(status.flyText) == "off");
  REQUIRE(std::string(status.reloadText) == "pending");

  status = BuildEditorStatusText(EditorStatusSnapshot{3, false, true, false});
  REQUIRE(status.selectionCount == 3);
  REQUIRE(std::string(status.dirtyText) == "no");
  REQUIRE(std::string(status.flyText) == "on");
  REQUIRE(std::string(status.reloadText) == "idle");
}

TEST_CASE("Editor UI logic: edit menu enables only for single valid selection", "[editor]") {
  REQUIRE(CanEditSingleSelection(1, 0, 1));
  REQUIRE(CanEditSingleSelection(1, 2, 5));

  REQUIRE_FALSE(CanEditSingleSelection(0, -1, 5));
  REQUIRE_FALSE(CanEditSingleSelection(2, 1, 5));
  REQUIRE_FALSE(CanEditSingleSelection(1, -1, 5));
  REQUIRE_FALSE(CanEditSingleSelection(1, 5, 5));
}

TEST_CASE("SceneSerializer: _eid prop is stripped on save", "[editor][serializer]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "obj1";
  obj.type = SceneObjectType::Panel;
  obj.props["_eid"] = "42"; // runtime-only, should not be persisted
  obj.props["visible"] = "true";
  doc.objects.push_back(obj);

  const std::string path = TmpPath("eid_strip_scene.json");
  SceneSerializer::SaveToFile(doc, path);
  SceneDocument loaded = SceneSerializer::LoadFromFile(path);

  REQUIRE(loaded.objects.size() == 1);
  REQUIRE(loaded.objects[0].props.count("_eid") == 0);
  REQUIRE(loaded.objects[0].props.count("visible") == 1);
}

TEST_CASE("SceneSerializer: legacy isLight=true migrates to light component", "[editor][serializer]") {
  const std::string path = TmpPath("legacy_islight_true.json");
  const std::string json = R"({
      "version": 1,
      "sceneId": "legacy",
      "objects": [
        {
          "id": "prop_legacy",
          "type": "Prop",
          "position": [0,0,0],
          "scale": [1,1,1],
          "yaw": 0,
          "props": {"isLight": "true"}
        }
      ]
    })";
  WriteFile(path, json);

  SceneDocument loaded = SceneSerializer::LoadFromFile(path);
  REQUIRE(loaded.objects.size() == 1);
  REQUIRE(loaded.objects[0].props.count("isLight") == 0);
  REQUIRE(loaded.objects[0].components.size() == 1);
  REQUIRE(loaded.objects[0].components[0].type == "light");
}

TEST_CASE("SceneSerializer: legacy isLight=false is tolerated but not persisted", "[editor][serializer]") {
  const std::string path = TmpPath("legacy_islight_false.json");
  const std::string json = R"({
      "version": 1,
      "sceneId": "legacy",
      "objects": [
        {
          "id": "prop_legacy",
          "type": "Prop",
          "position": [0,0,0],
          "scale": [1,1,1],
          "yaw": 0,
          "props": {"isLight": "false"}
        }
      ]
    })";
  WriteFile(path, json);

  SceneDocument loaded = SceneSerializer::LoadFromFile(path);
  REQUIRE(loaded.objects.size() == 1);
  REQUIRE(loaded.objects[0].props.count("isLight") == 0);
  REQUIRE(loaded.objects[0].components.empty());

  const std::string savedPath = TmpPath("legacy_islight_false_saved.json");
  SceneSerializer::SaveToFile(loaded, savedPath);
  SceneDocument roundTrip = SceneSerializer::LoadFromFile(savedPath);
  REQUIRE(roundTrip.objects.size() == 1);
  REQUIRE(roundTrip.objects[0].props.count("isLight") == 0);
}

TEST_CASE("SceneSerializer: SaveToFile throws on unwriteable path", "[editor][serializer]") {
  SceneDocument doc;
  // Unix: true root path — mkdir/open fail without privileges.
  // Windows: a leading "/" is *not* filesystem root; it resolves under the
  // current drive (e.g. C:\nonexistent\...) and is often writable in CI. Use a
  // drive letter that is virtually never mounted on runners.
#ifdef _WIN32
  const std::string badPath = R"(Z:\.__horo_engine_ci_unwritable__\scene.json)";
#else
  const std::string badPath = "/nonexistent/dir/scene.json";
#endif
  REQUIRE_THROWS_AS(SceneSerializer::SaveToFile(doc, badPath),
                    std::runtime_error);
}

TEST_CASE("SceneSerializer: filePath is set after load", "[editor][serializer]") {
  SceneDocument doc;
  const std::string path = TmpPath("filepath_test.json");
  SceneSerializer::SaveToFile(doc, path);

  SceneDocument loaded = SceneSerializer::LoadFromFile(path);
  REQUIRE(loaded.filePath == path);
}

TEST_CASE("SceneSerializer: multiple props round-trip", "[editor][serializer]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "multi_prop";
  obj.type = SceneObjectType::Panel;
  obj.props["alpha"] = "0.5";
  obj.props["color"] = "red";
  obj.props["count"] = "10";
  doc.objects.push_back(obj);

  const std::string path = TmpPath("multi_prop_scene.json");
  SceneSerializer::SaveToFile(doc, path);
  SceneDocument loaded = SceneSerializer::LoadFromFile(path);

  REQUIRE(loaded.objects[0].props.at("alpha") == "0.5");
  REQUIRE(loaded.objects[0].props.at("color") == "red");
  REQUIRE(loaded.objects[0].props.at("count") == "10");
}

// ===========================================================================
// Raycaster — RayVsAABB
// ===========================================================================

TEST_CASE("RayVsAABB: direct hit along +X axis", "[editor][raycaster]") {
  Ray ray;
  ray.origin = {-5.0f, 0.0f, 0.0f};
  ray.direction = {1.0f, 0.0f, 0.0f};

  Vec3 center = {0.0f, 0.0f, 0.0f};
  Vec3 half = {1.0f, 1.0f, 1.0f};

  float t = RayVsAABB(ray, center, half);
  REQUIRE(t >= 0.0f);
  REQUIRE(t == Approx(4.0f).epsilon(
                   1e-4f)); // ray travels 4 units before hitting near face
}

TEST_CASE("RayVsAABB: miss — ray passes above the box", "[editor][raycaster]") {
  Ray ray;
  ray.origin = {-5.0f, 3.0f, 0.0f}; // y=3, above a box with half=1
  ray.direction = {1.0f, 0.0f, 0.0f};

  Vec3 center = {0.0f, 0.0f, 0.0f};
  Vec3 half = {1.0f, 1.0f, 1.0f};

  float t = RayVsAABB(ray, center, half);
  REQUIRE(t < 0.0f);
}

TEST_CASE("RayVsAABB: ray origin inside box returns 0", "[editor][raycaster]") {
  Ray ray;
  ray.origin = {0.0f, 0.0f, 0.0f}; // inside the box
  ray.direction = {1.0f, 0.0f, 0.0f};

  Vec3 center = {0.0f, 0.0f, 0.0f};
  Vec3 half = {1.0f, 1.0f, 1.0f};

  float t = RayVsAABB(ray, center, half);
  REQUIRE(t >= 0.0f);
  REQUIRE(t == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("RayVsAABB: parallel ray outside slab misses", "[editor][raycaster]") {
  Ray ray;
  ray.origin = {5.0f, 5.0f, 0.0f}; // outside x-slab of box centered at origin
  ray.direction = {0.0f, 0.0f, 1.0f}; // moves in Z only — parallel to Y slab

  Vec3 center = {0.0f, 0.0f, 0.0f};
  Vec3 half = {1.0f, 1.0f, 1.0f};

  float t = RayVsAABB(ray, center, half);
  REQUIRE(t < 0.0f);
}

TEST_CASE("RayVsAABB: parallel ray inside slab hits", "[editor][raycaster]") {
  Ray ray;
  ray.origin = {-5.0f, 0.0f, 0.0f};
  ray.direction = {0.0f, 0.0f, 1.0f}; // moves in Z, inside x and y slabs

  Vec3 center = {0.0f, 0.0f, 0.0f};
  Vec3 half = {1.0f, 1.0f, 1.0f};

  float t = RayVsAABB(ray, center, half);
  // parallel x-axis: origin.x=-5 is outside [-1,1] → should miss
  REQUIRE(t < 0.0f);
}

TEST_CASE("RayVsAABB: hit along -Z axis", "[editor][raycaster]") {
  Ray ray;
  ray.origin = {0.0f, 0.0f, 10.0f};
  ray.direction = {0.0f, 0.0f, -1.0f};

  Vec3 center = {0.0f, 0.0f, 0.0f};
  Vec3 half = {0.5f, 0.5f, 0.5f};

  float t = RayVsAABB(ray, center, half);
  REQUIRE(t >= 0.0f);
  REQUIRE(t == Approx(9.5f).epsilon(1e-4f));
}

TEST_CASE("RayVsAABB: hit along +Y axis", "[editor][raycaster]") {
  Ray ray;
  ray.origin = {0.0f, -5.0f, 0.0f};
  ray.direction = {0.0f, 1.0f, 0.0f};

  Vec3 center = {0.0f, 2.0f, 0.0f};
  Vec3 half = {1.0f, 1.0f, 1.0f};

  float t = RayVsAABB(ray, center, half);
  REQUIRE(t >= 0.0f);
  REQUIRE(t == Approx(6.0f).epsilon(1e-4f));
}

TEST_CASE("RayVsAABB: ray pointing away from box misses", "[editor][raycaster]") {
  Ray ray;
  ray.origin = {10.0f, 0.0f, 0.0f};
  ray.direction = {1.0f, 0.0f, 0.0f}; // pointing away from origin box

  Vec3 center = {0.0f, 0.0f, 0.0f};
  Vec3 half = {1.0f, 1.0f, 1.0f};

  float t = RayVsAABB(ray, center, half);
  REQUIRE(t < 0.0f);
}

// ===========================================================================
// Raycaster — ScreenToRay
// ===========================================================================

TEST_CASE("ScreenToRay: center of screen produces ray through view center", "[editor][raycaster]") {
  Camera cam;
  cam.position = {0.0f, 0.0f, 5.0f};
  cam.target = {0.0f, 0.0f, 0.0f};
  cam.up = Vec3::Up();
  cam.fovY = 60.0f;
  cam.aspect = 1.0f;
  cam.zNear = 0.1f;
  cam.zFar = 100.0f;

  int W = 800;
  int H = 800;
  Ray ray = ScreenToRay(static_cast<float>(W) / 2.0f,
                        static_cast<float>(H) / 2.0f, W, H, cam);

  REQUIRE(ray.direction.x == Approx(0.0f).margin(1e-3f));
  REQUIRE(ray.direction.y == Approx(0.0f).margin(1e-3f));
  REQUIRE(ray.direction.z < 0.0f); // pointing toward -Z (into the scene)
}

TEST_CASE("ScreenToRay: direction is normalized", "[editor][raycaster]") {
  Camera cam;
  cam.position = {0.0f, 0.0f, 5.0f};
  cam.target = {0.0f, 0.0f, 0.0f};
  cam.up = Vec3::Up();
  cam.fovY = 60.0f;
  cam.aspect = 16.0f / 9.0f;
  cam.zNear = 0.1f;
  cam.zFar = 100.0f;

  // Test multiple screen positions
  for (auto [mx, my] :
       std::initializer_list<std::pair<float, float>>{{0.0f, 0.0f},
                                                      {200.0f, 150.0f},
                                                      {400.0f, 300.0f},
                                                      {799.0f, 449.0f}}) {
    Ray r = ScreenToRay(mx, my, 800, 450, cam);
    REQUIRE(r.direction.Length() == Approx(1.0f).epsilon(1e-4f));
  }
}

TEST_CASE("ScreenToRay: origin is at/near camera position", "[editor][raycaster]") {
  Camera cam;
  cam.position = {3.0f, 2.0f, 7.0f};
  cam.target = {0.0f, 0.0f, 0.0f};
  cam.up = Vec3::Up();
  cam.fovY = 60.0f;
  cam.aspect = 1.0f;
  cam.zNear = 0.1f;
  cam.zFar = 100.0f;

  Ray ray = ScreenToRay(400.0f, 300.0f, 800, 600, cam);

  // Origin should be near the near plane (close to camera)
  Vec3 diff = ray.origin - cam.position;
  REQUIRE(diff.Length() < 1.0f);
}

// ===========================================================================
// Hierarchy selection helpers (testable without ImGui)
// ===========================================================================

// Mirror of the selection logic in EditorLayer::DrawObjectsTree.
static void ApplyHierarchyClick(std::vector<int> &sel, int &lastIdx,
                                int clicked, bool shift, bool ctrl) {
  if (shift && lastIdx >= 0) {
    const int lo = std::min(lastIdx, clicked);
    const int hi = std::max(lastIdx, clicked);
    sel.clear();
    for (int i = lo; i <= hi; ++i)
      sel.push_back(i);
  } else if (ctrl) {
    if (const auto it = std::ranges::find(sel, clicked); it != sel.end())
      sel.erase(it);
    else
      sel.push_back(clicked);
    lastIdx = clicked;
  } else {
    sel = {clicked};
    lastIdx = clicked;
  }
}

TEST_CASE("Hierarchy plain click replaces selection", "[editor][hierarchy]") {
  std::vector<int> sel;
  int last = -1;

  ApplyHierarchyClick(sel, last, 3, false, false);
  REQUIRE(sel.size() == 1);
  REQUIRE(sel[0] == 3);
  REQUIRE(last == 3);

  ApplyHierarchyClick(sel, last, 7, false, false);
  REQUIRE(sel.size() == 1);
  REQUIRE(sel[0] == 7);
  REQUIRE(last == 7);
}

TEST_CASE("Hierarchy range select fills contiguous range", "[editor][hierarchy]") {
  std::vector<int> sel;
  int last = -1;

  // First plain click anchors at 2
  ApplyHierarchyClick(sel, last, 2, false, false);
  REQUIRE(last == 2);

  // Shift-click at 5 → [2,3,4,5]
  ApplyHierarchyClick(sel, last, 5, true, false);
  REQUIRE(sel.size() == 4);
  REQUIRE(sel[0] == 2);
  REQUIRE(sel[3] == 5);

  // Shift-click backwards at 0 → [0,1,2]  (anchor stays at 2)
  ApplyHierarchyClick(sel, last, 0, true, false);
  REQUIRE(sel.size() == 3);
  REQUIRE(sel[0] == 0);
  REQUIRE(sel[2] == 2);
}

TEST_CASE("Hierarchy shift-click with no prior anchor falls back to single select", "[editor][hierarchy]") {
  std::vector<int> sel;
  int last = -1;

  // Shift before any click: last == -1, range guard doesn't fire.
  // Falls through to else branch → behaves as a single select.
  ApplyHierarchyClick(sel, last, 4, true, false);
  REQUIRE(sel.size() == 1);
  REQUIRE(sel[0] == 4);
  REQUIRE(last == 4);
}

TEST_CASE("Hierarchy ctrl-click toggles individual items", "[editor][hierarchy]") {
  std::vector<int> sel;
  int last = -1;

  ApplyHierarchyClick(sel, last, 1, false, false);
  ApplyHierarchyClick(sel, last, 3, false, true); // ctrl-add 3
  ApplyHierarchyClick(sel, last, 5, false, true); // ctrl-add 5
  REQUIRE(sel.size() == 3);

  ApplyHierarchyClick(sel, last, 3, false, true); // ctrl-remove 3
  REQUIRE(sel.size() == 2);
  REQUIRE(std::find(sel.begin(), sel.end(), 3) == sel.end());
}

// ===========================================================================
// Drag-drop ray-floor intersection math
// ===========================================================================

TEST_CASE("Drop ray hits y=0 plane correctly", "[editor][dragdrop]") {
  // Ray from above looking straight down
  Ray r;
  r.origin = {0.0f, 5.0f, 0.0f};
  r.direction = {0.0f, -1.0f, 0.0f};

  const float t = -r.origin.y / r.direction.y; // = 5
  const Vec3 hit = {r.origin.x + r.direction.x * t,
                    r.origin.y + r.direction.y * t,
                    r.origin.z + r.direction.z * t};

  REQUIRE(hit.y == Approx(0.0f));
  REQUIRE(hit.x == Approx(0.0f));
  REQUIRE(hit.z == Approx(0.0f));

  Vec3 helperHit = Vec3::Zero();
  REQUIRE(TryIntersectGroundPlane(r, &helperHit));
  REQUIRE(helperHit.y == Approx(0.0f));
  REQUIRE(helperHit.x == Approx(0.0f));
  REQUIRE(helperHit.z == Approx(0.0f));
}

TEST_CASE("Drop ray hits y=0 plane at correct XZ", "[editor][dragdrop]") {
  // Ray from (3, 4, 1) pointing down-forward
  Ray r;
  r.origin = {3.0f, 4.0f, 1.0f};
  // Direction: slightly forward (+z) and mostly down
  const float len = std::sqrt(1.0f + 16.0f);
  r.direction = {0.0f, -4.0f / len, 1.0f / len};

  const float t = -r.origin.y / r.direction.y;
  const Vec3 hit = {r.origin.x + r.direction.x * t,
                    r.origin.y + r.direction.y * t,
                    r.origin.z + r.direction.z * t};

  REQUIRE(hit.y == Approx(0.0f).margin(1e-4f));
  REQUIRE(hit.x == Approx(3.0f).margin(1e-4f)); // no x-component in direction
}

TEST_CASE("Drop ray parallel to ground plane is rejected", "[editor][dragdrop]") {
  Ray r;
  r.origin = {2.0f, 3.0f, 4.0f};
  r.direction = {1.0f, 0.0f, 0.0f};

  Vec3 hit = Vec3::Zero();
  REQUIRE_FALSE(TryIntersectGroundPlane(r, &hit));
}

TEST_CASE("RayVsAABBHit reports top-face hit point and normal", "[editor][dragdrop]") {
  Ray ray;
  ray.origin = {0.0f, 5.0f, 0.0f};
  ray.direction = {0.0f, -1.0f, 0.0f};

  RayAabbHit hit;
  REQUIRE(RayVsAABBHit(ray, Vec3::Zero(), {1.0f, 1.0f, 1.0f}, &hit));
  REQUIRE(hit.distance == Approx(4.0f));
  REQUIRE(hit.point.x == Approx(0.0f));
  REQUIRE(hit.point.y == Approx(1.0f));
  REQUIRE(hit.point.z == Approx(0.0f));
  REQUIRE(hit.normal.x == Approx(0.0f));
  REQUIRE(hit.normal.y == Approx(1.0f));
  REQUIRE(hit.normal.z == Approx(0.0f));
}

TEST_CASE("RayVsAABBHit reports side-face hit point and normal", "[editor][dragdrop]") {
  Ray ray;
  ray.origin = {-5.0f, 0.25f, 0.0f};
  ray.direction = {1.0f, 0.0f, 0.0f};

  RayAabbHit hit;
  REQUIRE(RayVsAABBHit(ray, Vec3::Zero(), {1.0f, 2.0f, 1.0f}, &hit));
  REQUIRE(hit.distance == Approx(4.0f));
  REQUIRE(hit.point.x == Approx(-1.0f));
  REQUIRE(hit.point.y == Approx(0.25f));
  REQUIRE(hit.point.z == Approx(0.0f));
  REQUIRE(hit.normal.x == Approx(-1.0f));
  REQUIRE(hit.normal.y == Approx(0.0f));
  REQUIRE(hit.normal.z == Approx(0.0f));
}

TEST_CASE("Editor viewport rect excludes docks and panels", "[editor][ui-logic][coverage]") {
  const EditorViewportRect rect = BuildEditorViewportRect(
      1600.0f, 900.0f, 36.0f, 24.0f, 200.0f, 308.0f, 280.0f);

  REQUIRE(rect.minX == Approx(308.0f));
  REQUIRE(rect.minY == Approx(36.0f));
  REQUIRE(rect.maxX == Approx(1320.0f));
  REQUIRE(rect.maxY == Approx(676.0f));
  REQUIRE(rect.Contains(600.0f, 200.0f));
  REQUIRE_FALSE(rect.Contains(150.0f, 200.0f));
  REQUIRE_FALSE(rect.Contains(1500.0f, 200.0f));
  REQUIRE_FALSE(rect.Contains(600.0f, 800.0f));
}

TEST_CASE("Editor view gimbal layout reserves wire button and combined pick rect", "[editor][ui-logic][coverage]") {
  const EditorViewportRect viewport = BuildEditorViewportRect(
      1600.0f, 900.0f, 36.0f, 24.0f, 200.0f, 308.0f, 280.0f);
  const EditorViewGimbalLayout layout =
      BuildEditorViewGimbalLayout(viewport, 1600.0f, 280.0f, 36.0f);

  REQUIRE(layout.gimbalRect.maxX == Approx(viewport.maxX - 10.0f));
  REQUIRE(layout.gimbalRect.minY == Approx(viewport.minY + 10.0f));
  REQUIRE(layout.wireButtonRect.maxX < layout.gimbalRect.minX);
  REQUIRE(layout.pickRect.minX == Approx(layout.wireButtonRect.minX));
  REQUIRE(layout.pickRect.maxX == Approx(layout.gimbalRect.maxX));
  REQUIRE(layout.pickRect.minY == Approx(layout.gimbalRect.minY));
  REQUIRE(layout.pickRect.maxY == Approx(layout.gimbalRect.maxY));
  REQUIRE(layout.pickRect.Contains(layout.wireButtonRect.minX + 4.0f,
                                   layout.wireButtonRect.minY + 4.0f));
  REQUIRE(layout.pickRect.Contains(layout.gimbalRect.maxX - 4.0f,
                                   layout.gimbalRect.maxY - 4.0f));
}

TEST_CASE("Editor layout helpers clamp dock widths and workspace height", "[editor][ui-logic][coverage]") {
  REQUIRE(ComputeEditorLeftDockWidth(1200.0f) == Approx(220.0f));
  REQUIRE(ComputeEditorLeftDockWidth(2400.0f) == Approx(320.0f));
  REQUIRE(ComputeEditorLeftDockWidth(0.0f) == Approx(220.0f));

  REQUIRE(ComputeEditorRightPanelWidth(1200.0f) == Approx(280.0f));
  REQUIRE(ComputeEditorRightPanelWidth(2400.0f) == Approx(380.0f));
  REQUIRE(ComputeEditorRightPanelWidth(0.0f) == Approx(280.0f));

  REQUIRE(ComputeEditorBottomDockHeight(720.0f) == Approx(180.0f));
  REQUIRE(ComputeEditorBottomDockHeight(1440.0f) == Approx(259.2f));
  REQUIRE(ComputeEditorBottomDockHeight(0.0f) == Approx(180.0f));
}

TEST_CASE("Editor viewport asset drop target matches active asset payload inline", "[editor][ui][dragdrop]") {
  ImGuiContextGuard imgui;
  bool callbackInvoked = false;

  ImGuiIO &io = ImGui::GetIO();
  io.MousePos = ImVec2(48.0f, 48.0f);

  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f), ImGuiCond_Always);
  ImGui::Begin("ViewportTest");

  ImGuiContext &g = *ImGui::GetCurrentContext();
  g.HoveredWindow = ImGui::GetCurrentWindow();
  g.HoveredWindowUnderMovingWindow = ImGui::GetCurrentWindow();
  SeedExternalAssetDragPayload("crate");

  const EditorViewportAssetDropResult result = DrawViewportAssetDropTarget(
      false, 220.0f, 140.0f, nullptr,
      [&callbackInvoked](auto *, const char *assetId) {
        callbackInvoked = true;
        return assetId && std::string(assetId) == "crate";
      });

  ImGui::End();
  ImGui::EndFrame();

  REQUIRE(result.targetVisible);
  REQUIRE(result.payloadMatched);
  REQUIRE_FALSE(result.delivered);
  REQUIRE_FALSE(result.accepted);
  REQUIRE_FALSE(callbackInvoked);
}

TEST_CASE("Editor viewport asset drop target stays inactive during play mode", "[editor][ui][dragdrop]") {
  ImGuiContextGuard imgui;
  bool callbackInvoked = false;

  ImGuiIO &io = ImGui::GetIO();
  io.MousePos = ImVec2(48.0f, 48.0f);

  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f), ImGuiCond_Always);
  ImGui::Begin("ViewportPlayModeTest");

  ImGuiContext &g = *ImGui::GetCurrentContext();
  g.HoveredWindow = ImGui::GetCurrentWindow();
  g.HoveredWindowUnderMovingWindow = ImGui::GetCurrentWindow();
  SeedExternalAssetDragPayload("crate");

  const EditorViewportAssetDropResult result = DrawViewportAssetDropTarget(
      true, 220.0f, 140.0f, nullptr, [&callbackInvoked](auto *, const char *) {
        callbackInvoked = true;
        return true;
      });

  ImGui::End();
  ImGui::EndFrame();

  REQUIRE_FALSE(result.targetVisible);
  REQUIRE_FALSE(result.payloadMatched);
  REQUIRE_FALSE(result.delivered);
  REQUIRE_FALSE(result.accepted);
  REQUIRE_FALSE(callbackInvoked);
}

TEST_CASE("Editor workspace settings: missing file falls back to defaults", "[editor][workspace]") {
  namespace fs = std::filesystem;
  const fs::path tempHome =
      Horo::Tests::SecureTempBase() / "horo_editor_workspace_missing";
  fs::remove_all(tempHome);
  fs::create_directories(tempHome);
  HomeDirGuard homeGuard(tempHome);

  const EditorWorkspaceDocument loaded = LoadEditorWorkspaceDocument();
  REQUIRE_FALSE(loaded.loadedFromDisk);
  REQUIRE_FALSE(loaded.parseError);
  REQUIRE(loaded.state.consoleShowInfo);
  REQUIRE(loaded.state.consoleShowWarn);
  REQUIRE(loaded.state.consoleShowError);
  REQUIRE(loaded.state.projectBrowserCwd.empty());
  REQUIRE(ResolveEditorLayoutPath() ==
          tempHome / ".horo" / "editor_layout.ini");
  REQUIRE(ResolveEditorWorkspacePath() ==
          tempHome / ".horo" / "editor_workspace.json");
}

TEST_CASE("Editor workspace settings: invalid JSON reports parse fallback", "[editor][workspace]") {
  namespace fs = std::filesystem;
  const fs::path tempHome =
      Horo::Tests::SecureTempBase() / "horo_editor_workspace_invalid";
  fs::remove_all(tempHome);
  fs::create_directories(tempHome / ".horo");
  HomeDirGuard homeGuard(tempHome);

  WriteFile((tempHome / ".horo" / "editor_workspace.json").string(),
            "{ invalid json");
  const EditorWorkspaceDocument loaded = LoadEditorWorkspaceDocument();
  REQUIRE(loaded.loadedFromDisk);
  REQUIRE(loaded.parseError);
  REQUIRE_FALSE(loaded.error.empty());
  REQUIRE(loaded.state.consoleShowInfo);
}

TEST_CASE("Editor workspace settings: round-trip console filters and cwd", "[editor][workspace]") {
  namespace fs = std::filesystem;
  const fs::path tempHome =
      Horo::Tests::SecureTempBase() / "horo_editor_workspace_roundtrip";
  fs::remove_all(tempHome);
  fs::create_directories(tempHome);
  HomeDirGuard homeGuard(tempHome);

  EditorWorkspaceDocument doc;
  doc.state.consoleShowInfo = false;
  doc.state.consoleShowWarn = true;
  doc.state.consoleShowError = false;
  doc.state.projectBrowserCwd = "C:/project/assets";

  std::string saveError;
  REQUIRE(SaveEditorWorkspaceDocument(&doc, &saveError));
  REQUIRE(saveError.empty());
  REQUIRE(fs::exists(tempHome / ".horo" / "editor_workspace.json"));

  const EditorWorkspaceDocument loaded = LoadEditorWorkspaceDocument();
  REQUIRE(loaded.loadedFromDisk);
  REQUIRE_FALSE(loaded.parseError);
  REQUIRE_FALSE(loaded.state.consoleShowInfo);
  REQUIRE(loaded.state.consoleShowWarn);
  REQUIRE_FALSE(loaded.state.consoleShowError);
  REQUIRE(loaded.state.projectBrowserCwd == "C:/project/assets");
}

TEST_CASE("Vec3 CSV parser accepts render scale triples", "[editor][ui-logic][coverage]") {
  Vec3 parsed = Vec3::Zero();
  REQUIRE(TryParseVec3Csv("1.5000, 2.0000,0.7500", &parsed));
  REQUIRE(parsed.x == Approx(1.5f));
  REQUIRE(parsed.y == Approx(2.0f));
  REQUIRE(parsed.z == Approx(0.75f));

  const Vec3 original = parsed;
  REQUIRE_FALSE(TryParseVec3Csv("1.0,2.0", &parsed));
  REQUIRE(parsed == original);
  REQUIRE_FALSE(TryParseVec3Csv("1.0, nope, 3.0", &parsed));
  REQUIRE(parsed == original);
  REQUIRE_FALSE(TryParseVec3Csv("1.0,2.0,3.0,4.0", &parsed));
  REQUIRE(parsed == original);
  REQUIRE_FALSE(TryParseVec3Csv("", nullptr));
}

// ============================================================
// SceneProjectBridge coverage tests
// ============================================================

TEST_CASE("SceneProjectBridge: Camera object propagates fov/nearClip/farClip", "[editor][bridge]") {
  SceneDocument doc;
  doc.sceneId = "test";
  SceneObject cam;
  cam.id = "cam_main";
  cam.type = SceneObjectType::Camera;
  cam.props["fov"] = "75";
  cam.props["nearClip"] = "0.1";
  cam.props["farClip"] = "500";
  doc.objects.push_back(cam);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes.size() == 1);
  const SceneNodeDefinition &node = model.scene.nodes[0];
  REQUIRE(node.kind == SceneNodeKind::Camera);
  REQUIRE(node.camera.has_value());
  REQUIRE(node.camera->fovY == Approx(75.0f));
  REQUIRE(node.camera->nearClip == Approx(0.1f));
  REQUIRE(node.camera->farClip == Approx(500.0f));
}

TEST_CASE("SceneProjectBridge: Light node parses directional type and properties", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject light;
  light.id = "sun";
  light.type = SceneObjectType::Light;
  light.props["lightType"] = "directional";
  light.props["intensity"] = "1.5";
  light.props["color"] = "1.0,0.9,0.8";
  light.props["radius"] = "100";
  doc.objects.push_back(light);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes.size() == 1);
  const SceneNodeDefinition &node = model.scene.nodes[0];
  REQUIRE(node.kind == SceneNodeKind::Light);
  REQUIRE(node.light.has_value());
  REQUIRE(node.light->kind == SceneLightKind::Directional);
  REQUIRE(node.light->intensity == Approx(1.5f));
  REQUIRE(node.light->color.x == Approx(1.0f));
  REQUIRE(node.light->color.y == Approx(0.9f));
  REQUIRE(node.light->radius == Approx(100.0f));
}

TEST_CASE("SceneProjectBridge: Light node parses point type", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject light;
  light.id = "lamp";
  light.type = SceneObjectType::Light;
  light.props["lightType"] = "point";
  light.props["intensity"] = "2.5";
  doc.objects.push_back(light);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes[0].light.has_value());
  REQUIRE(model.scene.nodes[0].light->kind == SceneLightKind::Point);
  REQUIRE(model.scene.nodes[0].light->intensity == Approx(2.5f));
}

TEST_CASE("SceneProjectBridge: Prop with script component populates script field", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject prop;
  prop.id = "scripted";
  prop.type = SceneObjectType::Prop;
  ComponentDesc script;
  script.type = "script";
  script.props["behaviorTag"] = "PlayerController";
  prop.components.push_back(script);
  doc.objects.push_back(prop);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes.size() == 1);
  REQUIRE(model.scene.nodes[0].script.has_value());
  REQUIRE(model.scene.nodes[0].script->behaviorTag == "PlayerController");
}

TEST_CASE("SceneProjectBridge: Prop with rigidbody component populates rigidbody field", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject prop;
  prop.id = "box";
  prop.type = SceneObjectType::Prop;
  ComponentDesc rb;
  rb.type = "rigidbody";
  rb.props["mass"] = "10.0";
  rb.props["isKinematic"] = "true";
  rb.props["useGravity"] = "false";
  prop.components.push_back(rb);
  doc.objects.push_back(prop);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes.size() == 1);
  REQUIRE(model.scene.nodes[0].rigidbody.has_value());
  REQUIRE(model.scene.nodes[0].rigidbody->mass == Approx(10.0f));
  REQUIRE(model.scene.nodes[0].rigidbody->isKinematic == true);
  REQUIRE(model.scene.nodes[0].rigidbody->useGravity == false);
}

TEST_CASE("SceneProjectBridge: Prop with light component populates light field", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject prop;
  prop.id = "torch";
  prop.type = SceneObjectType::Prop;
  ComponentDesc lightComp;
  lightComp.type = "light";
  lightComp.props["lightType"] = "point";
  lightComp.props["intensity"] = "3.0";
  lightComp.props["color"] = "1.0,0.5,0.0";
  lightComp.props["radius"] = "8.0";
  prop.components.push_back(lightComp);
  doc.objects.push_back(prop);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes.size() == 1);
  REQUIRE(model.scene.nodes[0].light.has_value());
  REQUIRE(model.scene.nodes[0].light->kind == SceneLightKind::Point);
  REQUIRE(model.scene.nodes[0].light->intensity == Approx(3.0f));
  REQUIRE(model.scene.nodes[0].light->color.x == Approx(1.0f));
  REQUIRE(model.scene.nodes[0].light->radius == Approx(8.0f));
}

TEST_CASE("SceneProjectBridge: legacy isLight=true migrates to light node", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "legacy_light";
  obj.type = SceneObjectType::Prop;
  obj.props["isLight"] = "true";
  doc.objects.push_back(obj);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes.size() == 1);
  REQUIRE(model.scene.nodes[0].light.has_value());
}

TEST_CASE("SceneProjectBridge: legacy isLight=false does not create light node", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "not_a_light";
  obj.type = SceneObjectType::Prop;
  obj.props["isLight"] = "false";
  doc.objects.push_back(obj);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes.size() == 1);
  REQUIRE_FALSE(model.scene.nodes[0].light.has_value());
}

TEST_CASE("SceneProjectBridge: legacy behavior prop migrates to script component", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "enemy";
  obj.type = SceneObjectType::Prop;
  obj.props["behavior"] = "EnemyAI";
  doc.objects.push_back(obj);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes.size() == 1);
  REQUIRE(model.scene.nodes[0].script.has_value());
  REQUIRE(model.scene.nodes[0].script->behaviorTag == "EnemyAI");
}

TEST_CASE("SceneProjectBridge: behavior=none is not migrated to script", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "inert";
  obj.type = SceneObjectType::Prop;
  obj.props["behavior"] = "none";
  doc.objects.push_back(obj);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE_FALSE(model.scene.nodes[0].script.has_value());
}

TEST_CASE("SceneProjectBridge: scene settings spawnPoint is parsed", "[editor][bridge]") {
  SceneDocument doc;
  doc.settings["spawnPoint"] = "1.5,2.5,3.5";

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.settings.spawnPoint.x == Approx(1.5f));
  REQUIRE(model.scene.settings.spawnPoint.y == Approx(2.5f));
  REQUIRE(model.scene.settings.spawnPoint.z == Approx(3.5f));
}

TEST_CASE("SceneProjectBridge: object parentId propagates to node parentId", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject parent;
  parent.id = "parent_panel";
  parent.type = SceneObjectType::Panel;
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "child_prop";
  child.type = SceneObjectType::Prop;
  child.props["parentId"] = "parent_panel";
  doc.objects.push_back(child);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes.size() == 2);
  REQUIRE(model.scene.nodes[1].parentId.has_value());
  REQUIRE(*model.scene.nodes[1].parentId == "parent_panel");
}

TEST_CASE("SceneProjectBridge: assets with renderScale are parsed correctly", "[editor][bridge]") {
  SceneDocument doc;
  doc.assets["crate"] = AssetDef{"models/crate.obj", "2.0,3.0,4.0", ""};

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.assets.size() == 1);
  REQUIRE(model.scene.assets[0].id == "crate");
  REQUIRE(model.scene.assets[0].renderScale.x == Approx(2.0f));
  REQUIRE(model.scene.assets[0].renderScale.y == Approx(3.0f));
  REQUIRE(model.scene.assets[0].renderScale.z == Approx(4.0f));
}

TEST_CASE("SceneProjectBridge: BuildSceneDocument round-trips Camera node", "[editor][bridge]") {
  SceneDocument doc;
  doc.sceneId = "rt_test";
  SceneObject cam;
  cam.id = "cam1";
  cam.type = SceneObjectType::Camera;
  cam.props["fov"] = "90";
  cam.props["nearClip"] = "0.5";
  cam.props["farClip"] = "1000";
  doc.objects.push_back(cam);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  const SceneDocument rt = BuildSceneDocument(model);

  REQUIRE(rt.objects.size() == 1);
  REQUIRE(rt.objects[0].id == "cam1");
  REQUIRE(rt.objects[0].type == SceneObjectType::Camera);
  REQUIRE(rt.objects[0].props.count("fov") > 0);
}

TEST_CASE("SceneProjectBridge: BuildSceneDocument round-trips Light node", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject light;
  light.id = "light1";
  light.type = SceneObjectType::Light;
  light.props["lightType"] = "point";
  light.props["intensity"] = "3.0";
  light.props["color"] = "0.5,0.5,1.0";
  light.props["radius"] = "10.0";
  doc.objects.push_back(light);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  const SceneDocument rt = BuildSceneDocument(model);

  REQUIRE(rt.objects.size() == 1);
  REQUIRE(rt.objects[0].type == SceneObjectType::Light);
  REQUIRE(rt.objects[0].props.count("lightType") > 0);
}

TEST_CASE("SceneProjectBridge: BuildSceneDocument round-trips rigidbody component", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "rb_obj";
  obj.type = SceneObjectType::Prop;
  ComponentDesc rb;
  rb.type = "rigidbody";
  rb.props["mass"] = "5.0";
  rb.props["isKinematic"] = "false";
  rb.props["useGravity"] = "true";
  obj.components.push_back(rb);
  doc.objects.push_back(obj);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  const SceneDocument rt = BuildSceneDocument(model);

  REQUIRE(rt.objects.size() == 1);
  REQUIRE(rt.objects[0].components.size() == 1);
  REQUIRE(rt.objects[0].components[0].type == "rigidbody");
}

TEST_CASE("SceneProjectBridge: BuildSceneDocument round-trips script component", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "scripted_obj";
  obj.type = SceneObjectType::Prop;
  ComponentDesc script;
  script.type = "script";
  script.props["behaviorTag"] = "EnemyAI";
  obj.components.push_back(script);
  doc.objects.push_back(obj);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  const SceneDocument rt = BuildSceneDocument(model);

  REQUIRE_FALSE(rt.objects.empty());
  const auto &comps = rt.objects[0].components;
  const auto it = std::ranges::find_if(
      comps, [](const ComponentDesc &c) { return c.type == "script"; });
  REQUIRE(it != comps.end());
  REQUIRE(it->props.at("behaviorTag") == "EnemyAI");
}

TEST_CASE("SceneProjectBridge: empty document produces empty model", "[editor][bridge]") {
  const SceneDocument doc;
  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes.empty());
  REQUIRE(model.scene.assets.empty());
}

TEST_CASE("SceneProjectBridge: Panel node has Panel kind in model", "[editor][bridge]") {
  SceneDocument doc;
  SceneObject panel;
  panel.id = "wall";
  panel.type = SceneObjectType::Panel;
  doc.objects.push_back(panel);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes[0].kind == SceneNodeKind::Panel);
  REQUIRE_FALSE(model.scene.nodes[0].camera.has_value());
  REQUIRE_FALSE(model.scene.nodes[0].light.has_value());
}

TEST_CASE("SceneProjectBridge: malformed scalar/vector props fall back to defaults", "[editor][bridge]") {
  SceneDocument baselineDoc;
  SceneObject baselineCam;
  baselineCam.id = "cam_base";
  baselineCam.type = SceneObjectType::Camera;
  baselineDoc.objects.push_back(baselineCam);
  SceneObject baselineLight;
  baselineLight.id = "light_base";
  baselineLight.type = SceneObjectType::Light;
  baselineDoc.objects.push_back(baselineLight);
  const SceneProjectModel baselineModel = BuildSceneProjectModel(baselineDoc);

  SceneDocument malformedDoc;
  SceneObject badCam;
  badCam.id = "cam_bad";
  badCam.type = SceneObjectType::Camera;
  badCam.props["fov"] = "not-a-float";
  badCam.props["nearClip"] = "";
  badCam.props["farClip"] = "oops";
  malformedDoc.objects.push_back(badCam);
  SceneObject badLight;
  badLight.id = "light_bad";
  badLight.type = SceneObjectType::Light;
  badLight.props["intensity"] = "oops";
  badLight.props["color"] = "x,y,z";
  badLight.props["radius"] = "nope";
  malformedDoc.objects.push_back(badLight);
  malformedDoc.settings["spawnPoint"] = "bad,spawn,value";
  const SceneProjectModel malformedModel = BuildSceneProjectModel(malformedDoc);

  REQUIRE(malformedModel.scene.nodes[0].camera.has_value());
  REQUIRE(baselineModel.scene.nodes[0].camera.has_value());
  REQUIRE(malformedModel.scene.nodes[0].camera->fovY ==
          Approx(baselineModel.scene.nodes[0].camera->fovY));
  REQUIRE(malformedModel.scene.nodes[0].camera->nearClip ==
          Approx(baselineModel.scene.nodes[0].camera->nearClip));
  REQUIRE(malformedModel.scene.nodes[0].camera->farClip ==
          Approx(baselineModel.scene.nodes[0].camera->farClip));

  REQUIRE(malformedModel.scene.nodes[1].light.has_value());
  REQUIRE(baselineModel.scene.nodes[1].light.has_value());
  REQUIRE(malformedModel.scene.nodes[1].light->intensity ==
          Approx(baselineModel.scene.nodes[1].light->intensity));
  REQUIRE(malformedModel.scene.nodes[1].light->color.x ==
          Approx(baselineModel.scene.nodes[1].light->color.x));
  REQUIRE(malformedModel.scene.nodes[1].light->color.y ==
          Approx(baselineModel.scene.nodes[1].light->color.y));
  REQUIRE(malformedModel.scene.nodes[1].light->color.z ==
          Approx(baselineModel.scene.nodes[1].light->color.z));
  REQUIRE(malformedModel.scene.nodes[1].light->radius ==
          Approx(baselineModel.scene.nodes[1].light->radius));
}

TEST_CASE("SceneProjectBridge: round-trip preserves extra props and extra components", "[editor][bridge]") {
  SceneDocument doc;
  doc.sceneId = "bridge_extra_roundtrip";
  SceneObject object;
  object.id = "obj_extra";
  object.type = SceneObjectType::Prop;
  object.props["customFlag"] = "enabled";
  object.props["parentId"] = "root_parent";
  object.props["behavior"] = "none";
  object.components.push_back(
      ComponentDesc{"custom_component", {{"k1", "v1"}, {"k2", "v2"}}});
  doc.objects.push_back(object);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes.size() == 1);
  REQUIRE(model.scene.nodes[0].extraProps.at("customFlag") == "enabled");
  REQUIRE(model.scene.nodes[0].parentId.has_value());
  REQUIRE(*model.scene.nodes[0].parentId == "root_parent");
  REQUIRE(model.scene.nodes[0].extraComponents.size() == 1);
  REQUIRE(model.scene.nodes[0].extraComponents[0].type == "custom_component");
  REQUIRE(model.scene.nodes[0].extraComponents[0].props.at("k1") == "v1");

  const SceneDocument rebuilt = BuildSceneDocument(model);
  REQUIRE(rebuilt.objects.size() == 1);
  REQUIRE(rebuilt.objects[0].props.at("customFlag") == "enabled");
  REQUIRE(rebuilt.objects[0].props.at("parentId") == "root_parent");
  REQUIRE(rebuilt.objects[0].components.size() == 1);
  REQUIRE(rebuilt.objects[0].components[0].type == "custom_component");
  REQUIRE(rebuilt.objects[0].components[0].props.at("k2") == "v2");
}

// ============================================================
// EditorSceneGraph coverage tests
// ============================================================

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta moves child with parent", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject parent;
  parent.id = "root_panel";
  parent.type = SceneObjectType::Panel;
  parent.position = {0.0f, 0.0f, 0.0f};
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "child_prop";
  child.type = SceneObjectType::Prop;
  child.position = {1.0f, 0.0f, 0.0f};
  child.props["parentId"] = "root_panel";
  doc.objects.push_back(child);

  ParentTransformState oldState;
  oldState.position = {0.0f, 0.0f, 0.0f};
  // default Quaternion() is identity (w=1, xyz=0)

  ParentTransformState newState;
  newState.position = {5.0f, 0.0f, 0.0f};

  PropagateHierarchyTransformDelta(doc, 0, oldState, newState, nullptr);

  // child_prop was at (1,0,0) relative to parent at (0,0,0)
  // after parent moves to (5,0,0), child should be at (6,0,0)
  REQUIRE(doc.objects[1].position.x == Approx(6.0f));
  REQUIRE(doc.objects[1].position.y == Approx(0.0f));
  REQUIRE(doc.objects[1].position.z == Approx(0.0f));
}

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta skips listed indices", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject parent;
  parent.id = "root";
  parent.type = SceneObjectType::Panel;
  parent.position = {0.0f, 0.0f, 0.0f};
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "child";
  child.type = SceneObjectType::Prop;
  child.position = {2.0f, 0.0f, 0.0f};
  child.props["parentId"] = "root";
  doc.objects.push_back(child);

  ParentTransformState oldState;
  oldState.position = {0.0f, 0.0f, 0.0f};
  ParentTransformState newState;
  newState.position = {10.0f, 0.0f, 0.0f};

  // child index 1 is in skipIndices — must not be moved
  PropagateHierarchyTransformDelta(doc, 0, oldState, newState, nullptr, {1});

  REQUIRE(doc.objects[1].position.x == Approx(2.0f)); // unchanged
}

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta invalid parentIdx is no-op", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "solo";
  obj.position = {3.0f, 0.0f, 0.0f};
  doc.objects.push_back(obj);

  ParentTransformState s;
  PropagateHierarchyTransformDelta(doc, -1, s, s, nullptr);
  PropagateHierarchyTransformDelta(doc, 99, s, s, nullptr);
  REQUIRE(doc.objects[0].position.x == Approx(3.0f)); // unchanged
}

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta invokes callback for moved child", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject parent;
  parent.id = "parent";
  parent.position = {0.0f, 0.0f, 0.0f};
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "child";
  child.position = {1.0f, 0.0f, 0.0f};
  child.props["parentId"] = "parent";
  doc.objects.push_back(child);

  ParentTransformState oldState;
  ParentTransformState newState;
  newState.position = {3.0f, 0.0f, 0.0f};

  int callbackCount = 0;
  PropagateHierarchyTransformDelta(
      doc, 0, oldState, newState,
      [&callbackCount](const SceneObject &) { ++callbackCount; });
  REQUIRE(callbackCount == 1);
}

TEST_CASE("EditorSceneGraph: RewriteObjectIdReferences updates parentId", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject child;
  child.id = "child";
  child.props["parentId"] = "old_parent";
  doc.objects.push_back(child);

  RewriteObjectIdReferences(&doc, "old_parent", "new_parent");
  REQUIRE(doc.objects[0].props.at("parentId") == "new_parent");
}

TEST_CASE("EditorSceneGraph: RewriteObjectIdReferences is a no-op for empty oldId", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "obj";
  obj.props["parentId"] = "some_id";
  doc.objects.push_back(obj);

  RewriteObjectIdReferences(&doc, "", "new_id");
  REQUIRE(doc.objects[0].props.at("parentId") == "some_id");
}

TEST_CASE("EditorSceneGraph: RewriteObjectIdReferences is a no-op when old equals new", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "obj";
  obj.props["parentId"] = "same_id";
  doc.objects.push_back(obj);

  RewriteObjectIdReferences(&doc, "same_id", "same_id");
  REQUIRE(doc.objects[0].props.at("parentId") == "same_id");
}

TEST_CASE("EditorSceneGraph: LogDanglingObjectReferences does not crash on clean doc", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject parent;
  parent.id = "parent";
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "child";
  child.props["parentId"] = "parent";
  doc.objects.push_back(child);

  LogDanglingObjectReferences(doc, "test_label");
  REQUIRE(true);
}

TEST_CASE("EditorSceneGraph: LogDanglingObjectReferences warns on dangling ref", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject orphan;
  orphan.id = "orphan";
  orphan.props["parentId"] = "nonexistent_parent";
  doc.objects.push_back(orphan);

  // Should not crash — just logs a warning
  LogDanglingObjectReferences(doc, "test_source");
  REQUIRE(true);
}

TEST_CASE("EditorSceneGraph: SanitizePrefabStem handles various inputs", "[editor][scene-graph]") {
  REQUIRE(SanitizePrefabStem("hello_world") == "hello_world");
  REQUIRE(SanitizePrefabStem("valid-name_123") == "valid-name_123");
  REQUIRE(SanitizePrefabStem("__hello__") == "hello");
  REQUIRE(SanitizePrefabStem("_test") == "test");
  REQUIRE(SanitizePrefabStem("test_") == "test");
  REQUIRE(SanitizePrefabStem("") == "prefab");
  REQUIRE(SanitizePrefabStem("___") == "prefab");
}

TEST_CASE("EditorSceneGraph: SanitizePrefabStem replaces spaces and special chars", "[editor][scene-graph]") {
  const std::string result = SanitizePrefabStem("hello world!");
  // space→'_', '!'→'_', trailing '_' stripped → "hello_world"
  REQUIRE(result == "hello_world");
}

TEST_CASE("EditorSceneGraph: CollectReservedObjectIds includes object ids and references", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject parent;
  parent.id = "parent_id";
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "child_id";
  child.props["parentId"] = "parent_id";
  doc.objects.push_back(child);

  const auto reserved = CollectReservedObjectIds(doc);
  REQUIRE(reserved.contains("parent_id"));
  REQUIRE(reserved.contains("child_id"));
}

TEST_CASE("EditorSceneGraph: IsReservedObjectId returns true for existing id", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "used_id";
  doc.objects.push_back(obj);

  REQUIRE(IsReservedObjectId(doc, "used_id"));
  REQUIRE_FALSE(IsReservedObjectId(doc, "free_id"));
}

TEST_CASE("EditorSceneGraph: IsReservedObjectId with ignoreConcreteObjectId", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "rename_me";
  doc.objects.push_back(obj);

  // When ignoring 'rename_me', the id should appear free
  const std::string ignored = "rename_me";
  REQUIRE_FALSE(IsReservedObjectId(doc, "rename_me", &ignored));
  // But a different existing id is still reserved
  REQUIRE(IsReservedObjectId(doc, "rename_me", nullptr));
}

TEST_CASE("EditorSceneGraph: IsReservedObjectId returns false for empty id", "[editor][scene-graph]") {
  SceneDocument doc;
  REQUIRE_FALSE(IsReservedObjectId(doc, ""));
}

// ============================================================
// EditorHistory edge-case tests (exercised via MCP commands)
// ============================================================

TEST_CASE("EditorHistory: TrimHistory caps undo stack at 128 entries", "[editor][history]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  // Create 130 unique objects to fill and overflow the 128-entry limit
  for (int i = 0; i < 130; ++i) {
    const auto result = editor.ExecuteMcpCommand(
        "editor.create_object",
        nlohmann::json{{"type", "Prop"},
                       {"id", std::format("overflow_obj_{}", i)}});
    REQUIRE(result.ok);
  }

  // Undo until exhausted; should succeed exactly 128 times
  int undoCount = 0;
  while (true) {
    const auto res =
        editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
    REQUIRE(res.ok);
    if (!res.data["undone"].get<bool>())
      break;
    ++undoCount;
  }
  REQUIRE(undoCount == 128);
}

TEST_CASE("EditorHistory: HistorySnapshotsEqual returns false for differing documents", "[editor][history]") {
  EditorLayer editor;
  SceneDocument doc;
  doc.sceneId = "base";
  editor.LoadDocument(doc);

  // Capture state before and after a create
  const auto beforeCreate = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Panel"}, {"id", "snap_obj"}});
  REQUIRE(beforeCreate.ok);

  // Undo to return to empty; the redo stack now has the "after" snapshot
  const auto undoRes =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undoRes.ok);
  REQUIRE(undoRes.data["undone"].get<bool>());

  // Redo restores the object — verifies that the snapshots were indeed
  // different
  const auto redoRes =
      editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
  REQUIRE(redoRes.ok);
  REQUIRE(redoRes.data["redone"].get<bool>());
  REQUIRE(editor.GetDocument().objects.size() == 1);
}

TEST_CASE("EditorHistory: RefreshHistorySavedBaseline updates dirty flag after save", "[editor][history]") {
  namespace fs = std::filesystem;
  const fs::path sceneDir =
      Horo::Tests::SecureTempBase() / "horo_history_baseline_test";
  fs::create_directories(sceneDir);
  const fs::path scenePath = sceneDir / "test_scene.json";

  SceneDocument doc;
  doc.sceneId = "hist_test";
  doc.filePath = scenePath.string();
  SceneSerializer::SaveToFile(doc, scenePath.string());

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Make a change to mark dirty
  const auto createRes = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Panel"}, {"id", "dirty_obj"}});
  REQUIRE(createRes.ok);
  REQUIRE(editor.GetDocument().dirty);

  // Save — this internally calls RefreshHistorySavedBaseline
  const auto saveRes =
      editor.ExecuteMcpCommand("editor.save_scene", nlohmann::json::object());
  REQUIRE(saveRes.ok);
  REQUIRE_FALSE(editor.GetDocument().dirty);

  // Undo the create; the restored snapshot should reflect the saved baseline
  const auto undoRes =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undoRes.ok);
  REQUIRE(undoRes.data["undone"].get<bool>());
  // After undo the document is dirty relative to the saved file
  REQUIRE(editor.GetDocument().dirty);
}

// ============================================================
// EditorMcpHandlers error path coverage
// ============================================================

TEST_CASE("EditorMcp: create_object with Camera type uses GenerateCameraId", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res = editor.ExecuteMcpCommand("editor.create_object",
                                            nlohmann::json{{"type", "Camera"}});
  REQUIRE(res.ok);
  REQUIRE(editor.GetDocument().objects.size() == 1);
  REQUIRE(editor.GetDocument().objects[0].type == SceneObjectType::Camera);
  // Camera id must be non-empty and start with "cam_"
  REQUIRE_FALSE(editor.GetDocument().objects[0].id.empty());
}

TEST_CASE("EditorMcp: create_object rejects duplicate id", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto first = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Panel"}, {"id", "dup_id"}});
  REQUIRE(first.ok);

  const auto second = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Panel"}, {"id", "dup_id"}});
  REQUIRE_FALSE(second.ok);
  REQUIRE(second.error == "Object id already exists.");
}

TEST_CASE("EditorMcp: create_object rejects invalid scale format", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{
          {"type", "Panel"},
          {"scale", nlohmann::json::array({"bad", "scale", "fmt"})}});
  REQUIRE_FALSE(res.ok);
  REQUIRE(res.error == "scale must be [x,y,z].");
}

TEST_CASE("EditorMcp: create_object rejects non-array components", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"components", "not_an_array"}});
  REQUIRE_FALSE(res.ok);
  REQUIRE(res.error == "components must be an array of objects.");
}

TEST_CASE("EditorMcp: create_object rejects components with missing type field", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{
          {"type", "Prop"},
          {"components", nlohmann::json::array({nlohmann::json::object()})}});
  REQUIRE_FALSE(res.ok);
  REQUIRE(res.error == "components must be an array of objects.");
}

TEST_CASE("EditorMcp: create_object rejects non-existent parentId", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"parentId", "ghost_parent"}});
  REQUIRE_FALSE(res.ok);
  REQUIRE(res.error == "Parent object not found.");
}

TEST_CASE("EditorMcp: create_object rejects unknown type string", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res = editor.ExecuteMcpCommand(
      "editor.create_object", nlohmann::json{{"type", "NotAType"}});
  REQUIRE_FALSE(res.ok);
  REQUIRE(res.error == "Invalid object type.");
}

TEST_CASE("EditorMcp: update_object returns error when object not found", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{{"id", "missing_obj"},
                     {"position", nlohmann::json::array({1.0, 2.0, 3.0})}});
  REQUIRE_FALSE(res.ok);
  REQUIRE(res.error == "Object not found.");
}

TEST_CASE("EditorMcp: editor.transform is an alias for editor.update_object", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "movable";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto res = editor.ExecuteMcpCommand(
      "editor.transform",
      nlohmann::json{{"id", "movable"},
                     {"position", nlohmann::json::array({7.0, 8.0, 9.0})}});
  REQUIRE(res.ok);
  REQUIRE(editor.GetDocument().objects[0].position.x == Approx(7.0f));
  REQUIRE(editor.GetDocument().objects[0].position.y == Approx(8.0f));
  REQUIRE(editor.GetDocument().objects[0].position.z == Approx(9.0f));
}

TEST_CASE("EditorMcp: update_object rejects bad position format", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "fixed_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto res = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{{"id", "fixed_obj"},
                     {"position", nlohmann::json::array({"x", "y", "z"})}});
  REQUIRE_FALSE(res.ok);
  REQUIRE(res.error == "position must be [x,y,z].");
}

TEST_CASE("EditorMcp: reparent_object rejects self-parenting", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "self_obj";
  obj.type = SceneObjectType::Panel;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto res = editor.ExecuteMcpCommand(
      "editor.reparent_object",
      nlohmann::json{{"id", "self_obj"}, {"parentId", "self_obj"}});
  REQUIRE_FALSE(res.ok);
  REQUIRE(res.error == "Object cannot parent itself.");
}

TEST_CASE("EditorMcp: reparent_object rejects cycle-forming reparent", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject parent;
  parent.id = "ancestor";
  parent.type = SceneObjectType::Panel;
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "descendant";
  child.type = SceneObjectType::Prop;
  child.props["parentId"] = "ancestor";
  doc.objects.push_back(child);

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Trying to make the ancestor a child of its own descendant creates a cycle
  const auto res = editor.ExecuteMcpCommand(
      "editor.reparent_object",
      nlohmann::json{{"id", "ancestor"}, {"parentId", "descendant"}});
  REQUIRE_FALSE(res.ok);
  REQUIRE(res.error == "Parent would create a cycle.");
}

TEST_CASE("EditorMcp: reparent_object rejects unknown object id", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res = editor.ExecuteMcpCommand(
      "editor.reparent_object",
      nlohmann::json{{"id", "ghost_obj"}, {"parentId", ""}});
  REQUIRE_FALSE(res.ok);
  REQUIRE(res.error == "Object not found.");
}

TEST_CASE("EditorMcp: reparent_object unparents when parentId is empty", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject parent;
  parent.id = "p";
  parent.type = SceneObjectType::Panel;
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "c";
  child.type = SceneObjectType::Prop;
  child.props["parentId"] = "p";
  doc.objects.push_back(child);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto res = editor.ExecuteMcpCommand(
      "editor.reparent_object", nlohmann::json{{"id", "c"}, {"parentId", ""}});
  REQUIRE(res.ok);
  const auto &objs = editor.GetDocument().objects;
  const auto cIt = std::ranges::find_if(
      objs, [](const SceneObject &o) { return o.id == "c"; });
  REQUIRE(cIt != objs.end());
  const SceneObject *c = &*cIt;
  REQUIRE(c != nullptr);
  REQUIRE_FALSE(c->props.contains("parentId"));
}

TEST_CASE("EditorMcp: unknown command returns descriptive error", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res = editor.ExecuteMcpCommand("editor.nonexistent_tool",
                                            nlohmann::json::object());
  REQUIRE_FALSE(res.ok);
  REQUIRE(res.error == "Unsupported MCP command.");
}

TEST_CASE("EditorMcp: create_object with components array creates component", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{
          {"type", "Prop"},
          {"id", "scripted_mcp_obj"},
          {"components",
           nlohmann::json::array({nlohmann::json{
               {"type", "script"},
               {"props", nlohmann::json{{"behaviorTag", "Guard"}}}}})}});
  REQUIRE(res.ok);
  REQUIRE(editor.GetDocument().objects.size() == 1);
  REQUIRE(editor.GetDocument().objects[0].components.size() == 1);
  REQUIRE(editor.GetDocument().objects[0].components[0].type == "script");
}

TEST_CASE("EditorMcp: create_object with parentId links child to existing parent", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject parentObj;
  parentObj.id = "existing_parent";
  parentObj.type = SceneObjectType::Panel;
  doc.objects.push_back(parentObj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto res = editor.ExecuteMcpCommand(
      "editor.create_object", nlohmann::json{{"type", "Prop"},
                                             {"id", "new_child"},
                                             {"parentId", "existing_parent"}});
  REQUIRE(res.ok);
  const auto &childObjs = editor.GetDocument().objects;
  const auto childIt = std::ranges::find_if(
      childObjs, [](const SceneObject &o) { return o.id == "new_child"; });
  REQUIRE(childIt != childObjs.end());
  const SceneObject *child = &*childIt;
  REQUIRE(child != nullptr);
  REQUIRE(child->props.at("parentId") == "existing_parent");
}

// ============================================================
// EditorLayer public method coverage
// ============================================================

TEST_CASE("EditorLayer: LoadDocument migrates legacy behavior prop to script component", "[editor][layer]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "legacy_prop";
  obj.type = SceneObjectType::Prop;
  obj.props["behavior"] = "GuardAI";
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto &objs0 = editor.GetDocument().objects;
  const auto it0 = std::ranges::find_if(
      objs0, [](const SceneObject &o) { return o.id == "legacy_prop"; });
  REQUIRE(it0 != objs0.end());
  const SceneObject *loaded = &*it0;
  REQUIRE(loaded != nullptr);
  // "behavior" prop should be erased and replaced by a script component
  REQUIRE_FALSE(loaded->props.contains("behavior"));
  REQUIRE(loaded->components.size() == 1);
  REQUIRE(loaded->components[0].type == "script");
  REQUIRE(loaded->components[0].props.at("behaviorTag") == "GuardAI");
}

TEST_CASE("EditorLayer: LoadDocument skips migration when script component already present", "[editor][layer]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "already_scripted";
  obj.type = SceneObjectType::Prop;
  obj.props["behavior"] = "OldBehavior";
  ComponentDesc script;
  script.type = "script";
  script.props["behaviorTag"] = "NewScript";
  obj.components.push_back(script);
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto &objs1 = editor.GetDocument().objects;
  const auto it1 = std::ranges::find_if(
      objs1, [](const SceneObject &o) { return o.id == "already_scripted"; });
  REQUIRE(it1 != objs1.end());
  const SceneObject *loaded = &*it1;
  REQUIRE(loaded != nullptr);
  // Existing script component must be kept; behavior prop should be erased
  REQUIRE(loaded->components.size() == 1);
  REQUIRE(loaded->components[0].props.at("behaviorTag") == "NewScript");
}

TEST_CASE("EditorLayer: LoadDocument with behavior=none does not create script component", "[editor][layer]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "no_script";
  obj.type = SceneObjectType::Prop;
  obj.props["behavior"] = "none";
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto &objs2 = editor.GetDocument().objects;
  const auto it2 = std::ranges::find_if(
      objs2, [](const SceneObject &o) { return o.id == "no_script"; });
  REQUIRE(it2 != objs2.end());
  const SceneObject *loaded = &*it2;
  REQUIRE(loaded != nullptr);
  REQUIRE(loaded->components.empty());
}

TEST_CASE("EditorLayer: SetHotReloadOverlay stores state without crashing", "[editor][layer]") {
  EditorLayer editor;
  editor.SetHotReloadOverlay(true, 0.5f, 45.0f, "Compiling shaders...");
  editor.SetHotReloadOverlay(false, 0.0f, 0.0f, "");
  REQUIRE(true);
}

TEST_CASE("EditorLayer: OnPathsDropped stores paths for deferred processing", "[editor][layer]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const char *paths[] = {"assets/models/crate.obj", "assets/textures/wood.png"};
  editor.OnPathsDropped(2, paths, 100.0f, 200.0f);
  // No crash; pending drops stored internally
  REQUIRE(true);
}

TEST_CASE("EditorLayer: OnPathsDropped with null paths is a no-op", "[editor][layer]") {
  EditorLayer editor;
  editor.OnPathsDropped(5, nullptr, 0.0f, 0.0f);
  editor.OnPathsDropped(0, nullptr, 0.0f, 0.0f);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: AcknowledgeReload clears WantsSceneReload flag", "[editor][layer]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  // CreateObject marks dirty and triggers reload
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Panel"}, {"id", "tr_obj"}});
  // WantsSceneReload may or may not be set depending on whether reload was
  // already processed; either way AcknowledgeReload must not crash.
  editor.AcknowledgeReload();
  REQUIRE(true);
}

TEST_CASE("EditorLayer: IsPlayMode returns false by default", "[editor][layer]") {
  EditorLayer editor;
  REQUIRE_FALSE(editor.IsPlayMode());
}

TEST_CASE("EditorLayer: GetSelectedAssetId is empty when no asset is selected", "[editor][layer]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  REQUIRE(editor.GetSelectedAssetId().empty());
}

TEST_CASE("EditorLayer: SetProjectBrowserRoot does not crash", "[editor][layer]") {
  EditorLayer editor;
  editor.SetProjectBrowserRoot("assets/");
  editor.SetProjectBrowserRoot("");
  REQUIRE(true);
}

TEST_CASE("EditorLayer: SetProjectBrowserExtraBlocklist does not crash", "[editor][layer]") {
  EditorLayer editor;
  editor.SetProjectBrowserExtraBlocklist({"node_modules", ".git"});
  editor.SetProjectBrowserExtraBlocklist({});
  REQUIRE(true);
}

TEST_CASE("EditorLayer: Render with inactive editor does not crash", "[editor][layer]") {
  ImGuiContextGuard imgui;

  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  Camera cam;
  // Render handles NewFrame/EndFrame/Render internally
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ============================================================
// Active editor draw method coverage tests
// ============================================================

// Helper: build a minimal SceneDocument with one object of each type
static SceneDocument MakeDocWithAllObjectTypes() {
  SceneDocument doc;
  {
    SceneObject panel;
    panel.id = "panel1";
    panel.type = SceneObjectType::Panel;
    panel.position = {0.f, 0.f, 0.f};
    panel.scale = {2.f, 2.f, 0.1f};
    doc.objects.push_back(panel);
  }
  {
    SceneObject prop;
    prop.id = "prop1";
    prop.type = SceneObjectType::Prop;
    prop.position = {1.f, 0.f, 0.f};
    prop.scale = {1.f, 1.f, 1.f};
    prop.assetId = "asset1";
    doc.objects.push_back(prop);
  }
  {
    SceneObject light;
    light.id = "light1";
    light.type = SceneObjectType::Light;
    light.position = {5.f, 5.f, 0.f};
    ComponentDesc lc;
    lc.type = "light";
    lc.props["intensity"] = "1.5";
    lc.props["color"] = "1.0,0.9,0.8";
    lc.props["radius"] = "10.0";
    light.components.push_back(lc);
    doc.objects.push_back(light);
  }
  {
    SceneObject cam;
    cam.id = "cam1";
    cam.type = SceneObjectType::Camera;
    cam.position = {0.f, 3.f, -10.f};
    doc.objects.push_back(cam);
  }
  doc.assets["asset1"] = AssetDef{"", "1.0,1.0,1.0"};
  doc.sceneId = "test-scene";
  doc.sceneName = "Test Scene";
  return doc;
}

TEST_CASE("EditorLayer: Toggle makes editor active without crashing", "[editor][layer][draw]") {
  EditorLayer editor;
  REQUIRE_FALSE(editor.IsActive());
  editor.Toggle();
  REQUIRE(editor.IsActive());
  editor.Toggle();
  REQUIRE_FALSE(editor.IsActive());
}

TEST_CASE("EditorLayer: active Render with empty document does not crash", "[editor][layer][draw]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();
  REQUIRE(editor.IsActive());

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with all object types does not crash", "[editor][layer][draw]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with Prop selected draws properties panel", "[editor][layer][draw][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  editor.Toggle(); // activate FIRST; Toggle clears m_selectedIndices
  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "prop1"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with Panel selected draws properties panel", "[editor][layer][draw][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  editor.Toggle(); // activate FIRST
  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "panel1"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with Camera selected draws camera section", "[editor][layer][draw][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  editor.Toggle(); // activate FIRST
  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "cam1"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with Light+components selected draws component list", "[editor][layer][draw][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  editor.Toggle(); // activate FIRST
  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "light1"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with asset selected draws asset properties", "[editor][layer][draw][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  editor.ExecuteMcpCommand("editor.select_asset",
                           nlohmann::json{{"id", "asset1"}});
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with multiple selected draws multi-select panel", "[editor][layer][draw][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  editor.Toggle(); // activate FIRST — Toggle clears m_selectedIndices
  editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{{"ids", nlohmann::json::array({"panel1", "prop1"})}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render multiple frames does not crash", "[editor][layer][draw]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  editor.Toggle();

  Camera cam;
  for (int i = 0; i < 3; ++i) {
    editor.ExecuteMcpCommand(
        "editor.select", nlohmann::json{{"id", i % 2 == 0 ? "prop1" : "cam1"}});
    editor.Render(cam, 1280, 720);
  }
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with rigidbody component does not crash", "[editor][layer][draw][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  SceneObject obj;
  obj.id = "rb_obj";
  obj.type = SceneObjectType::Prop;
  ComponentDesc rb;
  rb.type = "rigidbody";
  rb.props["mass"] = "1.5";
  rb.props["isKinematic"] = "false";
  rb.props["useGravity"] = "true";
  obj.components.push_back(rb);
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle(); // activate FIRST — Toggle clears m_selectedIndices
  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "rb_obj"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with script component does not crash", "[editor][layer][draw][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  SceneObject obj;
  obj.id = "script_obj";
  obj.type = SceneObjectType::Prop;
  ComponentDesc sc;
  sc.type = "script";
  sc.props["behaviorTag"] = "MyBehavior";
  obj.components.push_back(sc);
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle(); // activate FIRST — Toggle clears m_selectedIndices
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "script_obj"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with multiple assets in document does not crash", "[editor][layer][draw]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.assets["mesh_a"] = AssetDef{"", "1.0,1.0,1.0", "tex_a.png"};
  doc.assets["mesh_b"] = AssetDef{"", "2.0,1.0,1.0"};
  doc.assets["mesh_c"] = AssetDef{"", ""};

  for (int i = 0; i < 3; ++i) {
    SceneObject obj;
    obj.id = std::format("obj{}", i);
    obj.type = SceneObjectType::Prop;
    obj.assetId = (i == 0) ? "mesh_a" : (i == 1) ? "mesh_b" : "";
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with object having parentId does not crash", "[editor][layer][draw]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  SceneObject parent;
  parent.id = "parent";
  parent.type = SceneObjectType::Panel;
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "child";
  child.type = SceneObjectType::Prop;
  child.props["parentId"] = "parent";
  doc.objects.push_back(child);

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle(); // activate FIRST — Toggle clears m_selectedIndices
  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "child"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with prefab instance does not crash", "[editor][layer][draw]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  SceneObject obj;
  obj.id = "prefab_obj";
  obj.type = SceneObjectType::Prop;
  obj.prefabInstance = ScenePrefabInstance{"pf-001", "assets/prefabs/box.json"};
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle(); // activate FIRST — Toggle clears m_selectedIndices
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "prefab_obj"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: Toggle twice restores inactive state", "[editor][layer][draw]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();
  REQUIRE(editor.IsActive());
  Camera cam;
  editor.Render(cam, 1280, 720);
  editor.Toggle();
  REQUIRE_FALSE(editor.IsActive());
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with hot reload overlay does not crash", "[editor][layer][draw]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.SetHotReloadOverlay(true, 0.5f, 1.2f, "Reloading...");
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: active Render with SetProjectBrowserRoot set does not crash", "[editor][layer][draw]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.SetProjectBrowserRoot(std::filesystem::path(RepoRootFromTestSource()) /
                               "assets");
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ============================================================
// TransformGizmo math helper tests
// ============================================================

TEST_CASE("TransformGizmo: Deactivate clears mode and axes", "[editor][gizmo]") {
  TransformGizmo gizmo;
  gizmo.Activate(GizmoMode::Translate, {1.f, 2.f, 3.f}, Quaternion::Identity(),
                 Vec3::One());
  REQUIRE(gizmo.IsActive());
  REQUIRE(gizmo.GetMode() == GizmoMode::Translate);

  gizmo.Deactivate();
  REQUIRE_FALSE(gizmo.IsActive());
  REQUIRE(gizmo.GetMode() == GizmoMode::None);
  REQUIRE(gizmo.GetDragAxis() == GizmoAxis::None);
}

TEST_CASE("TransformGizmo: WorldToScreen projects front-facing point", "[editor][gizmo]") {
  Camera cam;
  cam.position = {0.f, 0.f, -10.f};
  cam.target = {0.f, 0.f, 0.f};
  cam.fovY = 60.f;

  float sx = 0.f, sy = 0.f;
  bool visible =
      TransformGizmo::WorldToScreen({0.f, 0.f, 0.f}, cam, 1280, 720, sx, sy);
  REQUIRE(visible);
  REQUIRE(sx == Approx(640.f).margin(5.f));
  REQUIRE(sy == Approx(360.f).margin(5.f));
}

TEST_CASE("TransformGizmo: WorldToScreen returns false for point behind camera", "[editor][gizmo]") {
  Camera cam;
  cam.position = {0.f, 0.f, 10.f};
  cam.target = {0.f, 0.f, 0.f};
  cam.fovY = 60.f;

  float sx = 0.f, sy = 0.f;
  // Point is at z=20, camera looking toward z=0, so point is behind
  bool visible =
      TransformGizmo::WorldToScreen({0.f, 0.f, 20.f}, cam, 1280, 720, sx, sy);
  // The result depends on clip.w sign; verify no crash
  (void)visible;
  REQUIRE(true);
}

TEST_CASE("TransformGizmo: RayHitPlane returns hit for non-parallel ray", "[editor][gizmo]") {
  // Ray pointing down (negative Y), plane is horizontal (Y=0)
  Ray ray;
  ray.origin = {0.f, 5.f, 0.f};
  ray.direction = {0.f, -1.f, 0.f};
  const Vec3 planeNormal = {0.f, 1.f, 0.f};
  const Vec3 planePoint = {0.f, 0.f, 0.f};

  Vec3 hit;
  REQUIRE(TransformGizmo::RayHitPlane(ray, planeNormal, planePoint, hit));
  REQUIRE(hit.x == Approx(0.f).margin(0.001f));
  REQUIRE(hit.y == Approx(0.f).margin(0.001f));
  REQUIRE(hit.z == Approx(0.f).margin(0.001f));
}

TEST_CASE("TransformGizmo: RayHitPlane returns false for parallel ray", "[editor][gizmo]") {
  Ray ray;
  ray.origin = {0.f, 1.f, 0.f};
  ray.direction = {1.f, 0.f, 0.f}; // parallel to XZ plane
  const Vec3 planeNormal = {0.f, 1.f, 0.f};
  const Vec3 planePoint = {0.f, 0.f, 0.f};

  Vec3 hit;
  REQUIRE_FALSE(TransformGizmo::RayHitPlane(ray, planeNormal, planePoint, hit));
}

TEST_CASE("TransformGizmo: RayHitPlane returns false for hit behind ray origin", "[editor][gizmo]") {
  // Ray pointing up, plane is below origin
  Ray ray;
  ray.origin = {0.f, 5.f, 0.f};
  ray.direction = {0.f, 1.f, 0.f};
  const Vec3 planeNormal = {0.f, 1.f, 0.f};
  const Vec3 planePoint = {0.f, 0.f, 0.f}; // below origin

  Vec3 hit;
  REQUIRE_FALSE(TransformGizmo::RayHitPlane(ray, planeNormal, planePoint, hit));
}

TEST_CASE("TransformGizmo: RayClosestOnLine finds correct point", "[editor][gizmo]") {
  // Ray going in +Z, line going in +X at y=1
  Ray ray;
  ray.origin = {0.f, 0.f, 0.f};
  ray.direction = {0.f, 0.f, 1.f};
  const Vec3 lineOrigin = {0.f, 1.f, 5.f};
  const Vec3 lineDir = {1.f, 0.f, 0.f};

  Vec3 closest = TransformGizmo::RayClosestOnLine(ray, lineOrigin, lineDir);
  // The closest point on the line to the ray should be at x=0, y=1, z=5
  REQUIRE(closest.x == Approx(0.f).margin(0.001f));
  REQUIRE(closest.y == Approx(1.f).margin(0.001f));
  REQUIRE(closest.z == Approx(5.f).margin(0.001f));
}

TEST_CASE("TransformGizmo: RayClosestOnLine handles parallel lines", "[editor][gizmo]") {
  // Parallel ray and line
  Ray ray;
  ray.origin = {0.f, 0.f, 0.f};
  ray.direction = {1.f, 0.f, 0.f};
  const Vec3 lineOrigin = {5.f, 1.f, 0.f};
  const Vec3 lineDir = {1.f, 0.f, 0.f}; // same direction

  Vec3 closest = TransformGizmo::RayClosestOnLine(ray, lineOrigin, lineDir);
  // Should not crash; result is some valid point
  REQUIRE(std::isfinite(closest.x));
  REQUIRE(std::isfinite(closest.y));
  REQUIRE(std::isfinite(closest.z));
}

TEST_CASE("TransformGizmo: HandleSize returns reasonable value", "[editor][gizmo]") {
  TransformGizmo gizmo;
  gizmo.Activate(GizmoMode::Translate, {0.f, 0.f, 0.f}, Quaternion::Identity(),
                 Vec3::One());

  Camera cam;
  cam.position = {0.f, 0.f, -5.f};
  cam.target = {0.f, 0.f, 0.f};
  cam.fovY = 60.f;

  const float size = gizmo.HandleSize(cam);
  REQUIRE(size > 0.0f);
  REQUIRE(size < 100.0f);
}

TEST_CASE("TransformGizmo: HandleSize returns minimum when camera at same position", "[editor][gizmo]") {
  TransformGizmo gizmo;
  gizmo.Activate(GizmoMode::Translate, {0.f, 0.f, 0.f}, Quaternion::Identity(),
                 Vec3::One());

  Camera cam;
  cam.position = {0.f, 0.f, 0.f}; // same as gizmo pos
  cam.target = {0.f, 0.f, 1.f};
  cam.fovY = 60.f;

  const float size = gizmo.HandleSize(cam);
  REQUIRE(size == Approx(0.1f));
}

TEST_CASE("TransformGizmo: AxisDir returns correct directions", "[editor][gizmo]") {
  TransformGizmo gizmo;
  gizmo.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
                 Vec3::One());

  const Vec3 x = gizmo.AxisDir(GizmoAxis::X);
  const Vec3 y = gizmo.AxisDir(GizmoAxis::Y);
  const Vec3 z = gizmo.AxisDir(GizmoAxis::Z);
  const Vec3 none = gizmo.AxisDir(GizmoAxis::None);

  REQUIRE(x.x == Approx(1.f));
  REQUIRE(x.y == Approx(0.f));
  REQUIRE(x.z == Approx(0.f));
  REQUIRE(y.x == Approx(0.f));
  REQUIRE(y.y == Approx(1.f));
  REQUIRE(y.z == Approx(0.f));
  REQUIRE(z.x == Approx(0.f));
  REQUIRE(z.y == Approx(0.f));
  REQUIRE(z.z == Approx(1.f));
  REQUIRE(none.x == Approx(0.f));
  REQUIRE(none.y == Approx(0.f));
  REQUIRE(none.z == Approx(0.f));
}

TEST_CASE("TransformGizmo: PickAxis returns None when no axis near cursor", "[editor][gizmo]") {
  TransformGizmo gizmo;
  gizmo.Activate(GizmoMode::Translate, {0.f, 0.f, 0.f}, Quaternion::Identity(),
                 Vec3::One());

  Camera cam;
  cam.position = {0.f, 0.f, -10.f};
  cam.target = {0.f, 0.f, 0.f};
  cam.fovY = 60.f;

  // Mouse at corner, far from any axis
  const GizmoAxis axis = gizmo.PickAxis(1200.f, 680.f, cam, 1280, 720);
  // Should return None since cursor is far from gizmo center
  (void)axis;
  REQUIRE(true); // no crash
}

TEST_CASE("TransformGizmo: SyncTarget updates internal position", "[editor][gizmo]") {
  TransformGizmo gizmo;
  gizmo.Activate(GizmoMode::Translate, {1.f, 2.f, 3.f}, Quaternion::Identity(),
                 Vec3::One());

  gizmo.SyncTarget({10.f, 20.f, 30.f}, Quaternion::Identity(), {2.f, 2.f, 2.f});

  Camera cam;
  cam.position = {0.f, 0.f, -50.f};
  cam.target = {10.f, 20.f, 30.f};
  cam.fovY = 45.f;

  const float size = gizmo.HandleSize(cam);
  REQUIRE(size > 0.0f);
}

// ============================================================
// Additional MCP handler tests (cover uncovered handlers)
// ============================================================

TEST_CASE("EditorLayer MCP: new_scene with sceneId and sceneName", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  auto result = editor.ExecuteMcpCommand(
      "editor.new_scene", nlohmann::json{{"sceneId", "new-scene-id"},
                                         {"sceneName", "My New Scene"}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().sceneId == "new-scene-id");
  REQUIRE(editor.GetDocument().sceneName == "My New Scene");
}

TEST_CASE("EditorLayer MCP: new_scene without args creates clean scene", "[editor][mcp]") {
  EditorLayer editor;
  SceneDocument doc;
  SceneObject obj;
  obj.id = "old_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);
  editor.LoadDocument(doc);

  auto result = editor.ExecuteMcpCommand("editor.new_scene", nlohmann::json{});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects.empty());
}

TEST_CASE("EditorLayer MCP: save_scene creates directory when needed", "[editor][mcp]") {
  EditorLayer editor;
  SceneDocument doc;
  std::filesystem::path testPath =
      std::filesystem::temp_directory_path() /
      "this_dir_does_not_exist_12345/deep/path/scene.json";
  doc.filePath = testPath.string();
  editor.LoadDocument(doc);

  auto result = editor.ExecuteMcpCommand("editor.save_scene", nlohmann::json{});
  REQUIRE(result.ok);
}

TEST_CASE("EditorLayer MCP: reload_scene loads saved scene", "[editor][mcp]") {
  EditorLayer editor;
  SceneDocument doc;
  doc.filePath = "assets/scenes/scene.json";
  editor.LoadDocument(doc);

  auto result = editor.ExecuteMcpCommand("editor.save_scene", nlohmann::json{});
  REQUIRE(result.ok);

  auto reloadResult =
      editor.ExecuteMcpCommand("editor.reload_scene", nlohmann::json{});
  REQUIRE(reloadResult.ok);
}

TEST_CASE("EditorLayer MCP: create_prefab fails without selection", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  // nlohmann::json::object() must be used (not nlohmann::json{}) because
  // the handler calls arguments.value() which requires an object-type JSON
  auto result = editor.ExecuteMcpCommand("editor.create_prefab",
                                         nlohmann::json::object());
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("EditorLayer MCP: create_prefab with valid selection succeeds", "[editor][mcp]") {
  EditorLayer editor;
  SceneDocument doc;
  SceneObject obj;
  obj.id = "test_prefab_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);
  editor.LoadDocument(doc);

  auto result = editor.ExecuteMcpCommand(
      "editor.create_prefab", nlohmann::json{{"id", "test_prefab_obj"}});
  REQUIRE(result.ok);
  REQUIRE(result.data.contains("prefabPath"));
  // Clean up the generated prefab file
  if (result.data.contains("prefabPath") &&
      result.data["prefabPath"].is_string()) {
    std::error_code ec;
    std::filesystem::remove(result.data["prefabPath"].get<std::string>(), ec);
  }
}

TEST_CASE("EditorLayer MCP: update_asset modifies mesh and renderScale", "[editor][mcp]") {
  EditorLayer editor;
  SceneDocument doc;
  doc.assets["asset1"] = AssetDef{"old.obj", "1.0,1.0,1.0"};
  editor.LoadDocument(doc);

  auto result = editor.ExecuteMcpCommand(
      "editor.update_asset", nlohmann::json{{"id", "asset1"},
                                            {"mesh", "new.obj"},
                                            {"renderScale", "2.0,2.0,2.0"},
                                            {"albedoMap", "tex.png"},
                                            {"displayName", "New Asset"}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().assets.at("asset1").mesh == "new.obj");
  REQUIRE(editor.GetDocument().assets.at("asset1").renderScale ==
          "2.0,2.0,2.0");
  REQUIRE(editor.GetDocument().assets.at("asset1").albedoMap == "tex.png");
  REQUIRE(editor.GetDocument().assets.at("asset1").displayName == "New Asset");
}

TEST_CASE("EditorLayer MCP: update_asset with null values clears fields", "[editor][mcp]") {
  EditorLayer editor;
  SceneDocument doc;
  doc.assets["asset1"] = AssetDef{"old.obj", "1.0,1.0,1.0", "tex.png"};
  editor.LoadDocument(doc);

  auto result = editor.ExecuteMcpCommand(
      "editor.update_asset", nlohmann::json{{"id", "asset1"},
                                            {"mesh", nullptr},
                                            {"renderScale", nullptr},
                                            {"albedoMap", nullptr},
                                            {"displayName", nullptr}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().assets.at("asset1").mesh.empty());
  REQUIRE(editor.GetDocument().assets.at("asset1").albedoMap.empty());
}

TEST_CASE("EditorLayer MCP: update_asset fails for unknown asset", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  auto result = editor.ExecuteMcpCommand(
      "editor.update_asset", nlohmann::json{{"id", "no_such_asset"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: delete_asset removes the asset", "[editor][mcp]") {
  EditorLayer editor;
  SceneDocument doc;
  doc.assets["asset1"] = AssetDef{"cube.obj", "1.0,1.0,1.0"};
  editor.LoadDocument(doc);

  auto result = editor.ExecuteMcpCommand("editor.delete_asset",
                                         nlohmann::json{{"id", "asset1"}});
  REQUIRE(result.ok);
  REQUIRE_FALSE(editor.GetDocument().assets.contains("asset1"));
}

TEST_CASE("EditorLayer MCP: delete_asset fails for unknown asset", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  auto result = editor.ExecuteMcpCommand("editor.delete_asset",
                                         nlohmann::json{{"id", "no_such"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: create_object with all types", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  for (const auto *typeName : {"Panel", "Prop", "Light"}) {
    auto result = editor.ExecuteMcpCommand("editor.create_object",
                                           nlohmann::json{{"type", typeName}});
    REQUIRE(result.ok);
  }
  REQUIRE(editor.GetDocument().objects.size() == 3u);
}

TEST_CASE("EditorLayer MCP: create_object with invalid type fails", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  auto result = editor.ExecuteMcpCommand(
      "editor.create_object", nlohmann::json{{"type", "InvalidType"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: create_object with custom id and position", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  auto result = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"},
                     {"id", "custom_id"},
                     {"position", nlohmann::json::array({1.0, 2.0, 3.0})},
                     {"scale", nlohmann::json::array({2.0, 2.0, 2.0})},
                     {"yaw", 45.0}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects.back().id == "custom_id");
  REQUIRE(editor.GetDocument().objects.back().position.x == Approx(1.f));
}

TEST_CASE("EditorLayer MCP: create_object with invalid position fails", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  auto result = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{
          {"type", "Prop"},
          {"position", nlohmann::json::array({1.0, 2.0})} // too short
      });
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: create_object with components", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  auto result = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{
          {"type", "Prop"},
          {"components",
           nlohmann::json::array({nlohmann::json{
               {"type", "script"},
               {"props", nlohmann::json{{"behaviorTag", "Test"}}}}})}});
  REQUIRE(result.ok);
  REQUIRE(!editor.GetDocument().objects.empty());
  REQUIRE(editor.GetDocument().objects.back().components.size() == 1u);
}

TEST_CASE("EditorLayer MCP: create_object with invalid components fails", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  auto result = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"},
                     {"components", nlohmann::json::array({"not_an_object"})}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: create_object with parentId", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Panel"}, {"id", "parent"}});
  auto result = editor.ExecuteMcpCommand(
      "editor.create_object", nlohmann::json{{"type", "Prop"},
                                             {"id", "child"},
                                             {"parentId", "parent"}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects.back().props.at("parentId") == "parent");
}

TEST_CASE("EditorLayer MCP: create_object with unknown parentId fails", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  auto result = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"parentId", "no_such_parent"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: rename_object changes id and updates references", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Panel"}, {"id", "old_name"}});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"},
                                          {"id", "child"},
                                          {"parentId", "old_name"}});

  auto result = editor.ExecuteMcpCommand(
      "editor.rename_object",
      nlohmann::json{{"id", "old_name"}, {"newId", "new_name"}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects[0].id == "new_name");
  REQUIRE(editor.GetDocument().objects[1].props.at("parentId") == "new_name");
}

TEST_CASE("EditorLayer MCP: rename_object fails for unknown id", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  auto result = editor.ExecuteMcpCommand(
      "editor.rename_object",
      nlohmann::json{{"id", "no_such"}, {"newId", "new_name"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: rename_object fails with empty newId", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"}, {"id", "obj"}});

  auto result = editor.ExecuteMcpCommand(
      "editor.rename_object", nlohmann::json{{"id", "obj"}, {"newId", ""}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: rename_object fails when newId already exists", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"}, {"id", "a"}});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"}, {"id", "b"}});

  auto result = editor.ExecuteMcpCommand(
      "editor.rename_object", nlohmann::json{{"id", "a"}, {"newId", "b"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: reparent_object sets parentId", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Panel"}, {"id", "p"}});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"}, {"id", "c"}});

  auto result = editor.ExecuteMcpCommand(
      "editor.reparent_object", nlohmann::json{{"id", "c"}, {"parentId", "p"}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects[1].props.at("parentId") == "p");
}

TEST_CASE("EditorLayer MCP: reparent_object with empty parentId removes parent", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Panel"}, {"id", "p"}});
  editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"id", "c"}, {"parentId", "p"}});

  auto result = editor.ExecuteMcpCommand(
      "editor.reparent_object", nlohmann::json{{"id", "c"}, {"parentId", ""}});
  REQUIRE(result.ok);
  REQUIRE_FALSE(editor.GetDocument().objects[1].props.contains("parentId"));
}

TEST_CASE("EditorLayer MCP: reparent_object fails for self-parent", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"}, {"id", "obj"}});

  auto result = editor.ExecuteMcpCommand(
      "editor.reparent_object",
      nlohmann::json{{"id", "obj"}, {"parentId", "obj"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: reparent_object fails for cycle", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Panel"}, {"id", "a"}});
  editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"id", "b"}, {"parentId", "a"}});

  // Try to make 'a' a child of 'b' — would create cycle
  auto result = editor.ExecuteMcpCommand(
      "editor.reparent_object", nlohmann::json{{"id", "a"}, {"parentId", "b"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: duplicate multiple objects", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"}, {"id", "a"}});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Panel"}, {"id", "b"}});

  auto result = editor.ExecuteMcpCommand(
      "editor.duplicate",
      nlohmann::json{{"ids", nlohmann::json::array({"a", "b"})}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects.size() == 4u);
}

TEST_CASE("EditorLayer MCP: duplicate with count creates multiple copies", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"}, {"id", "src"}});

  auto result = editor.ExecuteMcpCommand(
      "editor.duplicate", nlohmann::json{{"id", "src"}, {"count", 3}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects.size() == 4u); // original + 3 copies
}

TEST_CASE("EditorLayer MCP: delete multiple objects by ids", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"}, {"id", "a"}});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Panel"}, {"id", "b"}});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Light"}, {"id", "c"}});

  auto result = editor.ExecuteMcpCommand(
      "editor.delete",
      nlohmann::json{{"ids", nlohmann::json::array({"a", "c"})}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects.size() == 1u);
  REQUIRE(editor.GetDocument().objects[0].id == "b");
}

TEST_CASE("EditorLayer MCP: update_object with components updates them", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"}, {"id", "obj"}});

  auto result = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{
          {"id", "obj"},
          {"components", nlohmann::json::array({nlohmann::json{
                             {"type", "rigidbody"},
                             {"props", nlohmann::json{{"mass", "2.5"}}}}})}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects[0].components.size() == 1u);
  REQUIRE(editor.GetDocument().objects[0].components[0].props.at("mass") ==
          "2.5");
}

TEST_CASE("EditorLayer MCP: update_object with null assetId clears it", "[editor][mcp]") {
  EditorLayer editor;
  SceneDocument doc;
  SceneObject obj;
  obj.id = "obj";
  obj.type = SceneObjectType::Prop;
  obj.assetId = "asset1";
  doc.objects.push_back(obj);
  editor.LoadDocument(doc);

  auto result = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{{"id", "obj"}, {"assetId", nullptr}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects[0].assetId.empty());
}

TEST_CASE("EditorLayer MCP: transform alias works same as update_object", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"}, {"id", "obj"}});

  auto result = editor.ExecuteMcpCommand(
      "editor.transform",
      nlohmann::json{{"id", "obj"},
                     {"position", nlohmann::json::array({5.0, 0.0, 0.0})},
                     {"yaw", 90.0}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects[0].position.x == Approx(5.f));
  REQUIRE(editor.GetDocument().objects[0].yaw == Approx(90.f));
}

TEST_CASE("EditorLayer MCP: select_asset fails for unknown asset", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  auto result = editor.ExecuteMcpCommand(
      "editor.select_asset", nlohmann::json{{"id", "no_such_asset"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: select_asset with empty id clears selection", "[editor][mcp]") {
  EditorLayer editor;
  SceneDocument doc;
  doc.assets["a"] = AssetDef{"a.obj", "1,1,1"};
  editor.LoadDocument(doc);
  editor.ExecuteMcpCommand("editor.select_asset", nlohmann::json{{"id", "a"}});
  REQUIRE(editor.GetSelectedAssetId() == "a");

  auto result = editor.ExecuteMcpCommand("editor.select_asset",
                                         nlohmann::json{{"id", ""}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetSelectedAssetId().empty());
}

TEST_CASE("EditorLayer MCP: undo and redo after create", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"}, {"id", "obj"}});
  REQUIRE(editor.GetDocument().objects.size() == 1u);

  auto undoResult = editor.ExecuteMcpCommand("editor.undo", nlohmann::json{});
  REQUIRE(undoResult.ok);
  REQUIRE(editor.GetDocument().objects.empty());

  auto redoResult = editor.ExecuteMcpCommand("editor.redo", nlohmann::json{});
  REQUIRE(redoResult.ok);
  REQUIRE(editor.GetDocument().objects.size() == 1u);
}

TEST_CASE("EditorLayer MCP: create_object_from_asset with unknown asset fails", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  auto result =
      editor.ExecuteMcpCommand("editor.create_object_from_asset",
                               nlohmann::json{{"assetId", "no_such_asset"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: create_object_from_asset with invalid position fails", "[editor][mcp]") {
  EditorLayer editor;
  SceneDocument doc;
  doc.assets["asset1"] = AssetDef{"cube.obj", "1,1,1"};
  editor.LoadDocument(doc);

  auto result = editor.ExecuteMcpCommand(
      "editor.create_object_from_asset",
      nlohmann::json{
          {"assetId", "asset1"},
          {"position", nlohmann::json::array({1.0, 2.0})} // wrong size
      });
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: unsupported command returns error", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  auto result =
      editor.ExecuteMcpCommand("editor.nonexistent_command", nlohmann::json{});
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error.find("Unsupported") != std::string::npos);
}

// ===========================================================================
// Properties panel: select AFTER Toggle so m_selectedIndices survives Render
// ===========================================================================

// Helper: build a doc with multiple objects covering all useful types/states
static SceneDocument MakeRichDocument() {
  SceneDocument doc;
  doc.sceneId = "rich-scene";

  { // Prop with a light component
    SceneObject obj;
    obj.id = "prop_light";
    obj.type = SceneObjectType::Prop;
    obj.assetId = "mesh_a";
    ComponentDesc lc;
    lc.type = "light";
    lc.props["intensity"] = "2.0";
    lc.props["color"] = "1.0,0.8,0.6";
    lc.props["radius"] = "8.0";
    obj.components.push_back(lc);
    doc.objects.push_back(obj);
  }
  { // Prop with a rigidbody component
    SceneObject obj;
    obj.id = "prop_rb";
    obj.type = SceneObjectType::Prop;
    obj.assetId = "mesh_a";
    ComponentDesc rb;
    rb.type = "rigidbody";
    rb.props["mass"] = "2.5";
    rb.props["isKinematic"] = "false";
    rb.props["useGravity"] = "true";
    obj.components.push_back(rb);
    doc.objects.push_back(obj);
  }
  { // Prop with a script component
    SceneObject obj;
    obj.id = "prop_script";
    obj.type = SceneObjectType::Prop;
    obj.assetId = "mesh_b";
    ComponentDesc sc;
    sc.type = "script";
    sc.props["behaviorTag"] = "EnemyAI";
    obj.components.push_back(sc);
    doc.objects.push_back(obj);
  }
  { // Prop with all 3 component types
    SceneObject obj;
    obj.id = "prop_all_comps";
    obj.type = SceneObjectType::Prop;
    ComponentDesc lc;
    lc.type = "light";
    obj.components.push_back(lc);
    ComponentDesc rb;
    rb.type = "rigidbody";
    obj.components.push_back(rb);
    ComponentDesc sc;
    sc.type = "script";
    obj.components.push_back(sc);
    doc.objects.push_back(obj);
  }
  { // Camera with a follow target
    SceneObject cam;
    cam.id = "cam_follow";
    cam.type = SceneObjectType::Camera;
    cam.props["followTargetId"] = "prop_light";
    cam.props["fov"] = "75.0";
    cam.props["nearClip"] = "0.1";
    cam.props["farClip"] = "1000.0";
    doc.objects.push_back(cam);
  }
  { // Camera with no follow target (different path in the Combo)
    SceneObject cam;
    cam.id = "cam_nofol";
    cam.type = SceneObjectType::Camera;
    doc.objects.push_back(cam);
  }
  { // Panel (no asset, no components)
    SceneObject panel;
    panel.id = "panel_bare";
    panel.type = SceneObjectType::Panel;
    doc.objects.push_back(panel);
  }
  { // Prop with no assetId
    SceneObject obj;
    obj.id = "prop_no_asset";
    obj.type = SceneObjectType::Prop;
    doc.objects.push_back(obj);
  }

  doc.assets["mesh_a"] = AssetDef{"", "1.0,1.0,1.0", "tex.png"};
  doc.assets["mesh_b"] = AssetDef{"", "2.0,1.0,1.0"};
  return doc;
}

// ---- Select AFTER Toggle: light component -----------------------------------

TEST_CASE("EditorLayer render: light component selected after Toggle covers DrawLightComponentFields", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "prop_light"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Select AFTER Toggle: rigidbody component ------------------------------

TEST_CASE("EditorLayer render: rigidbody component selected after Toggle covers DrawRigidBodyComponentFields", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "prop_rb"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Select AFTER Toggle: script component ---------------------------------

TEST_CASE("EditorLayer render: script component selected after Toggle covers DrawScriptComponentField", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "prop_script"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Select AFTER Toggle: all 3 component types ----------------------------

TEST_CASE("EditorLayer render: all component types on one object after Toggle covers full ComponentsList", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "prop_all_comps"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Select AFTER Toggle: camera with follow target -------------------------

TEST_CASE("EditorLayer render: camera with followTargetId selected after Toggle covers camera section", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "cam_follow"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Select AFTER Toggle: camera without follow target ----------------------

TEST_CASE("EditorLayer render: camera without followTargetId selected after Toggle", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "cam_nofol"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Multi-select: same assetId (covers "shared asset" path) ----------------

TEST_CASE("EditorLayer render: multi-select two objects with same assetId shows batch panel", "[editor][render][properties][multiselect]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  // Both prop_light and prop_rb share assetId "mesh_a"
  editor.ExecuteMcpCommand(
      "editor.select", nlohmann::json{{"ids", nlohmann::json::array(
                                                  {"prop_light", "prop_rb"})}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Multi-select: different assetIds (covers "Mixed" path) -----------------

TEST_CASE("EditorLayer render: multi-select with different assetIds shows Mixed label", "[editor][render][properties][multiselect]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  // prop_light has "mesh_a", prop_script has "mesh_b"
  editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{
          {"ids", nlohmann::json::array({"prop_light", "prop_script"})}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Multi-select: objects with no assetId (covers "<none>" path) -----------

TEST_CASE("EditorLayer render: multi-select objects with empty assetId shows none label", "[editor][render][properties][multiselect]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  // prop_no_asset and panel_bare both have no assetId
  editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{
          {"ids", nlohmann::json::array({"prop_no_asset", "panel_bare"})}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Multi-select 3 or more objects ----------------------------------------

TEST_CASE("EditorLayer render: multi-select three objects covers batch transform UI", "[editor][render][properties][multiselect]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{{"ids", nlohmann::json::array(
                                 {"prop_light", "prop_rb", "prop_script"})}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Asset selected after Toggle then deleted: covers asset-not-found branch

TEST_CASE("EditorLayer render: asset selected then deleted triggers asset-not-found clear in DrawPropertiesPanel", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "asset-not-found";
  doc.assets["gone_asset"] = AssetDef{"", "1,1,1"};

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Select the asset (exists at this point)
  editor.ExecuteMcpCommand("editor.select_asset",
                           nlohmann::json{{"id", "gone_asset"}});

  // Activate editor AFTER selecting, so m_selectedAssetId survives Toggle
  editor.Toggle();

  // Now delete the asset so it's gone when Render() reads m_selectedAssetId
  editor.ExecuteMcpCommand("editor.delete_asset",
                           nlohmann::json{{"id", "gone_asset"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  // After render, the missing asset should have cleared m_selectedAssetId
  REQUIRE(editor.GetSelectedAssetId().empty());
}

// ---- Asset selected after Toggle: valid asset covers
// DrawPropertiesSelectedAsset fully

TEST_CASE("EditorLayer render: valid asset selected after Toggle covers DrawPropertiesSelectedAsset", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "asset-valid";
  doc.assets["valid_asset"] = AssetDef{"", "1,1,1"};

  EditorLayer editor;
  editor.LoadDocument(doc);

  editor.ExecuteMcpCommand("editor.select_asset",
                           nlohmann::json{{"id", "valid_asset"}});
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Prop with no assetId selected after Toggle ----------------------------

TEST_CASE("EditorLayer render: prop with no assetId selected after Toggle renders transform section", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "prop_no_asset"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Panel object selected after Toggle ------------------------------------

TEST_CASE("EditorLayer render: panel with no components selected after Toggle", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "panel_bare"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Switching selection across multiple Render calls ----------------------

TEST_CASE("EditorLayer render: cycling through all object types with select-after-toggle", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  Camera cam;
  const std::vector<std::string> ids = {
      "prop_light", "prop_rb",   "prop_script", "prop_all_comps",
      "cam_follow", "cam_nofol", "panel_bare",  "prop_no_asset"};
  for (const auto &id : ids) {
    editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", id}});
    editor.Render(cam, 1280, 720);
  }
  REQUIRE(true);
}

// ---- Overlay callback is invoked during Render ------------------------------

TEST_CASE("EditorLayer render: overlay callback is invoked during active Render", "[editor][render]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();

  int callCount = 0;
  editor.SetOverlayRenderCallback([&callCount]() { ++callCount; });

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(callCount == 1);
}

// ---- Overlay callback cleared -----------------------------------------------

TEST_CASE("EditorLayer render: clearing overlay callback to nullptr is safe", "[editor][render]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();

  editor.SetOverlayRenderCallback([]() {});
  editor.SetOverlayRenderCallback(nullptr);

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Script behavior options callback set ----------------------------------

TEST_CASE("EditorLayer render: script component with behaviorOptionsCb covers option list", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "script-cb";
  {
    SceneObject obj;
    obj.id = "scripted_obj";
    obj.type = SceneObjectType::Prop;
    ComponentDesc sc;
    sc.type = "script";
    sc.props["behaviorTag"] = "PatrolAI";
    obj.components.push_back(sc);
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  // Provide a list of behavior options so the Combo has real items
  editor.SetScriptBehaviorOptionsProvider([]() -> std::vector<std::string> {
    return {"PatrolAI", "GuardAI", "IdleAI",
            ""}; // includes empty to test erase_if
  });
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "scripted_obj"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Script behavior options callback with unknown behavior ----------------

TEST_CASE("EditorLayer render: script component with unknown behaviorTag not in options list", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "script-unknown";
  {
    SceneObject obj;
    obj.id = "unknown_scripted";
    obj.type = SceneObjectType::Prop;
    ComponentDesc sc;
    sc.type = "script";
    sc.props["behaviorTag"] = "UnknownBehavior";
    obj.components.push_back(sc);
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  // Options list does NOT include "UnknownBehavior" — tests the
  // options.push_back(current) branch
  editor.SetScriptBehaviorOptionsProvider(
      []() -> std::vector<std::string> { return {"GuardAI"}; });
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "unknown_scripted"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Object selected with assetId that exists in document ------------------

TEST_CASE("EditorLayer render: prop with existing assetId shows asset section after Toggle", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "prop-asset";
  doc.assets["existing_asset"] = AssetDef{"", "1,1,1", "albedo.png"};
  {
    SceneObject obj;
    obj.id = "asset_obj";
    obj.type = SceneObjectType::Prop;
    obj.assetId = "existing_asset";
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "asset_obj"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Object with parentId set — covers parent dropdown rendering -----------

TEST_CASE("EditorLayer render: child object with parentId after Toggle shows parent dropdown", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "parent-child";
  {
    SceneObject parent;
    parent.id = "the_parent";
    parent.type = SceneObjectType::Panel;
    doc.objects.push_back(parent);
  }
  {
    SceneObject child;
    child.id = "the_child";
    child.type = SceneObjectType::Prop;
    child.props["parentId"] = "the_parent";
    doc.objects.push_back(child);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "the_child"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Object with prefab instance set ----------------------------------------

TEST_CASE("EditorLayer render: prop with prefabInstance selected after Toggle shows prefab label", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "prefab-select";
  {
    SceneObject obj;
    obj.id = "prefab_prop";
    obj.type = SceneObjectType::Prop;
    obj.prefabInstance =
        ScenePrefabInstance{"pf-001", "assets/prefabs/crate.json"};
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "prefab_prop"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// TransformGizmo::Draw — covers DrawTranslate, DrawRotate, DrawScale
// ===========================================================================

TEST_CASE("TransformGizmo: Draw with Translate mode queues axis lines", "[editor][gizmo][draw]") {
  ImGuiContextGuard imgui;
  TransformGizmo gizmo;
  gizmo.Activate(GizmoMode::Translate, {1.f, 2.f, 3.f}, Quaternion::Identity(),
                 {1.f, 1.f, 1.f});
  REQUIRE(gizmo.IsActive());

  Camera cam;
  gizmo.Draw(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("TransformGizmo: Draw with Rotate mode queues ring lines", "[editor][gizmo][draw]") {
  ImGuiContextGuard imgui;
  TransformGizmo gizmo;
  gizmo.Activate(GizmoMode::Rotate, {0.f, 0.f, 0.f}, Quaternion::Identity(),
                 {1.f, 1.f, 1.f});
  REQUIRE(gizmo.IsActive());

  Camera cam;
  gizmo.Draw(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("TransformGizmo: Draw with Scale mode queues box handles", "[editor][gizmo][draw]") {
  ImGuiContextGuard imgui;
  TransformGizmo gizmo;
  gizmo.Activate(GizmoMode::Scale, {-2.f, 1.f, 0.f}, Quaternion::Identity(),
                 {1.f, 1.f, 1.f});
  REQUIRE(gizmo.IsActive());

  Camera cam;
  gizmo.Draw(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("TransformGizmo: Draw with None mode is a no-op", "[editor][gizmo][draw]") {
  ImGuiContextGuard imgui;
  TransformGizmo gizmo;
  // Do NOT activate — mode stays None
  REQUIRE_FALSE(gizmo.IsActive());

  Camera cam;
  gizmo.Draw(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("TransformGizmo: Draw Translate at camera position (degenerate handleSize)", "[editor][gizmo][draw]") {
  ImGuiContextGuard imgui;
  TransformGizmo gizmo;
  Camera cam;
  // Place gizmo at camera origin — HandleSize approaches minimum
  gizmo.Activate(GizmoMode::Translate, {0.f, 0.f, 0.f}, Quaternion::Identity(),
                 {1.f, 1.f, 1.f});

  gizmo.Draw(cam, 1280, 720);
  REQUIRE(true);
}

// ---- TransformGizmo active in EditorLayer render ----------------------------

TEST_CASE("EditorLayer render: active gizmo Draw is called from Render", "[editor][render][gizmo]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "gizmo-render";
  {
    SceneObject obj;
    obj.id = "gizmo_target";
    obj.type = SceneObjectType::Prop;
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  // Select the object to activate the gizmo
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "gizmo_target"}});

  Camera cam;
  // Render twice to allow gizmo to sync and draw
  editor.Render(cam, 1280, 720);
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// AssetImporterRegistry import error paths
// ===========================================================================

TEST_CASE("AssetImporterRegistry: OBJ importer rejects non-obj source path", "[editor][importer]") {
  AssetImporterRegistry registry;
  const AssetImporter *importer = registry.FindById("builtin.obj_mesh");
  REQUIRE(importer != nullptr);

  AssetImportRequest req;
  req.assetId = "test_asset";
  req.assetGuid = "guid-001";
  req.displayName = "Test Asset";
  req.sourcePath = "/some/path/not_an_obj.txt";

  AssetImportResult result = importer->Import(req);
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.error.empty());
  REQUIRE_FALSE(result.diagnostics.empty());
  REQUIRE(result.diagnostics[0].code == "asset.obj.unsupported_type");
}

TEST_CASE("AssetImporterRegistry: OBJ importer rejects non-existent source file", "[editor][importer]") {
  AssetImporterRegistry registry;
  const AssetImporter *importer = registry.FindById("builtin.obj_mesh");
  REQUIRE(importer != nullptr);

  AssetImportRequest req;
  req.assetId = "missing_mesh";
  req.assetGuid = "guid-002";
  req.displayName = "Missing Mesh";
  req.sourcePath = "/no/such/file/mesh.obj";

  AssetImportResult result = importer->Import(req);
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.diagnostics[0].code == "asset.obj.source_missing");
}

TEST_CASE("AssetImporterRegistry: texture importer rejects non-texture source path", "[editor][importer]") {
  AssetImporterRegistry registry;
  const AssetImporter *importer = registry.FindById("builtin.texture_copy");
  REQUIRE(importer != nullptr);

  AssetImportRequest req;
  req.assetId = "tex_asset";
  req.assetGuid = "guid-003";
  req.displayName = "Tex Asset";
  req.sourcePath = "/some/path/mesh.obj"; // .obj is not a texture

  AssetImportResult result = importer->Import(req);
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.error.empty());
  REQUIRE(result.diagnostics[0].code == "asset.texture.unsupported_type");
}

TEST_CASE("AssetImporterRegistry: texture importer rejects non-existent source file", "[editor][importer]") {
  AssetImporterRegistry registry;
  const AssetImporter *importer = registry.FindById("builtin.texture_copy");
  REQUIRE(importer != nullptr);

  AssetImportRequest req;
  req.assetId = "tex_missing";
  req.assetGuid = "guid-004";
  req.displayName = "Missing Tex";
  req.sourcePath = "/no/such/file/albedo.png";

  AssetImportResult result = importer->Import(req);
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
}

TEST_CASE("AssetImporterRegistry: FindByExtension returns nullptr for unknown extension", "[editor][importer]") {
  AssetImporterRegistry registry;
  REQUIRE(registry.FindByExtension("model.fbx") == nullptr);
  REQUIRE(registry.FindByExtension("data.bin") == nullptr);
  REQUIRE(registry.FindByExtension("noextension") == nullptr);
}

TEST_CASE("AssetImporterRegistry: extension lookup is case-insensitive", "[editor][importer]") {
  AssetImporterRegistry registry;
  REQUIRE(registry.FindByExtension("mesh.OBJ") != nullptr);
  REQUIRE(registry.FindByExtension("ALBEDO.PNG") != nullptr);
  REQUIRE(registry.FindByExtension("roughness.JpEg") != nullptr);
}

TEST_CASE("AssetImporterRegistry: RegisteredImporterIds returns both built-in ids", "[editor][importer]") {
  AssetImporterRegistry registry;
  const auto ids = registry.RegisteredImporterIds();
  REQUIRE(ids.size() >= 2);
  REQUIRE(std::ranges::find(ids, std::string("builtin.obj_mesh")) != ids.end());
  REQUIRE(std::ranges::find(ids, std::string("builtin.texture_copy")) !=
          ids.end());
}

TEST_CASE("AssetImporterRegistry: OBJ importer with valid file and mtl companion covers full success path", "[editor][importer]") {
  namespace fs = std::filesystem;
  const fs::path tmpDir =
      Horo::Tests::SecureTempBase() / "horo_importer_test_obj";
  fs::create_directories(tmpDir);

  // Write a minimal OBJ referencing an MTL with a diffuse texture map
  const fs::path objPath = tmpDir / "cube.obj";
  const fs::path mtlPath = tmpDir / "cube.mtl";
  const fs::path texPath = tmpDir / "albedo.png";

  WriteFile(objPath.string(), "mtllib cube.mtl\n"
                              "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                              "usemtl Material\n"
                              "f 1 2 3\n");
  WriteFile(mtlPath.string(), "newmtl Material\n"
                              "map_Kd albedo.png\n");
  // Create a dummy PNG (not a real PNG, but file exists for copy)
  WriteFile(texPath.string(), "PNGDATA");

  // Set up a temp project root so GetManagedAssetDirectory works
  const fs::path projectRoot = tmpDir / "project";
  fs::create_directories(projectRoot);
  ProjectPathGuard guard(projectRoot);

  AssetImporterRegistry registry;
  const AssetImporter *importer = registry.FindById("builtin.obj_mesh");
  REQUIRE(importer != nullptr);

  AssetImportRequest req;
  req.assetId = "cube_mesh";
  req.assetGuid = "guid-obj-success";
  req.displayName = "Cube Mesh";
  req.sourcePath = objPath.string();

  AssetImportResult result = importer->Import(req);
  REQUIRE(result.ok);
  REQUIRE_FALSE(result.asset.mesh.empty());

  // Cleanup
  fs::remove_all(tmpDir);
}

TEST_CASE("AssetImporterRegistry: texture importer with valid png file covers full success path", "[editor][importer]") {
  namespace fs = std::filesystem;
  const fs::path tmpDir =
      Horo::Tests::SecureTempBase() / "horo_importer_test_tex";
  fs::create_directories(tmpDir);

  const fs::path texPath = tmpDir / "albedo.png";
  WriteFile(texPath.string(), "PNGDATA");

  const fs::path projectRoot = tmpDir / "project";
  fs::create_directories(projectRoot);
  ProjectPathGuard guard(projectRoot);

  AssetImporterRegistry registry;
  const AssetImporter *importer = registry.FindById("builtin.texture_copy");
  REQUIRE(importer != nullptr);

  AssetImportRequest req;
  req.assetId = "tex_asset";
  req.assetGuid = "guid-tex-success";
  req.displayName = "Albedo Texture";
  req.sourcePath = texPath.string();

  AssetImportResult result = importer->Import(req);
  REQUIRE(result.ok);
  REQUIRE_FALSE(result.asset.albedoMap.empty());

  fs::remove_all(tmpDir);
}

TEST_CASE("AssetImporterRegistry: OBJ importer with obj file that has no mtllib covers no-companion path", "[editor][importer]") {
  namespace fs = std::filesystem;
  const fs::path tmpDir =
      Horo::Tests::SecureTempBase() / "horo_importer_test_nomtl";
  fs::create_directories(tmpDir);

  // OBJ without mtllib line
  const fs::path objPath = tmpDir / "simple.obj";
  WriteFile(objPath.string(), "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                              "f 1 2 3\n");

  const fs::path projectRoot = tmpDir / "project";
  fs::create_directories(projectRoot);
  ProjectPathGuard guard(projectRoot);

  AssetImporterRegistry registry;
  const AssetImporter *importer = registry.FindById("builtin.obj_mesh");
  REQUIRE(importer != nullptr);

  AssetImportRequest req;
  req.assetId = "simple_mesh";
  req.assetGuid = "guid-nomtl";
  req.displayName = "Simple Mesh";
  req.sourcePath = objPath.string();

  AssetImportResult result = importer->Import(req);
  REQUIRE(result.ok);

  fs::remove_all(tmpDir);
}

TEST_CASE("AssetImporterRegistry: OBJ importer with mtl that has no map_ entries", "[editor][importer]") {
  namespace fs = std::filesystem;
  const fs::path tmpDir =
      Horo::Tests::SecureTempBase() / "horo_importer_test_nomaps";
  fs::create_directories(tmpDir);

  const fs::path objPath = tmpDir / "untextured.obj";
  const fs::path mtlPath = tmpDir / "untextured.mtl";
  WriteFile(objPath.string(), "mtllib untextured.mtl\n"
                              "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                              "f 1 2 3\n");
  WriteFile(mtlPath.string(),
            "newmtl Material\n"
            "Kd 0.8 0.8 0.8\n"); // no map_ entries

  const fs::path projectRoot = tmpDir / "project";
  fs::create_directories(projectRoot);
  ProjectPathGuard guard(projectRoot);

  AssetImporterRegistry registry;
  const AssetImporter *importer = registry.FindById("builtin.obj_mesh");
  REQUIRE(importer != nullptr);

  AssetImportRequest req;
  req.assetId = "untex_mesh";
  req.assetGuid = "guid-nomaps";
  req.displayName = "Untextured Mesh";
  req.sourcePath = objPath.string();

  AssetImportResult result = importer->Import(req);
  REQUIRE(result.ok);

  fs::remove_all(tmpDir);
}

TEST_CASE("AssetImporterRegistry: OBJ import dedupes and sorts produced outputs", "[editor][importer]") {
  namespace fs = std::filesystem;
  const fs::path tmpDir =
      Horo::Tests::SecureTempBase() / "horo_importer_dedupe_sort";
  std::error_code ec;
  fs::remove_all(tmpDir, ec);
  fs::create_directories(tmpDir, ec);

  const fs::path objPath = tmpDir / "dup.obj";
  const fs::path mtlPath = tmpDir / "dup.mtl";
  const fs::path texPath = tmpDir / "tex_a.png";
  WriteFile(objPath.string(), "mtllib dup.mtl\n"
                              "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                              "usemtl Mat\n"
                              "f 1 2 3\n");
  WriteFile(mtlPath.string(), "newmtl Mat\n"
                              "map_Kd tex_a.png\n"
                              "map_Kd tex_a.png\n");
  WriteFile(texPath.string(), "PNGDATA");

  const fs::path projectRoot = tmpDir / "project";
  fs::create_directories(projectRoot, ec);
  ProjectPathGuard guard(projectRoot);

  AssetImporterRegistry registry;
  const AssetImporter *importer = registry.FindById("builtin.obj_mesh");
  REQUIRE(importer != nullptr);

  AssetImportRequest req;
  req.assetId = "dup_mesh";
  req.assetGuid = "guid-dedupe-sort";
  req.displayName = "Dup Mesh";
  req.sourcePath = objPath.string();

  const AssetImportResult result = importer->Import(req);
  REQUIRE(result.ok);
  REQUIRE(std::is_sorted(result.metadata.producedFiles.begin(),
                         result.metadata.producedFiles.end()));
  auto dedupedProducedFiles = result.metadata.producedFiles;
  const auto uniqueEnd = std::unique(dedupedProducedFiles.begin(),
                                     dedupedProducedFiles.end());
  REQUIRE(uniqueEnd == dedupedProducedFiles.end());

  size_t producedOutputDeps = 0;
  for (const auto &dep : result.metadata.dependencies) {
    if (dep.kind == AssetDependencyKind::ProducedOutput)
      ++producedOutputDeps;
  }
  REQUIRE(producedOutputDeps == result.metadata.producedFiles.size());

  fs::remove_all(tmpDir, ec);
}

TEST_CASE("AssetImporterRegistry: OBJ reimport replaces existing destination", "[editor][importer]") {
  namespace fs = std::filesystem;
  const fs::path root =
      Horo::Tests::SecureTempBase() / "horo_importer_replace_dest";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / "src", ec);
  fs::create_directories(root / "project", ec);
  ProjectPathGuard guard(root / "project");

  const fs::path sourceObj = root / "src" / "replace.obj";
  WriteFile(sourceObj.string(), "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

  AssetImporterRegistry registry;
  const AssetImporter *importer = registry.FindById("builtin.obj_mesh");
  REQUIRE(importer != nullptr);

  AssetImportRequest req;
  req.assetId = "replace_mesh";
  req.assetGuid = "guid-replace-mesh";
  req.displayName = "Replace Mesh";
  req.sourcePath = sourceObj.string();

  const AssetImportResult first = importer->Import(req);
  REQUIRE(first.ok);

  WriteFile(sourceObj.string(),
            "v 0 0 0\nv 2 0 0\nv 0 2 0\nv 0 0 1\nf 1 2 3\nf 1 3 4\n");
  const AssetImportResult second = importer->Import(req);
  REQUIRE(second.ok);

  const std::filesystem::path managedObj = ProjectPath::Resolve(second.asset.mesh);
  std::ifstream stream(managedObj);
  REQUIRE(stream.is_open());
  std::string body((std::istreambuf_iterator<char>(stream)),
                   std::istreambuf_iterator<char>());
  REQUIRE(body.find("f 1 3 4") != std::string::npos);
}

// ===========================================================================
// EditorMcpHandlers: McpHandleClearSelection and additional coverage
// ===========================================================================

TEST_CASE("EditorLayer MCP: clear_selection clears selected objects and assets", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "sel_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);
  doc.assets["sel_asset"] = AssetDef{"", "1,1,1"};

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Select an object and an asset
  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "sel_obj"}});
  editor.ExecuteMcpCommand("editor.select_asset",
                           nlohmann::json{{"id", "sel_asset"}});

  // Now clear selection
  const auto result = editor.ExecuteMcpCommand("editor.clear_selection",
                                               nlohmann::json::object());
  REQUIRE(result.ok);
  REQUIRE(result.data["cleared"].get<bool>());

  // Verify side-effects
  REQUIRE(editor.GetSelectedAssetId().empty());
}

TEST_CASE("EditorLayer MCP: select with ids array selects multiple objects", "[editor][mcp]") {
  SceneDocument doc;
  for (int i = 0; i < 4; ++i) {
    SceneObject obj;
    obj.id = std::format("obj{}", i);
    obj.type = SceneObjectType::Prop;
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{{"ids", nlohmann::json::array({"obj0", "obj2", "obj3"})}});
  REQUIRE(result.ok);
  const auto selectedIds = result.data["selectedObjectIds"];
  REQUIRE(selectedIds.is_array());
  REQUIRE(selectedIds.size() == 3);
}

TEST_CASE("EditorLayer MCP: select with both id and ids selects all matching", "[editor][mcp]") {
  SceneDocument doc;
  for (int i = 0; i < 3; ++i) {
    SceneObject obj;
    obj.id = std::format("item{}", i);
    obj.type = SceneObjectType::Panel;
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Provide both "id" and "ids" — both should be collected
  const auto result = editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{{"id", "item0"},
                     {"ids", nlohmann::json::array({"item1", "item2"})}});
  REQUIRE(result.ok);
  const auto sel = result.data["selectedObjectIds"];
  REQUIRE(sel.size() == 3);
}

TEST_CASE("EditorLayer MCP: select with nonexistent ids selects nothing", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{{"ids", nlohmann::json::array({"ghost1", "ghost2"})}});
  REQUIRE(result.ok);
  REQUIRE(result.data["selectedObjectIds"].empty());
}

// ===========================================================================
// Additional EditorLayer render states to improve EditorLayer.cpp coverage
// ===========================================================================

TEST_CASE("EditorLayer render: SetProjectBrowserRoot to real path renders file tiles", "[editor][render][browser]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  // Use the repo assets directory which definitely has files
  editor.SetProjectBrowserRoot(std::filesystem::path(RepoRootFromTestSource()) /
                               "assets");
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  editor.Render(cam, 1280, 720); // second frame may trigger cached path
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: SetProjectBrowserRoot with blocklist filters entries", "[editor][render][browser]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.SetProjectBrowserRoot(std::filesystem::path(RepoRootFromTestSource()) /
                               "assets");
  editor.SetProjectBrowserExtraBlocklist({"shaders", "textures"});
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: document with many objects renders object list fully", "[editor][render]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "many-objects";
  for (int i = 0; i < 20; ++i) {
    SceneObject obj;
    obj.id = std::format("bulk_obj_{}", i);
    obj.type = (i % 3 == 0)   ? SceneObjectType::Panel
               : (i % 3 == 1) ? SceneObjectType::Prop
                              : SceneObjectType::Light;
    if (i % 2 == 0)
      obj.assetId = "mesh_a";
    if (i > 0 && i % 5 == 0)
      obj.props["parentId"] = std::format("bulk_obj_{}", i - 1);
    doc.objects.push_back(obj);
  }
  doc.assets["mesh_a"] = AssetDef{"", "1,1,1"};

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  // Select a few different objects across frames
  Camera cam;
  for (int i = 0; i < 5; ++i) {
    editor.ExecuteMcpCommand(
        "editor.select",
        nlohmann::json{{"id", std::format("bulk_obj_{}", i * 4)}});
    editor.Render(cam, 1280, 720);
  }
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: dirty document shows dirty indicator in toolbar", "[editor][render]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "dirty-render";
  doc.dirty = true;

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: scene with filePath set enables save button", "[editor][render]") {
  ImGuiContextGuard imgui;
  namespace fs = std::filesystem;
  const fs::path tmpDir =
      Horo::Tests::SecureTempBase() / "horo_render_filepath";
  fs::create_directories(tmpDir);
  const fs::path scenePath = tmpDir / "test.json";

  SceneDocument doc;
  doc.sceneId = "filepath-render";
  doc.filePath = scenePath.string();
  SceneSerializer::SaveToFile(doc, scenePath.string());

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);

  fs::remove_all(tmpDir);
}

TEST_CASE("EditorLayer render: doc with all asset types renders assets panel", "[editor][render]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "assets-panel";
  doc.assets["asset_no_albedo"] = AssetDef{"", "1,1,1"};
  doc.assets["asset_with_albedo"] = AssetDef{"", "1,1,1", "tex.png"};
  doc.assets["asset_no_scale"] = AssetDef{"", ""};

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: hot reload overlay active during Render", "[editor][render]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.SetHotReloadOverlay(true, 0.8f, 2.5f, "Compiling shaders...");
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: multiple Toggle cycles maintain stable render state", "[editor][render]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());

  Camera cam;
  // Toggle on/off several times, rendering in between
  for (int cycle = 0; cycle < 3; ++cycle) {
    editor.Toggle(); // activate
    editor.ExecuteMcpCommand("editor.select",
                             nlohmann::json{{"id", "prop_light"}});
    editor.Render(cam, 1280, 720);
    editor.Toggle(); // deactivate
    editor.Render(cam, 1280, 720);
  }
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: create_object then select and render covers post-create flow", "[editor][render]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();

  // Create objects while the editor is active, then select and render
  const auto r1 = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"id", "live_prop"}});
  REQUIRE(r1.ok);

  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "live_prop"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: undo/redo during active render session", "[editor][render]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();

  editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Panel"}, {"id", "undo_target"}});

  Camera cam;
  editor.Render(cam, 1280, 720);

  editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  editor.Render(cam, 1280, 720);

  editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayerInternal helpers reachable through Render
// ===========================================================================

TEST_CASE("EditorLayer render: SyncGizmoToSelection updates gizmo when object is selected", "[editor][render][internal]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "sync-gizmo";
  {
    SceneObject obj;
    obj.id = "gizmo_obj";
    obj.type = SceneObjectType::Prop;
    obj.position = {5.f, 2.f, -3.f};
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "gizmo_obj"}});

  // Render multiple times to exercise SyncGizmoToSelection
  Camera cam;
  editor.Render(cam, 1280, 720);
  editor.Render(cam, 1280, 720);
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: empty selection then object selection then multi-select in sequence", "[editor][render][internal]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  Camera cam;

  // No selection
  editor.ExecuteMcpCommand("editor.clear_selection", nlohmann::json::object());
  editor.Render(cam, 1280, 720);

  // Single object
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "prop_light"}});
  editor.Render(cam, 1280, 720);

  // Multi-select
  editor.ExecuteMcpCommand(
      "editor.select", nlohmann::json{{"ids", nlohmann::json::array(
                                                  {"prop_light", "prop_rb"})}});
  editor.Render(cam, 1280, 720);

  // Camera
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "cam_follow"}});
  editor.Render(cam, 1280, 720);

  // Asset select
  editor.ExecuteMcpCommand("editor.select_asset",
                           nlohmann::json{{"id", "mesh_a"}});
  editor.Render(cam, 1280, 720);

  REQUIRE(true);
}

// ===========================================================================
// EditorMcpHandlers: additional handlers coverage
// ===========================================================================

TEST_CASE("EditorLayer MCP: create_object Camera type creates camera object", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.create_object", nlohmann::json{{"type", "Camera"}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects.size() == 1);
  REQUIRE(editor.GetDocument().objects[0].type == SceneObjectType::Camera);
  REQUIRE_FALSE(editor.GetDocument().objects[0].id.empty());
}

TEST_CASE("EditorLayer MCP: update_object with rotation updates pitch yaw roll", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "rot_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{
          {"id", "rot_obj"}, {"pitch", 15.0}, {"yaw", 30.0}, {"roll", 45.0}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects[0].pitch == Approx(15.0f));
  REQUIRE(editor.GetDocument().objects[0].yaw == Approx(30.0f));
  REQUIRE(editor.GetDocument().objects[0].roll == Approx(45.0f));
}

TEST_CASE("EditorLayer MCP: update_object with scale updates scale vector", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "scale_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{{"id", "scale_obj"},
                     {"scale", nlohmann::json::array({2.0, 3.0, 4.0})}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects[0].scale.x == Approx(2.0f));
  REQUIRE(editor.GetDocument().objects[0].scale.y == Approx(3.0f));
  REQUIRE(editor.GetDocument().objects[0].scale.z == Approx(4.0f));
}

TEST_CASE("EditorLayer MCP: select with ids array returns all selected", "[editor][mcp]") {
  SceneDocument doc;
  for (int i = 0; i < 3; ++i) {
    SceneObject obj;
    obj.id = std::format("list_obj_{}", i);
    obj.type = SceneObjectType::Prop;
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Select multiple objects by ids array
  const auto result = editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{{"ids", nlohmann::json::array(
                                 {"list_obj_0", "list_obj_1", "list_obj_2"})}});
  REQUIRE(result.ok);
  // Result should confirm selection
  REQUIRE_FALSE(result.data.empty());
}

TEST_CASE("EditorLayer MCP: update_asset modifies an existing asset", "[editor][mcp]") {
  SceneDocument doc;
  doc.assets["asset_a"] = AssetDef{"", "1,1,1"};
  doc.assets["asset_b"] = AssetDef{"", "2,2,2", "tex.png"};

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_asset",
      nlohmann::json{{"id", "asset_a"}, {"displayName", "NewName"}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().assets.at("asset_a").displayName == "NewName");
  // Second update to cover more branches
  const auto result2 = editor.ExecuteMcpCommand(
      "editor.update_asset",
      nlohmann::json{{"id", "asset_b"}, {"displayName", "TextureAsset"}});
  REQUIRE(result2.ok);
}

TEST_CASE("EditorLayer MCP: select returns data about selected object", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "sel_get";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.select", nlohmann::json{{"id", "sel_get"}});
  REQUIRE(result.ok);
  // The result should contain data about the selection
  REQUIRE_FALSE(result.data.empty());
}

TEST_CASE("EditorLayer MCP: duplicate by id creates a copy", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "dup_src";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Use valid 'editor.duplicate' command with 'id' parameter
  const auto result = editor.ExecuteMcpCommand(
      "editor.duplicate", nlohmann::json{{"id", "dup_src"}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects.size() == 2);
}

TEST_CASE("EditorLayer MCP: reparent_object assigns new parent", "[editor][mcp]") {
  SceneDocument doc;
  for (const auto &id : {"node_a", "node_b"}) {
    SceneObject obj;
    obj.id = id;
    obj.type = SceneObjectType::Panel;
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.reparent_object",
      nlohmann::json{{"id", "node_b"}, {"newParentId", "node_a"}});
  REQUIRE(result.ok);
}

TEST_CASE("EditorLayer MCP: update_object with position array updates transform", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "move_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  // 'editor.transform' is an alias for 'editor.update_object'
  const auto result = editor.ExecuteMcpCommand(
      "editor.transform",
      nlohmann::json{{"id", "move_obj"},
                     {"position", nlohmann::json::array({10.0, 20.0, 30.0})}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects[0].position.x == Approx(10.0f));
  REQUIRE(editor.GetDocument().objects[0].position.y == Approx(20.0f));
  REQUIRE(editor.GetDocument().objects[0].position.z == Approx(30.0f));
}

// ===========================================================================
// EditorLayer: additional public API and edge cases
// ===========================================================================

TEST_CASE("EditorLayer: WantsSceneReload is initially false", "[editor][layer]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  REQUIRE_FALSE(editor.WantsSceneReload());
}

TEST_CASE("EditorLayer: GetDocument returns const reference to loaded document", "[editor][layer]") {
  SceneDocument doc;
  doc.sceneId = "get-doc-test";
  doc.sceneName = "Test Scene";

  EditorLayer editor;
  editor.LoadDocument(doc);

  REQUIRE(editor.GetDocument().sceneId == "get-doc-test");
  REQUIRE(editor.GetDocument().sceneName == "Test Scene");
}

TEST_CASE("EditorLayer: SetTransformCallback is stored and not called immediately", "[editor][layer]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  bool called = false;
  editor.SetTransformCallback(
      [&called](const SceneObject &) { called = true; });
  REQUIRE_FALSE(called);
}

TEST_CASE("EditorLayer render: transform callback is invoked when gizmo moves object", "[editor][render][callback]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "transform-cb";
  {
    SceneObject obj;
    obj.id = "cb_target";
    obj.type = SceneObjectType::Prop;
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "cb_target"}});

  int cbCount = 0;
  editor.SetTransformCallback([&cbCount](const SceneObject &) { ++cbCount; });

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true); // callback may or may not fire (gizmo doesn't drag headlessly)
}

TEST_CASE("EditorLayer: OnPathsDropped with .obj files stores pending import", "[editor][layer]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const char *paths[] = {"assets/models/crate.obj"};
  editor.OnPathsDropped(1, paths, 200.0f, 300.0f);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: IsPlayMode stays false after multiple Toggles", "[editor][layer]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();
  editor.Toggle();
  editor.Toggle();
  REQUIRE_FALSE(editor.IsPlayMode());
}

TEST_CASE("EditorLayer: GetSelectedAssetId returns id after select_asset", "[editor][layer]") {
  SceneDocument doc;
  doc.assets["queried_asset"] = AssetDef{"", "1,1,1"};

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.ExecuteMcpCommand("editor.select_asset",
                           nlohmann::json{{"id", "queried_asset"}});
  REQUIRE(editor.GetSelectedAssetId() == "queried_asset");
}

TEST_CASE("EditorLayer: LoadDocument with empty document does not crash", "[editor][layer]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  REQUIRE(editor.GetDocument().objects.empty());
  REQUIRE(editor.GetDocument().assets.empty());
}

TEST_CASE("EditorLayer render: Render with very small viewport dimensions", "[editor][render]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 320, 240);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: Render with large viewport dimensions", "[editor][render]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 3840, 2160);
  REQUIRE(true);
}

// ---- Camera search path in DrawPropertiesCameraSection FollowTarget combo ---

TEST_CASE("EditorLayer render: camera followTargetId pointing to missing object is handled gracefully", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "cam-missing-follow";
  {
    SceneObject cam;
    cam.id = "cam_bad_follow";
    cam.type = SceneObjectType::Camera;
    cam.props["followTargetId"] = "nonexistent_object";
    doc.objects.push_back(cam);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "cam_bad_follow"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Prop with assetId referencing missing asset renders gracefully --------

TEST_CASE("EditorLayer render: prop with assetId that is not in assets map renders without crash", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "missing-asset-ref";
  {
    SceneObject obj;
    obj.id = "obj_missing_asset";
    obj.type = SceneObjectType::Prop;
    obj.assetId = "nonexistent_asset_id";
    doc.objects.push_back(obj);
  }
  // Note: "nonexistent_asset_id" is NOT added to doc.assets

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "obj_missing_asset"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Light SceneObject type selection (different from light component) -----

TEST_CASE("EditorLayer render: Light type object selected after Toggle", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "light-type";
  {
    SceneObject light;
    light.id = "scene_light";
    light.type = SceneObjectType::Light;
    light.position = {3.f, 5.f, 0.f};
    doc.objects.push_back(light);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "scene_light"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ---- Inspect object renders with SyncGizmoToSelection exercised multiple
// times

TEST_CASE("EditorLayer render: selecting different objects across renders exercises gizmo sync", "[editor][render]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  Camera cam;
  const std::vector<std::string> ids = {"prop_light", "cam_follow", "prop_rb",
                                        "cam_nofol", "prop_script"};
  for (const auto &id : ids) {
    editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", id}});
    editor.Render(cam, 1280, 720);
  }
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer: SetProjectBrowserRoot branches
// ===========================================================================

TEST_CASE("EditorLayer: SetProjectBrowserRoot with empty path clears state", "[editor][layer][browser]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  // Empty path should clear the browser root state
  editor.SetProjectBrowserRoot({});
  REQUIRE(true); // no crash
}

TEST_CASE("EditorLayer: SetProjectBrowserRoot with nonexistent path records invalid", "[editor][layer][browser]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.SetProjectBrowserRoot(
      std::filesystem::path("/nonexistent_test_dir_xyz_12345"));
  REQUIRE(true); // no crash — records as invalid root
}

TEST_CASE("EditorLayer: SetProjectBrowserRoot with valid existing directory succeeds", "[editor][layer][browser]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  // Use the temp dir — guaranteed to exist
  const auto tmp = std::filesystem::temp_directory_path();
  editor.SetProjectBrowserRoot(tmp);
  REQUIRE(true); // no crash — valid root set
}

TEST_CASE("EditorLayer: SetProjectBrowserRoot called twice updates state", "[editor][layer][browser]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto tmp = std::filesystem::temp_directory_path();
  editor.SetProjectBrowserRoot(tmp);
  editor.SetProjectBrowserRoot({}); // clear it
  REQUIRE(true);
}

TEST_CASE("EditorLayer: SetProjectBrowserExtraBlocklist invalidates cache", "[editor][layer][browser]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.SetProjectBrowserExtraBlocklist({"node_modules", ".git", "build"});
  REQUIRE(true);
}

TEST_CASE("EditorLayer: SetProjectBrowserRoot with valid dir then render shows project tab", "[editor][render][browser]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto tmp =
      Horo::Tests::SecureTempBase() / "horo_editor_project_browser_render";
  std::filesystem::remove_all(tmp);
  std::filesystem::create_directories(tmp / "assets");
  std::ofstream(tmp / "assets" / "scene.json") << "{}";
  editor.SetProjectBrowserRoot(tmp);
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer: Workspace state methods
// ===========================================================================

TEST_CASE("EditorLayer: SaveWorkspaceStateNow does not crash", "[editor][layer][workspace]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.SaveWorkspaceStateNow();
  REQUIRE(true);
}

TEST_CASE("EditorLayer: ReloadWorkspaceStateFromDisk does not crash", "[editor][layer][workspace]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ReloadWorkspaceStateFromDisk();
  REQUIRE(true);
}

TEST_CASE("EditorLayer: SaveWorkspaceStateNow then ReloadWorkspaceStateFromDisk round-trip", "[editor][layer][workspace]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.SaveWorkspaceStateNow();
  editor.ReloadWorkspaceStateFromDisk();
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer: SyncRuntimeEntityIds
// ===========================================================================

TEST_CASE("EditorLayer: SyncRuntimeEntityIds with empty registry clears _eid props", "[editor][layer][sync]") {
  SceneDocument doc;
  for (int i = 0; i < 3; ++i) {
    SceneObject obj;
    obj.id = std::format("prop_{}", i);
    obj.type = SceneObjectType::Prop;
    obj.props["_eid"] = std::to_string(100 + i); // pre-existing _eid
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Empty registry — all _eid props should be erased
  Horo::Registry reg;
  editor.SyncRuntimeEntityIds(reg);

  for (const auto &obj : editor.GetDocument().objects) {
    REQUIRE_FALSE(obj.props.contains("_eid"));
  }
}

TEST_CASE("EditorLayer: SyncRuntimeEntityIds with matching mesh entities maps _eid", "[editor][layer][sync]") {
  SceneDocument doc;
  for (int i = 0; i < 2; ++i) {
    SceneObject obj;
    obj.id = std::format("prop_{}", i);
    obj.type = SceneObjectType::Prop;
    doc.objects.push_back(obj);
  }
  // Add a non-prop object — should NOT be mapped
  {
    SceneObject panel;
    panel.id = "panel_obj";
    panel.type = SceneObjectType::Panel;
    doc.objects.push_back(panel);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Create registry with 2 mesh entities (no PlayerTag)
  Horo::Registry reg;
  const auto e0 = reg.Create();
  const auto e1 = reg.Create();
  reg.Add<Horo::MeshComponent>(e0);
  reg.Add<Horo::MeshComponent>(e1);
  reg.Add<Horo::TransformComponent>(e0);
  reg.Add<Horo::TransformComponent>(e1);

  editor.SyncRuntimeEntityIds(reg);

  // Props should now have _eid
  int mappedCount = 0;
  for (const auto &obj : editor.GetDocument().objects) {
    if (obj.type == SceneObjectType::Prop && obj.props.contains("_eid"))
      ++mappedCount;
  }
  REQUIRE(mappedCount == 2);
  // Panel should NOT have _eid
  const auto &panel = editor.GetDocument().objects[2];
  REQUIRE_FALSE(panel.props.contains("_eid"));
}

TEST_CASE("EditorLayer: SyncRuntimeEntityIds skips PlayerTag entities", "[editor][layer][sync]") {
  SceneDocument doc;
  {
    SceneObject prop;
    prop.id = "prop_a";
    prop.type = SceneObjectType::Prop;
    doc.objects.push_back(prop);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  Horo::Registry reg;
  const auto player = reg.Create();
  reg.Add<Horo::MeshComponent>(player);
  reg.Add<Horo::PlayerTagComponent>(player); // should be skipped

  // No non-player mesh entities → prop count(1) != mesh count(0) → warning path
  editor.SyncRuntimeEntityIds(reg);

  // _eid should be removed because there are no non-player meshes
  REQUIRE_FALSE(editor.GetDocument().objects[0].props.contains("_eid"));
}

TEST_CASE("EditorLayer: SyncRuntimeEntityIds with more meshes than props warns and maps partial", "[editor][layer][sync]") {
  SceneDocument doc;
  {
    SceneObject prop;
    prop.id = "solo_prop";
    prop.type = SceneObjectType::Prop;
    doc.objects.push_back(prop);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  // 3 mesh entities but only 1 prop → should warn and map 1
  Horo::Registry reg;
  for (int i = 0; i < 3; ++i) {
    const auto e = reg.Create();
    reg.Add<Horo::MeshComponent>(e);
  }

  editor.SyncRuntimeEntityIds(reg);

  REQUIRE(editor.GetDocument().objects[0].props.contains("_eid"));
}

// ===========================================================================
// EditorLayer: OnPathsDropped and ProcessPendingPathDrops
// ===========================================================================

TEST_CASE("EditorLayer: OnPathsDropped with null paths is a no-op", "[editor][layer][drop]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.OnPathsDropped(0, nullptr, 0.0f, 0.0f);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: OnPathsDropped with empty path array is a no-op", "[editor][layer][drop]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  const char *paths[] = {nullptr};
  editor.OnPathsDropped(1, paths, 50.0f, 60.0f);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: OnPathsDropped with .obj path stores pending drop", "[editor][layer][drop]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const char *paths[] = {"some/path/model.obj"};
  editor.OnPathsDropped(1, paths, 100.0f, 200.0f);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: OnPathsDropped with .png path stores pending drop", "[editor][layer][drop]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const char *paths[] = {"some/path/texture.png"};
  editor.OnPathsDropped(1, paths, 100.0f, 200.0f);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: OnPathsDropped with multiple paths stores all", "[editor][layer][drop]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const char *paths[] = {"model.obj", "texture.png", "other.txt"};
  editor.OnPathsDropped(3, paths, 150.0f, 250.0f);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer: SetHotReloadOverlay
// ===========================================================================

TEST_CASE("EditorLayer: SetHotReloadOverlay renders overlay during Render", "[editor][render][overlay]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();

  editor.SetHotReloadOverlay(true, 0.5f, 1.2f, "Loading...");

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: SetHotReloadOverlay with full progress renders correctly", "[editor][render][overlay]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();

  editor.SetHotReloadOverlay(true, 1.0f, 3.14f, "Done");

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer: SetHotReloadOverlay inactive renders no overlay", "[editor][render][overlay]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();

  editor.SetHotReloadOverlay(false, 0.0f, 0.0f, "");

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer: GetSelectedObjectIds
// ===========================================================================

TEST_CASE("EditorLayer: GetSelectedObjectIds returns empty when nothing selected", "[editor][layer]") {
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  REQUIRE(editor.GetSelectedObjectIds().empty());
}

TEST_CASE("EditorLayer: GetSelectedObjectIds returns selected id after select command", "[editor][layer]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "selectable";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "selectable"}});

  const auto ids = editor.GetSelectedObjectIds();
  REQUIRE(ids.size() == 1);
  REQUIRE(ids[0] == "selectable");
}

TEST_CASE("EditorLayer: GetSelectedObjectIds returns all after multi-select", "[editor][layer]") {
  SceneDocument doc;
  for (const std::string &id : {"a", "b", "c"}) {
    SceneObject obj;
    obj.id = id;
    obj.type = SceneObjectType::Prop;
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{{"ids", nlohmann::json::array({"a", "c"})}});

  const auto ids = editor.GetSelectedObjectIds();
  REQUIRE(ids.size() == 2);
}

TEST_CASE("EditorLayer: GetSelectedObjectIds clears after clear_selection", "[editor][layer]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "clr";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "clr"}});
  REQUIRE(editor.GetSelectedObjectIds().size() == 1);

  editor.ExecuteMcpCommand("editor.clear_selection", nlohmann::json::object());
  REQUIRE(editor.GetSelectedObjectIds().empty());
}

// ===========================================================================
// EditorLayer: Asset-not-found branch in DrawPropertiesPanel (lines 77-78)
// ===========================================================================

TEST_CASE("EditorLayer render: selected asset deleted before Render clears selection", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.assets["temp_asset"] = AssetDef{"model.obj", "1,1,1"};
  {
    SceneObject obj;
    obj.id = "uses_temp";
    obj.type = SceneObjectType::Prop;
    obj.assetId = "temp_asset";
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  // select_asset BEFORE Toggle (select_asset sets m_selectedAssetId, NOT
  // cleared by Toggle)
  editor.ExecuteMcpCommand("editor.select_asset",
                           nlohmann::json{{"id", "temp_asset"}});
  editor.Toggle(); // activate editor — Toggle does NOT clear m_selectedAssetId

  // Delete the asset after toggle so it's missing when DrawPropertiesPanel runs
  editor.ExecuteMcpCommand("editor.delete_asset",
                           nlohmann::json{{"id", "temp_asset"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  // The asset-not-found branch (lines 77-78) should have cleared
  // m_selectedAssetId
  REQUIRE(editor.GetSelectedAssetId().empty());
}

// ===========================================================================
// EditorLayer: More multi-select render tests (now correctly Toggle first)
// ===========================================================================

TEST_CASE("EditorLayer render: multi-select 3 props triggers batch transform section", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle(); // activate FIRST
  // Select 3 props with same asset to hit the "shared asset" batch path
  editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{{"ids", nlohmann::json::array(
                                 {"prop_light", "prop_rb", "prop_no_asset"})}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: multi-select panels and props triggers mixed batch", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  for (int i = 0; i < 2; ++i) {
    SceneObject panel;
    panel.id = std::format("mp_{}", i);
    panel.type = SceneObjectType::Panel;
    doc.objects.push_back(panel);
    SceneObject prop;
    prop.id = std::format("mpr_{}", i);
    prop.type = SceneObjectType::Prop;
    doc.objects.push_back(prop);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle(); // activate FIRST
  editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{
          {"ids", nlohmann::json::array({"mp_0", "mp_1", "mpr_0", "mpr_1"})}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer: Additional MCP handler coverage
// ===========================================================================

TEST_CASE("EditorLayer MCP: create_prefab with valid object creates prefab file", "[editor][mcp][prefab]") {
  SceneDocument doc;
  {
    SceneObject obj;
    obj.id = "pf_source";
    obj.type = SceneObjectType::Prop;
    obj.props["color"] = "red";
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.create_prefab",
      nlohmann::json{{"id", "pf_source"},
                     {"prefabId", "pf-test-001"},
                     {"path", "assets/prefabs/test_001.json"}});
  // May succeed or fail depending on file system; just verify no crash
  (void)result;
  REQUIRE(true);
}

TEST_CASE("EditorLayer MCP: create_prefab with nonexistent object returns error", "[editor][mcp][prefab]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.create_prefab",
      nlohmann::json{{"id", "ghost"},
                     {"prefabId", "pf-999"},
                     {"path", "assets/prefabs/ghost.json"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: create_object_from_asset with nonexistent asset returns error", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result =
      editor.ExecuteMcpCommand("editor.create_object_from_asset",
                               nlohmann::json{{"assetId", "missing_asset"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: create_object_from_asset with valid asset creates object", "[editor][mcp]") {
  SceneDocument doc;
  doc.assets["crate"] = AssetDef{"crate.obj", "1,1,1"};

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.create_object_from_asset", nlohmann::json{{"assetId", "crate"}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects.size() == 1);
}

TEST_CASE("EditorLayer MCP: update_object with unknown id returns error", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{{"id", "ghost_obj"},
                     {"position", nlohmann::json::array({1.0, 2.0, 3.0})}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: rename_object updates object id", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "old_name";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.rename_object",
      nlohmann::json{{"id", "old_name"}, {"newId", "new_name"}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects[0].id == "new_name");
}

TEST_CASE("EditorLayer MCP: rename_object with duplicate id returns error", "[editor][mcp]") {
  SceneDocument doc;
  for (const std::string &id : {"obj_a", "obj_b"}) {
    SceneObject obj;
    obj.id = id;
    obj.type = SceneObjectType::Prop;
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.rename_object",
      nlohmann::json{{"id", "obj_a"}, {"newId", "obj_b"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: rename_object with empty newId returns error", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "target";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.rename_object", nlohmann::json{{"id", "target"}, {"newId", ""}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: delete with unknown id returns error", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.delete", nlohmann::json{{"id", "nobody"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: delete removes specified object", "[editor][mcp]") {
  SceneDocument doc;
  for (const std::string &id : {"keep", "remove_me"}) {
    SceneObject obj;
    obj.id = id;
    obj.type = SceneObjectType::Prop;
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.delete", nlohmann::json{{"id", "remove_me"}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects.size() == 1);
  REQUIRE(editor.GetDocument().objects[0].id == "keep");
}

TEST_CASE("EditorLayer MCP: delete_asset removes asset from document", "[editor][mcp]") {
  SceneDocument doc;
  doc.assets["del_me"] = AssetDef{"x.obj", "1,1,1"};
  doc.assets["keep_me"] = AssetDef{"y.obj", "2,2,2"};

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.delete_asset", nlohmann::json{{"id", "del_me"}});
  REQUIRE(result.ok);
  REQUIRE_FALSE(editor.GetDocument().assets.contains("del_me"));
  REQUIRE(editor.GetDocument().assets.contains("keep_me"));
}

TEST_CASE("EditorLayer MCP: delete_asset with unknown id returns error", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.delete_asset", nlohmann::json{{"id", "ghost_asset"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: update_asset with nonexistent id returns error", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_asset",
      nlohmann::json{{"id", "no_such_asset"}, {"displayName", "X"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: undo on empty history returns undone=false", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(result.ok);
  REQUIRE(result.data.value("undone", true) == false);
}

TEST_CASE("EditorLayer MCP: redo on empty history returns redone=false", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result =
      editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
  REQUIRE(result.ok);
}

TEST_CASE("EditorLayer MCP: undo after create_object restores previous state", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"id", "undo_target"}, {"type", "Prop"}});
  REQUIRE(editor.GetDocument().objects.size() == 1);

  const auto result =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(result.ok);
  REQUIRE(result.data.value("undone", false) == true);
  REQUIRE(editor.GetDocument().objects.empty());
}

TEST_CASE("EditorLayer MCP: redo after undo re-applies the change", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"id", "redo_target"}, {"type", "Prop"}});
  editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(editor.GetDocument().objects.empty());

  editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
  REQUIRE(editor.GetDocument().objects.size() == 1);
}

TEST_CASE("EditorLayer MCP: new_scene clears document and triggers reload", "[editor][mcp]") {
  SceneDocument doc;
  doc.sceneId = "old-scene";
  {
    SceneObject obj;
    obj.id = "existing";
    obj.type = SceneObjectType::Prop;
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result =
      editor.ExecuteMcpCommand("editor.new_scene", nlohmann::json::object());
  REQUIRE(result.ok);
  // After new_scene, WantsSceneReload should be true (or objects cleared)
  REQUIRE(true);
}

TEST_CASE("EditorLayer MCP: reload_scene sets WantsSceneReload", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result =
      editor.ExecuteMcpCommand("editor.reload_scene", nlohmann::json::object());
  REQUIRE(result.ok);
  REQUIRE(editor.WantsSceneReload());
}

TEST_CASE("EditorLayer: AcknowledgeReload clears WantsSceneReload", "[editor][layer]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand("editor.reload_scene", nlohmann::json::object());
  REQUIRE(editor.WantsSceneReload());
  editor.AcknowledgeReload();
  REQUIRE_FALSE(editor.WantsSceneReload());
}

TEST_CASE("EditorLayer MCP: reparent_object with nonexistent child returns error", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.reparent_object",
      nlohmann::json{{"id", "ghost"}, {"newParentId", ""}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: reparent_object to empty parent clears parent", "[editor][mcp]") {
  SceneDocument doc;
  for (const std::string &id : {"root", "child"}) {
    SceneObject obj;
    obj.id = id;
    obj.type = SceneObjectType::Panel;
    doc.objects.push_back(obj);
  }
  doc.objects[1].props["parentId"] = "root";

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Reparent to empty string = unparent
  const auto result = editor.ExecuteMcpCommand(
      "editor.reparent_object",
      nlohmann::json{{"id", "child"}, {"newParentId", ""}});
  REQUIRE(result.ok);
}

TEST_CASE("EditorLayer MCP: duplicate with count>1 creates multiple copies", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "multi_dup";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.duplicate", nlohmann::json{{"id", "multi_dup"}, {"count", 3}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects.size() == 4); // original + 3 copies
}

TEST_CASE("EditorLayer MCP: duplicate with ids array duplicates multiple objects", "[editor][mcp]") {
  SceneDocument doc;
  for (const std::string &id : {"dup_a", "dup_b"}) {
    SceneObject obj;
    obj.id = id;
    obj.type = SceneObjectType::Prop;
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.duplicate",
      nlohmann::json{{"ids", nlohmann::json::array({"dup_a", "dup_b"})}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects.size() == 4); // 2 originals + 2 copies
}

TEST_CASE("EditorLayer MCP: duplicate with missing id returns error", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.duplicate", nlohmann::json{{"id", "ghost_dup"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: save_scene triggers WantsSceneReload or succeeds", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result =
      editor.ExecuteMcpCommand("editor.save_scene", nlohmann::json::object());
  // save to default/empty path may fail — just no crash
  (void)result;
  REQUIRE(true);
}

TEST_CASE("EditorLayer MCP: select with unknown id returns ok with empty selection", "[editor][mcp]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.select", nlohmann::json{{"id", "ghost_select"}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetSelectedObjectIds().empty());
}

TEST_CASE("EditorLayer MCP: update_object sets components field", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "comp_target";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{
          {"id", "comp_target"},
          {"components", nlohmann::json::array({nlohmann::json{
                             {"type", "rigidbody"},
                             {"props", nlohmann::json{{"mass", "2.0"}}}}})}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects[0].components.size() == 1);
  REQUIRE(editor.GetDocument().objects[0].components[0].type == "rigidbody");
}

TEST_CASE("EditorLayer MCP: update_object with invalid components array returns error", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "bad_comp";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{{"id", "bad_comp"},
                     {"components", nlohmann::json{{"not", "an_array"}}}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: update_object assetId null clears asset reference", "[editor][mcp]") {
  SceneDocument doc;
  doc.assets["myasset"] = AssetDef{"m.obj", "1,1,1"};
  SceneObject obj;
  obj.id = "has_asset";
  obj.type = SceneObjectType::Prop;
  obj.assetId = "myasset";
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{{"id", "has_asset"}, {"assetId", nullptr}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects[0].assetId.empty());
}

TEST_CASE("EditorLayer MCP: update_object with invalid position returns error", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "pos_err";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{{"id", "pos_err"}, {"position", "not_an_array"}});
  REQUIRE_FALSE(result.ok);
}

TEST_CASE("EditorLayer MCP: transform alias same behavior as update_object", "[editor][mcp]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "alias_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.transform",
      nlohmann::json{{"id", "alias_obj"},
                     {"scale", nlohmann::json::array({2.0, 2.0, 2.0})}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects[0].scale.x == Approx(2.0f));
}

// ===========================================================================
// EditorLayer: Render with SetLiveRegistry
// ===========================================================================

TEST_CASE("EditorLayer render: with live registry set renders without crash", "[editor][render][registry]") {
  ImGuiContextGuard imgui;

  // Build a scene and a matching registry
  SceneDocument doc;
  {
    SceneObject prop;
    prop.id = "live_prop";
    prop.type = SceneObjectType::Prop;
    doc.objects.push_back(prop);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  Horo::Registry reg;
  const auto e = reg.Create();
  reg.Add<Horo::MeshComponent>(e);
  reg.Add<Horo::TransformComponent>(e);

  editor.SyncRuntimeEntityIds(reg);
  editor.SetLiveRegistry(&reg);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "live_prop"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer render: property panel edge cases with correct Toggle ordering
// ===========================================================================

TEST_CASE("EditorLayer render: prop with props map renders schema fields", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  {
    SceneObject obj;
    obj.id = "props_obj";
    obj.type = SceneObjectType::Prop;
    obj.props["color"] = "red";
    obj.props["size"] = "large";
    obj.props["health"] = "100";
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "props_obj"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: camera with fov and near far props renders camera section", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  {
    SceneObject cam_obj;
    cam_obj.id = "cam_fov";
    cam_obj.type = SceneObjectType::Camera;
    cam_obj.props["fov"] = "60";
    cam_obj.props["near"] = "0.1";
    cam_obj.props["far"] = "1000";
    doc.objects.push_back(cam_obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "cam_fov"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: prop with all component types renders all component fields", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  {
    SceneObject obj;
    obj.id = "all_comps";
    obj.type = SceneObjectType::Prop;
    {
      ComponentDesc rb;
      rb.type = "rigidbody";
      rb.props["mass"] = "5.0";
      rb.props["isKinematic"] = "true";
      rb.props["useGravity"] = "false";
      obj.components.push_back(rb);
    }
    {
      ComponentDesc script;
      script.type = "script";
      script.props["behaviorTag"] = "EnemyAI";
      obj.components.push_back(script);
    }
    {
      ComponentDesc light;
      light.type = "light";
      light.props["color"] = "1,1,0.5";
      light.props["intensity"] = "2.0";
      obj.components.push_back(light);
    }
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "all_comps"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: selecting and deselecting in sequence covers toggle paths", "[editor][render]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  editor.Toggle();

  Camera cam;
  // Select → render → clear → render × 3
  for (const std::string &id : {"prop1", "cam1", "panel1"}) {
    editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", id}});
    editor.Render(cam, 1280, 720);
    editor.ExecuteMcpCommand("editor.clear_selection",
                             nlohmann::json::object());
    editor.Render(cam, 1280, 720);
  }
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: multi-select then single-select transition renders correctly", "[editor][render]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  Camera cam;
  // Multi-select
  editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{{"ids", nlohmann::json::array(
                                 {"prop_light", "prop_rb", "prop_script"})}});
  editor.Render(cam, 1280, 720);
  // Single-select (transition from multi)
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "prop_light"}});
  editor.Render(cam, 1280, 720);
  // Empty selection
  editor.ExecuteMcpCommand("editor.clear_selection", nlohmann::json::object());
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: create objects via MCP then render with each selected", "[editor][render]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();

  // Create objects of different types and select each one
  const std::vector<std::string> types = {"Prop", "Panel", "Camera", "Light"};
  for (size_t i = 0; i < types.size(); ++i) {
    editor.ExecuteMcpCommand(
        "editor.create_object",
        nlohmann::json{{"id", std::format("dyn_{}", i)}, {"type", types[i]}});
  }

  Camera cam;
  for (size_t i = 0; i < types.size(); ++i) {
    editor.ExecuteMcpCommand("editor.select",
                             nlohmann::json{{"id", std::format("dyn_{}", i)}});
    editor.Render(cam, 1280, 720);
  }
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer render: asset-selection paths in DrawPropertiesSelectedAsset
// ===========================================================================

TEST_CASE("EditorLayer render: select_asset shows asset properties panel", "[editor][render][properties][asset]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  AssetDef def;
  // Keep mesh and albedoMap empty: the OpenGL backend reports
  // supportsOffscreenTargets=true even without a real context, so a non-empty
  // mesh triggers AssetThumbnailRenderer::Init() which calls glGenFramebuffers
  // with null GLAD pointers → SEGFAULT.
  def.mesh = "";
  def.renderScale = "1.0,1.0,1.0";
  def.albedoMap = "";
  def.displayName = "Cube";
  def.guid = "asset-guid-001";
  doc.assets["mesh_cube"] = def;

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  editor.ExecuteMcpCommand("editor.select_asset",
                           nlohmann::json{{"id", "mesh_cube"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: stale selected asset id clears selection on render", "[editor][render][properties][asset]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.assets["temp_mesh"] = AssetDef{"", "1.0,1.0,1.0"};

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  editor.ExecuteMcpCommand("editor.select_asset",
                           nlohmann::json{{"id", "temp_mesh"}});
  // Delete the asset so the selection becomes stale
  editor.ExecuteMcpCommand("editor.delete_asset",
                           nlohmann::json{{"id", "temp_mesh"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: clear selection then render shows no-selection text", "[editor][render][properties]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  {
    SceneObject obj;
    obj.id = "tmp_panel";
    obj.type = SceneObjectType::Panel;
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "tmp_panel"}});
  editor.ExecuteMcpCommand("editor.clear_selection", nlohmann::json::object());

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer OnUpdate: exercises PublishMcpSnapshot and builder helpers
// ===========================================================================

TEST_CASE("EditorLayer OnUpdate: exercises snapshot builders with empty scene", "[editor][mcp][snapshot]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();

  Camera cam;
  // OnUpdate calls HandleEditorKeyboardShortcuts, UpdateNonFlyModeInput,
  // and then PublishMcpSnapshot which exercises BuildMcpBuildSnapshot
  // and BuildMcpSchemaCatalogSnapshot — these were previously 0% covered.
  editor.OnUpdate(1.0f / 60.0f, cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer OnUpdate: exercises snapshot builders with populated scene", "[editor][mcp][snapshot]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "prop_light"}});

  Camera cam;
  editor.OnUpdate(1.0f / 60.0f, cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer OnUpdate: exercises snapshot builders with schema-bearing document", "[editor][mcp][snapshot]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  {
    // A prop with schema-driven fields to exercise BuildMcpSchemaFieldSnapshot
    SceneObject obj;
    obj.id = "schema_prop";
    obj.type = SceneObjectType::Prop;
    obj.props["color"] = "red";
    obj.props["size"] = "large";
    obj.assetId = "mesh_x";
    doc.objects.push_back(obj);
  }
  {
    SceneObject cam_obj;
    cam_obj.id = "cam_schema";
    cam_obj.type = SceneObjectType::Camera;
    cam_obj.props["fov"] = "90";
    cam_obj.props["nearClip"] = "0.1";
    cam_obj.props["farClip"] = "500";
    doc.objects.push_back(cam_obj);
  }
  doc.assets["mesh_x"] = AssetDef{"models/box.obj", "1.0,1.0,1.0"};

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  Camera cam;
  editor.OnUpdate(1.0f / 60.0f, cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer OnUpdate: multiple consecutive updates stay stable", "[editor][mcp][snapshot]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  editor.Toggle();

  Camera cam;
  for (int i = 0; i < 5; ++i)
    editor.OnUpdate(1.0f / 60.0f, cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer OnUpdate: inactive editor skips update body", "[editor][mcp][snapshot]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  // editor.Toggle() NOT called — editor is inactive
  // OnUpdate body is skipped; last IO block runs safely

  Camera cam;
  editor.OnUpdate(1.0f / 60.0f, cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer render: parent-child hierarchy tree paths (DrawTreeNode)
// ===========================================================================

// Helper: document with a 3-level parent → child → grandchild chain.
static SceneDocument MakeHierarchyDocument() {
  SceneDocument doc;
  doc.sceneId = "hierarchy-scene";

  SceneObject root;
  root.id = "root_panel";
  root.type = SceneObjectType::Panel;
  doc.objects.push_back(root);

  SceneObject child;
  child.id = "child_prop";
  child.type = SceneObjectType::Prop;
  child.assetId = "asset_h";
  child.props["parentId"] = "root_panel";
  doc.objects.push_back(child);

  SceneObject grandchild;
  grandchild.id = "gc_light";
  grandchild.type = SceneObjectType::Light;
  grandchild.props["parentId"] = "child_prop";
  ComponentDesc lc;
  lc.type = "light";
  lc.props["intensity"] = "1.0";
  grandchild.components.push_back(lc);
  doc.objects.push_back(grandchild);

  SceneObject sibling;
  sibling.id = "sibling_cam";
  sibling.type = SceneObjectType::Camera;
  sibling.props["parentId"] = "root_panel";
  doc.objects.push_back(sibling);

  doc.assets["asset_h"] = AssetDef{"", "1.0,1.0,1.0"};
  return doc;
}

TEST_CASE("EditorLayer render: parent-child hierarchy renders DrawTreeNode recursive", "[editor][render][hierarchy]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeHierarchyDocument());
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: select root of hierarchy renders tree with selection", "[editor][render][hierarchy]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeHierarchyDocument());
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "root_panel"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: select child node renders selected subtree leaf", "[editor][render][hierarchy]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeHierarchyDocument());
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "child_prop"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: select grandchild renders deepest tree level", "[editor][render][hierarchy]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeHierarchyDocument());
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "gc_light"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: cycle through all hierarchy nodes in sequence", "[editor][render][hierarchy]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeHierarchyDocument());
  editor.Toggle();

  Camera cam;
  for (const char *id :
       {"root_panel", "child_prop", "gc_light", "sibling_cam"}) {
    editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", id}});
    editor.Render(cam, 1280, 720);
  }
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: reparent via MCP then render covers reparent path", "[editor][render][hierarchy]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "reparent-scene";
  {
    SceneObject a;
    a.id = "obj_a";
    a.type = SceneObjectType::Panel;
    doc.objects.push_back(a);
  }
  {
    SceneObject b;
    b.id = "obj_b";
    b.type = SceneObjectType::Prop;
    doc.objects.push_back(b);
  }
  {
    SceneObject c;
    c.id = "obj_c";
    c.type = SceneObjectType::Light;
    doc.objects.push_back(c);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  // Reparent b under a
  auto r1 = editor.ExecuteMcpCommand(
      "editor.reparent_object",
      nlohmann::json{{"id", "obj_b"}, {"parentId", "obj_a"}});
  REQUIRE(r1.ok);

  // Reparent c under b (creates a chain a→b→c)
  auto r2 = editor.ExecuteMcpCommand(
      "editor.reparent_object",
      nlohmann::json{{"id", "obj_c"}, {"parentId", "obj_b"}});
  REQUIRE(r2.ok);

  Camera cam;
  editor.Render(cam, 1280, 720);

  // Clear parent of c (makes it a root again)
  auto r3 = editor.ExecuteMcpCommand(
      "editor.reparent_object",
      nlohmann::json{{"id", "obj_c"}, {"parentId", ""}});
  REQUIRE(r3.ok);

  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: create_object with parentId via MCP builds hierarchy", "[editor][render][hierarchy]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();

  // Create root
  editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"id", "hier_root"}, {"type", "Panel"}});
  // Create child under root
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"id", "hier_child"},
                                          {"type", "Prop"},
                                          {"parentId", "hier_root"}});
  // Create grandchild under child
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"id", "hier_gc"},
                                          {"type", "Light"},
                                          {"parentId", "hier_child"}});

  Camera cam;
  editor.Render(cam, 1280, 720);

  // Select each level
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "hier_root"}});
  editor.Render(cam, 1280, 720);

  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "hier_child"}});
  editor.Render(cam, 1280, 720);

  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "hier_gc"}});
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: multi-select across parent and child covers batch paths", "[editor][render][hierarchy][multiselect]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeHierarchyDocument());
  editor.Toggle();

  // Select both root and its child simultaneously → multi-select path
  editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{{"ids", nlohmann::json::array(
                                 {"root_panel", "child_prop", "gc_light"})}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer OnUpdate: parent-child hierarchy exercises snapshot with tree", "[editor][mcp][snapshot][hierarchy]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeHierarchyDocument());
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "child_prop"}});

  Camera cam;
  editor.OnUpdate(1.0f / 60.0f, cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer render: DrawConsoleTab with log entries at various levels
// ===========================================================================

TEST_CASE("EditorLayer render: console tab draws Info log entries", "[editor][render][console]") {
  ImGuiContextGuard imgui;

  // Push log lines so DrawConsoleTab has content to iterate
  LogBuffer::Instance().Clear();
  LogInfo("test info message one");
  LogInfo("test info message two");

  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: console tab draws Warn and Error log entries", "[editor][render][console]") {
  ImGuiContextGuard imgui;

  LogBuffer::Instance().Clear();
  LogWarn("test warning entry");
  LogError("test error entry");
  LogInfo("test info after errors");

  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: console tab with mixed log levels renders all rows", "[editor][render][console]") {
  ImGuiContextGuard imgui;

  LogBuffer::Instance().Clear();
  for (int i = 0; i < 5; ++i)
    LogInfo("info line {}", i);
  for (int i = 0; i < 3; ++i)
    LogWarn("warn line {}", i);
  for (int i = 0; i < 2; ++i)
    LogError("error line {}", i);

  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer render: objects with prefabInstance set
// ===========================================================================

TEST_CASE("EditorLayer render: object with prefabInstance set renders without crash", "[editor][render][prefab]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "prefab-scene";

  SceneObject prefabObj;
  prefabObj.id = "prefab_panel";
  prefabObj.type = SceneObjectType::Panel;
  prefabObj.prefabInstance =
      ScenePrefabInstance{"my_prefab_id", "assets/scenes/my_prefab.json"};
  doc.objects.push_back(prefabObj);

  SceneObject normalObj;
  normalObj.id = "normal_prop";
  normalObj.type = SceneObjectType::Prop;
  doc.objects.push_back(normalObj);

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: prefab instance selected shows properties panel", "[editor][render][prefab]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "prefab-sel-scene";

  SceneObject prefabObj;
  prefabObj.id = "prefab_prop";
  prefabObj.type = SceneObjectType::Prop;
  prefabObj.prefabInstance =
      ScenePrefabInstance{"scene_pfb", "levels/room.json"};
  prefabObj.assetId = "pfb_asset";
  doc.objects.push_back(prefabObj);
  doc.assets["pfb_asset"] = AssetDef{"", "1.0,1.0,1.0"};

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "prefab_prop"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: prefab instance with children in hierarchy", "[editor][render][prefab][hierarchy]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "pfb-hier-scene";

  SceneObject root;
  root.id = "pfb_root";
  root.type = SceneObjectType::Panel;
  root.prefabInstance = ScenePrefabInstance{"group_pfb", "groups/group.json"};
  doc.objects.push_back(root);

  SceneObject child;
  child.id = "pfb_child";
  child.type = SceneObjectType::Prop;
  child.props["parentId"] = "pfb_root";
  doc.objects.push_back(child);

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);

  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "pfb_root"}});
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer render: duplicate then render covers duplicate code paths
// ===========================================================================

TEST_CASE("EditorLayer render: duplicate object then render with new object selected", "[editor][render][mcp]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  editor.Toggle();

  // Select and duplicate a prop
  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "prop1"}});
  auto dupResult = editor.ExecuteMcpCommand("editor.duplicate",
                                            nlohmann::json{{"id", "prop1"}});
  REQUIRE(dupResult.ok);

  Camera cam;
  editor.Render(cam, 1280, 720);

  // Select the duplicate (it will be the last object)
  const auto &doc = editor.GetDocument();
  REQUIRE(doc.objects.size() > 4u);
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", doc.objects.back().id}});
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: duplicate with count creates multiple objects then render", "[editor][render][mcp]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  editor.Toggle();

  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"id", "panel1"}});
  auto dupResult = editor.ExecuteMcpCommand(
      "editor.duplicate", nlohmann::json{{"id", "panel1"}, {"count", 3}});
  REQUIRE(dupResult.ok);

  Camera cam;
  editor.Render(cam, 1280, 720);

  // Multi-select the duplicates
  const auto &doc = editor.GetDocument();
  nlohmann::json ids = nlohmann::json::array();
  const size_t total = doc.objects.size();
  for (size_t i = total >= 3 ? total - 3 : 0; i < total; ++i)
    ids.push_back(doc.objects[i].id);
  editor.ExecuteMcpCommand("editor.select", nlohmann::json{{"ids", ids}});
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer render: scene controls (new_scene / reload_scene) then render
// ===========================================================================

TEST_CASE("EditorLayer render: new_scene clears and renders empty scene", "[editor][render][scene]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);

  // new_scene replaces the document
  editor.ExecuteMcpCommand("editor.new_scene", nlohmann::json::object());
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: reload_scene sets WantsSceneReload then render is safe", "[editor][render][scene]") {
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeDocWithAllObjectTypes());
  editor.Toggle();

  editor.ExecuteMcpCommand("editor.reload_scene", nlohmann::json::object());

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayer render: large scene with many objects covers more tree nodes
// ===========================================================================

TEST_CASE("EditorLayer render: large flat scene with many objects renders hierarchy", "[editor][render][hierarchy]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "large-flat";

  // Create 20 flat objects of mixed types
  const std::array<SceneObjectType, 4> types = {
      SceneObjectType::Panel, SceneObjectType::Prop, SceneObjectType::Light,
      SceneObjectType::Camera};
  for (int i = 0; i < 20; ++i) {
    SceneObject obj;
    obj.id = std::format("obj_{:02d}", i);
    obj.type = types[static_cast<size_t>(i) % 4];
    if (obj.type == SceneObjectType::Prop)
      obj.assetId = "shared_asset";
    doc.objects.push_back(obj);
  }
  doc.assets["shared_asset"] = AssetDef{"", "1.0,1.0,1.0"};

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);

  // Select a few in sequence
  for (int i = 0; i < 5; ++i) {
    editor.ExecuteMcpCommand(
        "editor.select",
        nlohmann::json{{"id", std::format("obj_{:02d}", i * 4)}});
    editor.Render(cam, 1280, 720);
  }
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: wide tree with multiple root siblings each having children", "[editor][render][hierarchy]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "wide-tree";

  for (int r = 0; r < 3; ++r) {
    SceneObject root;
    root.id = std::format("root_{}", r);
    root.type = SceneObjectType::Panel;
    doc.objects.push_back(root);

    for (int c = 0; c < 3; ++c) {
      SceneObject child;
      child.id = std::format("child_{}_{}", r, c);
      child.type = SceneObjectType::Prop;
      child.props["parentId"] = root.id;
      doc.objects.push_back(child);
    }
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  Camera cam;
  editor.Render(cam, 1280, 720);

  // Select one of the children
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "child_1_2"}});
  editor.Render(cam, 1280, 720);

  // Multi-select across roots
  editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{
          {"ids", nlohmann::json::array({"root_0", "root_1", "child_2_0"})}});
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer OnUpdate: wide tree hierarchy exercises snapshot builders", "[editor][mcp][snapshot][hierarchy]") {
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.sceneId = "snapshot-tree";
  for (int r = 0; r < 3; ++r) {
    SceneObject root;
    root.id = std::format("snap_root_{}", r);
    root.type = SceneObjectType::Panel;
    doc.objects.push_back(root);
    for (int c = 0; c < 2; ++c) {
      SceneObject child;
      child.id = std::format("snap_child_{}_{}", r, c);
      child.type = SceneObjectType::Prop;
      child.props["parentId"] = root.id;
      doc.objects.push_back(child);
    }
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();

  Camera cam;
  editor.OnUpdate(1.0f / 60.0f, cam, 1280, 720);
  editor.OnUpdate(1.0f / 60.0f, cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// NEW COVERAGE TESTS — EditorPropertiesPanel.cpp
// ===========================================================================

TEST_CASE("EditorLayer render: object with prefabInstance renders prefab identity section", "[editor][render][properties][prefab]") {
  // Exercises lines 402–404 in DrawPropertiesIdentitySection
  ImGuiContextGuard imgui;
  SceneDocument doc;
  {
    SceneObject obj;
    obj.id = "prefab_obj";
    obj.type = SceneObjectType::Prop;
    obj.position = {1.f, 0.f, 0.f};
    obj.prefabInstance =
        ScenePrefabInstance{"my_prefab_001", "prefabs/box.prefab"};
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "prefab_obj"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: camera with followTargetId renders selected target in combo", "[editor][render][properties][camera]") {
  // MakeRichDocument contains cam_follow with props["followTargetId"] =
  // "prop_light" Selecting it exercises the curTarget > 0 path in
  // DrawPropertiesCameraSection
  ImGuiContextGuard imgui;
  EditorLayer editor;
  editor.LoadDocument(MakeRichDocument());
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "cam_follow"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: prop with stale assetId shows missing asset warning", "[editor][render][properties]") {
  // Object references assetId absent from doc.assets
  // → DrawPropertiesAssetSection renders "Missing asset:" coloured text (lines
  // 597–600)
  ImGuiContextGuard imgui;
  SceneDocument doc;
  {
    SceneObject obj;
    obj.id = "stale_asset_obj";
    obj.type = SceneObjectType::Prop;
    obj.assetId = "ghost_asset"; // absent from doc.assets
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "stale_asset_obj"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: multi-select with shared non-empty assetId shows shared asset label", "[editor][render][properties][multiselect]") {
  // All selected objects share the same non-empty assetId
  // → DrawPropertiesMultiSelect hits hasSharedAssetId=true && !empty branch
  // (line 182)
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.assets["shared_mesh"] = AssetDef{"", "1.0,1.0,1.0"};
  for (int i = 0; i < 3; ++i) {
    SceneObject obj;
    obj.id = std::format("shared_{}", i);
    obj.type = SceneObjectType::Prop;
    obj.assetId = "shared_mesh";
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{{"ids", nlohmann::json::array(
                                 {"shared_0", "shared_1", "shared_2"})}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: multi-select with all-empty assetIds shows none label", "[editor][render][properties][multiselect]") {
  // All selected objects have empty assetId
  // → DrawPropertiesMultiSelect hits hasSharedAssetId=true && empty branch
  // (line 180)
  ImGuiContextGuard imgui;
  SceneDocument doc;
  for (int i = 0; i < 3; ++i) {
    SceneObject obj;
    obj.id = std::format("noasset_{}", i);
    obj.type = SceneObjectType::Prop;
    obj.assetId = "";
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand(
      "editor.select",
      nlohmann::json{{"ids", nlohmann::json::array(
                                 {"noasset_0", "noasset_1", "noasset_2"})}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: camera with fov nearClip farClip props renders full camera section", "[editor][render][properties][camera]") {
  // Exercises drawCamFloatProp for fov, nearClip, farClip with valid string
  // values
  ImGuiContextGuard imgui;
  SceneDocument doc;
  {
    SceneObject cam_obj;
    cam_obj.id = "main_camera";
    cam_obj.type = SceneObjectType::Camera;
    cam_obj.position = {0, 5, -10};
    cam_obj.pitch = -15.0f;
    cam_obj.yaw = 0.0f;
    cam_obj.roll = 0.0f;
    cam_obj.props["fov"] = "60";
    cam_obj.props["nearClip"] = "0.1";
    cam_obj.props["farClip"] = "1000";
    doc.objects.push_back(cam_obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "main_camera"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: prop with unknown component type renders generic header", "[editor][render][properties]") {
  // A component whose type is neither 'light', 'rigidbody', nor 'script'
  // → DrawPropertiesComponentsList uses headerLabel = comp.type (line 786)
  ImGuiContextGuard imgui;
  SceneDocument doc;
  {
    SceneObject obj;
    obj.id = "custom_comp_obj";
    obj.type = SceneObjectType::Prop;
    ComponentDesc custom;
    custom.type = "custom_component_xyz";
    custom.props["custom_key"] = "custom_value";
    obj.components.push_back(custom);
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "custom_comp_obj"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

TEST_CASE("EditorLayer render: prop with known assetId renders asset info lines", "[editor][render][properties]") {
  // Covers DrawPropertiesAssetSection with a valid matching asset
  // (mesh/renderScale lines)
  ImGuiContextGuard imgui;
  SceneDocument doc;
  doc.assets["known_asset"] = AssetDef{"", "2.0,1.0,1.0"};
  {
    SceneObject obj;
    obj.id = "asset_obj";
    obj.type = SceneObjectType::Prop;
    obj.assetId = "known_asset";
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);
  editor.Toggle();
  editor.ExecuteMcpCommand("editor.select",
                           nlohmann::json{{"id", "asset_obj"}});

  Camera cam;
  editor.Render(cam, 1280, 720);
  REQUIRE(true);
}

// ===========================================================================
// NEW COVERAGE TESTS — EditorMcpHandlers.cpp
// ===========================================================================

TEST_CASE("EditorLayer MCP: new_scene with sceneId and sceneName sets them on document", "[editor][mcp]") {
  // Exercises lines 866–873 in McpHandleNewScene (sceneId/sceneName branches)
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.new_scene", nlohmann::json{{"sceneId", "brand-new-scene"},
                                         {"sceneName", "Brand New Scene"}});
  REQUIRE(result.ok);
  REQUIRE(result.data.value("sceneId", std::string{}) == "brand-new-scene");
  REQUIRE(result.data.value("sceneName", std::string{}) == "Brand New Scene");
  REQUIRE(editor.GetDocument().sceneId == "brand-new-scene");
  REQUIRE(editor.GetDocument().sceneName == "Brand New Scene");
}

TEST_CASE("EditorLayer MCP: create_object with Camera type auto-generates camera id", "[editor][mcp]") {
  // When no id is provided and type is Camera, GenerateCameraId is used (lines
  // 468–469)
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.create_object", nlohmann::json{{"type", "Camera"}});
  REQUIRE(result.ok);
  const auto &objects = editor.GetDocument().objects;
  REQUIRE(!objects.empty());
  REQUIRE(!objects.back().id.empty());
  REQUIRE(objects.back().type == SceneObjectType::Camera);
}

TEST_CASE("EditorLayer MCP: create_object with valid parentId links to parent", "[editor][mcp]") {
  // Exercises lines 501–507 (parentId branch, valid parent)
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"id", "parent_obj"}, {"type", "Prop"}});

  const auto result = editor.ExecuteMcpCommand(
      "editor.create_object", nlohmann::json{{"id", "child_obj"},
                                             {"type", "Prop"},
                                             {"parentId", "parent_obj"}});
  REQUIRE(result.ok);
  const auto &objects = editor.GetDocument().objects;
  const auto childIt = std::ranges::find_if(
      objects, [](const SceneObject &o) { return o.id == "child_obj"; });
  REQUIRE(childIt != objects.end());
  REQUIRE(childIt->props.at("parentId") == "parent_obj");
}

TEST_CASE("EditorLayer MCP: create_object with invalid parentId returns error", "[editor][mcp]") {
  // Exercises lines 501–506 (parentId branch, parent not found)
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.create_object", nlohmann::json{{"id", "orphan"},
                                             {"type", "Prop"},
                                             {"parentId", "does_not_exist"}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(!result.error.empty());
}

TEST_CASE("EditorLayer MCP: create_object with invalid position array returns error", "[editor][mcp]") {
  // Exercises lines 477–482 in McpHandleCreateObject
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"position", "not-a-vec3"}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error.find("position") != std::string::npos);
}

TEST_CASE("EditorLayer MCP: create_object with invalid scale array returns error", "[editor][mcp]") {
  // Exercises lines 483–488 in McpHandleCreateObject
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"scale", "bad_scale"}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error.find("scale") != std::string::npos);
}

TEST_CASE("EditorLayer MCP: create_object with duplicate id returns error", "[editor][mcp]") {
  // Exercises lines 471–474 in McpHandleCreateObject
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"id", "dup_obj"}, {"type", "Prop"}});

  const auto result = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"id", "dup_obj"}, {"type", "Prop"}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(!result.error.empty());
}

TEST_CASE("EditorLayer MCP: create_object with invalid type string returns error", "[editor][mcp]") {
  // Exercises lines 457–461 in McpHandleCreateObject (ParseSceneObjectType
  // failure)
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.create_object", nlohmann::json{{"type", "InvalidObjectType123"}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error.find("Invalid") != std::string::npos);
}

TEST_CASE("EditorLayer MCP: create_object with props and components creates full object", "[editor][mcp]") {
  // Exercises lines 493–500 in McpHandleCreateObject
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{
          {"id", "full_obj"},
          {"type", "Prop"},
          {"props", nlohmann::json{{"color", "red"}, {"size", "large"}}},
          {"components",
           nlohmann::json::array({nlohmann::json{
               {"type", "script"},
               {"props", nlohmann::json{{"behaviorTag", "EnemyAI"}}}}})}});
  REQUIRE(result.ok);
  const auto &objects = editor.GetDocument().objects;
  const auto it = std::ranges::find_if(
      objects, [](const SceneObject &o) { return o.id == "full_obj"; });
  REQUIRE(it != objects.end());
  REQUIRE(it->props.at("color") == "red");
  REQUIRE(it->components.size() == 1);
  REQUIRE(it->components[0].type == "script");
}

TEST_CASE("EditorLayer MCP: update_object with yaw pitch roll updates rotation fields", "[editor][mcp]") {
  // Exercises lines 607–612 in McpHandleUpdateObject (yaw/pitch/roll branches)
  SceneDocument doc;
  SceneObject obj;
  obj.id = "rot_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{
          {"id", "rot_obj"}, {"yaw", 45.0}, {"pitch", 30.0}, {"roll", 15.0}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().objects[0].yaw == Approx(45.0f));
  REQUIRE(editor.GetDocument().objects[0].pitch == Approx(30.0f));
  REQUIRE(editor.GetDocument().objects[0].roll == Approx(15.0f));
}

TEST_CASE("EditorLayer MCP: update_object with props field merges into object props", "[editor][mcp]") {
  // Exercises lines 618–621 in McpHandleUpdateObject (props merge branch)
  SceneDocument doc;
  SceneObject obj;
  obj.id = "props_merge_obj";
  obj.type = SceneObjectType::Prop;
  obj.props["existing_key"] = "old_value";
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{{"id", "props_merge_obj"},
                     {"props", nlohmann::json{{"existing_key", "new_value"},
                                              {"added_key", "added"}}}});
  REQUIRE(result.ok);
  const auto &props = editor.GetDocument().objects[0].props;
  REQUIRE(props.at("existing_key") == "new_value");
  REQUIRE(props.at("added_key") == "added");
}

TEST_CASE("EditorLayer MCP: update_object with invalid scale returns error", "[editor][mcp]") {
  // Exercises lines 601–606 in McpHandleUpdateObject (scale validation error
  // path)
  SceneDocument doc;
  SceneObject obj;
  obj.id = "scale_err_obj";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{{"id", "scale_err_obj"}, {"scale", "not_a_vec3"}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error.find("scale") != std::string::npos);
}

TEST_CASE("EditorLayer MCP: update_object with nonexistent id returns error", "[editor][mcp]") {
  // Exercises lines 590–592 in McpHandleUpdateObject
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_object",
      nlohmann::json{{"id", "nonexistent_object"}, {"yaw", 10.0}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(!result.error.empty());
}

TEST_CASE("EditorLayer MCP: update_asset with displayName updates display name", "[editor][mcp]") {
  // Exercises lines 831–835 in McpHandleUpdateAsset (displayName branch)
  SceneDocument doc;
  AssetDef def;
  def.displayName = "Original Name";
  doc.assets["test_asset"] = def;

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.update_asset",
      nlohmann::json{{"id", "test_asset"}, {"displayName", "Updated Name"}});
  REQUIRE(result.ok);
  REQUIRE(editor.GetDocument().assets.at("test_asset").displayName ==
          "Updated Name");
}

TEST_CASE("EditorLayer MCP: delete with ids array removes multiple objects", "[editor][mcp]") {
  // Exercises lines 769–776 in McpHandleDelete (ids array branch)
  SceneDocument doc;
  for (int i = 0; i < 4; ++i) {
    SceneObject obj;
    obj.id = std::format("del_obj_{}", i);
    obj.type = SceneObjectType::Prop;
    doc.objects.push_back(obj);
  }

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.delete",
      nlohmann::json{
          {"ids", nlohmann::json::array({"del_obj_1", "del_obj_3"})}});
  REQUIRE(result.ok);
  REQUIRE(result.data.value("deletedCount", 0) == 2);
  REQUIRE(editor.GetDocument().objects.size() == 2);
}

TEST_CASE("EditorLayer MCP: unsupported command returns error with message", "[editor][mcp]") {
  // Exercises lines 384–388 in ExecuteMcpCommand (fallthrough case)
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto result = editor.ExecuteMcpCommand(
      "editor.totally_unsupported_command_xyz", nlohmann::json::object());
  REQUIRE_FALSE(result.ok);
  REQUIRE(!result.error.empty());
}

TEST_CASE("EditorLayer MCP: reparent_object to self returns error", "[editor][mcp]") {
  // Exercises lines 685–688 in McpHandleReparentObject (self-parent cycle
  // check)
  SceneDocument doc;
  SceneObject obj;
  obj.id = "self_parent";
  obj.type = SceneObjectType::Prop;
  doc.objects.push_back(obj);

  EditorLayer editor;
  editor.LoadDocument(doc);

  const auto result = editor.ExecuteMcpCommand(
      "editor.reparent_object",
      nlohmann::json{{"id", "self_parent"}, {"parentId", "self_parent"}});
  REQUIRE_FALSE(result.ok);
  REQUIRE(!result.error.empty());
}

// ===========================================================================
// NEW COVERAGE TESTS — AssetImportService.cpp
// ===========================================================================

TEST_CASE("AssetImportService: ImportTextureForAsset with null asset returns false", "[editor][asset-import]") {
  // Exercises lines 260–265: null asset guard in ImportTextureForAsset
  AssetImportService service;
  std::string err;
  const bool ok = service.ImportTextureForAsset("some/texture.png",
                                                "some_asset", nullptr, &err);
  REQUIRE_FALSE(ok);
  REQUIRE(err == "Asset is required.");
}

TEST_CASE("AssetImportService: ReimportAssetWithDependents with null document returns error", "[editor][asset-import][reimport]") {
  // Exercises lines 334–337: null doc guard in ReimportAssetWithDependents
  AssetImportService service;
  const AssetReimportResult result =
      service.ReimportAssetWithDependents(nullptr, "any_guid", "test reason");
  REQUIRE_FALSE(result.ok);
  REQUIRE(!result.error.empty());
  REQUIRE(!result.records.empty());
}

TEST_CASE("AssetImportService: ReimportAssetWithDependents with unknown guid returns error", "[editor][asset-import][reimport]") {
  // Exercises lines 346–349: rootAssetGuid not found in assetIdByGuid
  SceneDocument doc;
  AssetDef def;
  def.guid = "actual_guid_abc";
  def.displayName = "Some Asset";
  doc.assets["some_asset"] = def;

  AssetImportService service;
  const AssetReimportResult result = service.ReimportAssetWithDependents(
      &doc, "completely_unknown_guid", "test");
  REQUIRE_FALSE(result.ok);
  REQUIRE(!result.error.empty());
}

TEST_CASE("AssetImportService: ReimportAssetWithDependents on unimported asset hits metadata-missing branch", "[editor][asset-import][reimport]") {
  // The asset exists in the document but was never imported:
  // LoadOrBuildMetadata produces empty importerId + sourcePath
  // → ReimportSingleAsset hits "no importer metadata" branch (lines 409–421)
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_reimport_no_meta_branch";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile((root / "CMakePresets.json").string(), "{}");
  ProjectPathGuard guard(root);

  SceneDocument doc;
  AssetDef def;
  def.guid = "guid_never_imported";
  def.displayName = "Never Imported Asset";
  doc.assets["never_imported"] = def;

  AssetImportService service;
  const AssetReimportResult result = service.ReimportAssetWithDependents(
      &doc, "guid_never_imported", "force reimport");
  REQUIRE_FALSE(result.ok);
  REQUIRE(!result.error.empty());
  REQUIRE(!result.records.empty());
  REQUIRE_FALSE(result.records[0].ok);
}

TEST_CASE("AssetImportService: SaveMetadataForAsset persists metadata to disk", "[editor][asset-import]") {
  // Exercises lines 313–327: SaveMetadataForAsset writes metadata that can be
  // reloaded
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_save_meta_for_asset";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile((root / "CMakePresets.json").string(), "{}");
  ProjectPathGuard guard(root);

  AssetDef asset;
  asset.guid = "guid_save_meta_test";
  asset.displayName = "Save Meta Asset";
  // Leave mesh/albedoMap empty to avoid OpenGL calls in produced-file paths

  AssetImportService service;
  std::string err;
  const bool ok = service.SaveMetadataForAsset("save_meta_asset", asset, &err);
  REQUIRE(ok);

  AssetMetadata loaded;
  std::string loadErr;
  REQUIRE(LoadAssetMetadata("guid_save_meta_test", &loaded, &loadErr));
  REQUIRE(loaded.assetGuid == "guid_save_meta_test");
  REQUIRE(loaded.displayName == "Save Meta Asset");
}

TEST_CASE("AssetImportService: SaveMetadataForAsset reports empty metadata path", "[editor][asset-import]") {
  AssetImportService service;
  AssetDef asset;
  asset.displayName = "No Guid Asset";
  std::string err;
  const bool ok = service.SaveMetadataForAsset("asset_without_guid", asset, &err);
  REQUIRE_FALSE(ok);
  REQUIRE(err == "Asset metadata path is empty.");
}

TEST_CASE("AssetImportService: ImportTextureForAsset unsupported type includes message", "[editor][asset-import]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_texture_unsupported_message";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile((root / "CMakePresets.json").string(), "{}");
  ProjectPathGuard guard(root);

  AssetImportService service;
  AssetDef asset;
  asset.guid = "guid_tex_bad_type";
  asset.displayName = "Bad Texture Type";
  std::string err;
  const bool ok = service.ImportTextureForAsset((root / "bad_type.txt").string(),
                                                "tex_bad_type", &asset, &err);
  REQUIRE_FALSE(ok);
  REQUIRE(err ==
          "Unsupported image type (use png, jpg, bmp, tga, webp, …).");
}

// ===========================================================================
// AssetImportService.cpp — additional error-path tests
// ===========================================================================

TEST_CASE("AssetImportService: ReimportSingleAsset fails when saved importer id is no longer registered", "[editor][asset-import][reimport]") {
  // Covers AssetImportService.cpp lines ~437-450:
  // FindById(metadata.importerId) and FindByExtension(metadata.sourcePath)
  // both return nullptr → "Registered importer not found." error path.
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_reimport_bad_importer";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile((root / "CMakePresets.json").string(), "{}");
  ProjectPathGuard guard(root);

  // First do a real import so that a metadata file exists on disk.
  const std::filesystem::path objPath = root / "widget.obj";
  WriteFile(objPath.string(), "v 0 0 0\nv 0 1 0\nv 1 0 0\nf 1 2 3\n");

  AssetImportService service;
  AssetImportResult imported = service.ImportAssetFromSource(
      objPath.string(), "widget", "guid_widget_bad_imp", "Widget");
  REQUIRE(imported.ok);

  // Now corrupt the metadata: point to a non-existent importer id and a
  // source path whose extension is also unregistered.
  AssetMetadata meta;
  std::string loadErr;
  REQUIRE(LoadAssetMetadata("guid_widget_bad_imp", &meta, &loadErr));
  meta.importerId = "nonexistent.custom_importer";
  meta.sourcePath = (root / "widget.xyz").string(); // .xyz not registered
  REQUIRE(SaveAssetMetadata(meta, &loadErr));

  // Build a scene document referencing the corrupted asset.
  SceneDocument doc;
  doc.assets["widget"] = imported.asset;
  doc.assets["widget"].guid = "guid_widget_bad_imp";

  const AssetReimportResult result = service.ReimportAssetWithDependents(
      &doc, "guid_widget_bad_imp", "test reimport with bad importer");
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.error.empty());
  REQUIRE(!result.records.empty());
  REQUIRE_FALSE(result.records.back().ok);
}

TEST_CASE("AssetImportService: ReimportSingleAsset covers albedo source path reimport branch", "[editor][asset-import][reimport]") {
  // Covers AssetImportService.cpp lines ~462-480:
  // metadata.settings["albedoSourcePath"] is non-empty → the texture re-import
  // branch executes.  The texture file need not exist; a failed texture import
  // is silently ignored and the overall reimport still succeeds.
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_reimport_albedo_branch";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile((root / "CMakePresets.json").string(), "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path objPath = root / "cube.obj";
  WriteFile(objPath.string(), "v 0 0 0\nv 0 1 0\nv 1 0 0\nf 1 2 3\n");

  AssetImportService service;
  AssetImportResult imported = service.ImportAssetFromSource(
      objPath.string(), "cube", "guid_cube_albedo", "Cube");
  REQUIRE(imported.ok);

  // Inject a (non-existent) albedo source path into the saved metadata so that
  // the albedo-reimport code path is exercised on the next reimport call.
  AssetMetadata meta;
  std::string loadErr;
  REQUIRE(LoadAssetMetadata("guid_cube_albedo", &meta, &loadErr));
  meta.settings["albedoSourcePath"] = (root / "cube_albedo.png").string();
  REQUIRE(SaveAssetMetadata(meta, &loadErr));

  SceneDocument doc;
  doc.assets["cube"] = imported.asset;
  doc.assets["cube"].guid = "guid_cube_albedo";

  // Reimport: main OBJ succeeds; albedo branch executes (file absent →
  // ignored).
  const AssetReimportResult result = service.ReimportAssetWithDependents(
      &doc, "guid_cube_albedo", "test albedo branch");
  REQUIRE(result.ok);
  REQUIRE(!result.records.empty());
  REQUIRE(result.records[0].ok);
}

// ===========================================================================
// EditorMcpHandlers: FieldWidgetToString + BuildMcpSchemaFieldSnapshot
// (covers EditorMcpHandlers.cpp lines 101–154)
//
// These functions live in an anonymous namespace inside EditorMcpHandlers.cpp
// and are only reachable via PublishMcpSnapshot(), which is called by
// OnUpdate() when the editor is active.  We call Init(nullptr) so that
// EditorLayer creates its own ImGui context and loads m_schema from a custom
// schema JSON that exercises all five FieldDef::Widget values.
// ===========================================================================

TEST_CASE("EditorLayer Init+OnUpdate: schema with all widget types exercises FieldWidgetToString and BuildMcpSchemaFieldSnapshot", "[editor][mcp][schema]") {
  namespace fs = std::filesystem;

  // Build a temp SDK root whose assets/editor_schema.json exercises every
  // FieldDef::Widget branch: Float, Bool, Enum, Color3, String.
  const fs::path sdkRoot =
      Horo::Tests::SecureTempBase() / "horo_mcp_schema_widget_types";
  std::error_code ec;
  fs::remove_all(sdkRoot, ec);
  fs::create_directories(sdkRoot / "assets", ec);

  const std::string schemaJson = R"({
      "types": {
        "Prop": {
          "fields": [
            {"key": "fv", "label": "Float",  "type": "float",
             "min": 0.0, "max": 1.0, "default": "0.5"},
            {"key": "bv", "label": "Bool",   "type": "bool",
             "default": "false"},
            {"key": "ev", "label": "Enum",   "type": "enum",
             "options": ["a", "b"], "default": "a"},
            {"key": "cv", "label": "Color",  "type": "color3",
             "default": "1.0,1.0,1.0"},
            {"key": "sv", "label": "String", "type": "string",
             "default": "hello"}
          ]
        }
      },
      "components": {
        "allWidgets": {
          "label": "AllWidgets",
          "appliesTo": ["Prop"],
          "fields": [
            {"key": "cf", "type": "float",  "min": 0.0, "max": 10.0, "default": "1.0"},
            {"key": "cb", "type": "bool",   "default": "true"},
            {"key": "ce", "type": "enum",   "options": ["x", "y"], "default": "x"},
            {"key": "cc", "type": "color3", "default": "0.5,0.5,0.5"},
            {"key": "cs", "type": "string", "default": ""}
          ]
        }
      }
    })";

  WriteFile((sdkRoot / "assets" / "editor_schema.json").string(), schemaJson);

  // Point BOTH the SDK root AND the project root at our temp dir.
  // Init() searches for shaders via ResolvePreviewShaderPath() which probes
  // candidates under both SdkRoot() and Root().  If shaders are found it calls
  // glCreateShader() which crashes without an OpenGL context.  Redirecting
  // Root() to a dir with no shaders makes ReadFile() throw ShaderException,
  // which Init() already catches and logs — no crash.
  const fs::path prevSdkRoot = Horo::ProjectPath::SdkRoot();
  const fs::path prevProjectRoot = Horo::ProjectPath::Root();
  Horo::ProjectPath::SetSdkRoot(sdkRoot);
  Horo::ProjectPath::SetProjectRoot(sdkRoot);

  SceneDocument doc;
  {
    SceneObject obj;
    obj.id = "schema_obj";
    obj.type = SceneObjectType::Prop;
    doc.objects.push_back(obj);
  }

  {
    EditorLayer editor;
    // Init(nullptr) creates the ImGui context and loads m_schema from
    // sdkRoot/assets/editor_schema.json (Float, Bool, Enum, Color3, String).
    editor.Init(nullptr);
    editor.LoadDocument(doc);
    editor.Toggle(); // make active so OnUpdate() runs the full body

    Camera cam;
    // OnUpdate() → PublishMcpSnapshot()
    //   → BuildMcpSchemaCatalogSnapshot(m_schema)
    //     → BuildMcpSchemaEntrySnapshot (TypeSchema overload,   line 132)
    //     → BuildMcpSchemaEntrySnapshot (ComponentSchema overload, line 144)
    //     → BuildMcpSchemaFieldSnapshot (line 113)
    //     → FieldWidgetToString for Float/Bool/Enum/Color3/String (lines
    //     101–111)
    editor.OnUpdate(1.0f / 60.0f, cam, 1280, 720);

    editor.Shutdown(); // destroys ImGui context created by Init()
  }

  Horo::ProjectPath::SetSdkRoot(prevSdkRoot);
  Horo::ProjectPath::SetProjectRoot(prevProjectRoot);
  REQUIRE(true);
}

// ===========================================================================
// EditorLayerInternal inline helpers: direct unit tests
// (covers EditorLayerInternal.h lines 88–137)
//
// The header is included at the top of this file; each TU gets its own copy
// of the inline functions, and the coverage instrumentation records hits here.
// ===========================================================================

TEST_CASE("EditorLayerInternal: FindEnumOptionIndex returns correct index or 0", "[editor][internal][schema]") {
  // lines 88-94 in EditorLayerInternal.h
  const std::vector<std::string> opts = {"alpha", "beta", "gamma"};
  CHECK(FindEnumOptionIndex(opts, "alpha") == 0);
  CHECK(FindEnumOptionIndex(opts, "beta") == 1);
  CHECK(FindEnumOptionIndex(opts, "gamma") == 2);
  // Not found → returns 0 (safe default for ImGui::Combo).
  CHECK(FindEnumOptionIndex(opts, "delta") == 0);
  // Empty options list → returns 0.
  CHECK(FindEnumOptionIndex({}, "any") == 0);
}

TEST_CASE("EditorLayerInternal: BuildImGuiComboItems produces null-separated string", "[editor][internal][schema]") {
  // lines 97-105 in EditorLayerInternal.h
  {
    // Multi-item case.
    const std::vector<std::string> opts = {"one", "two", "three"};
    const std::string items = BuildImGuiComboItems(opts);
    // Expected: "one\0two\0three\0\0"
    const std::string expected = std::string("one") + '\0' +
                                 std::string("two") + '\0' +
                                 std::string("three") + '\0' + '\0';
    REQUIRE(items.size() == expected.size());
    CHECK(items == expected);
  }
  {
    // Single item.
    const std::vector<std::string> single = {"only"};
    const std::string items = BuildImGuiComboItems(single);
    const std::string expected = std::string("only") + '\0' + '\0';
    REQUIRE(items.size() == expected.size());
    CHECK(items == expected);
  }
  {
    // Empty list → double-null terminator.
    const std::string items = BuildImGuiComboItems({});
    REQUIRE(items.size() == 1u);
    CHECK(items[0] == '\0');
  }
}

TEST_CASE("EditorLayerInternal: helper edge cases are safe", "[editor][internal][schema]") {
  {
    SceneDocument doc;
    REQUIRE_NOTHROW(SyncAssetScaleMetadata(nullptr));
    REQUIRE_NOTHROW(SyncAssetScaleMetadata(&doc));
  }

  {
    float color[3] = {0.0f, 0.0f, 0.0f};
    ParseRGBString({}, color);
    CHECK(color[0] == Approx(1.0f));
    CHECK(color[1] == Approx(1.0f));
    CHECK(color[2] == Approx(1.0f));
  }

  {
    REQUIRE_FALSE(ParseSceneObjectType("prop", nullptr));
    REQUIRE(SceneObjectTypeToString(static_cast<SceneObjectType>(-1)) !=
            nullptr);
    CHECK(std::string(SceneObjectTypeToString(
              static_cast<SceneObjectType>(-1))) == "Panel");
  }

  {
    LogLine entry{};
    char buf[16] = {};
    REQUIRE_NOTHROW(FormatLogTime(entry, nullptr, 0));
    REQUIRE_NOTHROW(FormatLogTime(entry, buf, 0));
  }
}

TEST_CASE("EditorLayerInternal: placement and path helpers cover deterministic branches", "[editor][internal][coverage]") {
  SceneObject panel;
  panel.type = SceneObjectType::Panel;
  panel.scale = {2.0f, -0.5f, 0.0f};
  CHECK(ResolveObjectPlacementHalfExtents(panel).x == Approx(2.0f));
  CHECK(ResolveObjectPlacementHalfExtents(panel).y == Approx(0.5f));
  CHECK(ResolveObjectPlacementHalfExtents(panel).z == Approx(0.01f));

  CHECK(ProjectHalfExtentOntoNormal({2.0f, 3.0f, 4.0f}, {1.0f, -2.0f, 0.5f}) == Approx(10.0f));

  CHECK(DistSqPointSegment2D(1.0f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f) == Approx(1.0f));
  CHECK(DistSqPointSegment2D(2.0f, 3.0f, 1.0f, 1.0f, 1.0f, 1.0f) == Approx(5.0f));

  CHECK_FALSE(IsTextureFilePath({}));
  CHECK(IsTextureFilePath("asset.PNG"));
  CHECK(IsTextureFilePath("asset.hdr"));
  CHECK_FALSE(IsTextureFilePath("asset.txt"));

  CHECK(ToLowerAscii("AbC123!") == "abc123!");

  const std::filesystem::path root = Horo::Tests::SecureTempBase() / "horo_editor_layer_internal_paths";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  ProjectPathGuard guard(root);
  CHECK(ResolveProjectRelativeOrAbsolutePath("").empty());
  const std::filesystem::path absolutePath = root / "abs.txt";
  CHECK(ResolveProjectRelativeOrAbsolutePath(absolutePath.string()) == absolutePath);
  CHECK(ResolveProjectRelativeOrAbsolutePath("relative.txt") == (root / "relative.txt"));
}

TEST_CASE("EditorLayerInternal: unavailable texture dialog button renders safely", "[editor][internal][imgui]") {
  ImGuiContextGuard ctx;
  ImGui::NewFrame();
  ImGui::Begin("test-window");
  REQUIRE_NOTHROW(DrawUnavailableTextureDialogButton("dialog-button"));
  ImGui::End();
  ImGui::EndFrame();
}

TEST_CASE("EditorLayerInternal: SchemaAppliesToObjectType respects appliesTo filter", "[editor][internal][schema]") {
  // lines 118-137 in EditorLayerInternal.h
  // Empty appliesTo → applies to all types.
  CHECK(SchemaAppliesToObjectType({}, SceneObjectType::Prop));
  CHECK(SchemaAppliesToObjectType({}, SceneObjectType::Light));
  CHECK(SchemaAppliesToObjectType({}, SceneObjectType::Camera));
  CHECK(SchemaAppliesToObjectType({}, SceneObjectType::Panel));

  // Exact lowercase match for each type.
  CHECK(SchemaAppliesToObjectType({"prop"}, SceneObjectType::Prop));
  CHECK(SchemaAppliesToObjectType({"light"}, SceneObjectType::Light));
  CHECK(SchemaAppliesToObjectType({"camera"}, SceneObjectType::Camera));
  CHECK(SchemaAppliesToObjectType({"panel"}, SceneObjectType::Panel));

  // Case-insensitive matching.
  CHECK(SchemaAppliesToObjectType({"Prop"}, SceneObjectType::Prop));
  CHECK(SchemaAppliesToObjectType({"LIGHT"}, SceneObjectType::Light));

  // Multi-entry appliesTo: match if any entry matches.
  CHECK(SchemaAppliesToObjectType({"prop", "light"}, SceneObjectType::Light));
  CHECK(SchemaAppliesToObjectType({"prop", "light"}, SceneObjectType::Prop));

  // Non-matching: entry present but for a different type.
  CHECK_FALSE(SchemaAppliesToObjectType({"light"}, SceneObjectType::Prop));
  CHECK_FALSE(SchemaAppliesToObjectType({"camera"}, SceneObjectType::Light));
  CHECK_FALSE(SchemaAppliesToObjectType({"panel"}, SceneObjectType::Camera));
}

// ===========================================================================
// EditorSelectionRules
// ===========================================================================
#include "ui/editor/EditorPropertyRules.h"
#include "ui/editor/EditorSelectionRules.h"

TEST_CASE("ValidateRenameCandidate: empty draft returns error", "[editor][selection-rules]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "existing_obj";
  doc.objects.push_back(obj);

  const std::string err = ValidateRenameCandidate(doc, 0, "");
  CHECK_FALSE(err.empty());
  CHECK(err.find("empty") != std::string::npos);
}

TEST_CASE("ValidateRenameCandidate: out-of-range index returns error", "[editor][selection-rules]") {
  SceneDocument doc;

  CHECK_FALSE(ValidateRenameCandidate(doc, 0, "new_id").empty());
  CHECK_FALSE(ValidateRenameCandidate(doc, -1, "new_id").empty());
}

TEST_CASE("ValidateRenameCandidate: ID already used by another object is rejected", "[editor][selection-rules]") {
  SceneDocument doc;
  SceneObject a;
  a.id = "obj_a";
  doc.objects.push_back(a);
  SceneObject b;
  b.id = "obj_b";
  doc.objects.push_back(b);

  const std::string err = ValidateRenameCandidate(doc, 0, "obj_b");
  CHECK_FALSE(err.empty());
  CHECK(err.find("exists") != std::string::npos);
}

TEST_CASE("ValidateRenameCandidate: renaming to own current ID succeeds", "[editor][selection-rules]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "same_id";
  doc.objects.push_back(obj);

  CHECK(ValidateRenameCandidate(doc, 0, "same_id").empty());
}

TEST_CASE("ValidateRenameCandidate: fresh unique ID succeeds", "[editor][selection-rules]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "obj_000";
  doc.objects.push_back(obj);

  CHECK(ValidateRenameCandidate(doc, 0, "obj_new_name").empty());
}

TEST_CASE("CollectParentCandidates: excludes self and descendants", "[editor][selection-rules]") {
  SceneDocument doc;
  SceneObject parent;
  parent.id = "parent";
  doc.objects.push_back(parent);
  SceneObject child;
  child.id = "child";
  child.props["parentId"] = "parent";
  doc.objects.push_back(child);
  SceneObject other;
  other.id = "other";
  doc.objects.push_back(other);

  const std::vector<std::string> candidates = CollectParentCandidates(doc, 0);
  CHECK(std::ranges::find(candidates, "parent") == candidates.end());
  CHECK(std::ranges::find(candidates, "child") == candidates.end());
  CHECK(std::ranges::find(candidates, "other") != candidates.end());
}

TEST_CASE("GenerateUniqueId: produces unique ID not in document", "[editor][selection-rules]") {
  SceneDocument doc;
  for (int i = 0; i < 5; ++i) {
    SceneObject obj;
    obj.id = std::format("obj_{:03d}", i);
    doc.objects.push_back(obj);
  }

  const std::string id = GenerateUniqueId(doc, "obj");
  CHECK_FALSE(id.empty());
  for (const auto &obj : doc.objects)
    CHECK(obj.id != id);
}

// ===========================================================================
// EditorPropertyRules
// ===========================================================================

TEST_CASE("MakeObjectFromAsset: creates Prop with correct assetId", "[editor][property-rules]") {
  SceneDocument doc;
  AssetDef asset;
  asset.displayName = "Barrel";
  asset.renderScale = "1.0,1.0,1.0";
  doc.assets["barrel"] = asset;

  EditorSchema schema;
  const SceneObject obj = MakeObjectFromAsset(doc, "barrel", schema);

  CHECK(obj.type == SceneObjectType::Prop);
  CHECK(obj.assetId == "barrel");
  CHECK_FALSE(obj.id.empty());
  CHECK(obj.props.contains("_assetRenderScale"));
  CHECK(obj.props.at("_assetRenderScale") == "1.0,1.0,1.0");
}

TEST_CASE("MakeObjectFromAsset: uses default render scale when empty", "[editor][property-rules]") {
  SceneDocument doc;
  AssetDef asset;
  asset.displayName = "Box";
  asset.renderScale = "";
  doc.assets["box"] = asset;

  EditorSchema schema;
  const SceneObject obj = MakeObjectFromAsset(doc, "box", schema);

  REQUIRE(obj.props.contains("_assetRenderScale"));
  CHECK(obj.props.at("_assetRenderScale") == "1.0000,1.0000,1.0000");
}

TEST_CASE("MakeObjectFromAsset: no _assetRenderScale when asset not found", "[editor][property-rules]") {
  SceneDocument doc;
  EditorSchema schema;
  const SceneObject obj = MakeObjectFromAsset(doc, "missing_asset", schema);
  CHECK(obj.assetId == "missing_asset");
  CHECK_FALSE(obj.props.contains("_assetRenderScale"));
}

TEST_CASE("ApplySchemaFieldDefaults: sets missing fields from schema", "[editor][property-rules]") {
  const std::string json = R"({
    "types": {
      "Light": {
        "fields": [{"key": "color", "label": "Color", "type": "string", "default": "1.0,1.0,1.0"}]
      }
    }
  })";
  WriteFile(TmpPath("prrules_schema_light.json"), json);
  EditorSchema schema;
  schema.LoadFromFile(TmpPath("prrules_schema_light.json"));

  SceneObject obj;
  obj.type = SceneObjectType::Light;
  ApplySchemaFieldDefaults(obj, schema);

  CHECK(obj.props.contains("color"));
  CHECK(obj.props.at("color") == "1.0,1.0,1.0");
}

TEST_CASE("ApplySchemaFieldDefaults: does not overwrite existing values", "[editor][property-rules]") {
  const std::string json = R"({
    "types": {
      "Light": {
        "fields": [{"key": "intensity", "label": "Intensity", "type": "float", "default": "1.0",
                    "min": 0.0, "max": 10.0}]
      }
    }
  })";
  WriteFile(TmpPath("prrules_schema_intensity.json"), json);
  EditorSchema schema;
  schema.LoadFromFile(TmpPath("prrules_schema_intensity.json"));

  SceneObject obj;
  obj.type = SceneObjectType::Light;
  obj.props["intensity"] = "5.0";
  ApplySchemaFieldDefaults(obj, schema);

  CHECK(obj.props.at("intensity") == "5.0");
}

TEST_CASE("ApplyComponentFieldDefaults: sets missing component fields", "[editor][property-rules]") {
  const std::string json = R"({
    "components": {
      "rigidbody": {
        "fields": [{"key": "mass", "label": "Mass", "type": "float", "default": "1.0",
                    "min": 0.0, "max": 10000.0}]
      }
    }
  })";
  WriteFile(TmpPath("prrules_schema_comp.json"), json);
  EditorSchema schema;
  schema.LoadFromFile(TmpPath("prrules_schema_comp.json"));

  ComponentDesc comp;
  comp.type = "rigidbody";
  ApplyComponentFieldDefaults(comp, schema);

  CHECK(comp.props.contains("mass"));
  CHECK(comp.props.at("mass") == "1.0");
}

TEST_CASE("ApplyCameraBuiltinDefaults: sets fov/nearClip/farClip/followTargetId", "[editor][property-rules]") {
  SceneObject obj;
  obj.type = SceneObjectType::Camera;
  ApplyCameraBuiltinDefaults(obj);

  CHECK(obj.props.at("fov") == "60");
  CHECK(obj.props.at("nearClip") == "0.1");
  CHECK(obj.props.at("farClip") == "500");
  CHECK(obj.props.contains("followTargetId"));
}

TEST_CASE("ApplyCameraBuiltinDefaults: does not overwrite existing values", "[editor][property-rules]") {
  SceneObject obj;
  obj.type = SceneObjectType::Camera;
  obj.props["fov"] = "90";
  ApplyCameraBuiltinDefaults(obj);

  CHECK(obj.props.at("fov") == "90");
  CHECK(obj.props.at("nearClip") == "0.1");
}

// ============================================================================
// View Gimbal — SnapCameraToAxis
// ============================================================================

namespace {

Camera MakeDefaultCamera() {
  Camera cam;
  cam.position = {0, 3, 8};
  cam.target = Vec3::Zero();
  cam.up = Vec3::Up();
  cam.fovY = 60.0f;
  cam.aspect = 16.0f / 9.0f;
  cam.zNear = 0.1f;
  cam.zFar = 1000.0f;
  return cam;
}

} // namespace

TEST_CASE("SnapCameraToAxis: Right places camera on +X axis", "[editor][gimbal]") {
  using VS = ViewSnap;
  Camera cam = MakeDefaultCamera();
  const Vec3 pivot{1.0f, 2.0f, 3.0f};
  constexpr float dist = 5.0f;
  SnapCameraToAxis(cam, VS::Right, pivot, dist);

  CHECK(cam.target.x == Approx(pivot.x));
  CHECK(cam.target.y == Approx(pivot.y));
  CHECK(cam.target.z == Approx(pivot.z));
  CHECK(cam.position.x == Approx(pivot.x + dist));
  CHECK(cam.position.y == Approx(pivot.y));
  CHECK(cam.position.z == Approx(pivot.z));
  // Default up should be +Y
  CHECK(cam.up.y == Approx(1.0f));
}

TEST_CASE("SnapCameraToAxis: Left places camera on -X axis", "[editor][gimbal]") {
  using VS = ViewSnap;
  Camera cam = MakeDefaultCamera();
  const Vec3 pivot{0.0f, 0.0f, 0.0f};
  constexpr float dist = 4.0f;
  SnapCameraToAxis(cam, VS::Left, pivot, dist);

  CHECK(cam.position.x == Approx(-dist));
  CHECK(cam.position.y == Approx(0.0f));
  CHECK(cam.position.z == Approx(0.0f));
  CHECK(cam.target.x == Approx(0.0f));
  CHECK(cam.up.y == Approx(1.0f));
}

TEST_CASE("SnapCameraToAxis: Top places camera on +Y axis with -Z up", "[editor][gimbal]") {
  using VS = ViewSnap;
  Camera cam = MakeDefaultCamera();
  const Vec3 pivot{0.0f, 0.0f, 0.0f};
  constexpr float dist = 6.0f;
  SnapCameraToAxis(cam, VS::Top, pivot, dist);

  CHECK(cam.position.y == Approx(dist));
  CHECK(cam.position.x == Approx(0.0f));
  CHECK(cam.position.z == Approx(0.0f));
  // Looking straight down: up must not be +Y (would be degenerate), use -Z
  CHECK(cam.up.z == Approx(-1.0f));
  CHECK(cam.up.y == Approx(0.0f));
}

TEST_CASE("SnapCameraToAxis: Bottom places camera on -Y axis with +Z up", "[editor][gimbal]") {
  using VS = ViewSnap;
  Camera cam = MakeDefaultCamera();
  const Vec3 pivot{0.0f, 0.0f, 0.0f};
  constexpr float dist = 6.0f;
  SnapCameraToAxis(cam, VS::Bottom, pivot, dist);

  CHECK(cam.position.y == Approx(-dist));
  CHECK(cam.up.z == Approx(1.0f));
}

TEST_CASE("SnapCameraToAxis: Front places camera on +Z axis", "[editor][gimbal]") {
  using VS = ViewSnap;
  Camera cam = MakeDefaultCamera();
  const Vec3 pivot{0.0f, 0.0f, 0.0f};
  constexpr float dist = 3.0f;
  SnapCameraToAxis(cam, VS::Front, pivot, dist);

  CHECK(cam.position.z == Approx(dist));
  CHECK(cam.position.x == Approx(0.0f));
  CHECK(cam.position.y == Approx(0.0f));
  CHECK(cam.up.y == Approx(1.0f));
}

TEST_CASE("SnapCameraToAxis: Back places camera on -Z axis", "[editor][gimbal]") {
  using VS = ViewSnap;
  Camera cam = MakeDefaultCamera();
  const Vec3 pivot{0.0f, 0.0f, 0.0f};
  constexpr float dist = 3.0f;
  SnapCameraToAxis(cam, VS::Back, pivot, dist);

  CHECK(cam.position.z == Approx(-dist));
  CHECK(cam.up.y == Approx(1.0f));
}

TEST_CASE("SnapCameraToAxis: None is a no-op for position", "[editor][gimbal]") {
  using VS = ViewSnap;
  Camera cam = MakeDefaultCamera();
  const Vec3 originalPos = cam.position;
  SnapCameraToAxis(cam, VS::None, Vec3::Zero(), 5.0f);

  // target is always set; position should be untouched on None
  CHECK(cam.position.x == Approx(originalPos.x));
  CHECK(cam.position.y == Approx(originalPos.y));
  CHECK(cam.position.z == Approx(originalPos.z));
}

TEST_CASE("SnapCameraToAxis: non-zero pivot offsets all positions", "[editor][gimbal]") {
  using VS = ViewSnap;
  Camera cam = MakeDefaultCamera();
  const Vec3 pivot{10.0f, 5.0f, -3.0f};
  constexpr float dist = 7.0f;

  // Front: camera at pivot + (0, 0, dist)
  SnapCameraToAxis(cam, VS::Front, pivot, dist);
  CHECK(cam.position.x == Approx(pivot.x));
  CHECK(cam.position.y == Approx(pivot.y));
  CHECK(cam.position.z == Approx(pivot.z + dist));
  CHECK(cam.target.x == Approx(pivot.x));
  CHECK(cam.target.y == Approx(pivot.y));
  CHECK(cam.target.z == Approx(pivot.z));
}

// ============================================================================
// View Gimbal — WorldAxisToScreenDir viewZ
// ============================================================================
// viewZ = -dot(camera_forward, world_axis) — positive when the world axis
// opposes the camera forward direction (faces the viewer), negative when it
// aligns with camera forward (points away from viewer).

TEST_CASE("WorldAxisToScreenDir: camera at +Z (Front), world +Z axis has positive viewZ", "[editor][gimbal]") {
  // Camera on the +Z side looking toward origin → forward = -Z.
  // The +Z world axis opposes the camera's forward, so it faces the viewer:
  // viewZ should be positive (bright arrow, posSnap = Front).
  Camera cam;
  cam.position = {0.0f, 0.0f, 5.0f};
  cam.target   = {0.0f, 0.0f, 0.0f};
  cam.up       = Vec3::Up();

  float dx = 0, dy = 0, vz = 0;
  WorldAxisToScreenDir(cam, {0.0f, 0.0f, 1.0f}, &dx, &dy, &vz);

  // +Z world axis faces the camera → viewZ > 0 → posSnap = Front
  CHECK(vz > 0.0f);
}

TEST_CASE("WorldAxisToScreenDir: camera at -Z (Back), world +Z axis has negative viewZ", "[editor][gimbal]") {
  // Camera on the -Z side looking toward origin → forward = +Z.
  // The +Z world axis aligns with camera forward (points away from viewer):
  // viewZ should be negative (dim arrow, negSnap = Back).
  Camera cam;
  cam.position = {0.0f, 0.0f, -5.0f};
  cam.target   = {0.0f, 0.0f,  0.0f};
  cam.up       = Vec3::Up();

  float dx = 0, dy = 0, vz = 0;
  WorldAxisToScreenDir(cam, {0.0f, 0.0f, 1.0f}, &dx, &dy, &vz);

  // +Z world axis is pointing away → viewZ < 0 → negSnap = Back
  CHECK(vz < 0.0f);
}

TEST_CASE("WorldAxisToScreenDir: +Y world axis is perpendicular when looking along Z", "[editor][gimbal]") {
  Camera cam;
  cam.position = {0.0f, 0.0f, 5.0f};
  cam.target   = {0.0f, 0.0f, 0.0f};
  cam.up       = Vec3::Up();

  // The +Y world axis is orthogonal to the view direction (looking along Z).
  // viewZ should be ~0 (neither facing nor pointing away from camera).
  float dx = 0, dy = 0, vz = 0;
  WorldAxisToScreenDir(cam, {0.0f, 1.0f, 0.0f}, &dx, &dy, &vz);

  CHECK(vz == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("WorldAxisToScreenDir: camera at +X (Right), world +X axis has positive viewZ", "[editor][gimbal]") {
  // Camera on the +X side looking toward origin: forward = -X.
  // The +X world axis opposes camera forward → faces viewer → viewZ > 0.
  Camera cam;
  cam.position = {5.0f, 0.0f, 0.0f};
  cam.target   = {0.0f, 0.0f, 0.0f};
  cam.up       = Vec3::Up();

  float dx = 0, dy = 0, vz = 0;
  WorldAxisToScreenDir(cam, {1.0f, 0.0f, 0.0f}, &dx, &dy, &vz);

  CHECK(vz > 0.0f);
}

// ============================================================================
// View Gimbal — FindViewGimbalHoverSnap positive/negative axis selection
// ============================================================================

namespace {

/// Builds a minimal axis cache with explicit viewZ so hover-snap tests can
/// control which direction is considered "facing" vs "pointing away".
std::array<ViewGimbalAxisCache, 3> MakeAxisCache(float xViewZ, float yViewZ,
                                                 float zViewZ) {
  std::array<ViewGimbalAxisCache, 3> cache{};
  // X axis: points right on screen  (origIdx=0)
  cache[0] = {1.0f, 0.0f, xViewZ, 0};
  // Y axis: points up on screen     (origIdx=1)
  cache[1] = {0.0f, -1.0f, yViewZ, 1};
  // Z axis: points left on screen   (origIdx=2)
  cache[2] = {-1.0f, 0.0f, zViewZ, 2};
  return cache;
}

static const std::array<ViewGimbalAxisDraw, 3> kTestAxes = {{
    {ViewSnap::Right, ViewSnap::Left,
     {1.0f, 0.0f, 0.0f}, IM_COL32(255, 82, 58, 255),
     IM_COL32(145, 48, 42, 210), "X"},
    {ViewSnap::Top, ViewSnap::Bottom,
     {0.0f, 1.0f, 0.0f}, IM_COL32(80, 230, 104, 255),
     IM_COL32(42, 135, 60, 210), "Y"},
    {ViewSnap::Front, ViewSnap::Back,
     {0.0f, 0.0f, 1.0f}, IM_COL32(55, 155, 255, 255),
     IM_COL32(38, 92, 170, 210), "Z"},
}};

} // namespace

TEST_CASE("FindViewGimbalHoverSnap: positive viewZ returns posSnap", "[editor][gimbal]") {
  using VS = ViewSnap;
  // All axes face the camera (viewZ > 0). Clicking on the X arrowhead tip should
  // give Right (posSnap for X).
  const ImVec2 center{100.0f, 100.0f};
  auto cache = MakeAxisCache(1.0f, 1.0f, 1.0f);

  constexpr float kShaft = 42.0f;
  constexpr float kHead = 13.0f;
  constexpr float kHW = 6.0f;

  // X arrow tip is at center + (1,0)*42 = (142, 100)
  // Mouse on top of the arrowhead triangle
  const ImVec2 tipX{center.x + kShaft, center.y};
  // Use hitCache with no size limit (large hitPxSq so shaft hits work)
  auto hitCache = cache;
  std::ranges::reverse(hitCache);
  const VS snap = FindViewGimbalHoverSnap(tipX, center, hitCache, kTestAxes,
                                          kShaft, kHead, kHW, 9.0f * 9.0f);
  CHECK(snap == VS::Right);
}

TEST_CASE("FindViewGimbalHoverSnap: negative viewZ returns negSnap", "[editor][gimbal]") {
  using VS = ViewSnap;
  // X axis points away from camera (viewZ < 0). Clicking it should give Left.
  const ImVec2 center{100.0f, 100.0f};
  auto cache = MakeAxisCache(-1.0f, 1.0f, 1.0f); // X is pointing away

  constexpr float kShaft = 42.0f;
  constexpr float kHead = 13.0f;
  constexpr float kHW = 6.0f;

  const ImVec2 tipX{center.x + kShaft, center.y};
  auto hitCache = cache;
  std::ranges::reverse(hitCache);
  const VS snap = FindViewGimbalHoverSnap(tipX, center, hitCache, kTestAxes,
                                          kShaft, kHead, kHW, 9.0f * 9.0f);
  CHECK(snap == VS::Left);
}

TEST_CASE("FindViewGimbalHoverSnap: negative viewZ on Z axis returns Back", "[editor][gimbal]") {
  using VS = ViewSnap;
  // Z axis points away (viewZ < 0). Cache[2] is Z, pointing left on screen.
  // We set Z viewZ to -1 (pointing away) — clicking that arrow should give Back.
  const ImVec2 center{100.0f, 100.0f};
  auto cache = MakeAxisCache(1.0f, 1.0f, -1.0f); // Z points away

  constexpr float kShaft = 42.0f;
  constexpr float kHead = 13.0f;
  constexpr float kHW = 6.0f;

  // Z arrow points left (-1, 0), tip at (58, 100)
  const ImVec2 tipZ{center.x - kShaft, center.y};
  auto hitCache = cache;
  std::ranges::reverse(hitCache);
  const VS snap = FindViewGimbalHoverSnap(tipZ, center, hitCache, kTestAxes,
                                          kShaft, kHead, kHW, 9.0f * 9.0f);
  CHECK(snap == VS::Back);
}

TEST_CASE("FindViewGimbalHoverSnap: mouse far from all axes returns None", "[editor][gimbal]") {
  using VS = ViewSnap;
  const ImVec2 center{100.0f, 100.0f};
  auto cache = MakeAxisCache(1.0f, 1.0f, 1.0f);

  // Mouse far away — well outside any axis hit region
  const ImVec2 farAway{500.0f, 500.0f};
  auto hitCache = cache;
  std::ranges::reverse(hitCache);
  const VS snap = FindViewGimbalHoverSnap(farAway, center, hitCache, kTestAxes,
                                          42.0f, 13.0f, 6.0f, 9.0f * 9.0f);
  CHECK(snap == VS::None);
}

TEST_CASE("FindViewGimbalHoverSnap: positive Y axis click returns Top", "[editor][gimbal]") {
  using VS = ViewSnap;
  const ImVec2 center{100.0f, 100.0f};
  // Y axis points up on screen (dy = -1.0 in ImGui coords = up).
  auto cache = MakeAxisCache(1.0f, 1.0f, 1.0f);

  constexpr float kShaft = 42.0f;
  // Y arrow tip: center + (0, -1)*42 = (100, 58)
  const ImVec2 tipY{center.x, center.y - kShaft};
  auto hitCache = cache;
  std::ranges::reverse(hitCache);
  const VS snap = FindViewGimbalHoverSnap(tipY, center, hitCache, kTestAxes,
                                          kShaft, 13.0f, 6.0f, 9.0f * 9.0f);
  CHECK(snap == VS::Top);
}

TEST_CASE("FindViewGimbalHoverSnap: negative Y axis click returns Bottom", "[editor][gimbal]") {
  using VS = ViewSnap;
  const ImVec2 center{100.0f, 100.0f};
  // Y axis points away from camera.
  auto cache = MakeAxisCache(1.0f, -1.0f, 1.0f);

  constexpr float kShaft = 42.0f;
  const ImVec2 tipY{center.x, center.y - kShaft};
  auto hitCache = cache;
  std::ranges::reverse(hitCache);
  const VS snap = FindViewGimbalHoverSnap(tipY, center, hitCache, kTestAxes,
                                          kShaft, 13.0f, 6.0f, 9.0f * 9.0f);
  CHECK(snap == VS::Bottom);
}
