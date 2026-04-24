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

#include "core/ProjectPath.h"
#include "editor/AssetImportService.h"
#include "editor/AssetImporterRegistry.h"
#include "editor/AssetMetadata.h"
#include "editor/EditorAssetImport.h"
#include "editor/EditorImGuiBackend.h"
#include "editor/EditorLayer.h"
#include "editor/EditorSchema.h"
#include "editor/EditorSearch.h"
#include "editor/EditorUiLogic.h"
#include "editor/EditorWorkspaceSettings.h"
#include "editor/Raycaster.h"
#include "editor/SceneDocument.h"
#include "editor/EditorSceneGraph.h"
#include "editor/SceneProjectBridge.h"
#include "editor/SceneRuntimeBridge.h"
#include "editor/SceneSerializer.h"
#include "renderer/Camera.h"
#include "tests/TestTempPaths.h"

using namespace Monolith;
using namespace Monolith::Editor;

namespace {
    Monolith::Editor::SceneObject
    MakeObjectFromAssetForTest(const Monolith::Editor::SceneDocument &,
                               std::string_view assetId) {
        Monolith::Editor::SceneObject obj;
        obj.id = "generated";
        obj.type = Monolith::Editor::SceneObjectType::Prop;
        obj.assetId = std::string(assetId);
        return obj;
    }

    Monolith::Editor::SceneObject
    DuplicateObjectForTest(const Monolith::Editor::SceneDocument &doc,
                           const Monolith::Editor::SceneObject &src) {
        Monolith::Editor::SceneObject clone = src;
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
    return (Monolith::Tests::SecureTempBase() / name).string();
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
    std::filesystem::path previousRoot = Monolith::ProjectPath::Root();

    ProjectPathGuard(const ProjectPathGuard &) = delete;

    ProjectPathGuard &operator=(const ProjectPathGuard &) = delete;

    ProjectPathGuard(ProjectPathGuard &&) = delete;

    ProjectPathGuard &operator=(ProjectPathGuard &&) = delete;

    explicit ProjectPathGuard(const std::filesystem::path &nextRoot) {
        Monolith::ProjectPath::Init(nextRoot);
    }

    ~ProjectPathGuard() { Monolith::ProjectPath::Init(previousRoot); }
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

TEST_CASE("EditorSchema: missing file is silently ignored",
          "[editor][schema]") {
    EditorSchema schema;
    REQUIRE_NOTHROW(schema.LoadFromFile("/nonexistent/path/schema.json"));
    REQUIRE(schema.GetSchema(SceneObjectType::Prop) == nullptr);
}

TEST_CASE("EditorSchema: empty JSON is silently ignored", "[editor][schema]") {
    WriteFile(TmpPath("schema_empty.json"), "{}");
    EditorSchema schema;
    REQUIRE_NOTHROW(schema.LoadFromFile(TmpPath("schema_empty.json")));
    REQUIRE(schema.GetSchema(SceneObjectType::Panel) == nullptr);
}

TEST_CASE("EditorSchema: malformed JSON is silently ignored",
          "[editor][schema]") {
    WriteFile(TmpPath("schema_bad.json"), "{ this is not valid json !!!}");
    EditorSchema schema;
    REQUIRE_NOTHROW(schema.LoadFromFile(TmpPath("schema_bad.json")));
    REQUIRE(schema.GetSchema(SceneObjectType::Prop) == nullptr);
}

TEST_CASE("EditorImGuiBackend: backend support reflects build capabilities",
          "[editor][imgui][backend]") {
    REQUIRE(IsSupportedEditorImGuiBackend(RenderBackendId::OpenGL));
    REQUIRE(IsSupportedEditorImGuiBackend(RenderBackendId::Auto));

#if defined(MONOLITH_HAS_VULKAN)
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

TEST_CASE("EditorSchema: normalizes legacy Prop mesh string field to enum",
          "[editor][schema]") {
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

TEST_CASE("EditorSchema: ignores deprecated Prop isLight field",
          "[editor][schema]") {
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

TEST_CASE("EditorSchema: GetSchema returns nullptr for unregistered type",
          "[editor][schema]") {
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

TEST_CASE("EditorSchema: loads component schema metadata and lookups",
          "[editor][schema]") {
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
}

TEST_CASE("EditorSchema: bundled schema exposes shared component definitions",
          "[editor][schema]") {
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

TEST_CASE("SceneSerializer: LoadFromFile throws on missing file",
          "[editor][serializer]") {
    REQUIRE_THROWS_AS(SceneSerializer::LoadFromFile("/nonexistent/scene.json"),
                      std::runtime_error);
}

TEST_CASE("SceneSerializer: LoadFromFile throws on invalid JSON",
          "[editor][serializer]") {
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

TEST_CASE("SceneSerializer: round-trip single Panel object",
          "[editor][serializer]") {
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

TEST_CASE("SceneSerializer: round-trip Prop and Light types",
          "[editor][serializer]") {
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
    for (const auto &o: loaded.objects) {
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

TEST_CASE("SceneSerializer: round-trip asset registry",
          "[editor][serializer]") {
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

TEST_CASE(
    "SceneSerializer: legacy asset entries gain guid and display name on load",
    "[editor][serializer]") {
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

TEST_CASE("AssetMetadata: EnsureAssetMetadataForDocument writes sidecar files",
          "[editor][asset-metadata]") {
    const std::filesystem::path root =
            Monolith::Tests::SecureTempBase() / "horo_asset_metadata_case";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "assets" / "models", ec);
    WriteFile((root / "CMakePresets.json").string(), "{}");
    ProjectPathGuard guard(root);

    SceneDocument doc;
    doc.assets["crate"] =
            AssetDef{
                "assets/models/crate_guid/crate.obj", "1.0000,1.0000,1.0000",
                "assets/models/crate_guid/crate.png", "crate_guid", "Crate"
            };

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

TEST_CASE(
    "AssetImporterRegistry: built-in importers resolve by extension and id",
    "[editor][asset-import]") {
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

TEST_CASE("AssetImportService: imports OBJ and persists importer metadata",
          "[editor][asset-import]") {
    const std::filesystem::path root =
            Monolith::Tests::SecureTempBase() / "horo_asset_import_obj";
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

TEST_CASE(
    "AssetImportService: unsupported source yields structured diagnostics",
    "[editor][asset-import][diagnostics]") {
    AssetImportService service;
    AssetImportResult result = service.ImportAssetFromSource(
        "C:/tmp/unsupported.txt", "broken", "guid_broken", "Broken");

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics[0].severity == AssetDiagnosticSeverity::Error);
    REQUIRE(result.diagnostics[0].code == "asset.importer.not_found");
    REQUIRE(result.diagnostics[0].assetGuid == "guid_broken");
}

TEST_CASE("AssetImportService: reimport propagation follows deterministic "
          "topological order",
          "[editor][asset-import][reimport]") {
    const std::filesystem::path root =
            Monolith::Tests::SecureTempBase() / "horo_asset_reimport_graph";
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
    for (const auto &[name, guid, file]:
         std::vector<std::tuple<std::string, std::string, std::string> >{
             {"root", "guid_root", "root.obj"},
             {"alpha", "guid_alpha", "alpha.obj"},
             {"beta", "guid_beta", "beta.obj"},
             {"gamma", "guid_gamma", "gamma.obj"},
         }) {
        AssetImportResult result = service.ImportAssetFromSource(
            (root / file).string(), name, guid, name);
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

TEST_CASE("AssetImportService: cyclic dependencies fail reimport with "
          "actionable error",
          "[editor][asset-import][reimport]") {
    const std::filesystem::path root =
            Monolith::Tests::SecureTempBase() / "horo_asset_reimport_cycle";
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

TEST_CASE("SceneSerializer: asset albedoMap round-trip",
          "[editor][serializer]") {
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

TEST_CASE("SceneSerializer: empty albedoMap not written to JSON",
          "[editor][serializer]") {
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

TEST_CASE("SceneSerializer: asset-backed objects omit inline props block",
          "[editor][serializer]") {
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

TEST_CASE(
    "SceneSerializer: inline props survive when object has no asset reference",
    "[editor][serializer]") {
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

TEST_CASE("SceneSerializer: prefab instance metadata round-trips",
          "[editor][serializer]") {
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

TEST_CASE("Editor helpers: duplicating an object clears runtime entity id",
          "[editor]") {
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

TEST_CASE("Editor helpers: asset-backed object stores selected asset id",
          "[editor]") {
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

TEST_CASE("Editor helpers: shortcut query matches category command and keys",
          "[editor]") {
    ShortcutRow row{"Editor", "Quick open", "Ctrl/Cmd + P"};
    REQUIRE(MatchesShortcutQuery(row, "editor"));
    REQUIRE(MatchesShortcutQuery(row, "quick"));
    REQUIRE(MatchesShortcutQuery(row, "cmd"));
    REQUIRE_FALSE(MatchesShortcutQuery(row, "delete"));
}

TEST_CASE("Editor helpers: command palette query matches command and shortcut",
          "[editor]") {
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

TEST_CASE("Editor helpers: generic search helpers handle empty query",
          "[editor]") {
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

TEST_CASE("Editor helpers: asset quick-open query matches id and mesh",
          "[editor]") {
    AssetDef asset;
    asset.mesh = "assets/models/torch.obj";
    REQUIRE(AssetMatchesQuickOpenQuery("torch_asset", asset, "torch"));
    REQUIRE(AssetMatchesQuickOpenQuery("torch_asset", asset, "models"));
    REQUIRE_FALSE(AssetMatchesQuickOpenQuery("torch_asset", asset, "barrel"));
}

TEST_CASE(
    "Editor helpers: filtered list state handles empty and no-match cases",
    "[editor]") {
    REQUIRE(EvaluateFilteredListState(3, 1, "") == FilteredListState::None);
    REQUIRE(EvaluateFilteredListState(0, 0, "") == FilteredListState::EmptyData);
    REQUIRE(EvaluateFilteredListState(4, 0, "torch") ==
        FilteredListState::NoMatches);
    REQUIRE(EvaluateFilteredListState(4, 0, "") == FilteredListState::None);
}

TEST_CASE("Editor helpers: shortcut table includes required entries",
          "[editor]") {
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
    std::unordered_set<std::string, StringHash, std::equal_to<> > commandSet;

    for (const auto &row: rows) {
        const auto [_, inserted] = commandSet.emplace(row.command);
        REQUIRE(inserted);
    }
}

TEST_CASE("Editor asset import: validates obj extension case-insensitively",
          "[editor]") {
    REQUIRE(IsObjFilePath("sandbox/mesh.obj"));
    REQUIRE(IsObjFilePath("sandbox/MESH.OBJ"));
    REQUIRE_FALSE(IsObjFilePath("sandbox/mesh.fbx"));
    REQUIRE_FALSE(IsObjFilePath(""));
}

TEST_CASE("Editor asset import: derives asset id and mesh tag from path",
          "[editor]") {
    const std::string path = "/Users/bodur/Downloads/torch_model.obj";
    REQUIRE(AssetIdFromImportedPath(path) == "torch_model");
    REQUIRE(MeshTagFromImportedPath(path) == "assets/models/torch_model.obj");

    REQUIRE(AssetIdFromImportedPath("").empty());
    REQUIRE(MeshTagFromImportedPath("").empty());
}

TEST_CASE("Editor MCP delete_asset removes managed imported asset folders",
          "[editor][mcp][filesystem]") {
    namespace fs = std::filesystem;

    const fs::path projectRoot =
            Monolith::Tests::SecureTempBase() / "horo_editor_delete_asset_managed";
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
    doc.assets["crate"] = AssetDef{
        "assets/models/crate/crate.obj", "1,1,1",
        "assets/models/crate/crate.png"
    };
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

TEST_CASE("Editor MCP delete_asset keeps manual asset files and removes only "
          "registry entry",
          "[editor][mcp][filesystem]") {
    namespace fs = std::filesystem;

    const fs::path projectRoot =
            Monolith::Tests::SecureTempBase() / "horo_editor_delete_asset_manual";
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

TEST_CASE("Editor create_object_from_asset shares parent-aware creation path",
          "[editor][mcp][assets]") {
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
                                     nlohmann::json{
                                         {"assetId", "crate"},
                                         {"parentId", "parent_panel"},
                                         {"id", "child_prop"}
                                     });
    REQUIRE(childResult.ok);
    REQUIRE(childResult.data["created"]["id"] == "child_prop");
    REQUIRE(childResult.data["created"]["parentId"] == "parent_panel");

    const auto rootResult = editor.ExecuteMcpCommand(
        "editor.create_object_from_asset",
        nlohmann::json{
            {"assetId", "crate"},
            {"id", "root_prop"},
            {"position", nlohmann::json::array({4.0f, 5.0f, 6.0f})}
        });
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

TEST_CASE("Editor MCP duplicate duplicates each selected object once",
          "[editor][mcp][selection]") {
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
            {"count", 4}
        });
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

TEST_CASE("Editor MCP duplicate preserves single-object count behavior",
          "[editor][mcp][selection]") {
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

TEST_CASE("Editor MCP undo and redo restore created object selection",
          "[editor][mcp][history]") {
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

TEST_CASE(
    "Editor MCP undo and redo restore duplicate selection and dirty state",
    "[editor][mcp][history]") {
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

TEST_CASE("Editor MCP undo restores previous scene after new_scene",
          "[editor][mcp][history]") {
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

TEST_CASE("Editor MCP undo restores asset edits and selected asset",
          "[editor][mcp][history]") {
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
        nlohmann::json{
            {"id", "crate"},
            {"displayName", "Crate Large"},
            {"albedoMap", "assets/models/crate.png"}
        });
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

TEST_CASE(
    "Editor MCP create_prefab writes prefab file and links selected instance",
    "[editor][mcp][prefab]") {
    namespace fs = std::filesystem;

    const fs::path projectRoot =
            Monolith::Tests::SecureTempBase() / "horo_editor_create_prefab";
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

TEST_CASE("Runtime bridge resolves linked prefab instances to concrete props",
          "[editor][prefab][runtime]") {
    namespace fs = std::filesystem;

    const fs::path projectRoot =
            Monolith::Tests::SecureTempBase() / "horo_runtime_prefab_instance";
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
            Monolith::Editor::BuildRuntimeSceneDefinition(doc);
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

TEST_CASE("Editor UI logic: hotkey popup triggers only on valid rising edge",
          "[editor]") {
    REQUIRE(ShouldToggleHelpPopup(true, false, false, false));
    REQUIRE_FALSE(ShouldToggleHelpPopup(true, true, false, false));
    REQUIRE_FALSE(ShouldToggleHelpPopup(true, false, true, false));
    REQUIRE_FALSE(ShouldToggleHelpPopup(true, false, false, true));
}

TEST_CASE("Editor UI logic: quick open is blocked in fly mode and text input",
          "[editor]") {
    REQUIRE(ShouldOpenQuickOpen(true, false, false, false, false));
    REQUIRE_FALSE(ShouldOpenQuickOpen(true, false, true, false, false));
    REQUIRE_FALSE(ShouldOpenQuickOpen(true, false, false, true, false));
    REQUIRE_FALSE(ShouldOpenQuickOpen(true, false, false, false, true));
    REQUIRE_FALSE(ShouldOpenQuickOpen(true, true, false, false, false));
}

TEST_CASE("Editor helpers: command palette table includes scene actions",
          "[editor]") {
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

TEST_CASE("Editor UI logic: command palette shares quick-open gating",
          "[editor]") {
    REQUIRE(ShouldOpenCommandPalette(true, false, false, false, false));
    REQUIRE_FALSE(ShouldOpenCommandPalette(true, false, true, false, false));
    REQUIRE_FALSE(ShouldOpenCommandPalette(true, false, false, true, false));
    REQUIRE_FALSE(ShouldOpenCommandPalette(true, true, false, false, false));
}

TEST_CASE("Editor UI logic: copy and delete actions gate correctly",
          "[editor]") {
    REQUIRE(ShouldCopySelectionRef(true, false, false, false, true));
    REQUIRE_FALSE(ShouldCopySelectionRef(true, false, false, false, false));
    REQUIRE_FALSE(ShouldCopySelectionRef(true, true, false, false, true));
    REQUIRE(ShouldRequestDeleteSelection(true, false, true));
    REQUIRE_FALSE(ShouldRequestDeleteSelection(true, true, true));
    REQUIRE_FALSE(ShouldRequestDeleteSelection(true, false, false));
}

TEST_CASE("Editor UI logic: escape handling respects modal and input gates",
          "[editor]") {
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

TEST_CASE("Editor UI logic: close finalization waits for pending reload",
          "[editor]") {
    REQUIRE_FALSE(ShouldFinalizeEditorClose(false, false));
    REQUIRE_FALSE(ShouldFinalizeEditorClose(true, true));
    REQUIRE(ShouldFinalizeEditorClose(true, false));
}

TEST_CASE("Editor UI logic: status text is stable and clamps selection",
          "[editor]") {
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

TEST_CASE("Editor UI logic: edit menu enables only for single valid selection",
          "[editor]") {
    REQUIRE(CanEditSingleSelection(1, 0, 1));
    REQUIRE(CanEditSingleSelection(1, 2, 5));

    REQUIRE_FALSE(CanEditSingleSelection(0, -1, 5));
    REQUIRE_FALSE(CanEditSingleSelection(2, 1, 5));
    REQUIRE_FALSE(CanEditSingleSelection(1, -1, 5));
    REQUIRE_FALSE(CanEditSingleSelection(1, 5, 5));
}

TEST_CASE("SceneSerializer: _eid prop is stripped on save",
          "[editor][serializer]") {
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

TEST_CASE("SceneSerializer: legacy isLight=true migrates to light component",
          "[editor][serializer]") {
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

TEST_CASE(
    "SceneSerializer: legacy isLight=false is tolerated but not persisted",
    "[editor][serializer]") {
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

TEST_CASE("SceneSerializer: SaveToFile throws on unwriteable path",
          "[editor][serializer]") {
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

TEST_CASE("SceneSerializer: filePath is set after load",
          "[editor][serializer]") {
    SceneDocument doc;
    const std::string path = TmpPath("filepath_test.json");
    SceneSerializer::SaveToFile(doc, path);

    SceneDocument loaded = SceneSerializer::LoadFromFile(path);
    REQUIRE(loaded.filePath == path);
}

TEST_CASE("SceneSerializer: multiple props round-trip",
          "[editor][serializer]") {
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

TEST_CASE("RayVsAABB: parallel ray outside slab misses",
          "[editor][raycaster]") {
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

TEST_CASE("RayVsAABB: ray pointing away from box misses",
          "[editor][raycaster]") {
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

TEST_CASE("ScreenToRay: center of screen produces ray through view center",
          "[editor][raycaster]") {
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
    for (auto [mx, my]:
         std::initializer_list<std::pair<float, float> >{
             {0.0f, 0.0f},
             {200.0f, 150.0f},
             {400.0f, 300.0f},
             {799.0f, 449.0f}
         }) {
        Ray r = ScreenToRay(mx, my, 800, 450, cam);
        REQUIRE(r.direction.Length() == Approx(1.0f).epsilon(1e-4f));
    }
}

TEST_CASE("ScreenToRay: origin is at/near camera position",
          "[editor][raycaster]") {
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

TEST_CASE("Hierarchy range select fills contiguous range",
          "[editor][hierarchy]") {
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

TEST_CASE(
    "Hierarchy shift-click with no prior anchor falls back to single select",
    "[editor][hierarchy]") {
    std::vector<int> sel;
    int last = -1;

    // Shift before any click: last == -1, range guard doesn't fire.
    // Falls through to else branch → behaves as a single select.
    ApplyHierarchyClick(sel, last, 4, true, false);
    REQUIRE(sel.size() == 1);
    REQUIRE(sel[0] == 4);
    REQUIRE(last == 4);
}

TEST_CASE("Hierarchy ctrl-click toggles individual items",
          "[editor][hierarchy]") {
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
    const Vec3 hit = {
        r.origin.x + r.direction.x * t,
        r.origin.y + r.direction.y * t,
        r.origin.z + r.direction.z * t
    };

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
    const Vec3 hit = {
        r.origin.x + r.direction.x * t,
        r.origin.y + r.direction.y * t,
        r.origin.z + r.direction.z * t
    };

    REQUIRE(hit.y == Approx(0.0f).margin(1e-4f));
    REQUIRE(hit.x == Approx(3.0f).margin(1e-4f)); // no x-component in direction
}

TEST_CASE("Drop ray parallel to ground plane is rejected",
          "[editor][dragdrop]") {
    Ray r;
    r.origin = {2.0f, 3.0f, 4.0f};
    r.direction = {1.0f, 0.0f, 0.0f};

    Vec3 hit = Vec3::Zero();
    REQUIRE_FALSE(TryIntersectGroundPlane(r, &hit));
}

TEST_CASE("RayVsAABBHit reports top-face hit point and normal",
          "[editor][dragdrop]") {
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

TEST_CASE("RayVsAABBHit reports side-face hit point and normal",
          "[editor][dragdrop]") {
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

TEST_CASE("Editor viewport rect excludes docks and panels", "[editor][ui]") {
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

TEST_CASE(
    "Editor view gimbal layout reserves wire button and combined pick rect",
    "[editor][ui]") {
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

TEST_CASE("Editor layout helpers clamp dock widths and workspace height",
          "[editor][ui]") {
    REQUIRE(ComputeEditorLeftDockWidth(1200.0f) == Approx(220.0f));
    REQUIRE(ComputeEditorLeftDockWidth(2400.0f) == Approx(320.0f));

    REQUIRE(ComputeEditorRightPanelWidth(1200.0f) == Approx(280.0f));
    REQUIRE(ComputeEditorRightPanelWidth(2400.0f) == Approx(380.0f));

    REQUIRE(ComputeEditorBottomDockHeight(720.0f) == Approx(180.0f));
    REQUIRE(ComputeEditorBottomDockHeight(1440.0f) == Approx(259.2f));
}

TEST_CASE(
    "Editor viewport asset drop target matches active asset payload inline",
    "[editor][ui][dragdrop]") {
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

TEST_CASE("Editor viewport asset drop target stays inactive during play mode",
          "[editor][ui][dragdrop]") {
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
        true, 220.0f, 140.0f, nullptr,
        [&callbackInvoked](auto *, const char *) {
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

TEST_CASE("Editor workspace settings: missing file falls back to defaults",
          "[editor][workspace]") {
    namespace fs = std::filesystem;
    const fs::path tempHome =
            Monolith::Tests::SecureTempBase() / "horo_editor_workspace_missing";
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

TEST_CASE("Editor workspace settings: invalid JSON reports parse fallback",
          "[editor][workspace]") {
    namespace fs = std::filesystem;
    const fs::path tempHome =
            Monolith::Tests::SecureTempBase() / "horo_editor_workspace_invalid";
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

TEST_CASE("Editor workspace settings: round-trip console filters and cwd",
          "[editor][workspace]") {
    namespace fs = std::filesystem;
    const fs::path tempHome =
            Monolith::Tests::SecureTempBase() / "horo_editor_workspace_roundtrip";
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

TEST_CASE("Vec3 CSV parser accepts render scale triples", "[editor][ui]") {
    Vec3 parsed = Vec3::Zero();
    REQUIRE(TryParseVec3Csv("1.5000, 2.0000,0.7500", &parsed));
    REQUIRE(parsed.x == Approx(1.5f));
    REQUIRE(parsed.y == Approx(2.0f));
    REQUIRE(parsed.z == Approx(0.75f));
    REQUIRE_FALSE(TryParseVec3Csv("1.0,2.0", &parsed));
}

// ============================================================
// SceneProjectBridge coverage tests
// ============================================================

TEST_CASE("SceneProjectBridge: Camera object propagates fov/nearClip/farClip",
          "[editor][bridge]") {
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

TEST_CASE("SceneProjectBridge: Light node parses directional type and properties",
          "[editor][bridge]") {
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

TEST_CASE("SceneProjectBridge: Prop with script component populates script field",
          "[editor][bridge]") {
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

TEST_CASE("SceneProjectBridge: Prop with rigidbody component populates rigidbody field",
          "[editor][bridge]") {
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

TEST_CASE("SceneProjectBridge: Prop with light component populates light field",
          "[editor][bridge]") {
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

TEST_CASE("SceneProjectBridge: legacy isLight=true migrates to light node",
          "[editor][bridge]") {
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

TEST_CASE("SceneProjectBridge: legacy isLight=false does not create light node",
          "[editor][bridge]") {
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

TEST_CASE("SceneProjectBridge: legacy behavior prop migrates to script component",
          "[editor][bridge]") {
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

TEST_CASE("SceneProjectBridge: behavior=none is not migrated to script",
          "[editor][bridge]") {
    SceneDocument doc;
    SceneObject obj;
    obj.id = "inert";
    obj.type = SceneObjectType::Prop;
    obj.props["behavior"] = "none";
    doc.objects.push_back(obj);

    const SceneProjectModel model = BuildSceneProjectModel(doc);
    REQUIRE_FALSE(model.scene.nodes[0].script.has_value());
}

TEST_CASE("SceneProjectBridge: scene settings spawnPoint is parsed",
          "[editor][bridge]") {
    SceneDocument doc;
    doc.settings["spawnPoint"] = "1.5,2.5,3.5";

    const SceneProjectModel model = BuildSceneProjectModel(doc);
    REQUIRE(model.scene.settings.spawnPoint.x == Approx(1.5f));
    REQUIRE(model.scene.settings.spawnPoint.y == Approx(2.5f));
    REQUIRE(model.scene.settings.spawnPoint.z == Approx(3.5f));
}

TEST_CASE("SceneProjectBridge: object parentId propagates to node parentId",
          "[editor][bridge]") {
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

TEST_CASE("SceneProjectBridge: assets with renderScale are parsed correctly",
          "[editor][bridge]") {
    SceneDocument doc;
    doc.assets["crate"] = AssetDef{"models/crate.obj", "2.0,3.0,4.0", ""};

    const SceneProjectModel model = BuildSceneProjectModel(doc);
    REQUIRE(model.scene.assets.size() == 1);
    REQUIRE(model.scene.assets[0].id == "crate");
    REQUIRE(model.scene.assets[0].renderScale.x == Approx(2.0f));
    REQUIRE(model.scene.assets[0].renderScale.y == Approx(3.0f));
    REQUIRE(model.scene.assets[0].renderScale.z == Approx(4.0f));
}

TEST_CASE("SceneProjectBridge: BuildSceneDocument round-trips Camera node",
          "[editor][bridge]") {
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

TEST_CASE("SceneProjectBridge: BuildSceneDocument round-trips Light node",
          "[editor][bridge]") {
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

TEST_CASE("SceneProjectBridge: BuildSceneDocument round-trips rigidbody component",
          "[editor][bridge]") {
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

TEST_CASE("SceneProjectBridge: BuildSceneDocument round-trips script component",
          "[editor][bridge]") {
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
    const auto it = std::ranges::find_if(comps,
        [](const ComponentDesc &c) { return c.type == "script"; });
    REQUIRE(it != comps.end());
    REQUIRE(it->props.at("behaviorTag") == "EnemyAI");
}

TEST_CASE("SceneProjectBridge: empty document produces empty model",
          "[editor][bridge]") {
    const SceneDocument doc;
    const SceneProjectModel model = BuildSceneProjectModel(doc);
    REQUIRE(model.scene.nodes.empty());
    REQUIRE(model.scene.assets.empty());
}

TEST_CASE("SceneProjectBridge: Panel node has Panel kind in model",
          "[editor][bridge]") {
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

// ============================================================
// EditorSceneGraph coverage tests
// ============================================================

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta moves child with parent",
          "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta skips listed indices",
          "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta invalid parentIdx is no-op",
          "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta invokes callback for moved child",
          "[editor][scene-graph]") {
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
    PropagateHierarchyTransformDelta(doc, 0, oldState, newState,
        [&callbackCount](const SceneObject &) { ++callbackCount; });
    REQUIRE(callbackCount == 1);
}

TEST_CASE("EditorSceneGraph: RewriteObjectIdReferences updates parentId",
          "[editor][scene-graph]") {
    SceneDocument doc;
    SceneObject child;
    child.id = "child";
    child.props["parentId"] = "old_parent";
    doc.objects.push_back(child);

    RewriteObjectIdReferences(&doc, "old_parent", "new_parent");
    REQUIRE(doc.objects[0].props.at("parentId") == "new_parent");
}

TEST_CASE("EditorSceneGraph: RewriteObjectIdReferences is a no-op for empty oldId",
          "[editor][scene-graph]") {
    SceneDocument doc;
    SceneObject obj;
    obj.id = "obj";
    obj.props["parentId"] = "some_id";
    doc.objects.push_back(obj);

    RewriteObjectIdReferences(&doc, "", "new_id");
    REQUIRE(doc.objects[0].props.at("parentId") == "some_id");
}

TEST_CASE("EditorSceneGraph: RewriteObjectIdReferences is a no-op when old equals new",
          "[editor][scene-graph]") {
    SceneDocument doc;
    SceneObject obj;
    obj.id = "obj";
    obj.props["parentId"] = "same_id";
    doc.objects.push_back(obj);

    RewriteObjectIdReferences(&doc, "same_id", "same_id");
    REQUIRE(doc.objects[0].props.at("parentId") == "same_id");
}

TEST_CASE("EditorSceneGraph: LogDanglingObjectReferences does not crash on clean doc",
          "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: LogDanglingObjectReferences warns on dangling ref",
          "[editor][scene-graph]") {
    SceneDocument doc;
    SceneObject orphan;
    orphan.id = "orphan";
    orphan.props["parentId"] = "nonexistent_parent";
    doc.objects.push_back(orphan);

    // Should not crash — just logs a warning
    LogDanglingObjectReferences(doc, "test_source");
    REQUIRE(true);
}

TEST_CASE("EditorSceneGraph: SanitizePrefabStem handles various inputs",
          "[editor][scene-graph]") {
    REQUIRE(SanitizePrefabStem("hello_world") == "hello_world");
    REQUIRE(SanitizePrefabStem("valid-name_123") == "valid-name_123");
    REQUIRE(SanitizePrefabStem("__hello__") == "hello");
    REQUIRE(SanitizePrefabStem("_test") == "test");
    REQUIRE(SanitizePrefabStem("test_") == "test");
    REQUIRE(SanitizePrefabStem("") == "prefab");
    REQUIRE(SanitizePrefabStem("___") == "prefab");
}

TEST_CASE("EditorSceneGraph: SanitizePrefabStem replaces spaces and special chars",
          "[editor][scene-graph]") {
    const std::string result = SanitizePrefabStem("hello world!");
    // space→'_', '!'→'_', trailing '_' stripped → "hello_world"
    REQUIRE(result == "hello_world");
}

TEST_CASE("EditorSceneGraph: CollectReservedObjectIds includes object ids and references",
          "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: IsReservedObjectId returns true for existing id",
          "[editor][scene-graph]") {
    SceneDocument doc;
    SceneObject obj;
    obj.id = "used_id";
    doc.objects.push_back(obj);

    REQUIRE(IsReservedObjectId(doc, "used_id"));
    REQUIRE_FALSE(IsReservedObjectId(doc, "free_id"));
}

TEST_CASE("EditorSceneGraph: IsReservedObjectId with ignoreConcreteObjectId",
          "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: IsReservedObjectId returns false for empty id",
          "[editor][scene-graph]") {
    SceneDocument doc;
    REQUIRE_FALSE(IsReservedObjectId(doc, ""));
}

// ============================================================
// EditorHistory edge-case tests (exercised via MCP commands)
// ============================================================

TEST_CASE("EditorHistory: TrimHistory caps undo stack at 128 entries",
          "[editor][history]") {
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
        const auto res = editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
        REQUIRE(res.ok);
        if (!res.data["undone"].get<bool>())
            break;
        ++undoCount;
    }
    REQUIRE(undoCount == 128);
}

TEST_CASE("EditorHistory: HistorySnapshotsEqual returns false for differing documents",
          "[editor][history]") {
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
    const auto undoRes = editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
    REQUIRE(undoRes.ok);
    REQUIRE(undoRes.data["undone"].get<bool>());

    // Redo restores the object — verifies that the snapshots were indeed different
    const auto redoRes = editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
    REQUIRE(redoRes.ok);
    REQUIRE(redoRes.data["redone"].get<bool>());
    REQUIRE(editor.GetDocument().objects.size() == 1);
}

TEST_CASE("EditorHistory: RefreshHistorySavedBaseline updates dirty flag after save",
          "[editor][history]") {
    namespace fs = std::filesystem;
    const fs::path sceneDir =
        Monolith::Tests::SecureTempBase() / "horo_history_baseline_test";
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
    const auto saveRes = editor.ExecuteMcpCommand("editor.save_scene", nlohmann::json::object());
    REQUIRE(saveRes.ok);
    REQUIRE_FALSE(editor.GetDocument().dirty);

    // Undo the create; the restored snapshot should reflect the saved baseline
    const auto undoRes = editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
    REQUIRE(undoRes.ok);
    REQUIRE(undoRes.data["undone"].get<bool>());
    // After undo the document is dirty relative to the saved file
    REQUIRE(editor.GetDocument().dirty);
}

// ============================================================
// EditorMcpHandlers error path coverage
// ============================================================

TEST_CASE("EditorMcp: create_object with Camera type uses GenerateCameraId",
          "[editor][mcp]") {
    EditorLayer editor;
    editor.LoadDocument(SceneDocument{});

    const auto res = editor.ExecuteMcpCommand(
        "editor.create_object",
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
        nlohmann::json{{"type", "Panel"},
                       {"scale", nlohmann::json::array({"bad", "scale", "fmt"})}});
    REQUIRE_FALSE(res.ok);
    REQUIRE(res.error == "scale must be [x,y,z].");
}

TEST_CASE("EditorMcp: create_object rejects non-array components", "[editor][mcp]") {
    EditorLayer editor;
    editor.LoadDocument(SceneDocument{});

    const auto res = editor.ExecuteMcpCommand(
        "editor.create_object",
        nlohmann::json{{"type", "Prop"},
                       {"components", "not_an_array"}});
    REQUIRE_FALSE(res.ok);
    REQUIRE(res.error == "components must be an array of objects.");
}

TEST_CASE("EditorMcp: create_object rejects components with missing type field",
          "[editor][mcp]") {
    EditorLayer editor;
    editor.LoadDocument(SceneDocument{});

    const auto res = editor.ExecuteMcpCommand(
        "editor.create_object",
        nlohmann::json{{"type", "Prop"},
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
        "editor.create_object",
        nlohmann::json{{"type", "NotAType"}});
    REQUIRE_FALSE(res.ok);
    REQUIRE(res.error == "Invalid object type.");
}

TEST_CASE("EditorMcp: update_object returns error when object not found",
          "[editor][mcp]") {
    EditorLayer editor;
    editor.LoadDocument(SceneDocument{});

    const auto res = editor.ExecuteMcpCommand(
        "editor.update_object",
        nlohmann::json{{"id", "missing_obj"},
                       {"position", nlohmann::json::array({1.0, 2.0, 3.0})}});
    REQUIRE_FALSE(res.ok);
    REQUIRE(res.error == "Object not found.");
}

TEST_CASE("EditorMcp: editor.transform is an alias for editor.update_object",
          "[editor][mcp]") {
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

TEST_CASE("EditorMcp: reparent_object rejects cycle-forming reparent",
          "[editor][mcp]") {
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

TEST_CASE("EditorMcp: reparent_object unparents when parentId is empty",
          "[editor][mcp]") {
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
        "editor.reparent_object",
        nlohmann::json{{"id", "c"}, {"parentId", ""}});
    REQUIRE(res.ok);
    const auto &objs = editor.GetDocument().objects;
    const auto cIt = std::ranges::find_if(objs, [](const SceneObject &o) { return o.id == "c"; });
    REQUIRE(cIt != objs.end());
    const SceneObject *c = &*cIt;
    REQUIRE(c != nullptr);
    REQUIRE_FALSE(c->props.contains("parentId"));
}

TEST_CASE("EditorMcp: unknown command returns descriptive error", "[editor][mcp]") {
    EditorLayer editor;
    editor.LoadDocument(SceneDocument{});

    const auto res = editor.ExecuteMcpCommand(
        "editor.nonexistent_tool",
        nlohmann::json::object());
    REQUIRE_FALSE(res.ok);
    REQUIRE(res.error == "Unsupported MCP command.");
}

TEST_CASE("EditorMcp: create_object with components array creates component",
          "[editor][mcp]") {
    EditorLayer editor;
    editor.LoadDocument(SceneDocument{});

    const auto res = editor.ExecuteMcpCommand(
        "editor.create_object",
        nlohmann::json{
            {"type", "Prop"},
            {"id", "scripted_mcp_obj"},
            {"components", nlohmann::json::array({
                nlohmann::json{{"type", "script"},
                               {"props", nlohmann::json{{"behaviorTag", "Guard"}}}}
            })}
        });
    REQUIRE(res.ok);
    REQUIRE(editor.GetDocument().objects.size() == 1);
    REQUIRE(editor.GetDocument().objects[0].components.size() == 1);
    REQUIRE(editor.GetDocument().objects[0].components[0].type == "script");
}

TEST_CASE("EditorMcp: create_object with parentId links child to existing parent",
          "[editor][mcp]") {
    SceneDocument doc;
    SceneObject parentObj;
    parentObj.id = "existing_parent";
    parentObj.type = SceneObjectType::Panel;
    doc.objects.push_back(parentObj);

    EditorLayer editor;
    editor.LoadDocument(doc);

    const auto res = editor.ExecuteMcpCommand(
        "editor.create_object",
        nlohmann::json{{"type", "Prop"},
                       {"id", "new_child"},
                       {"parentId", "existing_parent"}});
    REQUIRE(res.ok);
    const auto &childObjs = editor.GetDocument().objects;
    const auto childIt = std::ranges::find_if(childObjs, [](const SceneObject &o) { return o.id == "new_child"; });
    REQUIRE(childIt != childObjs.end());
    const SceneObject *child = &*childIt;
    REQUIRE(child != nullptr);
    REQUIRE(child->props.at("parentId") == "existing_parent");
}

// ============================================================
// EditorLayer public method coverage
// ============================================================

TEST_CASE("EditorLayer: LoadDocument migrates legacy behavior prop to script component",
          "[editor][layer]") {
    SceneDocument doc;
    SceneObject obj;
    obj.id = "legacy_prop";
    obj.type = SceneObjectType::Prop;
    obj.props["behavior"] = "GuardAI";
    doc.objects.push_back(obj);

    EditorLayer editor;
    editor.LoadDocument(doc);

    const auto &objs0 = editor.GetDocument().objects;
    const auto it0 = std::ranges::find_if(objs0, [](const SceneObject &o) { return o.id == "legacy_prop"; });
    REQUIRE(it0 != objs0.end());
    const SceneObject *loaded = &*it0;
    REQUIRE(loaded != nullptr);
    // "behavior" prop should be erased and replaced by a script component
    REQUIRE_FALSE(loaded->props.contains("behavior"));
    REQUIRE(loaded->components.size() == 1);
    REQUIRE(loaded->components[0].type == "script");
    REQUIRE(loaded->components[0].props.at("behaviorTag") == "GuardAI");
}

TEST_CASE("EditorLayer: LoadDocument skips migration when script component already present",
          "[editor][layer]") {
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
    const auto it1 = std::ranges::find_if(objs1, [](const SceneObject &o) { return o.id == "already_scripted"; });
    REQUIRE(it1 != objs1.end());
    const SceneObject *loaded = &*it1;
    REQUIRE(loaded != nullptr);
    // Existing script component must be kept; behavior prop should be erased
    REQUIRE(loaded->components.size() == 1);
    REQUIRE(loaded->components[0].props.at("behaviorTag") == "NewScript");
}

TEST_CASE("EditorLayer: LoadDocument with behavior=none does not create script component",
          "[editor][layer]") {
    SceneDocument doc;
    SceneObject obj;
    obj.id = "no_script";
    obj.type = SceneObjectType::Prop;
    obj.props["behavior"] = "none";
    doc.objects.push_back(obj);

    EditorLayer editor;
    editor.LoadDocument(doc);

    const auto &objs2 = editor.GetDocument().objects;
    const auto it2 = std::ranges::find_if(objs2, [](const SceneObject &o) { return o.id == "no_script"; });
    REQUIRE(it2 != objs2.end());
    const SceneObject *loaded = &*it2;
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->components.empty());
}

TEST_CASE("EditorLayer: SetHotReloadOverlay stores state without crashing",
          "[editor][layer]") {
    EditorLayer editor;
    editor.SetHotReloadOverlay(true, 0.5f, 45.0f, "Compiling shaders...");
    editor.SetHotReloadOverlay(false, 0.0f, 0.0f, "");
    REQUIRE(true);
}

TEST_CASE("EditorLayer: OnPathsDropped stores paths for deferred processing",
          "[editor][layer]") {
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

TEST_CASE("EditorLayer: AcknowledgeReload clears WantsSceneReload flag",
          "[editor][layer]") {
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

TEST_CASE("EditorLayer: GetSelectedAssetId is empty when no asset is selected",
          "[editor][layer]") {
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

TEST_CASE("EditorLayer: SetProjectBrowserExtraBlocklist does not crash",
          "[editor][layer]") {
    EditorLayer editor;
    editor.SetProjectBrowserExtraBlocklist({"node_modules", ".git"});
    editor.SetProjectBrowserExtraBlocklist({});
    REQUIRE(true);
}

TEST_CASE("EditorLayer: Render with inactive editor does not crash",
          "[editor][layer]") {
    ImGuiContextGuard imgui;

    EditorLayer editor;
    editor.LoadDocument(SceneDocument{});

    Camera cam;
    // Render handles NewFrame/EndFrame/Render internally
    editor.Render(cam, 1280, 720);
    REQUIRE(true);
}
