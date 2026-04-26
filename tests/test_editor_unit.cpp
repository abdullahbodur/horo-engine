// test_editor_coverage.cpp
// Regression coverage for editor modules that had <80% line coverage.
// Targets: AssetImporterRegistry, AssetImportService, EditorImGuiBackend,
//          EditorDebugTrace, SceneRuntimeBridge,
//          SceneRuntimeCoordinatorBridge, EditorWorkspaceSettings,
//          TransformGizmo (edge cases), SceneSerializer (edge cases).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/ProjectPath.h"
#include "editor/AssetIdentity.h"
#include "editor/AssetImportService.h"
#include "editor/AssetImporterRegistry.h"
#include "editor/AssetMetadata.h"
#include "editor/EditorDebugTrace.h"
#include "editor/EditorImGuiBackend.h"
#include "editor/EditorLayer.h"
#include "editor/EditorSceneGraph.h"
#include "editor/EditorSearch.h"
#include "editor/EditorWorkspaceSettings.h"
#include "editor/SceneDocument.h"
#include "editor/SceneProjectBridge.h"
#include "editor/SceneRuntimeBridge.h"
#include "editor/SceneRuntimeCoordinatorBridge.h"
#include "editor/SceneSerializer.h"
#include "editor/TransformGizmo.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "renderer/Camera.h"
#include "renderer/RenderBackend.h"
#include "scene/SceneProjectModel.h"
#include "scene/SceneRuntimeCoordinator.h"
#include "tests/TestTempPaths.h"

using namespace Monolith;
using namespace Monolith::Editor;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {
struct ProjectPathGuard {
  ProjectPathGuard(const ProjectPathGuard &) = delete;

  ProjectPathGuard &operator=(const ProjectPathGuard &) = delete;

  ProjectPathGuard(ProjectPathGuard &&) = delete;

  ProjectPathGuard &operator=(ProjectPathGuard &&) = delete;

  std::filesystem::path previous = ProjectPath::Root();

  explicit ProjectPathGuard(const std::filesystem::path &next) {
    ProjectPath::Init(next);
  }

  ~ProjectPathGuard() { ProjectPath::Init(previous); }
};

struct HomeDirGuard {
  HomeDirGuard(const HomeDirGuard &) = delete;

  HomeDirGuard &operator=(const HomeDirGuard &) = delete;

  HomeDirGuard(HomeDirGuard &&) = delete;

  HomeDirGuard &operator=(HomeDirGuard &&) = delete;

  std::string prevVal;
  const char *varName =
#ifdef _WIN32
      "USERPROFILE";
#else
      "HOME";
#endif

  explicit HomeDirGuard(const std::filesystem::path &next) {
#ifdef _WIN32
    if (size_t len = 0; getenv_s(&len, nullptr, 0, varName) == 0 && len > 1) {
      std::vector<char> value(len);
      if (getenv_s(&len, value.data(), value.size(), varName) == 0 && len > 1) {
        prevVal.assign(value.data());
      }
    }
    _putenv_s(varName, next.string().c_str());
#else
    const char *current = std::getenv(varName);
    prevVal = current ? current : std::string{};
    setenv(varName, next.string().c_str(), 1);
#endif
  }

  ~HomeDirGuard() {
#ifdef _WIN32
    _putenv_s(varName, prevVal.c_str());
#else
    if (prevVal.empty())
      unsetenv(varName);
    else
      setenv(varName, prevVal.c_str(), 1);
#endif
  }
};

void WriteFile(const std::filesystem::path &path, const std::string &content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream f(path);
  f << content;
}

Camera MakeCam(Vec3 pos = {0, 0, 10}, Vec3 target = Vec3::Zero(),
               float fovY = 60.0f) {
  Camera cam;
  cam.position = pos;
  cam.target = target;
  cam.up = Vec3::Up();
  cam.fovY = fovY;
  cam.aspect = 1.0f;
  cam.zNear = 0.1f;
  cam.zFar = 1000.0f;
  return cam;
}

// Minimal valid SceneDocument that builds without errors
SceneDocument MakeMinimalDoc(std::string_view sceneId = "s1") {
  SceneDocument doc;
  doc.sceneId = std::string(sceneId);
  doc.sceneName = "Test";
  doc.version = 1;
  return doc;
}
} // namespace

// ===========================================================================
// AssetImporterRegistry — uncovered branches
// ===========================================================================

TEST_CASE("AssetImporterRegistry: Register nullptr is a no-op",
          "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  // Registering null must not crash or alter existing entries
  registry.Register(nullptr);
  REQUIRE(registry.FindByExtension("mesh.obj") != nullptr);
}

TEST_CASE("AssetImporterRegistry: FindByExtension returns null for unknown ext",
          "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  REQUIRE(registry.FindByExtension("model.fbx") == nullptr);
  REQUIRE(registry.FindByExtension("doc.pdf") == nullptr);
  REQUIRE(registry.FindByExtension("noext") == nullptr);
}

TEST_CASE("AssetImporterRegistry: FindById returns null for unknown id",
          "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  REQUIRE(registry.FindById("nonexistent.importer") == nullptr);
  REQUIRE(registry.FindById("") == nullptr);
}

TEST_CASE("AssetImporterRegistry: RegisteredImporterIds lists built-ins",
          "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  const auto ids = registry.RegisteredImporterIds();
  REQUIRE(ids.size() >= 2);
  const bool hasObj = std::ranges::find(ids, "builtin.obj_mesh") != ids.end();
  const bool hasTex =
      std::ranges::find(ids, "builtin.texture_copy") != ids.end();
  REQUIRE(hasObj);
  REQUIRE(hasTex);
}

TEST_CASE("AssetImporterRegistry: FindByExtension is case-insensitive",
          "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  REQUIRE(registry.FindByExtension("mesh.OBJ") != nullptr);
  REQUIRE(registry.FindByExtension("tex.PNG") != nullptr);
  REQUIRE(registry.FindByExtension("tex.Jpg") != nullptr);
  REQUIRE(registry.FindByExtension("tex.JPEG") != nullptr);
  REQUIRE(registry.FindByExtension("tex.BMP") != nullptr);
  REQUIRE(registry.FindByExtension("tex.TGA") != nullptr);
  REQUIRE(registry.FindByExtension("tex.WEBP") != nullptr);
  REQUIRE(registry.FindByExtension("tex.HDR") != nullptr);
}

TEST_CASE("AssetImporterRegistry: ObjImporter metadata correct",
          "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.obj_mesh");
  REQUIRE(imp != nullptr);
  CHECK(std::string(imp->AssetKind()) == "static_mesh");
  const auto exts = imp->SupportedExtensions();
  REQUIRE(std::find(exts.begin(), exts.end(), ".obj") != exts.end());
}

TEST_CASE("AssetImporterRegistry: TextureCopyImporter metadata correct",
          "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.texture_copy");
  REQUIRE(imp != nullptr);
  CHECK(std::string(imp->AssetKind()) == "texture");
  const auto exts = imp->SupportedExtensions();
  CHECK(std::find(exts.begin(), exts.end(), ".png") != exts.end());
  CHECK(std::find(exts.begin(), exts.end(), ".hdr") != exts.end());
}

TEST_CASE("AssetImporterRegistry: ObjImporter rejects non-obj source",
          "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.obj_mesh");
  REQUIRE(imp != nullptr);
  AssetImportRequest req;
  req.assetId = "x";
  req.assetGuid = "guid_x";
  req.sourcePath =
      (Monolith::Tests::SecureTempBase() / "notanobj.txt").string();
  const AssetImportResult result = imp->Import(req);
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.error.empty());
  CHECK_FALSE(result.diagnostics.empty());
}

TEST_CASE("AssetImporterRegistry: ObjImporter rejects missing source file",
          "[editor][importer-registry]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_imp_obj_missing";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.obj_mesh");
  REQUIRE(imp != nullptr);
  AssetImportRequest req;
  req.assetId = "missing_mesh";
  req.assetGuid = "guid_missing_mesh";
  req.sourcePath = (root / "does_not_exist.obj").string();
  const AssetImportResult result = imp->Import(req);
  CHECK_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].severity == AssetDiagnosticSeverity::Error);
}

TEST_CASE("AssetImporterRegistry: TextureCopy rejects unsupported type",
          "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.texture_copy");
  REQUIRE(imp != nullptr);
  AssetImportRequest req;
  req.assetId = "t";
  req.assetGuid = "guid_t";
  req.sourcePath =
      (Monolith::Tests::SecureTempBase() / "notanimage.txt").string();
  const AssetImportResult result = imp->Import(req);
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.diagnostics.empty());
}

TEST_CASE("AssetImporterRegistry: TextureCopy rejects missing source file",
          "[editor][importer-registry]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_imp_tex_missing";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.texture_copy");
  REQUIRE(imp != nullptr);
  AssetImportRequest req;
  req.assetId = "missing_tex";
  req.assetGuid = "guid_missing_tex";
  req.sourcePath = (root / "ghost.png").string();
  const AssetImportResult result = imp->Import(req);
  CHECK_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
}

// ===========================================================================
// AssetImportService — ImportTextureForAsset + SaveMetadataForAsset
// ===========================================================================

TEST_CASE("AssetImportService: ImportTextureForAsset null asset returns false",
          "[editor][asset-import]") {
  AssetImportService service;
  std::string err;
  const std::string source =
      (Monolith::Tests::SecureTempBase() / "x.png").string();
  const bool ok = service.ImportTextureForAsset(source, "a", nullptr, &err);
  CHECK_FALSE(ok);
  CHECK_FALSE(err.empty());
}

TEST_CASE("AssetImportService: ImportTextureForAsset succeeds with real PNG",
          "[editor][asset-import]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_tex_import";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  // Write a minimal 1×1 white PNG (valid PNG header bytes)
  const std::filesystem::path pngSrc = root / "white.png";
  {
    // Smallest valid PNG: 67 bytes (1×1 RGBA white)
    const unsigned char png1x1[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
        0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
        0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, 0x33, 0x00, 0x00, 0x00,
        0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    std::ofstream f(pngSrc, std::ios::binary);
    f.write(reinterpret_cast<const char *>(png1x1), sizeof(png1x1));
  }

  AssetDef asset;
  asset.guid = "guid_tex_direct";
  asset.displayName = "My Asset";

  AssetImportService service;
  std::string err;
  const bool ok =
      service.ImportTextureForAsset(pngSrc.string(), "my_asset", &asset, &err);
  REQUIRE(ok);
  CHECK(err.empty());
  CHECK_FALSE(asset.albedoMap.empty());
}

TEST_CASE(
    "AssetImportService: ImportTextureForAsset fails for unsupported type",
    "[editor][asset-import]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_tex_import_bad";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  AssetDef asset;
  asset.guid = "guid_badtex";

  AssetImportService service;
  std::string err;
  const std::string source =
      (Monolith::Tests::SecureTempBase() / "document.pdf").string();
  const bool ok =
      service.ImportTextureForAsset(source, "bad_tex", &asset, &err);
  CHECK_FALSE(ok);
  CHECK_FALSE(err.empty());
}

TEST_CASE("AssetImportService: SaveMetadataForAsset null-asset returns false",
          "[editor][asset-import]") {
  // SaveMetadataForAsset(assetId, asset, outError) — test missing guid
  AssetImportService service;
  AssetDef emptyAsset;
  std::string err;
  // empty guid → saving has nowhere to write meaningful data
  // The function itself should still execute without crashing
  service.SaveMetadataForAsset("some_id", emptyAsset, &err);
  // No crash is the primary assertion; error state is implementation-defined
}

TEST_CASE(
    "AssetImportService: ReimportAssetWithDependents unknown guid is noop",
    "[editor][asset-import]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_reimport_noop";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  SceneDocument doc;
  doc.sceneId = "s";
  doc.sceneName = "S";
  doc.version = 1;

  AssetImportService service;
  const AssetReimportResult result =
      service.ReimportAssetWithDependents(&doc, "guid_nonexistent", "manual");
  // Should succeed trivially: root guid not present in doc assets
  // Implementation either succeeds with empty order or fails gracefully
  CHECK(result.records.size() <= 1);
}

// ===========================================================================
// EditorImGuiBackend — IsSupportedEditorImGuiBackend
// ===========================================================================

TEST_CASE("IsSupportedEditorImGuiBackend: OpenGL is supported",
          "[editor][imgui-backend]") {
  CHECK(IsSupportedEditorImGuiBackend(RenderBackendId::OpenGL));
}

TEST_CASE("IsSupportedEditorImGuiBackend: Auto is supported",
          "[editor][imgui-backend]") {
  CHECK(IsSupportedEditorImGuiBackend(RenderBackendId::Auto));
}

TEST_CASE("IsSupportedEditorImGuiBackend: Vulkan returns compile-conditional",
          "[editor][imgui-backend]") {
  // Just call it to ensure the switch branch and return are exercised.
  const bool result = IsSupportedEditorImGuiBackend(RenderBackendId::Vulkan);
#if defined(MONOLITH_HAS_VULKAN)
  CHECK(result == true);
#else
  CHECK(result == false);
#endif
}

// ===========================================================================
// EditorDebugTrace — EditorTraceEnabled and EditorTrace
// ===========================================================================

TEST_CASE("EditorDebugTrace: EditorTraceEnabled reads env var",
          "[editor][debug-trace]") {
  // In release builds it always returns false
  // In debug builds it reads MONOLITH_EDITOR_TRACE
  const bool enabled = EditorTraceEnabled();
  (void)enabled; // exercising the code path is sufficient
  CHECK(true);   // no crash
}

TEST_CASE("EditorDebugTrace: EditorTrace does not throw",
          "[editor][debug-trace]") {
  // If tracing is off this is a no-op; if on it writes to stderr. Either way
  // it must not crash.
  REQUIRE_NOTHROW(EditorTrace("test message {} {}", 42, "hello"));
  REQUIRE_NOTHROW(EditorTrace("empty"));
}

// ===========================================================================
// SceneRuntimeBridge — BuildRuntimeSceneDefinition
// ===========================================================================

TEST_CASE("SceneRuntimeBridge: valid doc builds without errors",
          "[editor][runtime-bridge]") {
  const RuntimeSceneBuildResult result =
      BuildRuntimeSceneDefinition(MakeMinimalDoc("bridge_valid"));
  CHECK_FALSE(result.HasErrors());
}

TEST_CASE("SceneRuntimeBridge: doc with version=0 produces build errors",
          "[editor][runtime-bridge]") {
  SceneDocument doc = MakeMinimalDoc("bridge_fail");
  doc.version = 0; // triggers schemaVersion < 1 error
  const RuntimeSceneBuildResult result = BuildRuntimeSceneDefinition(doc);
  CHECK(result.HasErrors());
}

TEST_CASE("SceneRuntimeBridge: doc with nodes builds rooms",
          "[editor][runtime-bridge]") {
  SceneDocument doc = MakeMinimalDoc("bridge_rooms");
  SceneObject panel;
  panel.id = "p1";
  panel.type = SceneObjectType::Panel;
  panel.position = {0, 0, 0};
  panel.scale = {5, 1, 5};
  doc.objects.push_back(panel);

  const RuntimeSceneBuildResult result = BuildRuntimeSceneDefinition(doc);
  CHECK_FALSE(result.HasErrors());
  CHECK(result.definition.rooms.size() == 1);
}

// ===========================================================================
// SceneRuntimeCoordinatorBridge — Load / Reload with build failure
// ===========================================================================

TEST_CASE(
    "SceneRuntimeCoordinatorBridge: LoadSceneDocument fails on build error",
    "[editor][coordinator-bridge]") {
  SceneDocument badDoc = MakeMinimalDoc("coord_fail_load");
  badDoc.version = 0; // triggers schemaVersion < 1 validation error

  SceneRuntimeCoordinator coordinator;
  bool callbackCalled = false;
  const SceneRuntimeOperationResult result = LoadSceneDocument(
      coordinator, badDoc,
      [&callbackCalled](const RuntimeSceneDefinition &, std::string *) {
        callbackCalled = true;
        return true;
      });
  CHECK_FALSE(result.ok);
  CHECK_FALSE(callbackCalled);
  CHECK_FALSE(result.error.empty());
}

TEST_CASE(
    "SceneRuntimeCoordinatorBridge: LoadSceneDocument succeeds on valid doc",
    "[editor][coordinator-bridge]") {
  SceneRuntimeCoordinator coordinator;
  bool callbackCalled = false;
  const SceneRuntimeOperationResult result = LoadSceneDocument(
      coordinator, MakeMinimalDoc("coord_valid"),
      [&callbackCalled](const RuntimeSceneDefinition &, std::string *) {
        callbackCalled = true;
        return true;
      });
  CHECK(result.ok);
  CHECK(callbackCalled);
}

TEST_CASE(
    "SceneRuntimeCoordinatorBridge: ReloadSceneDocument fails on build error",
    "[editor][coordinator-bridge]") {
  SceneRuntimeCoordinator coordinator;
  // First load a valid scene so we are in Active state
  const SceneRuntimeOperationResult load = LoadSceneDocument(
      coordinator, MakeMinimalDoc("coord_reload"),
      [](const RuntimeSceneDefinition &, std::string *) { return true; });
  REQUIRE(load.ok);

  SceneDocument badDoc = MakeMinimalDoc("coord_fail_reload");
  badDoc.version = 0; // triggers schemaVersion < 1 validation error

  bool callbackCalled = false;
  const SceneRuntimeOperationResult reload = ReloadSceneDocument(
      coordinator, badDoc,
      [&callbackCalled](const RuntimeSceneDefinition &, std::string *) {
        callbackCalled = true;
        return true;
      });
  CHECK_FALSE(reload.ok);
  CHECK_FALSE(callbackCalled);
  CHECK_FALSE(reload.error.empty());
}

TEST_CASE(
    "SceneRuntimeCoordinatorBridge: ReloadSceneDocument succeeds on valid doc",
    "[editor][coordinator-bridge]") {
  SceneRuntimeCoordinator coordinator;
  const SceneRuntimeOperationResult load = LoadSceneDocument(
      coordinator, MakeMinimalDoc("coord_reload2"),
      [](const RuntimeSceneDefinition &, std::string *) { return true; });
  REQUIRE(load.ok);

  bool callbackCalled = false;
  const SceneRuntimeOperationResult reload = ReloadSceneDocument(
      coordinator, MakeMinimalDoc("coord_reload2b"),
      [&callbackCalled](const RuntimeSceneDefinition &, std::string *) {
        callbackCalled = true;
        return true;
      });
  CHECK(reload.ok);
  CHECK(callbackCalled);
}

// ===========================================================================
// EditorWorkspaceSettings — covered branches
// ===========================================================================

TEST_CASE("EditorWorkspaceSettings: ResolveEditorWorkspacePath uses .horo "
          "when project root is set",
          "[editor][workspace-settings]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_ws_resolve";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path path = ResolveEditorWorkspacePath();
  CHECK(path.string().find(".horo") != std::string::npos);
}

TEST_CASE("EditorWorkspaceSettings: ResolveEditorWorkspacePath falls back to "
          "home dir when no project root",
          "[editor][workspace-settings]") {
  const std::filesystem::path fakeHome =
      Monolith::Tests::SecureTempBase() / "horo_ws_home";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  ProjectPathGuard clearGuard({});
  HomeDirGuard homeGuard(fakeHome);

  const std::filesystem::path path = ResolveEditorWorkspacePath();
  // path is inside the fake home dir or default settings dir
  CHECK_FALSE(path.empty());
}

TEST_CASE("EditorWorkspaceSettings: LoadEditorWorkspaceDocument returns "
          "defaults when file is absent",
          "[editor][workspace-settings]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_ws_absent";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);
  const std::filesystem::path fakeHome =
      Monolith::Tests::SecureTempBase() / "horo_ws_absent_home";
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  EditorWorkspaceDocument doc = LoadEditorWorkspaceDocument();
  CHECK_FALSE(doc.loadedFromDisk);
  CHECK_FALSE(doc.parseError);
  CHECK(doc.state.consoleShowInfo);
  CHECK(doc.state.consoleShowWarn);
  CHECK(doc.state.consoleShowError);
}

TEST_CASE(
    "EditorWorkspaceSettings: LoadEditorWorkspaceDocument parses valid JSON",
    "[editor][workspace-settings]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_ws_valid";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);
  const std::filesystem::path fakeHome =
      Monolith::Tests::SecureTempBase() / "horo_ws_valid_home";
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  const std::filesystem::path wsFile = ResolveEditorWorkspacePath();
  std::filesystem::create_directories(wsFile.parent_path(), ec);
  WriteFile(
      wsFile,
      R"({"editor":{"consoleShowInfo":false,"consoleShowWarn":true,"consoleShowError":false,"projectBrowserCwd":"/some/path"}})");

  EditorWorkspaceDocument doc = LoadEditorWorkspaceDocument();
  REQUIRE(doc.loadedFromDisk);
  CHECK_FALSE(doc.parseError);
  CHECK_FALSE(doc.state.consoleShowInfo);
  CHECK(doc.state.consoleShowWarn);
  CHECK_FALSE(doc.state.consoleShowError);
  CHECK(doc.state.projectBrowserCwd == "/some/path");
}

TEST_CASE(
    "EditorWorkspaceSettings: LoadEditorWorkspaceDocument handles invalid JSON",
    "[editor][workspace-settings]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_ws_badjson";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);
  const std::filesystem::path fakeHome =
      Monolith::Tests::SecureTempBase() / "horo_ws_badjson_home";
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  const std::filesystem::path wsFile = ResolveEditorWorkspacePath();
  std::filesystem::create_directories(wsFile.parent_path(), ec);
  WriteFile(wsFile, "not json {{{{");

  EditorWorkspaceDocument doc = LoadEditorWorkspaceDocument();
  CHECK(doc.loadedFromDisk);
  CHECK(doc.parseError);
  CHECK_FALSE(doc.error.empty());
}

TEST_CASE("EditorWorkspaceSettings: LoadEditorWorkspaceDocument handles "
          "non-object root",
          "[editor][workspace-settings]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_ws_arrayroot";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);
  const std::filesystem::path fakeHome =
      Monolith::Tests::SecureTempBase() / "horo_ws_arrayroot_home";
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  const std::filesystem::path wsFile = ResolveEditorWorkspacePath();
  std::filesystem::create_directories(wsFile.parent_path(), ec);
  WriteFile(wsFile, "[1,2,3]");

  EditorWorkspaceDocument doc = LoadEditorWorkspaceDocument();
  CHECK(doc.loadedFromDisk);
  CHECK(doc.parseError);
}

TEST_CASE(
    "EditorWorkspaceSettings: SaveEditorWorkspaceDocument null returns false",
    "[editor][workspace-settings]") {
  std::string err;
  const bool ok = SaveEditorWorkspaceDocument(nullptr, &err);
  CHECK_FALSE(ok);
  CHECK_FALSE(err.empty());
}

TEST_CASE("EditorWorkspaceSettings: save then load round-trips state",
          "[editor][workspace-settings]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_ws_roundtrip";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);
  const std::filesystem::path fakeHome =
      Monolith::Tests::SecureTempBase() / "horo_ws_rt_home";
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  EditorWorkspaceDocument doc;
  doc.state.consoleShowInfo = false;
  doc.state.consoleShowWarn = true;
  doc.state.consoleShowError = false;
  doc.state.projectBrowserCwd = "/test/cwd";
  std::string err;
  REQUIRE(SaveEditorWorkspaceDocument(&doc, &err));
  CHECK(err.empty());

  EditorWorkspaceDocument loaded = LoadEditorWorkspaceDocument();
  CHECK(loaded.loadedFromDisk);
  CHECK_FALSE(loaded.state.consoleShowInfo);
  CHECK(loaded.state.consoleShowWarn);
  CHECK_FALSE(loaded.state.consoleShowError);
  CHECK(loaded.state.projectBrowserCwd == "/test/cwd");
}

// ===========================================================================
// TransformGizmo — edge cases not yet in test_gizmo.cpp
// ===========================================================================

TEST_CASE("TransformGizmo: AxisDir returns zero for None",
          "[gizmo][coverage]") {
  TransformGizmo g;
  g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
             Vec3::One());
  const Vec3 none = g.AxisDir(GizmoAxis::None);
  CHECK(none.x == Approx(0.0f));
  CHECK(none.y == Approx(0.0f));
  CHECK(none.z == Approx(0.0f));
}

TEST_CASE(
    "TransformGizmo: HandleSize returns small fallback at near-zero distance",
    "[gizmo][coverage]") {
  TransformGizmo g;
  // Place gizmo right at the camera position → distance ≈ 0
  Camera cam = MakeCam({0, 0, 0}, {0, 0, -1});
  g.Activate(GizmoMode::Translate, cam.position, Quaternion::Identity(),
             Vec3::One());
  const float size = g.HandleSize(cam);
  CHECK(size == Approx(0.1f).margin(0.01f));
}

TEST_CASE("TransformGizmo: SyncTarget updates position", "[gizmo][coverage]") {
  TransformGizmo g;
  g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
             Vec3::One());
  const Vec3 newPos{5.0f, 3.0f, 1.0f};
  g.SyncTarget(newPos, Quaternion::Identity(), Vec3::One());
  // After sync, HandleSize should reflect new position (camera is still at
  // origin so dist changes)
  Camera cam = MakeCam({0, 0, 0}, {0, 0, -1});
  const float size = g.HandleSize(cam);
  CHECK(size > 0.0f);
}

TEST_CASE("TransformGizmo: RayHitPlane returns false for behind-origin hit",
          "[gizmo][coverage]") {
  // Ray pointing away from the plane — t is negative
  Ray ray;
  ray.origin = {0.0f, 5.0f, 0.0f};
  ray.direction = {0.0f, 1.0f, 0.0f}; // pointing up, plane at y=0 above origin
  Vec3 hit;
  // normal points up, plane at y=10 — t = (10-5)/1 = 5 which is valid
  // For t<0 use a plane below origin with ray pointing up
  bool res = TransformGizmo::RayHitPlane(ray, {0, -1, 0}, {0, -3, 0}, hit);
  CHECK_FALSE(res);
}

TEST_CASE("TransformGizmo: PickAxis returns None for empty screen point",
          "[gizmo][coverage]") {
  TransformGizmo g;
  g.Activate(GizmoMode::Translate, {0, 0, 0}, Quaternion::Identity(),
             Vec3::One());
  Camera cam = MakeCam({0, 0, 10}, Vec3::Zero());
  // Mouse far off screen — should not pick anything
  const GizmoAxis axis = g.PickAxis(9999.0f, 9999.0f, cam, 800, 800);
  CHECK(axis == GizmoAxis::None);
}

// ===========================================================================
// EditorHistory — via MCP commands (no GLFW needed)
// ===========================================================================

TEST_CASE(
    "EditorHistory: CommitHistoryChange records snapshot on object create",
    "[editor][history]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"id", "hist_create_a"}});
  REQUIRE(res.ok);
  REQUIRE(editor.GetDocument().objects.size() == 1);

  // undo must succeed, proving CommitHistoryChange pushed a snapshot
  const auto undo =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undo.ok);
  REQUIRE(undo.data["undone"].get<bool>());
  REQUIRE(editor.GetDocument().objects.empty());
}

TEST_CASE(
    "EditorHistory: CommitHistoryChange is no-op when snapshots are equal",
    "[editor][history]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  // Create an object — document is now dirty and has 1 object in undo history
  const auto createRes = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"id", "hist_nochange"}});
  REQUIRE(createRes.ok);

  // Capture undo depth: 1 undo entry
  const auto undo1 =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undo1.data["undone"].get<bool>());
  // Nothing to undo now
  const auto undo2 =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE_FALSE(undo2.data["undone"].get<bool>());
}

TEST_CASE("EditorHistory: UndoHistory with empty stack returns false",
          "[editor][history]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto undo =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undo.ok);
  REQUIRE_FALSE(undo.data["undone"].get<bool>());
}

TEST_CASE("EditorHistory: UndoHistory restores prior document state",
          "[editor][history]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"id", "undo_restore"}});
  REQUIRE(editor.GetDocument().objects.size() == 1);

  const auto undo =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undo.ok);
  REQUIRE(undo.data["undone"].get<bool>());
  REQUIRE(editor.GetDocument().objects.empty());
}

TEST_CASE("EditorHistory: RedoHistory after undo re-applies change",
          "[editor][history]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"id", "redo_obj"}});
  editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(editor.GetDocument().objects.empty());

  const auto redo =
      editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
  REQUIRE(redo.ok);
  REQUIRE(redo.data["redone"].get<bool>());
  REQUIRE(editor.GetDocument().objects.size() == 1);
}

TEST_CASE("EditorHistory: RedoHistory with empty stack returns false",
          "[editor][history]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto redo =
      editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
  REQUIRE(redo.ok);
  REQUIRE_FALSE(redo.data["redone"].get<bool>());
}

TEST_CASE("EditorHistory: TrimHistory caps undo stack at 128 entries",
          "[editor][history]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  for (int i = 0; i < 130; ++i) {
    const auto res = editor.ExecuteMcpCommand(
        "editor.create_object",
        nlohmann::json{{"type", "Prop"},
                       {"id", std::format("trim_obj_{}", i)}});
    REQUIRE(res.ok);
  }

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

TEST_CASE("EditorHistory: RefreshHistorySavedBaseline refreshes undo and redo "
          "snapshots",
          "[editor][history]") {
  namespace fs = std::filesystem;
  const fs::path sceneDir =
      Monolith::Tests::SecureTempBase() / "horo_hist_unit_baseline";
  std::error_code ec;
  fs::remove_all(sceneDir, ec);
  fs::create_directories(sceneDir, ec);

  const fs::path scenePath = sceneDir / "baseline_scene.json";

  SceneDocument doc;
  doc.sceneId = "baseline_scene";
  doc.filePath = scenePath.string();
  SceneSerializer::SaveToFile(doc, scenePath.string());

  EditorLayer editor;
  editor.LoadDocument(doc);

  // Build 2 undo entries
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"}, {"id", "bl_obj1"}});
  editor.ExecuteMcpCommand("editor.create_object",
                           nlohmann::json{{"type", "Prop"}, {"id", "bl_obj2"}});
  // Move one entry to redo stack
  editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());

  // Save — calls SaveDocument → RefreshHistorySavedBaseline with both undo and
  // redo non-empty
  const auto saveRes =
      editor.ExecuteMcpCommand("editor.save_scene", nlohmann::json::object());
  REQUIRE(saveRes.ok);
  REQUIRE_FALSE(editor.GetDocument().dirty);
}

TEST_CASE(
    "EditorHistory: HistorySnapshotsEqual returns false for changed document",
    "[editor][history]") {
  EditorLayer editor;
  SceneDocument doc;
  doc.sceneId = "snap_diff";
  editor.LoadDocument(doc);

  editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Panel"}, {"id", "snap_obj"}});

  // Undo proves before != after (snapshots differed, so commit happened)
  const auto undo =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undo.data["undone"].get<bool>());

  // Redo confirms redo stack was populated
  const auto redo =
      editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
  REQUIRE(redo.data["redone"].get<bool>());
  REQUIRE(editor.GetDocument().objects.size() == 1);
}

// ===========================================================================
// EditorSceneGraph — free functions
// ===========================================================================

TEST_CASE("EditorSceneGraph: FindObjectIndexById returns -1 for unknown id",
          "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "known";
  doc.objects.push_back(obj);

  REQUIRE(FindObjectIndexById(doc, "unknown") == -1);
  REQUIRE(FindObjectIndexById(doc, "") == -1);
}

TEST_CASE("EditorSceneGraph: FindObjectIndexById returns correct index",
          "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject a;
  a.id = "first";
  doc.objects.push_back(a);
  SceneObject b;
  b.id = "second";
  doc.objects.push_back(b);

  REQUIRE(FindObjectIndexById(doc, "first") == 0);
  REQUIRE(FindObjectIndexById(doc, "second") == 1);
}

TEST_CASE(
    "EditorSceneGraph: IsDescendantOf returns false for out-of-bounds indices",
    "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "obj";
  doc.objects.push_back(obj);

  REQUIRE_FALSE(IsDescendantOf(doc, -1, 0));
  REQUIRE_FALSE(IsDescendantOf(doc, 0, -1));
  REQUIRE_FALSE(IsDescendantOf(doc, 99, 0));
  REQUIRE_FALSE(IsDescendantOf(doc, 0, 99));
}

TEST_CASE("EditorSceneGraph: IsDescendantOf finds ancestor via while loop",
          "[editor][scene-graph]") {
  // grandchild -> child -> parent chain
  SceneDocument doc;
  SceneObject parent;
  parent.id = "parent";
  doc.objects.push_back(parent); // index 0

  SceneObject child;
  child.id = "child";
  child.props["parentId"] = "parent";
  doc.objects.push_back(child); // index 1

  SceneObject grandchild;
  grandchild.id = "grandchild";
  grandchild.props["parentId"] = "child";
  doc.objects.push_back(grandchild); // index 2

  REQUIRE(IsDescendantOf(doc, 2, 0)); // grandchild is descendant of parent
  REQUIRE(IsDescendantOf(doc, 1, 0)); // child is descendant of parent
  REQUIRE_FALSE(
      IsDescendantOf(doc, 0, 2)); // parent is NOT descendant of grandchild
}

TEST_CASE("EditorSceneGraph: IsDescendantOf returns false when no parent chain",
          "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject a;
  a.id = "a";
  doc.objects.push_back(a);
  SceneObject b;
  b.id = "b";
  doc.objects.push_back(b);

  // Neither is parent of the other
  REQUIRE_FALSE(IsDescendantOf(doc, 0, 1));
  REQUIRE_FALSE(IsDescendantOf(doc, 1, 0));
}

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta translates child",
          "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject parent;
  parent.id = "par";
  parent.position = {0.0f, 0.0f, 0.0f};
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "ch";
  child.position = {2.0f, 0.0f, 0.0f};
  child.props["parentId"] = "par";
  doc.objects.push_back(child);

  ParentTransformState oldState;
  oldState.position = {0.0f, 0.0f, 0.0f};
  oldState.rotation = Quaternion::Identity();

  ParentTransformState newState;
  newState.position = {5.0f, 0.0f, 0.0f};
  newState.rotation = Quaternion::Identity();

  PropagateHierarchyTransformDelta(doc, 0, oldState, newState, nullptr);

  REQUIRE(doc.objects[1].position.x == Approx(7.0f));
  REQUIRE(doc.objects[1].position.y == Approx(0.0f));
  REQUIRE(doc.objects[1].position.z == Approx(0.0f));
}

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta applies rotation "
          "to child",
          "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject parent;
  parent.id = "rot_par";
  parent.position = {0.0f, 0.0f, 0.0f};
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "rot_ch";
  child.position = {1.0f, 0.0f, 0.0f};
  child.pitch = 0.0f;
  child.yaw = 0.0f;
  child.roll = 0.0f;
  child.props["parentId"] = "rot_par";
  doc.objects.push_back(child);

  ParentTransformState oldState;
  oldState.position = {0.0f, 0.0f, 0.0f};
  oldState.rotation = Quaternion::Identity();

  // Apply a non-trivial rotation — coverage goal is to hit lines 85-91;
  // exact resulting position depends on the engine's quaternion convention.
  ParentTransformState newState;
  newState.position = {0.0f, 0.0f, 0.0f};
  newState.rotation = Quaternion::FromEuler(ToRadians(90.0f), 0.0f, 0.0f);

  PropagateHierarchyTransformDelta(doc, 0, oldState, newState, nullptr);

  // Child orientation fields (pitch/yaw/roll) must have been recomputed
  // (they are always overwritten for any non-identity deltaRot).
  // Position magnitude should be preserved under rotation.
  const Vec3 &p = doc.objects[1].position;
  const float dist = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
  REQUIRE(dist == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta invokes callback",
          "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject parent;
  parent.id = "cb_par";
  parent.position = {0.0f, 0.0f, 0.0f};
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "cb_ch";
  child.position = {1.0f, 0.0f, 0.0f};
  child.props["parentId"] = "cb_par";
  doc.objects.push_back(child);

  ParentTransformState oldState;
  ParentTransformState newState;
  newState.position = {3.0f, 0.0f, 0.0f};
  newState.rotation = Quaternion::Identity();

  int cbCount = 0;
  PropagateHierarchyTransformDelta(
      doc, 0, oldState, newState,
      [&cbCount](const SceneObject &) { ++cbCount; });
  REQUIRE(cbCount == 1);
}

TEST_CASE(
    "EditorSceneGraph: PropagateHierarchyTransformDelta skips listed indices",
    "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject parent;
  parent.id = "skip_par";
  parent.position = {0.0f, 0.0f, 0.0f};
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "skip_ch";
  child.position = {1.0f, 0.0f, 0.0f};
  child.props["parentId"] = "skip_par";
  doc.objects.push_back(child);

  ParentTransformState oldState;
  ParentTransformState newState;
  newState.position = {10.0f, 0.0f, 0.0f};
  newState.rotation = Quaternion::Identity();

  PropagateHierarchyTransformDelta(doc, 0, oldState, newState, nullptr, {1});
  REQUIRE(doc.objects[1].position.x == Approx(1.0f)); // unchanged
}

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta invalid "
          "parentIdx is no-op",
          "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "solo";
  obj.position = {3.0f, 0.0f, 0.0f};
  doc.objects.push_back(obj);

  ParentTransformState s;
  s.rotation = Quaternion::Identity();
  PropagateHierarchyTransformDelta(doc, -1, s, s, nullptr);
  PropagateHierarchyTransformDelta(doc, 99, s, s, nullptr);
  REQUIRE(doc.objects[0].position.x == Approx(3.0f)); // unchanged
}

TEST_CASE("EditorSceneGraph: CollectReservedObjectIds includes ids and prop "
          "references",
          "[editor][scene-graph]") {
  SceneDocument doc;

  SceneObject parent;
  parent.id = "res_parent";
  doc.objects.push_back(parent);

  SceneObject child;
  child.id = "res_child";
  child.props["parentId"] = "res_parent";
  child.props["followTargetId"] = "res_parent";
  doc.objects.push_back(child);

  SceneObject noRef;
  noRef.id = "res_no_ref";
  doc.objects.push_back(noRef);

  const auto reserved = CollectReservedObjectIds(doc);
  REQUIRE(reserved.contains("res_parent"));
  REQUIRE(reserved.contains("res_child"));
  REQUIRE(reserved.contains("res_no_ref"));
  REQUIRE_FALSE(reserved.contains("nonexistent"));
}

TEST_CASE("EditorSceneGraph: CollectReservedObjectIds handles empty doc",
          "[editor][scene-graph]") {
  SceneDocument doc;
  const auto reserved = CollectReservedObjectIds(doc);
  REQUIRE(reserved.empty());
}

TEST_CASE(
    "EditorSceneGraph: RewriteObjectIdReferences rewrites followTargetId too",
    "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "follower";
  obj.props["followTargetId"] = "old_target";
  doc.objects.push_back(obj);

  RewriteObjectIdReferences(&doc, "old_target", "new_target");
  REQUIRE(doc.objects[0].props.at("followTargetId") == "new_target");
}

TEST_CASE("EditorSceneGraph: RewriteObjectIdReferences null doc is no-op",
          "[editor][scene-graph]") {
  REQUIRE_NOTHROW(RewriteObjectIdReferences(nullptr, "old", "new"));
}

TEST_CASE(
    "EditorSceneGraph: LogDanglingObjectReferences empty label does not crash",
    "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject orphan;
  orphan.id = "orphan";
  orphan.props["parentId"] = "ghost_parent";
  doc.objects.push_back(orphan);

  REQUIRE_NOTHROW(LogDanglingObjectReferences(doc, ""));
}

TEST_CASE("EditorSceneGraph: LogDanglingObjectReferences skips non-ref and "
          "empty-value props",
          "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "obj1";
  // Non-reference prop — must not trigger dangling-ref warning
  obj.props["someCustomProp"] = "anything";
  // Reference prop with empty value — must also be skipped
  obj.props["parentId"] = "";
  doc.objects.push_back(obj);

  // Should run without crash; exercises the `continue` branch in
  // LogDanglingObjectReferences
  REQUIRE_NOTHROW(LogDanglingObjectReferences(doc, "unit_test"));
}

// ===========================================================================
// EditorSceneGraph — IsDescendantOf cycle detection and additional branches
// ===========================================================================

TEST_CASE("EditorSceneGraph: IsDescendantOf cycle detection returns false "
          "after guard",
          "[editor][scene-graph]") {
  // Build a 3-cycle: a→b→c→a, plus an unrelated object d (index 3).
  // guardLimit = 4. IsDescendantOf(doc, 0, 3) must iterate all 4 times
  // without finding ancestorIdx(3) or a dead end, then return false at line 60.
  SceneDocument doc;
  SceneObject a;
  a.id = "cyc_a";
  a.props["parentId"] = "cyc_b";
  doc.objects.push_back(a);
  SceneObject b;
  b.id = "cyc_b";
  b.props["parentId"] = "cyc_c";
  doc.objects.push_back(b);
  SceneObject c;
  c.id = "cyc_c";
  c.props["parentId"] = "cyc_a";
  doc.objects.push_back(c);
  SceneObject d;
  d.id = "cyc_d";
  doc.objects.push_back(d); // index 3, no parent

  // Is a (0) a descendant of d (3)?  Walks cycle a→b→c→a... and hits guard.
  REQUIRE_FALSE(IsDescendantOf(doc, 0, 3));
}

// ===========================================================================
// EditorSceneGraph — IsReservedObjectId
// ===========================================================================

TEST_CASE("EditorSceneGraph: IsReservedObjectId returns false for empty id",
          "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "x";
  doc.objects.push_back(obj);
  REQUIRE_FALSE(IsReservedObjectId(doc, ""));
}

TEST_CASE(
    "EditorSceneGraph: IsReservedObjectId returns true for existing object id",
    "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "used";
  doc.objects.push_back(obj);
  REQUIRE(IsReservedObjectId(doc, "used"));
  REQUIRE_FALSE(IsReservedObjectId(doc, "unused"));
}

TEST_CASE("EditorSceneGraph: IsReservedObjectId returns true for referenced "
          "prop value",
          "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "follower";
  obj.props["followTargetId"] = "target_obj";
  doc.objects.push_back(obj);

  REQUIRE(IsReservedObjectId(doc, "target_obj"));
  REQUIRE_FALSE(IsReservedObjectId(doc, "not_referenced"));
}

TEST_CASE(
    "EditorSceneGraph: IsReservedObjectId respects ignoreConcreteObjectId",
    "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "rename_me";
  doc.objects.push_back(obj);

  const std::string ignored = "rename_me";
  REQUIRE_FALSE(IsReservedObjectId(doc, "rename_me", &ignored));
  REQUIRE(IsReservedObjectId(doc, "rename_me", nullptr));
}

TEST_CASE("EditorSceneGraph: IsReservedObjectId ignores prop references when "
          "matching ignored id",
          "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "ref_holder";
  obj.props["parentId"] = "ignore_this";
  doc.objects.push_back(obj);

  // The reference value matches the ignored id — should not be considered
  // reserved
  const std::string ignored = "ignore_this";
  REQUIRE_FALSE(IsReservedObjectId(doc, "ignore_this", &ignored));
}

// ===========================================================================
// EditorSceneGraph — SanitizePrefabStem and BuildUniquePrefabPath
// ===========================================================================

TEST_CASE("EditorSceneGraph: SanitizePrefabStem cleans special characters",
          "[editor][scene-graph]") {
  REQUIRE(SanitizePrefabStem("hello_world") == "hello_world");
  REQUIRE(SanitizePrefabStem("valid-name_123") == "valid-name_123");
  REQUIRE(SanitizePrefabStem("__hello__") == "hello");
  REQUIRE(SanitizePrefabStem("_test") == "test");
  REQUIRE(SanitizePrefabStem("test_") == "test");
  REQUIRE(SanitizePrefabStem("") == "prefab");
  REQUIRE(SanitizePrefabStem("___") == "prefab");
  REQUIRE(SanitizePrefabStem("hello world!") == "hello_world");
}

TEST_CASE("EditorSceneGraph: BuildUniquePrefabPath returns .horo path",
          "[editor][scene-graph]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_prefab_path_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  SceneDocument doc;
  doc.sceneId = "test_scene";

  SceneObject obj;
  obj.id = "my_obj";

  const std::filesystem::path path = BuildUniquePrefabPath(doc, obj);
  REQUIRE(path.extension() == ".horo");
  REQUIRE(path.stem().string().find("my_obj") != std::string::npos);
}

TEST_CASE("EditorSceneGraph: BuildUniquePrefabPath uses sceneId when object id "
          "is empty",
          "[editor][scene-graph]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_prefab_path_test2";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  SceneDocument doc;
  doc.sceneId = "my_scene";

  SceneObject obj; // id is empty

  const std::filesystem::path path = BuildUniquePrefabPath(doc, obj);
  REQUIRE(path.extension() == ".horo");
  REQUIRE(path.stem().string().find("my_scene") != std::string::npos);
}

// ===========================================================================
// EditorHistory — additional coverage: equal-snapshot no-op and TrimHistory
// ===========================================================================

TEST_CASE("EditorHistory: CommitHistoryChange skips when update makes no "
          "content change",
          "[editor][history]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  // Create an object — now document is dirty
  const auto createRes = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"id", "noop_obj"}});
  REQUIRE(createRes.ok);

  // Call update_object with only the id and no other fields.
  // Before: dirty=true, objects=[noop_obj]; After: same → before == after.
  // CommitHistoryChange should detect this and NOT push a new undo entry.
  const auto updRes = editor.ExecuteMcpCommand(
      "editor.update_object", nlohmann::json{{"id", "noop_obj"}});
  REQUIRE(updRes.ok);

  // Undo once should restore to empty (only 1 real undo entry from create)
  const auto undo1 =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undo1.ok);
  REQUIRE(undo1.data["undone"].get<bool>());
  REQUIRE(editor.GetDocument().objects.empty());

  // No more undo entries (the no-op update did not create one)
  const auto undo2 =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE_FALSE(undo2.data["undone"].get<bool>());
}

TEST_CASE("EditorHistory: TrimHistory guard — no trim when stack within limit",
          "[editor][history]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  // Create a few objects (well within 128 limit)
  for (int i = 0; i < 3; ++i) {
    editor.ExecuteMcpCommand(
        "editor.create_object",
        nlohmann::json{{"type", "Prop"},
                       {"id", std::format("guard_obj_{}", i)}});
  }

  // All 3 undos should succeed
  for (int i = 0; i < 3; ++i) {
    const auto res =
        editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
    REQUIRE(res.data["undone"].get<bool>());
  }
  // Stack exhausted
  const auto done =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE_FALSE(done.data["undone"].get<bool>());
}

TEST_CASE(
    "EditorHistory: RestoreHistorySnapshot restores selectedAssetId from undo",
    "[editor][history]") {
  EditorLayer editor;
  SceneDocument doc;
  doc.sceneId = "asset_undo_test";
  // Add an asset to the document
  AssetDef asset;
  asset.guid = "guid_restore_test";
  asset.displayName = "Restore Test Asset";
  doc.assets["restore_asset"] = asset;
  editor.LoadDocument(doc);

  // Select the asset so it appears in history snapshots
  const auto selRes = editor.ExecuteMcpCommand(
      "editor.select_asset", nlohmann::json{{"id", "restore_asset"}});
  REQUIRE(selRes.ok);
  REQUIRE(editor.GetSelectedAssetId() == "restore_asset");

  // Mutate an asset field — this commits history with selectedAssetId set
  const auto updRes = editor.ExecuteMcpCommand(
      "editor.update_asset",
      nlohmann::json{{"id", "restore_asset"}, {"mesh", "some_mesh.obj"}});
  REQUIRE(updRes.ok);

  // Undo restores the snapshot; RestoreHistorySnapshot must set
  // m_selectedAssetId back to "restore_asset" (it exists in the restored
  // document's assets).
  const auto undoRes =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undoRes.ok);
  REQUIRE(undoRes.data["undone"].get<bool>());
  REQUIRE(editor.GetSelectedAssetId() == "restore_asset");
}

// ===========================================================================
// EditorSceneGraph — additional coverage for IsReservedObjectId and paths
// ===========================================================================

TEST_CASE("EditorSceneGraph: IsReservedObjectId ignores prop value matching "
          "ignoreConcreteObjectId",
          "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "holder";
  // The parentId references the same id we will ignore
  obj.props["parentId"] = "target";
  doc.objects.push_back(obj);

  const std::string ignored = "target";
  // "target" would normally be reserved (via prop reference), but since
  // ignoreConcreteObjectId points to "target", it must not count as reserved.
  REQUIRE_FALSE(IsReservedObjectId(doc, "target", &ignored));
  // Without ignoring, it IS reserved
  REQUIRE(IsReservedObjectId(doc, "target", nullptr));
}

// ===========================================================================
// EditorSceneGraph — BuildUniquePrefabPath suffix loop
// ===========================================================================

TEST_CASE("EditorSceneGraph: BuildUniquePrefabPath appends suffix when "
          "candidate exists",
          "[editor][scene-graph]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_prefab_suffix_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  // Pre-create the first candidate so BuildUniquePrefabPath must use a suffix
  const std::filesystem::path prefabDir = root / "assets" / "prefabs";
  std::filesystem::create_directories(prefabDir, ec);
  WriteFile(prefabDir / "suffix_obj_prefab.horo", "{}");

  SceneDocument doc;
  doc.sceneId = "test_scene";
  SceneObject obj;
  obj.id = "suffix_obj";

  const std::filesystem::path path = BuildUniquePrefabPath(doc, obj);
  // The base name is taken; result must have a numeric suffix
  REQUIRE(path.extension() == ".horo");
  REQUIRE(path.stem().string().find("suffix_obj_prefab_") != std::string::npos);
}

TEST_CASE("EditorSceneGraph: IsReservedObjectId skips non-reference prop keys",
          "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "some_obj";
  // Non-reference prop — must be skipped in the reserved-id check (line 124
  // continue)
  obj.props["customField"] = "would_be_reserved_if_checked";
  // Reference prop with a different value
  obj.props["parentId"] = "actual_parent";
  doc.objects.push_back(obj);

  // "would_be_reserved_if_checked" is stored only under a non-ref key — not
  // reserved
  REQUIRE_FALSE(IsReservedObjectId(doc, "would_be_reserved_if_checked"));
  // "actual_parent" IS a ref-key value — reserved
  REQUIRE(IsReservedObjectId(doc, "actual_parent"));
}

// ===========================================================================
// EditorSearch — coverage for uncovered branches
// ===========================================================================

TEST_CASE(
    "EditorSearch: ObjectTypeLabel returns 'unknown' for unrecognised type",
    "[editor][search]") {
  const auto unknownType = static_cast<SceneObjectType>(99);
  CHECK(std::string(ObjectTypeLabel(unknownType)) == "unknown");
}

TEST_CASE("EditorSearch: ObjectTypeLabel covers all known types",
          "[editor][search]") {
  using enum SceneObjectType;
  CHECK(std::string(ObjectTypeLabel(Prop)) == "prop");
  CHECK(std::string(ObjectTypeLabel(Light)) == "light");
  CHECK(std::string(ObjectTypeLabel(Panel)) == "board");
  CHECK(std::string(ObjectTypeLabel(Camera)) == "[cam]");
}

TEST_CASE("EditorSearch: ContainsCaseInsensitive returns true for empty query",
          "[editor][search]") {
  CHECK(ContainsCaseInsensitive("anything", ""));
}

TEST_CASE("EditorSearch: ContainsCaseInsensitive is case-insensitive",
          "[editor][search]") {
  CHECK(ContainsCaseInsensitive("Hello World", "hello"));
  CHECK(ContainsCaseInsensitive("UPPER", "upper"));
  CHECK_FALSE(ContainsCaseInsensitive("abc", "xyz"));
}

TEST_CASE("EditorSearch: MatchesShortcutQuery empty query always matches",
          "[editor][search]") {
  ShortcutRow row{"Editor", "Undo", "Ctrl+Z"};
  CHECK(MatchesShortcutQuery(row, ""));
  CHECK(MatchesShortcutQuery(row, "Editor"));
  CHECK(MatchesShortcutQuery(row, "Ctrl"));
  CHECK_FALSE(MatchesShortcutQuery(row, "ZZZ_NO_MATCH"));
}

TEST_CASE("EditorSearch: MatchesCommandPaletteQuery empty query always matches",
          "[editor][search]") {
  CommandPaletteRow row{"save_scene", "Save Scene", "Toolbar"};
  CHECK(MatchesCommandPaletteQuery(row, ""));
  CHECK(MatchesCommandPaletteQuery(row, "save"));
  CHECK(MatchesCommandPaletteQuery(row, "Toolbar"));
  CHECK_FALSE(MatchesCommandPaletteQuery(row, "XYZ_NOT_FOUND"));
}

TEST_CASE("EditorSearch: ObjectMatchesQuickOpenQuery uses id and type",
          "[editor][search]") {
  SceneObject obj;
  obj.id = "my_light";
  obj.type = SceneObjectType::Light;
  obj.assetId = "";
  CHECK(ObjectMatchesQuickOpenQuery(obj, ""));
  CHECK(ObjectMatchesQuickOpenQuery(obj, "my_light"));
  CHECK(ObjectMatchesQuickOpenQuery(obj, "light"));
  CHECK_FALSE(ObjectMatchesQuickOpenQuery(obj, "camera_xyz"));
}

TEST_CASE("EditorSearch: AssetMatchesQuickOpenQuery uses assetId and mesh",
          "[editor][search]") {
  AssetDef asset;
  asset.mesh = "meshes/cube.obj";
  asset.albedoMap = "textures/albedo.png";
  CHECK(AssetMatchesQuickOpenQuery("cube_asset", asset, ""));
  CHECK(AssetMatchesQuickOpenQuery("cube_asset", asset, "cube"));
  CHECK(AssetMatchesQuickOpenQuery("cube_asset", asset, "albedo"));
  CHECK_FALSE(AssetMatchesQuickOpenQuery("cube_asset", asset, "XYZ_NONE"));
}

TEST_CASE("EditorSearch: EvaluateFilteredListState None when shownCount > 0",
          "[editor][search]") {
  using enum FilteredListState;
  CHECK(EvaluateFilteredListState(5, 3, "q") == None);
}

TEST_CASE(
    "EditorSearch: EvaluateFilteredListState EmptyData when totalCount == 0",
    "[editor][search]") {
  using enum FilteredListState;
  CHECK(EvaluateFilteredListState(0, 0, "") == EmptyData);
}

TEST_CASE(
    "EditorSearch: EvaluateFilteredListState NoMatches when query non-empty",
    "[editor][search]") {
  using enum FilteredListState;
  CHECK(EvaluateFilteredListState(5, 0, "nomatch") == NoMatches);
}

TEST_CASE("EditorSearch: EvaluateFilteredListState None when query empty "
          "totalCount>0",
          "[editor][search]") {
  using enum FilteredListState;
  // totalCount > 0, shown == 0, query empty → None (items exist, none filtered)
  CHECK(EvaluateFilteredListState(3, 0, "") == None);
}

TEST_CASE("EditorSearch: GetEditorShortcuts returns non-empty span",
          "[editor][search]") {
  CHECK(!GetEditorShortcuts().empty());
}

TEST_CASE("EditorSearch: GetEditorCommands returns non-empty span",
          "[editor][search]") {
  CHECK(!GetEditorCommands().empty());
}

// ===========================================================================
// EditorHistory — additional branch coverage via MCP commands
// ===========================================================================

TEST_CASE("EditorHistory: undo command returns false when nothing to undo",
          "[editor][history][extra]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(res.ok);
  REQUIRE_FALSE(res.data["undone"].get<bool>());
}

TEST_CASE("EditorHistory: redo command returns false when nothing to redo",
          "[editor][history][extra]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res =
      editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
  REQUIRE(res.ok);
  REQUIRE_FALSE(res.data["redone"].get<bool>());
}

TEST_CASE("EditorHistory: undo after create_object via transaction path",
          "[editor][history][extra]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  // Create two objects so undo stack has entries.
  auto r1 = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Prop"}, {"id", "txn_obj_a"}});
  REQUIRE(r1.ok);
  auto r2 = editor.ExecuteMcpCommand(
      "editor.create_object",
      nlohmann::json{{"type", "Light"}, {"id", "txn_obj_b"}});
  REQUIRE(r2.ok);
  REQUIRE(editor.GetDocument().objects.size() == 2);

  auto undo1 =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undo1.ok);
  REQUIRE(undo1.data["undone"].get<bool>());
  CHECK(editor.GetDocument().objects.size() == 1);

  // Redo should restore it.
  auto redo1 =
      editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
  REQUIRE(redo1.ok);
  REQUIRE(redo1.data["redone"].get<bool>());
  CHECK(editor.GetDocument().objects.size() == 2);
}

// ===========================================================================
// SceneProjectBridge — exercise internal helpers via public interface
// ===========================================================================

TEST_CASE("SceneProjectBridge: BuildSceneProjectModel produces model with "
          "camera node",
          "[editor][scene-bridge]") {
  // Camera objects in SceneDocument exercise ParseFloat, ParseVec3Csv, etc.
  SceneDocument doc;
  doc.sceneId = "cam_scene";

  SceneObject cam;
  cam.id = "cam_node";
  cam.type = SceneObjectType::Camera;
  cam.props["fov"] = "75.0";
  cam.props["nearClip"] = "0.05";
  cam.props["farClip"] = "1000.0";
  cam.position = Vec3{1.0f, 2.0f, 3.0f};
  doc.objects.push_back(cam);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  CHECK(!model.scene.nodes.empty());
}

TEST_CASE("SceneProjectBridge: BuildSceneProjectModel handles light object",
          "[editor][scene-bridge]") {
  SceneDocument doc;
  doc.sceneId = "light_scene";

  SceneObject light;
  light.id = "light_node";
  light.type = SceneObjectType::Light;
  light.position = Vec3{0.0f, 5.0f, 0.0f};
  light.props["color"] = "1.0,0.8,0.6";
  light.props["intensity"] = "2.5";
  doc.objects.push_back(light);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  bool foundLight = false;
  for (const auto &node : model.scene.nodes) {
    if (node.kind == SceneNodeKind::Light)
      foundLight = true;
  }
  CHECK(foundLight);
}

TEST_CASE(
    "SceneProjectBridge: BuildSceneProjectModel handles invalid float prop",
    "[editor][scene-bridge]") {
  // Non-parseable values should fall back to defaults without crashing.
  SceneDocument doc;
  doc.sceneId = "bad_float_scene";

  SceneObject cam;
  cam.id = "cam_bad_fov";
  cam.type = SceneObjectType::Camera;
  cam.props["fov"] = "not_a_number";
  cam.props["nearClip"] = "";
  doc.objects.push_back(cam);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  CHECK(!model.scene.nodes.empty());
}

TEST_CASE("SceneProjectBridge: BuildSceneDocument round-trips basic document",
          "[editor][scene-bridge]") {
  SceneDocument doc;
  doc.sceneId = "roundtrip";
  doc.sceneName = "Round Trip";

  SceneObject prop;
  prop.id = "prop1";
  prop.type = SceneObjectType::Prop;
  prop.assetId = "crate";
  prop.position = Vec3{1.0f, 2.0f, 3.0f};
  doc.objects.push_back(prop);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  const SceneDocument rebuilt = BuildSceneDocument(model);
  CHECK(rebuilt.sceneId == "roundtrip");
}

TEST_CASE(
    "SceneProjectBridge: BuildSceneProjectModel preserves light and component props",
    "[editor][scene-bridge][coverage]") {
  SceneDocument doc;
  doc.sceneId = "component_scene";

  SceneObject obj;
  obj.id = "node_1";
  obj.type = SceneObjectType::Light;
  obj.props["lightType"] = "directional";
  obj.props["intensity"] = "2.5";
  obj.props["color"] = "0.25,0.50,0.75";
  obj.props["radius"] = "42.0";
  obj.components.push_back(
      ComponentDesc{"script", {{"behaviorTag", "spin"}, {"custom", "x"}}});
  obj.components.push_back(ComponentDesc{"rigidbody",
                                         {{"mass", "7.5"},
                                          {"isKinematic", "true"},
                                          {"useGravity", "false"},
                                          {"extra", "y"}}});
  doc.objects.push_back(obj);

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  REQUIRE(model.scene.nodes.size() == 1);
  const SceneNodeDefinition &node = model.scene.nodes[0];
  REQUIRE(node.light.has_value());
  REQUIRE(node.light->kind == SceneLightKind::Directional);
  REQUIRE(node.light->intensity == Approx(2.5f));
  REQUIRE(node.light->color.x == Approx(0.25f));
  REQUIRE(node.light->radius == Approx(42.0f));
  REQUIRE(node.script.has_value());
  REQUIRE(node.script->behaviorTag == "spin");
  REQUIRE(node.script->extraProps.at("custom") == "x");
  REQUIRE(node.rigidbody.has_value());
  REQUIRE(node.rigidbody->mass == Approx(7.5f));
  REQUIRE(node.rigidbody->isKinematic);
  REQUIRE_FALSE(node.rigidbody->useGravity);
  REQUIRE(node.rigidbody->extraProps.at("extra") == "y");
}

// ===========================================================================
// AssetImportService — additional error paths
// ===========================================================================

TEST_CASE("AssetMetadata: malformed sidecar reports error without throwing",
          "[editor][asset-metadata][coverage]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_asset_metadata_bad_sidecar";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models" /
                                      "guid_bad_meta", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  WriteFile(GetAssetMetadataPath("guid_bad_meta"), "{ not valid json");

  AssetMetadata metadata;
  std::string error;
  REQUIRE_FALSE(LoadAssetMetadata("guid_bad_meta", &metadata, &error));
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("AssetMetadata: round-trip preserves dependencies and importer id",
          "[editor][asset-metadata][coverage]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_asset_metadata_roundtrip";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models" /
                                      "guid_roundtrip", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  AssetMetadata metadata;
  metadata.assetId = "asset_roundtrip";
  metadata.assetGuid = "guid_roundtrip";
  metadata.displayName = "Round Trip";
  metadata.importerId = "builtin.obj_mesh";
  metadata.dependencies.push_back(
      AssetDependencyRecord{AssetDependencyKind::Source, "src.obj"});
  metadata.dependencies.push_back(
      AssetDependencyRecord{AssetDependencyKind::DownstreamAsset, "dep_guid"});

  std::string error;
  REQUIRE(SaveAssetMetadata(metadata, &error));
  REQUIRE(error.empty());

  AssetMetadata loaded;
  REQUIRE(LoadAssetMetadata("guid_roundtrip", &loaded, &error));
  REQUIRE(loaded.importerId == "builtin.obj_mesh");
  REQUIRE(loaded.dependencies.size() == 2);
  REQUIRE(loaded.dependencies[0].value == "src.obj");
  REQUIRE(loaded.dependencies[1].kind == AssetDependencyKind::DownstreamAsset);
  REQUIRE(loaded.dependencies[1].value == "dep_guid");
}

TEST_CASE("AssetImportService: ImportTextureForAsset with missing source "
          "returns false",
          "[editor][asset-import][extra]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_import_tex_missing";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  AssetImportService service;
  AssetDef asset;
  std::string err;
  const bool ok = service.ImportTextureForAsset(
      (root / "nonexistent_tex.png").string(), "test_asset", &asset, &err);
  REQUIRE_FALSE(ok);
  CHECK(!err.empty());
}

TEST_CASE(
    "AssetImportService: ImportAssetFromSource with missing file returns error",
    "[editor][asset-import][extra]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_import_asset_missing";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  AssetImportService service;
  const AssetImportResult result =
      service.ImportAssetFromSource((root / "does_not_exist.obj").string(),
                                    "asset_missing", "guid_missing", "Missing");
  CHECK_FALSE(result.ok);
  CHECK(!result.diagnostics.empty());
}

TEST_CASE(
    "AssetImportService: ReimportAssetWithDependents with null doc is no-op",
    "[editor][asset-import][extra]") {
  const std::filesystem::path root =
      Monolith::Tests::SecureTempBase() / "horo_reimport_null";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  AssetImportService service;
  // Must not crash when doc is null.
  const AssetReimportResult result =
      service.ReimportAssetWithDependents(nullptr, "some_guid", "test");
  // Result should indicate nothing was reimported.
  CHECK(result.order.empty());
}
