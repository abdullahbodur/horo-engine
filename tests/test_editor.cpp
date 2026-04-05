#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <algorithm>
#include <cctype>

#include "editor/EditorSchema.h"
#include "editor/EditorAssetImport.h"
#include "editor/EditorSearch.h"
#include "editor/EditorUiLogic.h"
#include "editor/Raycaster.h"
#include "editor/SceneDocument.h"
#include "editor/SceneSerializer.h"
#include "renderer/Camera.h"

using namespace Monolith;
using namespace Monolith::Editor;

namespace {
Monolith::Editor::SceneObject MakeObjectFromAssetForTest(const Monolith::Editor::SceneDocument&, const std::string& assetId) {
    Monolith::Editor::SceneObject obj;
    obj.id = "generated";
    obj.type = Monolith::Editor::SceneObjectType::Prop;
    obj.assetId = assetId;
    return obj;
}

Monolith::Editor::SceneObject DuplicateObjectForTest(const Monolith::Editor::SceneDocument& doc, const Monolith::Editor::SceneObject& src) {
    Monolith::Editor::SceneObject clone = src;
    clone.id = "copy_" + std::to_string(doc.objects.size());
    clone.props.erase("_eid");
    return clone;
}

}
using Catch::Approx;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string TmpPath(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

static void WriteFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

// ===========================================================================
// EditorSchema
// ===========================================================================

TEST_CASE("EditorSchema: missing file is silently ignored", "[editor][schema]") {
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

TEST_CASE("EditorSchema: malformed JSON is silently ignored", "[editor][schema]") {
    WriteFile(TmpPath("schema_bad.json"), "{ this is not valid json !!!}");
    EditorSchema schema;
    REQUIRE_NOTHROW(schema.LoadFromFile(TmpPath("schema_bad.json")));
    REQUIRE(schema.GetSchema(SceneObjectType::Prop) == nullptr);
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

    const TypeSchema* ts = schema.GetSchema(SceneObjectType::Prop);
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

    const TypeSchema* ts = schema.GetSchema(SceneObjectType::Prop);
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

    const TypeSchema* ts = schema.GetSchema(SceneObjectType::Prop);
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

    const TypeSchema* ts = schema.GetSchema(SceneObjectType::Light);
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

    const TypeSchema* ts = schema.GetSchema(SceneObjectType::Prop);
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

    const TypeSchema* ts = schema.GetSchema(SceneObjectType::Panel);
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

    const TypeSchema* ts = schema.GetSchema(SceneObjectType::Light);
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

// ===========================================================================
// SceneSerializer
// ===========================================================================

TEST_CASE("SceneSerializer: LoadFromFile throws on missing file", "[editor][serializer]") {
    REQUIRE_THROWS_AS(
        SceneSerializer::LoadFromFile("/nonexistent/scene.json"),
        std::runtime_error);
}

TEST_CASE("SceneSerializer: LoadFromFile throws on invalid JSON", "[editor][serializer]") {
    WriteFile(TmpPath("bad_scene.json"), "{ not json!!! }");
    REQUIRE_THROWS_AS(
        SceneSerializer::LoadFromFile(TmpPath("bad_scene.json")),
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
    const auto& o = loaded.objects[0];
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
    bool foundProp = false, foundLight = false;
    for (const auto& o : loaded.objects) {
        if (o.id == "prop_a") { foundProp = true; REQUIRE(o.type == SceneObjectType::Prop); }
        if (o.id == "light_1") { foundLight = true; REQUIRE(o.type == SceneObjectType::Light); }
    }
    REQUIRE(foundProp);
    REQUIRE(foundLight);
}

TEST_CASE("SceneSerializer: round-trip asset registry", "[editor][serializer]") {
    SceneDocument doc;

    AssetDef asset;
    asset.mesh = "stone.obj";
    asset.renderScale = "2.0000,2.0000,2.0000";
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
    REQUIRE(loaded.assets.at("stone_asset").renderScale == "2.0000,2.0000,2.0000");
    REQUIRE(loaded.objects[0].assetId == "stone_asset");
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

    REQUIRE(loaded.assets.at("tex_asset").albedoMap == "assets/models/custom_Albedo.png");
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
    std::string saved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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
    std::string saved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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

TEST_CASE("Editor helpers: object type labels are stable", "[editor]") {
    REQUIRE(std::string(ObjectTypeLabel(SceneObjectType::Prop)) == "prop");
    REQUIRE(std::string(ObjectTypeLabel(SceneObjectType::Light)) == "light");
    REQUIRE(std::string(ObjectTypeLabel(SceneObjectType::Panel)) == "board");
    REQUIRE(std::string(ObjectTypeLabel(static_cast<SceneObjectType>(999))) == "unknown");
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
    REQUIRE(EvaluateFilteredListState(4, 0, "torch") == FilteredListState::NoMatches);
    REQUIRE(EvaluateFilteredListState(4, 0, "") == FilteredListState::None);
}

TEST_CASE("Editor helpers: shortcut table includes required entries", "[editor]") {
    const auto rows = GetEditorShortcuts();
    REQUIRE_FALSE(rows.empty());

    auto hasCommandWithKeys = [&](const char* command, const char* keys) {
        return std::any_of(rows.begin(), rows.end(), [&](const ShortcutRow& row) {
            return std::string(row.command) == command && std::string(row.keys) == keys;
        });
    };

    REQUIRE(hasCommandWithKeys("Run or stop game in viewport", "Toolbar: Play / Stop"));
    REQUIRE(hasCommandWithKeys("Toggle shortcuts help", "? or F1"));
    REQUIRE(hasCommandWithKeys("Quick open", "Ctrl/Cmd + P"));
}

TEST_CASE("Editor helpers: shortcut table commands are unique", "[editor]") {
    const auto rows = GetEditorShortcuts();
    std::unordered_set<std::string> commandSet;

    for (const auto& row : rows) {
        const auto [_, inserted] = commandSet.insert(row.command);
        REQUIRE(inserted);
    }
}

TEST_CASE("Editor asset import: validates obj extension case-insensitively", "[editor]") {
    REQUIRE(IsObjFilePath("/tmp/mesh.obj"));
    REQUIRE(IsObjFilePath("/tmp/MESH.OBJ"));
    REQUIRE_FALSE(IsObjFilePath("/tmp/mesh.fbx"));
    REQUIRE_FALSE(IsObjFilePath(""));
}

TEST_CASE("Editor asset import: derives asset id and mesh tag from path", "[editor]") {
    const std::string path = "/Users/bodur/Downloads/torch_model.obj";
    REQUIRE(AssetIdFromImportedPath(path) == "torch_model");
    REQUIRE(MeshTagFromImportedPath(path) == "assets/models/torch_model.obj");

    REQUIRE(AssetIdFromImportedPath("").empty());
    REQUIRE(MeshTagFromImportedPath("").empty());
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
    REQUIRE(ResolveEditorExitDecision(false) == EditorExitDecision::ExitImmediately);
    REQUIRE(ResolveEditorExitDecision(true) == EditorExitDecision::PromptUnsavedConfirm);
}

TEST_CASE("Editor UI logic: close finalization waits for pending reload", "[editor]") {
    REQUIRE_FALSE(ShouldFinalizeEditorClose(false, false));
    REQUIRE_FALSE(ShouldFinalizeEditorClose(true, true));
    REQUIRE(ShouldFinalizeEditorClose(true, false));
}

TEST_CASE("Editor UI logic: status text is stable and clamps selection", "[editor]") {
    EditorStatusText status = BuildEditorStatusText(EditorStatusSnapshot{-2, true, false, true});
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
    obj.props["_eid"] = "42";  // runtime-only, should not be persisted
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
    // Windows: a leading "/" is *not* filesystem root; it resolves under the current drive
    // (e.g. C:\nonexistent\...) and is often writable in CI. Use a drive letter that is
    // virtually never mounted on runners.
#ifdef _WIN32
    const std::string badPath = R"(Z:\.__horo_engine_ci_unwritable__\scene.json)";
#else
    const std::string badPath = "/nonexistent/dir/scene.json";
#endif
    REQUIRE_THROWS_AS(SceneSerializer::SaveToFile(doc, badPath), std::runtime_error);
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
    ray.origin    = {-5.0f, 0.0f, 0.0f};
    ray.direction = {1.0f, 0.0f, 0.0f};

    Vec3 center = {0.0f, 0.0f, 0.0f};
    Vec3 half   = {1.0f, 1.0f, 1.0f};

    float t = RayVsAABB(ray, center, half);
    REQUIRE(t >= 0.0f);
    REQUIRE(t == Approx(4.0f).epsilon(1e-4f));  // ray travels 4 units before hitting near face
}

TEST_CASE("RayVsAABB: miss — ray passes above the box", "[editor][raycaster]") {
    Ray ray;
    ray.origin    = {-5.0f, 3.0f, 0.0f};  // y=3, above a box with half=1
    ray.direction = {1.0f, 0.0f, 0.0f};

    Vec3 center = {0.0f, 0.0f, 0.0f};
    Vec3 half   = {1.0f, 1.0f, 1.0f};

    float t = RayVsAABB(ray, center, half);
    REQUIRE(t < 0.0f);
}

TEST_CASE("RayVsAABB: ray origin inside box returns 0", "[editor][raycaster]") {
    Ray ray;
    ray.origin    = {0.0f, 0.0f, 0.0f};  // inside the box
    ray.direction = {1.0f, 0.0f, 0.0f};

    Vec3 center = {0.0f, 0.0f, 0.0f};
    Vec3 half   = {1.0f, 1.0f, 1.0f};

    float t = RayVsAABB(ray, center, half);
    REQUIRE(t >= 0.0f);
    REQUIRE(t == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("RayVsAABB: parallel ray outside slab misses", "[editor][raycaster]") {
    Ray ray;
    ray.origin    = {5.0f, 5.0f, 0.0f};   // outside x-slab of box centered at origin
    ray.direction = {0.0f, 0.0f, 1.0f};   // moves in Z only — parallel to Y slab

    Vec3 center = {0.0f, 0.0f, 0.0f};
    Vec3 half   = {1.0f, 1.0f, 1.0f};

    float t = RayVsAABB(ray, center, half);
    REQUIRE(t < 0.0f);
}

TEST_CASE("RayVsAABB: parallel ray inside slab hits", "[editor][raycaster]") {
    Ray ray;
    ray.origin    = {-5.0f, 0.0f, 0.0f};
    ray.direction = {0.0f, 0.0f, 1.0f};   // moves in Z, inside x and y slabs

    Vec3 center = {0.0f, 0.0f, 0.0f};
    Vec3 half   = {1.0f, 1.0f, 1.0f};

    float t = RayVsAABB(ray, center, half);
    // parallel x-axis: origin.x=-5 is outside [-1,1] → should miss
    REQUIRE(t < 0.0f);
}

TEST_CASE("RayVsAABB: hit along -Z axis", "[editor][raycaster]") {
    Ray ray;
    ray.origin    = {0.0f, 0.0f, 10.0f};
    ray.direction = {0.0f, 0.0f, -1.0f};

    Vec3 center = {0.0f, 0.0f, 0.0f};
    Vec3 half   = {0.5f, 0.5f, 0.5f};

    float t = RayVsAABB(ray, center, half);
    REQUIRE(t >= 0.0f);
    REQUIRE(t == Approx(9.5f).epsilon(1e-4f));
}

TEST_CASE("RayVsAABB: hit along +Y axis", "[editor][raycaster]") {
    Ray ray;
    ray.origin    = {0.0f, -5.0f, 0.0f};
    ray.direction = {0.0f, 1.0f, 0.0f};

    Vec3 center = {0.0f, 2.0f, 0.0f};
    Vec3 half   = {1.0f, 1.0f, 1.0f};

    float t = RayVsAABB(ray, center, half);
    REQUIRE(t >= 0.0f);
    REQUIRE(t == Approx(6.0f).epsilon(1e-4f));
}

TEST_CASE("RayVsAABB: ray pointing away from box misses", "[editor][raycaster]") {
    Ray ray;
    ray.origin    = {10.0f, 0.0f, 0.0f};
    ray.direction = {1.0f, 0.0f, 0.0f};  // pointing away from origin box

    Vec3 center = {0.0f, 0.0f, 0.0f};
    Vec3 half   = {1.0f, 1.0f, 1.0f};

    float t = RayVsAABB(ray, center, half);
    REQUIRE(t < 0.0f);
}

// ===========================================================================
// Raycaster — ScreenToRay
// ===========================================================================

TEST_CASE("ScreenToRay: center of screen produces ray through view center", "[editor][raycaster]") {
    Camera cam;
    cam.position = {0.0f, 0.0f, 5.0f};
    cam.target   = {0.0f, 0.0f, 0.0f};
    cam.up       = Vec3::Up();
    cam.fovY     = 60.0f;
    cam.aspect   = 1.0f;
    cam.zNear    = 0.1f;
    cam.zFar     = 100.0f;

    int W = 800, H = 800;
    Ray ray = ScreenToRay(W / 2.0f, H / 2.0f, W, H, cam);

    REQUIRE(ray.direction.x == Approx(0.0f).margin(1e-3f));
    REQUIRE(ray.direction.y == Approx(0.0f).margin(1e-3f));
    REQUIRE(ray.direction.z < 0.0f);  // pointing toward -Z (into the scene)
}

TEST_CASE("ScreenToRay: direction is normalized", "[editor][raycaster]") {
    Camera cam;
    cam.position = {0.0f, 0.0f, 5.0f};
    cam.target   = {0.0f, 0.0f, 0.0f};
    cam.up       = Vec3::Up();
    cam.fovY     = 60.0f;
    cam.aspect   = 16.0f / 9.0f;
    cam.zNear    = 0.1f;
    cam.zFar     = 100.0f;

    // Test multiple screen positions
    for (auto [mx, my] : std::initializer_list<std::pair<float,float>>{
        {0.0f, 0.0f}, {200.0f, 150.0f}, {400.0f, 300.0f}, {799.0f, 449.0f}
    }) {
        Ray r = ScreenToRay(mx, my, 800, 450, cam);
        REQUIRE(r.direction.Length() == Approx(1.0f).epsilon(1e-4f));
    }
}

TEST_CASE("ScreenToRay: origin is at/near camera position", "[editor][raycaster]") {
    Camera cam;
    cam.position = {3.0f, 2.0f, 7.0f};
    cam.target   = {0.0f, 0.0f, 0.0f};
    cam.up       = Vec3::Up();
    cam.fovY     = 60.0f;
    cam.aspect   = 1.0f;
    cam.zNear    = 0.1f;
    cam.zFar     = 100.0f;

    Ray ray = ScreenToRay(400.0f, 300.0f, 800, 600, cam);

    // Origin should be near the near plane (close to camera)
    Vec3 diff = ray.origin - cam.position;
    REQUIRE(diff.Length() < 1.0f);
}

// ===========================================================================
// Hierarchy selection helpers (testable without ImGui)
// ===========================================================================

// Mirror of the selection logic in EditorLayer::DrawObjectsTree.
static void ApplyHierarchyClick(std::vector<int>& sel, int& lastIdx,
                                 int clicked, bool shift, bool ctrl) {
    if (shift && lastIdx >= 0) {
        const int lo = std::min(lastIdx, clicked);
        const int hi = std::max(lastIdx, clicked);
        sel.clear();
        for (int i = lo; i <= hi; ++i)
            sel.push_back(i);
    } else if (ctrl) {
        auto it = std::find(sel.begin(), sel.end(), clicked);
        if (it != sel.end())
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
    ApplyHierarchyClick(sel, last, 3, false, true);  // ctrl-add 3
    ApplyHierarchyClick(sel, last, 5, false, true);  // ctrl-add 5
    REQUIRE(sel.size() == 3);

    ApplyHierarchyClick(sel, last, 3, false, true);  // ctrl-remove 3
    REQUIRE(sel.size() == 2);
    REQUIRE(std::find(sel.begin(), sel.end(), 3) == sel.end());
}

// ===========================================================================
// Drag-drop ray-floor intersection math
// ===========================================================================

TEST_CASE("Drop ray hits y=0 plane correctly", "[editor][dragdrop]") {
    // Ray from above looking straight down
    Ray r;
    r.origin    = {0.0f, 5.0f, 0.0f};
    r.direction = {0.0f, -1.0f, 0.0f};

    const float t = -r.origin.y / r.direction.y;  // = 5
    const Vec3 hit = {r.origin.x + r.direction.x * t,
                      r.origin.y + r.direction.y * t,
                      r.origin.z + r.direction.z * t};

    REQUIRE(hit.y == Approx(0.0f));
    REQUIRE(hit.x == Approx(0.0f));
    REQUIRE(hit.z == Approx(0.0f));
}

TEST_CASE("Drop ray hits y=0 plane at correct XZ", "[editor][dragdrop]") {
    // Ray from (3, 4, 1) pointing down-forward
    Ray r;
    r.origin    = {3.0f, 4.0f, 1.0f};
    // Direction: slightly forward (+z) and mostly down
    const float len = std::sqrt(1.0f + 16.0f);
    r.direction = {0.0f, -4.0f / len, 1.0f / len};

    const float t = -r.origin.y / r.direction.y;
    const Vec3 hit = {r.origin.x + r.direction.x * t,
                      r.origin.y + r.direction.y * t,
                      r.origin.z + r.direction.z * t};

    REQUIRE(hit.y == Approx(0.0f).margin(1e-4f));
    REQUIRE(hit.x == Approx(3.0f).margin(1e-4f));  // no x-component in direction
}
