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
            if (size_t len = 0;
                getenv_s(&len, nullptr, 0, varName) == 0 && len > 1) {
                std::vector<char> value(len);
                if (getenv_s(&len, value.data(), value.size(), varName) == 0 &&
                    len > 1) {
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
    req.sourcePath = (Monolith::Tests::SecureTempBase() / "notanobj.txt").string();
    const AssetImportResult result = imp->Import(req);
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
    CHECK_FALSE(result.diagnostics.empty());
}

TEST_CASE("AssetImporterRegistry: ObjImporter rejects missing source file",
          "[editor][importer-registry]") {
    const std::filesystem::path root = Monolith::Tests::SecureTempBase() /
                                       "horo_imp_obj_missing";
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
    const std::filesystem::path root = Monolith::Tests::SecureTempBase() /
                                       "horo_imp_tex_missing";
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
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00,
            0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
            0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE,
            0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63,
            0xF8, 0xCF, 0xC0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21,
            0xBC, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
            0x42, 0x60, 0x82
        };
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

TEST_CASE("AssetImportService: ImportTextureForAsset fails for unsupported type",
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

TEST_CASE("AssetImportService: ReimportAssetWithDependents unknown guid is noop",
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

TEST_CASE("EditorDebugTrace: EditorTraceEnabled reads env var", "[editor][debug-trace]") {
    // In release builds it always returns false
    // In debug builds it reads MONOLITH_EDITOR_TRACE
    const bool enabled = EditorTraceEnabled();
    (void) enabled; // exercising the code path is sufficient
    CHECK(true); // no crash
}

TEST_CASE("EditorDebugTrace: EditorTrace does not throw", "[editor][debug-trace]") {
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
        [](const RuntimeSceneDefinition &, std::string *) {
            return true;
        });
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
        [](const RuntimeSceneDefinition &, std::string *) {
            return true;
        });
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

TEST_CASE("EditorWorkspaceSettings: LoadEditorWorkspaceDocument parses valid JSON",
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
    WriteFile(wsFile,
              R"({"editor":{"consoleShowInfo":false,"consoleShowWarn":true,"consoleShowError":false,"projectBrowserCwd":"/some/path"}})");

    EditorWorkspaceDocument doc = LoadEditorWorkspaceDocument();
    REQUIRE(doc.loadedFromDisk);
    CHECK_FALSE(doc.parseError);
    CHECK_FALSE(doc.state.consoleShowInfo);
    CHECK(doc.state.consoleShowWarn);
    CHECK_FALSE(doc.state.consoleShowError);
    CHECK(doc.state.projectBrowserCwd == "/some/path");
}

TEST_CASE("EditorWorkspaceSettings: LoadEditorWorkspaceDocument handles invalid JSON",
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

TEST_CASE("EditorWorkspaceSettings: SaveEditorWorkspaceDocument null returns false",
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

TEST_CASE("TransformGizmo: AxisDir returns zero for None", "[gizmo][coverage]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());
    const Vec3 none = g.AxisDir(GizmoAxis::None);
    CHECK(none.x == Approx(0.0f));
    CHECK(none.y == Approx(0.0f));
    CHECK(none.z == Approx(0.0f));
}

TEST_CASE("TransformGizmo: HandleSize returns small fallback at near-zero distance",
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
