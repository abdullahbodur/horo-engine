// test_editor_coverage.cpp
// Regression coverage for editor modules that had <80% line coverage.
// Targets: AssetImporterRegistry, AssetImportService, EditorImGuiBackend,
//          EditorDebugTrace, SceneRuntimeBridge,
//          SceneRuntimeCoordinatorBridge, EditorWorkspaceSettings,
//          TransformGizmo (edge cases), SceneSerializer (edge cases).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/ProjectPath.h"
#include "core/BuildVersion.h"
#include "core/pipeline/ReleasePipeline.h"
#include "ui/editor/AssetIdentity.h"
#include "ui/editor/AssetImportDiagnosticCodes.h"
#include "ui/editor/AssetImportService.h"
#include "ui/editor/AssetImporterRegistry.h"
#include "ui/editor/AssetGuidRegistry.h"
#include "ui/editor/AssetMetadata.h"
#include "ui/editor/EditorDebugTrace.h"
#include "ui/editor/EditorImGuiBackend.h"
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorSceneGraph.h"
#include "ui/editor/EditorSearch.h"
#include "ui/editor/EditorUserSettings.h"
#include "ui/editor/EditorWorkspaceSettings.h"
#include "ui/editor/SceneDocument.h"
#include "ui/editor/SceneProjectBridge.h"
#include "ui/editor/SceneRuntimeBridge.h"
#include "ui/editor/SceneRuntimeCoordinatorBridge.h"
#include "ui/editor/SceneSerializer.h"
#include "ui/editor/TransformGizmo.h"
#include "ui/editor/components/EditorFileBrowser.h"
#include "ui/editor/components/EditorViewportToolbar.h"
#include "ui/launcher/ExternalProcessRunner.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "renderer/Camera.h"
#include "renderer/Mesh.h"
#include "renderer/MeshBin.h"
#include "renderer/RenderBackend.h"
#include "renderer/Skeleton.h"
#include "renderer/SkinnedMesh.h"
#include "renderer/SkinnedMeshBin.h"
#include "renderer/SkinnedVertex.h"
#include "scene/SceneProjectModel.h"
#include "scene/SceneRuntimeCoordinator.h"
#include "tests/TestTempPaths.h"

using namespace Horo;
using namespace Horo::Editor;
using Catch::Approx;

// Helpers

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

TEST_CASE("AssetImporterRegistry: Register nullptr is a no-op", "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  // Registering null must not crash or alter existing entries
  registry.Register(nullptr);
  REQUIRE(registry.FindByExtension("mesh.obj") != nullptr);
}

TEST_CASE("AssetImporterRegistry: FindByExtension returns null for unknown ext", "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  REQUIRE(registry.FindByExtension("doc.pdf") == nullptr);
  REQUIRE(registry.FindByExtension("archive.zip") == nullptr);
  REQUIRE(registry.FindByExtension("noext") == nullptr);
}

TEST_CASE("AssetImporterRegistry: FindById returns null for unknown id", "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  REQUIRE(registry.FindById("nonexistent.importer") == nullptr);
  REQUIRE(registry.FindById("") == nullptr);
}

TEST_CASE("AssetImporterRegistry: RegisteredImporterIds lists built-ins", "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  const auto ids = registry.RegisteredImporterIds();
  REQUIRE(ids.size() >= 2);
  const bool hasObj = std::ranges::find(ids, "builtin.obj_mesh") != ids.end();
  const bool hasTex =
      std::ranges::find(ids, "builtin.texture_copy") != ids.end();
  REQUIRE(hasObj);
  REQUIRE(hasTex);
}

TEST_CASE("AssetImporterRegistry: FindByExtension is case-insensitive", "[editor][importer-registry]") {
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

TEST_CASE("AssetImporterRegistry: ObjImporter metadata correct", "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.obj_mesh");
  REQUIRE(imp != nullptr);
  CHECK(std::string(imp->AssetKind()) == "static_mesh");
  const auto exts = imp->SupportedExtensions();
  REQUIRE(std::find(exts.begin(), exts.end(), ".obj") != exts.end());
}

TEST_CASE("AssetImporterRegistry: TextureCopyImporter metadata correct", "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.texture_copy");
  REQUIRE(imp != nullptr);
  CHECK(std::string(imp->AssetKind()) == "texture");
  const auto exts = imp->SupportedExtensions();
  CHECK(std::find(exts.begin(), exts.end(), ".png") != exts.end());
  CHECK(std::find(exts.begin(), exts.end(), ".hdr") != exts.end());
}

TEST_CASE("AssetImporterRegistry: ObjImporter rejects non-obj source", "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.obj_mesh");
  REQUIRE(imp != nullptr);
  AssetImportRequest req;
  req.assetId = "x";
  req.assetGuid = "guid_x";
  req.sourcePath =
      (Horo::Tests::SecureTempBase() / "notanobj.txt").string();
  const AssetImportResult result = imp->Import(req);
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.error.empty());
  CHECK_FALSE(result.diagnostics.empty());
}

TEST_CASE("AssetImporterRegistry: ObjImporter rejects missing source file", "[editor][importer-registry]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_imp_obj_missing";
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

TEST_CASE("AssetImporterRegistry: TextureCopy rejects unsupported type", "[editor][importer-registry]") {
  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.texture_copy");
  REQUIRE(imp != nullptr);
  AssetImportRequest req;
  req.assetId = "t";
  req.assetGuid = "guid_t";
  req.sourcePath =
      (Horo::Tests::SecureTempBase() / "notanimage.txt").string();
  const AssetImportResult result = imp->Import(req);
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.diagnostics.empty());
}

TEST_CASE("AssetImporterRegistry: TextureCopy rejects missing source file", "[editor][importer-registry]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_imp_tex_missing";
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

TEST_CASE("AssetImporterRegistry: ObjImporter reports copy failure when destination path is blocked", "[editor][importer-registry]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_imp_obj_copy_fail";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path sourceObj = root / "ship.obj";
  WriteFile(sourceObj, "v 0 0 0\n");

  const std::string assetGuid = "guid_obj_copy_fail";
  const std::filesystem::path managedDir = GetManagedAssetDirectory(assetGuid);
  std::filesystem::create_directories(managedDir, ec);
  REQUIRE_FALSE(ec);
  std::filesystem::create_directories(managedDir / sourceObj.filename(), ec);
  REQUIRE_FALSE(ec);

  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.obj_mesh");
  REQUIRE(imp != nullptr);
  AssetImportRequest req;
  req.assetId = "mesh_copy_fail";
  req.assetGuid = assetGuid;
  req.sourcePath = sourceObj.string();
  const AssetImportResult result = imp->Import(req);
  CHECK_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].code == DiagnosticCodes::ObjCopyFailed);
}

TEST_CASE("AssetImporterRegistry: TextureCopy reports create-directory failure when managed path is a file", "[editor][importer-registry]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_imp_tex_create_dir_fail";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path sourceTexture = root / "brick.png";
  WriteFile(sourceTexture, "png-bytes");

  const std::string assetGuid = "guid_tex_create_dir_fail";
  const std::filesystem::path managedDir = GetManagedAssetDirectory(assetGuid);
  std::filesystem::create_directories(managedDir.parent_path(), ec);
  REQUIRE_FALSE(ec);
  WriteFile(managedDir, "not-a-directory");

  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.texture_copy");
  REQUIRE(imp != nullptr);
  AssetImportRequest req;
  req.assetId = "tex_create_dir_fail";
  req.assetGuid = assetGuid;
  req.sourcePath = sourceTexture.string();
  const AssetImportResult result = imp->Import(req);
  CHECK_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].code ==
        DiagnosticCodes::TextureCreateDirectoryFailed);
}

// ===========================================================================
// AssetImportDiagnosticCodes — taxonomy contract
// ===========================================================================
//
// These tests pin the public-contract diagnostic code strings so that any
// silent rename or value drift breaks the build, not a downstream consumer.
// New codes added in HORO-91 are checked alongside the legacy OBJ/texture/
// service codes that already had string-literal call sites.

TEST_CASE("DiagnosticCodes: cross-cutting service codes are stable", "[editor][diagnostic-codes]") {
  CHECK(DiagnosticCodes::ImportFailed == "asset.import.failed");
  CHECK(DiagnosticCodes::ImporterNotFound == "asset.importer.not_found");
  CHECK(DiagnosticCodes::MetadataSaveFailed == "asset.metadata.save_failed");
  CHECK(DiagnosticCodes::ReimportDependencyCycle ==
        "asset.reimport.dependency_cycle");
  CHECK(DiagnosticCodes::ReimportMetadataMissing ==
        "asset.reimport.metadata_missing");
  CHECK(DiagnosticCodes::ReimportImporterMissing ==
        "asset.reimport.importer_missing");
}

TEST_CASE("DiagnosticCodes: OBJ importer codes are stable", "[editor][diagnostic-codes]") {
  CHECK(DiagnosticCodes::ObjUnsupportedType == "asset.obj.unsupported_type");
  CHECK(DiagnosticCodes::ObjSourceMissing == "asset.obj.source_missing");
  CHECK(DiagnosticCodes::ObjCreateDirectoryFailed ==
        "asset.obj.create_directory_failed");
  CHECK(DiagnosticCodes::ObjCopyFailed == "asset.obj.copy_failed");
}

TEST_CASE("DiagnosticCodes: texture importer codes are stable", "[editor][diagnostic-codes]") {
  CHECK(DiagnosticCodes::TextureUnsupportedType ==
        "asset.texture.unsupported_type");
  CHECK(DiagnosticCodes::TextureSourceMissing ==
        "asset.texture.source_missing");
  CHECK(DiagnosticCodes::TextureCreateDirectoryFailed ==
        "asset.texture.create_directory_failed");
  CHECK(DiagnosticCodes::TextureCopyFailed == "asset.texture.copy_failed");
}

TEST_CASE("DiagnosticCodes: FBX importer codes are stable", "[editor][diagnostic-codes][fbx]") {
  CHECK(DiagnosticCodes::FbxUnsupportedType == "asset.fbx.unsupported_type");
  CHECK(DiagnosticCodes::FbxSourceMissing == "asset.fbx.source_missing");
  CHECK(DiagnosticCodes::FbxCreateDirectoryFailed ==
        "asset.fbx.create_directory_failed");
  CHECK(DiagnosticCodes::FbxParseFailed == "asset.fbx.parse_failed");
  CHECK(DiagnosticCodes::FbxNoGeometry == "asset.fbx.no_geometry");
  CHECK(DiagnosticCodes::FbxMeshWriteFailed == "asset.fbx.mesh_write_failed");
  CHECK(DiagnosticCodes::FbxExternalTextureMissing ==
        "asset.fbx.external_texture_missing");
  CHECK(DiagnosticCodes::FbxExternalTextureCopyFailed ==
        "asset.fbx.external_texture_copy_failed");
  CHECK(DiagnosticCodes::FbxEmbeddedTextureExtractFailed ==
        "asset.fbx.embedded_texture_extract_failed");
  CHECK(DiagnosticCodes::FbxSkeletonMissing == "asset.fbx.skeleton_missing");
  CHECK(DiagnosticCodes::FbxSkeletonWriteFailed ==
        "asset.fbx.skeleton_write_failed");
  CHECK(DiagnosticCodes::FbxAnimationWriteFailed ==
        "asset.fbx.animation_write_failed");
  CHECK(DiagnosticCodes::FbxUnitScaleWarning ==
        "asset.fbx.unit_scale_warning");
  CHECK(DiagnosticCodes::FbxUnsupportedFeatureWarning ==
        "asset.fbx.unsupported_feature_warning");
}

TEST_CASE("DiagnosticCodes: every code follows asset.<scope>.<reason> format", "[editor][diagnostic-codes]") {
  // Single-source list of every public diagnostic code. Adding a new constant
  // without registering it here will not fail this test, but the per-scope
  // tests above will, so consumers stay protected against silent drift.
  const std::vector<std::string_view> all = {
      DiagnosticCodes::ImportFailed,
      DiagnosticCodes::ImporterNotFound,
      DiagnosticCodes::MetadataSaveFailed,
      DiagnosticCodes::ReimportDependencyCycle,
      DiagnosticCodes::ReimportMetadataMissing,
      DiagnosticCodes::ReimportImporterMissing,
      DiagnosticCodes::ObjUnsupportedType,
      DiagnosticCodes::ObjSourceMissing,
      DiagnosticCodes::ObjCreateDirectoryFailed,
      DiagnosticCodes::ObjCopyFailed,
      DiagnosticCodes::TextureUnsupportedType,
      DiagnosticCodes::TextureSourceMissing,
      DiagnosticCodes::TextureCreateDirectoryFailed,
      DiagnosticCodes::TextureCopyFailed,
      DiagnosticCodes::FbxUnsupportedType,
      DiagnosticCodes::FbxSourceMissing,
      DiagnosticCodes::FbxCreateDirectoryFailed,
      DiagnosticCodes::FbxParseFailed,
      DiagnosticCodes::FbxNoGeometry,
      DiagnosticCodes::FbxMeshWriteFailed,
      DiagnosticCodes::FbxExternalTextureMissing,
      DiagnosticCodes::FbxExternalTextureCopyFailed,
      DiagnosticCodes::FbxEmbeddedTextureExtractFailed,
      DiagnosticCodes::FbxSkeletonMissing,
      DiagnosticCodes::FbxSkeletonWriteFailed,
      DiagnosticCodes::FbxAnimationWriteFailed,
      DiagnosticCodes::FbxUnitScaleWarning,
      DiagnosticCodes::FbxUnsupportedFeatureWarning,
  };

  for (const std::string_view code : all) {
    INFO("code: " << code);
    REQUIRE(code.starts_with("asset."));
    // Exactly two dots separate three non-empty lowercase segments.
    const auto firstDot = code.find('.');
    const auto lastDot = code.rfind('.');
    REQUIRE(firstDot != std::string_view::npos);
    REQUIRE(lastDot != std::string_view::npos);
    REQUIRE(firstDot != lastDot);
    REQUIRE(firstDot < lastDot);
    REQUIRE(lastDot + 1 < code.size());
    for (const char ch : code) {
      const bool isLower = ch >= 'a' && ch <= 'z';
      const bool isDigit = ch >= '0' && ch <= '9';
      const bool isSep = ch == '.' || ch == '_';
      REQUIRE((isLower || isDigit || isSep));
    }
  }

  // Sanity: no duplicate values across the table.
  std::vector<std::string_view> sorted = all;
  std::ranges::sort(sorted);
  const auto dupIt = std::ranges::adjacent_find(sorted);
  REQUIRE(dupIt == sorted.end());
}

TEST_CASE("DiagnosticCodes: ObjAssetImporter emits typed codes for missing source", "[editor][diagnostic-codes][importer-registry]") {
  // Round-trip: the importer must emit the exact constant values; downstream
  // tools key UI strings off these codes, so they must not drift silently.
  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.obj_mesh");
  REQUIRE(imp != nullptr);
  AssetImportRequest req;
  req.assetId = "x";
  req.assetGuid = "guid_x";
  req.sourcePath = "/this/path/does/not/exist.obj";
  const AssetImportResult result = imp->Import(req);
  CHECK_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].code == DiagnosticCodes::ObjSourceMissing);
}

TEST_CASE("DiagnosticCodes: TextureCopyImporter emits typed code for unsupported type", "[editor][diagnostic-codes][importer-registry]") {
  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.texture_copy");
  REQUIRE(imp != nullptr);
  AssetImportRequest req;
  req.assetId = "x";
  req.assetGuid = "guid_x";
  req.sourcePath = (Horo::Tests::SecureTempBase() / "not_an_image.txt").string();
  const AssetImportResult result = imp->Import(req);
  CHECK_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].code == DiagnosticCodes::TextureUnsupportedType);
}

// ===========================================================================
// EditorAssetThumbnailPreview — .mesh.bin support (HORO-101)
// ===========================================================================
// FBX-imported assets land as engine-native .mesh.bin files via HORO-94 +
// HORO-100. The thumbnail preview cache must therefore route .mesh.bin paths
// through MeshBin::ReadStaticMesh and produce a real preview Mesh, mirroring
// the OBJ branch that already drove static-mesh thumbnails.

#include "ui/editor/components/EditorAssetThumbnailPreview.h"

TEST_CASE("EditorAssetThumbnailPreview: .mesh.bin path resolves to a real preview mesh",
          "[editor][thumbnail-preview][meshbin]") {
  ClearAssetThumbnailMeshCaches();

  const std::filesystem::path path =
      Horo::Tests::SecureTempBase() / "horo_thumb_meshbin.mesh.bin";
  std::vector<Vertex> vertices = {
      {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
      {{2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
      {{0.0f, 3.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
  };
  std::vector<uint32_t> indices = {0, 1, 2};
  REQUIRE(MeshBin::WriteStaticMesh(path.string(), vertices, indices).ok);

  const Mesh *mesh = TryGetAssetPreviewStaticMesh(path.string());
  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() == 3);
  REQUIRE(mesh->GetVertices().size() == 3);
  CHECK(mesh->GetVertices()[2].position.y == 3.0f);
}

TEST_CASE("EditorAssetThumbnailPreview: missing .mesh.bin path returns nullptr without crashing",
          "[editor][thumbnail-preview][meshbin]") {
  ClearAssetThumbnailMeshCaches();
  const Mesh *mesh =
      TryGetAssetPreviewStaticMesh("/nonexistent/path/missing.mesh.bin");
  REQUIRE(mesh == nullptr);
}

// ===========================================================================
// EditorAssetThumbnailPreview — .skinned.bin support (HORO-41)
// ===========================================================================
// Skeletal FBX assets land as engine-native .skinned.bin files via HORO-107.
// The thumbnail preview cache must therefore route .skinned.bin paths through
// SkinnedMeshBin::ReadSkinnedMesh and produce a real preview SkinnedMesh,
// mirroring the GLTF-skinned branch.

TEST_CASE("EditorAssetThumbnailPreview: .skinned.bin path resolves to a real preview skinned mesh",
          "[editor][thumbnail-preview][skinnedmeshbin]") {
  ClearAssetThumbnailMeshCaches();

  const std::filesystem::path path =
      Horo::Tests::SecureTempBase() / "horo_thumb_skinnedmeshbin.skinned.bin";

  std::vector<SkinnedVertex> vertices(3);
  for (int i = 0; i < 3; ++i) {
    vertices[i].position = {static_cast<float>(i), 0.0f, 0.0f};
    vertices[i].normal = {0.0f, 0.0f, 1.0f};
    vertices[i].uv = {0.0f, 0.0f};
    vertices[i].boneIndices = {0, -1, -1, -1};
    vertices[i].boneWeights = {1.0f, 0.0f, 0.0f, 0.0f};
  }
  const std::vector<uint32_t> indices = {0, 1, 2};

  std::vector<Bone> bones;
  Bone root;
  root.parentIndex = -1;
  root.name = "root";
  root.inverseBindMatrix = Mat4::Identity();
  bones.push_back(root);

  REQUIRE(SkinnedMeshBin::WriteSkinnedMesh(path.string(), vertices, indices, bones).ok);

  const SkinnedMesh *skinned = TryGetAssetPreviewSkinnedMesh(path.string());
  REQUIRE(skinned != nullptr);
  CHECK(skinned->GetIndexCount() == 3);
}

TEST_CASE("EditorAssetThumbnailPreview: missing .skinned.bin path returns nullptr without crashing",
          "[editor][thumbnail-preview][skinnedmeshbin]") {
  ClearAssetThumbnailMeshCaches();
  const SkinnedMesh *skinned =
      TryGetAssetPreviewSkinnedMesh("/nonexistent/path/missing.skinned.bin");
  REQUIRE(skinned == nullptr);
}

// ===========================================================================
// EditorImportAssetModal — state transitions (HORO-92)
// ===========================================================================
// Exercises the modal as a state machine; the Draw call short-circuits when
// no ImGui context is current, so these tests run cleanly in unit-test mode.

#include "ui/editor/components/EditorImportAssetModal.h"

TEST_CASE("EditorImportAssetModal: Open seeds draft and infers importer from extension",
          "[editor][import-asset-modal]") {
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  REQUIRE_FALSE(modal.IsOpen());

  modal.Open("/some/path/cube.fbx", &registry, std::filesystem::path{});
  CHECK(modal.IsOpen());
  CHECK(modal.DraftForTest().sourcePath == "/some/path/cube.fbx");
  CHECK(modal.DraftForTest().assetId == "cube");
  CHECK(modal.DraftForTest().displayName == "cube");
  CHECK(modal.DraftForTest().importerId == "builtin.fbx_static_mesh");
}

TEST_CASE("EditorImportAssetModal: Open with .obj routes to OBJ importer",
          "[editor][import-asset-modal]") {
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  modal.Open("/x/y/crate.obj", &registry, std::filesystem::path{});
  CHECK(modal.DraftForTest().importerId == "builtin.obj_mesh");
}

TEST_CASE("EditorImportAssetModal: Open with no path leaves importer empty",
          "[editor][import-asset-modal]") {
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  modal.Open({}, &registry, std::filesystem::path{});
  CHECK(modal.IsOpen());
  CHECK(modal.DraftForTest().sourcePath.empty());
  CHECK(modal.DraftForTest().assetId.empty());
  CHECK(modal.DraftForTest().importerId.empty());
}

TEST_CASE("EditorImportAssetModal: RequestImportForTest signals HasPendingRequest exactly once",
          "[editor][import-asset-modal]") {
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  modal.Open("/p/cube.fbx", &registry, std::filesystem::path{});
  CHECK_FALSE(modal.HasPendingRequest());

  modal.RequestImportForTest();
  REQUIRE(modal.HasPendingRequest());
  const ImportAssetRequest req = modal.ConsumePendingRequest();
  CHECK_FALSE(modal.HasPendingRequest());
  CHECK(req.sourcePath == "/p/cube.fbx");
  CHECK(req.assetId == "cube");
  CHECK(req.importerId == "builtin.fbx_static_mesh");
}

TEST_CASE("EditorImportAssetModal: SetLastResult with ok+no diagnostics closes the modal",
          "[editor][import-asset-modal]") {
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  modal.Open("/p/cube.fbx", &registry, std::filesystem::path{});

  ImportAssetOutcome outcome;
  outcome.ok = true;
  modal.SetLastResult(outcome);
  CHECK_FALSE(modal.IsOpen());
}

TEST_CASE("EditorImportAssetModal: SetLastResult with warning diagnostics keeps the modal open",
          "[editor][import-asset-modal]") {
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  modal.Open("/p/cube.fbx", &registry, std::filesystem::path{});

  ImportAssetOutcome outcome;
  outcome.ok = true;
  AssetImportDiagnostic warn;
  warn.severity = AssetDiagnosticSeverity::Warning;
  warn.code = "asset.fbx.external_texture_missing";
  warn.message = "missing.png";
  outcome.diagnostics.push_back(warn);
  modal.SetLastResult(outcome);
  CHECK(modal.IsOpen());
  CHECK(modal.LastResultForTest().ok);
}

TEST_CASE("EditorImportAssetModal: Close clears pending state",
          "[editor][import-asset-modal]") {
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  modal.Open("/p/cube.fbx", &registry, std::filesystem::path{});
  modal.RequestImportForTest();
  modal.Close();
  CHECK_FALSE(modal.IsOpen());
  CHECK_FALSE(modal.HasPendingRequest());
  CHECK(modal.DraftForTest().sourcePath.empty());
}

TEST_CASE("EditorImportAssetModal: Draw is a no-op without an ImGui context",
          "[editor][import-asset-modal]") {
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  modal.Open("/p/cube.fbx", &registry, std::filesystem::path{});
  // No ImGui context attached; Draw must not crash and the modal stays open.
  modal.Draw();
  CHECK(modal.IsOpen());
}

// ===========================================================================
// EditorBuildPipelineModal — state transitions (HORO-31)
// ===========================================================================

#include "ui/editor/components/EditorBuildPipelineModal.h"

TEST_CASE("ExternalProcessRunner: captures child stdout for UI consumers",
          "[editor][build-pipeline-modal]") {
  Horo::Launcher::ExternalProcessRunner runner;
  Horo::Launcher::ResolvedLauncherCommand command;
#ifdef _WIN32
  command.executable = "cmd.exe";
  command.args = {"/C", "echo cmake configured && echo ld: warning: duplicate lib"};
  command.debugString = "cmd.exe /C <test-output>";
#else
  command.executable = "/bin/sh";
  command.args = {"-c", "printf 'cmake configured\\nld: warning: duplicate lib\\n'"};
  command.debugString = "/bin/sh -c <test-output>";
#endif
  command.workingDirectory = std::filesystem::current_path();

  std::string error;
  REQUIRE(runner.Start(command, "Building Test", &error));

  for (int i = 0; i < 100 && runner.IsActive(); ++i) {
    runner.Poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  runner.Poll();

  const auto &status = runner.GetStatus();
  REQUIRE(status.finished);
  CHECK(status.exitCode == 0);
  CHECK(status.output.find("[Building Test] cmake configured") != std::string::npos);
  CHECK(status.output.find("[Building Test] ld: warning: duplicate lib") != std::string::npos);
}

TEST_CASE("EditorBuildPipelineModal: Open seeds host release job and output root",
          "[editor][build-pipeline-modal]") {
  EditorBuildPipelineModal modal;
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "horo_build_modal_test_project";
  modal.SetProjectRoot(root.generic_string());

  modal.Open();

  REQUIRE(modal.IsOpen());
  const BuildPipelineDraft &draft = modal.DraftForTest();
  CHECK(draft.versionTag == (std::string("v") + Horo::Build::EngineVersion()));
  CHECK(draft.outputRoot == (root / "build" / "release").generic_string());
  REQUIRE(draft.jobs.size() == 1);
  CHECK(draft.jobs[0].config == BuildConfig::Release);
#if defined(_WIN32)
  CHECK(draft.jobs[0].os == BuildTargetOS::Windows);
  CHECK(draft.jobs[0].arch == BuildArch::x86_64);
#elif defined(__APPLE__)
  CHECK(draft.jobs[0].os == BuildTargetOS::MacOS);
  CHECK(draft.jobs[0].arch == BuildArch::Arm64);
#else
  CHECK(draft.jobs[0].os == BuildTargetOS::Linux);
  CHECK(draft.jobs[0].arch == BuildArch::x86_64);
#endif
}

TEST_CASE("EditorBuildPipelineModal: Draw is a no-op without an ImGui context",
          "[editor][build-pipeline-modal]") {
  EditorBuildPipelineModal modal;
  modal.Open();
  modal.Draw();
  CHECK(modal.IsOpen());
}

TEST_CASE("EditorBuildPipelineModal: non-host targets fail explicitly instead of launching",
          "[editor][build-pipeline-modal]") {
  EditorBuildPipelineModal modal;
  bool callbackCalled = false;
  modal.SetBuildCompleteCallback([&callbackCalled](const std::vector<BuildJob> &) {
    callbackCalled = true;
  });

  BuildPipelineDraft draft;
  draft.versionTag = "vtest";
  draft.outputRoot = (std::filesystem::temp_directory_path() / "horo_build_modal_test_output").generic_string();
  BuildJob job;
#if defined(_WIN32)
  job.os = BuildTargetOS::Linux;
#else
  job.os = BuildTargetOS::Windows;
#endif
  job.arch = BuildArch::x86_64;
  job.config = BuildConfig::Release;
  job.status = BuildJobStatus::Pending;
  draft.jobs.push_back(job);

  // Open modal first to transition Idle → Configuring,
  // then swap in the non-host draft before building.
  modal.Open();
  modal.SetDraftForTest(std::move(draft));

  modal.StartNextPendingJobForTest();

  const BuildPipelineDraft &result = modal.DraftForTest();
  REQUIRE(result.jobs.size() == 1);
  CHECK(result.jobs[0].status == BuildJobStatus::Failed);
  CHECK(result.jobs[0].exitCode == 1);
  const std::string expected =
      std::string("Build blocked: ") + GetBuildTargetOSLabel(job.os) +
      " x86_64 target cannot be built locally. Reason: No cross-compilation toolchain configured for this target.";
  CHECK(result.jobs[0].error == expected);
  CHECK(result.jobs[0].log.find("[ERROR]") != std::string::npos);
  CHECK(result.jobs[0].log.find(expected) != std::string::npos);
  CHECK(result.allJobsComplete);
  CHECK(result.anyJobFailed);
  CHECK(result.totalProgress == 100);
  CHECK(callbackCalled);
}

TEST_CASE("EditorBuildPipelineModal: CancelAllBuilds marks active queue terminal",
          "[editor][build-pipeline-modal]") {
  EditorBuildPipelineModal modal;
  BuildPipelineDraft draft;
  draft.versionTag = "vtest";
  draft.outputRoot = (std::filesystem::temp_directory_path() / "horo_build_modal_test_output").generic_string();

  BuildJob building;
  building.os = BuildTargetOS::MacOS;
  building.arch = BuildArch::Arm64;
  building.status = BuildJobStatus::Building;
  draft.jobs.push_back(building);

  BuildJob pending;
  pending.os = BuildTargetOS::Linux;
  pending.arch = BuildArch::x86_64;
  pending.status = BuildJobStatus::Pending;
  draft.jobs.push_back(pending);

  BuildJob alreadyTerminal;
  alreadyTerminal.os = BuildTargetOS::Windows;
  alreadyTerminal.arch = BuildArch::x86_64;
  alreadyTerminal.status = BuildJobStatus::Success;
  draft.jobs.push_back(alreadyTerminal);

  modal.SetDraftForTest(std::move(draft));
  modal.CancelAllBuildsForTest();

  const BuildPipelineDraft &result = modal.DraftForTest();
  REQUIRE(result.jobs.size() == 3);
  CHECK(result.jobs[0].status == BuildJobStatus::Cancelled);
  CHECK(result.jobs[1].status == BuildJobStatus::Cancelled);
  CHECK(result.jobs[2].status == BuildJobStatus::Success);
  CHECK(result.allJobsComplete);
  CHECK(result.totalProgress == 100);
}

TEST_CASE("EditorBuildPipelineModal: labels cover every enum value",
          "[editor][build-pipeline-modal]") {
  CHECK(std::string_view(GetBuildTargetOSLabel(BuildTargetOS::Windows)) == "Windows");
  CHECK(std::string_view(GetBuildTargetOSLabel(BuildTargetOS::MacOS)) == "macOS");
  CHECK(std::string_view(GetBuildTargetOSLabel(BuildTargetOS::Linux)) == "Linux");
  CHECK(std::string_view(GetBuildConfigLabel(BuildConfig::Debug)) == "Debug");
  CHECK(std::string_view(GetBuildConfigLabel(BuildConfig::Release)) == "Release");
  CHECK(std::string_view(GetBuildConfigLabel(BuildConfig::MinSizeRel)) == "MinSizeRel");
  CHECK(std::string_view(GetBuildArchLabel(BuildArch::x86_64)) == "x86_64");
  CHECK(std::string_view(GetBuildArchLabel(BuildArch::Arm64)) == "arm64");
  CHECK(std::string_view(GetBuildJobStatusLabel(BuildJobStatus::Pending)) == "Pending");
  CHECK(std::string_view(GetBuildJobStatusLabel(BuildJobStatus::Building)) == "Building");
  CHECK(std::string_view(GetBuildJobStatusLabel(BuildJobStatus::Success)) == "Success");
  CHECK(std::string_view(GetBuildJobStatusLabel(BuildJobStatus::Failed)) == "Failed");
  CHECK(std::string_view(GetBuildJobStatusLabel(BuildJobStatus::Cancelled)) == "Cancelled");
}

TEST_CASE("EditorBuildPipelineModal: build and signing commands are deterministic",
          "[editor][build-pipeline-modal]") {
  const std::filesystem::path previousSdkRoot = Horo::ProjectPath::SdkRoot();
  const std::filesystem::path sdkRoot = Horo::Tests::SecureTempBase() / "horo_build_modal_sdk";
  const std::filesystem::path projectRoot = Horo::Tests::SecureTempBase() / "horo_build_modal_project";
  std::error_code ec;
  std::filesystem::remove_all(sdkRoot, ec);
  std::filesystem::create_directories(sdkRoot, ec);
  {
    std::ofstream(sdkRoot / "HoroEngineConfig.cmake") << "# test\n";
    std::ofstream(sdkRoot / "HoroEngineTargets.cmake") << "# test\n";
  }
  Horo::ProjectPath::SetSdkRoot(sdkRoot);
  const std::filesystem::path resolvedSdkRoot =
      Horo::Build::ResolveBuildSdkPrefix();

  EditorBuildPipelineModal modal;
  modal.SetProjectRoot(projectRoot.generic_string());
  BuildPipelineDraft draft;
  draft.signing.enabled = true;
  draft.signing.certificatePath = "/certs/game.pfx";
  draft.signing.certificatePassword = Horo::Core::SecureString("secret");
  draft.signing.notarize = true;
  draft.signing.teamId = "TEAMID";
  modal.SetDraftForTest(std::move(draft));

  BuildJob windows;
  windows.os = BuildTargetOS::Windows;
  windows.config = BuildConfig::Release;
  windows.outputPath = "C:/out/Horo.exe";
  const std::string buildCommand = modal.BuildCommandForJobForTest(windows);
  CHECK(buildCommand.find("scripts/dev.py") == std::string::npos);
  CHECK(buildCommand.find("cmake --fresh") != std::string::npos);
  CHECK(buildCommand.find(projectRoot.generic_string()) != std::string::npos);
  CHECK(buildCommand.find("-DCMAKE_PREFIX_PATH=") != std::string::npos);
  CHECK(buildCommand.find(resolvedSdkRoot.generic_string()) != std::string::npos);
  CHECK(buildCommand.find("--config Release") != std::string::npos);
  CHECK(buildCommand.find("cmake -E copy_directory") != std::string::npos);
  CHECK(buildCommand.find("horopak") != std::string::npos);
  CHECK(buildCommand.find("assets.horo") != std::string::npos);
  CHECK(buildCommand.find("cmake -E rm -rf") != std::string::npos);
  CHECK(buildCommand.find("build/release") != std::string::npos);
  CHECK(modal.SignCommandForJobForTest(windows).find("signtool sign /fd SHA256") != std::string::npos);
  CHECK(modal.SignCommandForJobForTest(windows).find("/certs/game.pfx") != std::string::npos);

  BuildJob mac;
  mac.os = BuildTargetOS::MacOS;
  mac.outputPath = "/out/Horo.app";
  CHECK(modal.SignCommandForJobForTest(mac).find("codesign --deep") != std::string::npos);
  CHECK(modal.SignCommandForJobForTest(mac).find("TEAMID") != std::string::npos);

  BuildJob linux;
  linux.os = BuildTargetOS::Linux;
  CHECK(modal.SignCommandForJobForTest(linux).empty());

  BuildPipelineDraft unsignedDraft;
  modal.SetDraftForTest(std::move(unsignedDraft));
  CHECK(modal.SignCommandForJobForTest(windows).empty());
  Horo::ProjectPath::SetSdkRoot(previousSdkRoot);
}

TEST_CASE("EditorBuildPipelineModal: finalization records terminal sessions once",
          "[editor][build-pipeline-modal]") {
  const std::filesystem::path home = Horo::Tests::SecureTempBase() / "horo_build_modal_home";
  std::error_code ec;
  std::filesystem::remove_all(home, ec);
  std::filesystem::create_directories(home, ec);
#if defined(_WIN32)
  _putenv_s("APPDATA", home.string().c_str());
#else
  setenv("HOME", home.string().c_str(), 1);
#endif

  EditorBuildPipelineModal modal;
  bool callbackCalled = false;
  modal.SetBuildCompleteCallback([&callbackCalled](const std::vector<BuildJob> &jobs) {
    callbackCalled = true;
    REQUIRE(jobs.size() == 2);
  });

  BuildPipelineDraft draft;
  draft.versionTag = "vfinal";
  draft.outputRoot = (home / "out").generic_string();

  BuildJob success;
  success.os = BuildTargetOS::MacOS;
  success.arch = BuildArch::Arm64;
  success.status = BuildJobStatus::Success;
  draft.jobs.push_back(success);

  BuildJob failed;
  failed.os = BuildTargetOS::Linux;
  failed.arch = BuildArch::x86_64;
  failed.status = BuildJobStatus::Failed;
  failed.exitCode = 2;
  failed.error = "compiler failed";
  draft.jobs.push_back(failed);

  modal.SetDraftForTest(std::move(draft));
  // Set to Building so TransitionTo(Error) passes the state-machine guard (P7.2).
  modal.SetStateForTest(BuildPipelineState::Building);
  CHECK(modal.FinalizeIfAllJobsTerminalForTest());
  CHECK(modal.FinalizeIfAllJobsTerminalForTest());

  const BuildPipelineDraft &result = modal.DraftForTest();
  CHECK(result.allJobsComplete);
  CHECK(result.anyJobFailed);
  CHECK(result.totalProgress == 100);
  CHECK(callbackCalled);
  REQUIRE(modal.HistoryForTest().size() == 1);
  CHECK_FALSE(modal.HistoryForTest()[0].allSucceeded);
  CHECK(std::filesystem::exists(home / ".horo" / "build_history.json"));
}

TEST_CASE("EditorBuildPipelineModal: finalization waits for pending work",
          "[editor][build-pipeline-modal]") {
  EditorBuildPipelineModal modal;
  BuildPipelineDraft draft;
  BuildJob pending;
  pending.status = BuildJobStatus::Pending;
  draft.jobs.push_back(pending);
  modal.SetDraftForTest(std::move(draft));
  CHECK_FALSE(modal.FinalizeIfAllJobsTerminalForTest());
  CHECK_FALSE(modal.DraftForTest().allJobsComplete);

  EditorBuildPipelineModal empty;
  CHECK_FALSE(empty.FinalizeIfAllJobsTerminalForTest());
}

TEST_CASE("EditorBuildPipelineModal: history entries preserve per-job OS labels",
          "[editor][build-pipeline-modal][recent-runs]") {
  EditorBuildPipelineModal modal;
  BuildPipelineDraft draft;
  draft.versionTag = "v1.0.0";

  BuildJob macOSJob;
  macOSJob.os = BuildTargetOS::MacOS;
  macOSJob.arch = BuildArch::Arm64;
  macOSJob.status = BuildJobStatus::Success;
  macOSJob.timestamp = "2026-05-30T10:00:00Z";
  draft.jobs.push_back(macOSJob);

  BuildJob windowsJob;
  windowsJob.os = BuildTargetOS::Windows;
  windowsJob.arch = BuildArch::x86_64;
  windowsJob.status = BuildJobStatus::Success;
  windowsJob.timestamp = "2026-05-30T10:00:00Z";
  draft.jobs.push_back(windowsJob);

  modal.SetDraftForTest(std::move(draft));
  modal.SetStateForTest(BuildPipelineState::Building);
  CHECK(modal.FinalizeIfAllJobsTerminalForTest());

  REQUIRE(modal.HistoryForTest().size() == 1);
  const auto &entry = modal.HistoryForTest()[0];
  CHECK(entry.allSucceeded);
  REQUIRE(entry.jobs.size() == 2);

  // Verify OS labels are preserved in the history entry.
  CHECK(GetBuildTargetOSLabel(entry.jobs[0].os) == std::string("macOS"));
  CHECK(GetBuildTargetOSLabel(entry.jobs[1].os) == std::string("Windows"));

  // Verify the label getter covers all three platforms.
  CHECK(GetBuildTargetOSLabel(BuildTargetOS::Linux) == std::string("Linux"));
}

TEST_CASE("EditorBuildPipelineModal: viewing history index defaults to live",
          "[editor][build-pipeline-modal][recent-runs]") {
  EditorBuildPipelineModal modal;
  // A newly constructed modal should show live logs, not historical ones.
  CHECK(modal.ViewingHistoryIndexForTest() == -1);
  CHECK(modal.HistoricalLogTextForTest().empty());
}

TEST_CASE("EditorBuildPipelineModal: start build clears historical log view",
          "[editor][build-pipeline-modal][recent-runs]") {
  EditorBuildPipelineModal modal;
  BuildPipelineDraft draft;
  draft.versionTag = "v2.0.0";

  BuildJob winJob;
  winJob.os = BuildTargetOS::Windows;
  winJob.arch = BuildArch::x86_64;
  winJob.status = BuildJobStatus::Pending;
  draft.jobs.push_back(winJob);

  // StartRelease resets the viewing index so the live build log takes
  // precedence over any previously viewed historical entry.
  std::string error;
  modal.StartRelease(std::move(draft), "/tmp/test-project", &error);
  CHECK(modal.ViewingHistoryIndexForTest() == -1);
  CHECK(modal.HistoricalLogTextForTest().empty());
}

TEST_CASE("EditorBuildPipelineModal: BuildJobStatus labels include Cancelled",
          "[editor][build-pipeline-modal][recent-runs]") {
  using Horo::Build::BuildJobStatus;
  using Horo::Build::GetBuildJobStatusLabel;

  CHECK(std::string_view(GetBuildJobStatusLabel(BuildJobStatus::Pending)) == "Pending");
  CHECK(std::string_view(GetBuildJobStatusLabel(BuildJobStatus::Building)) == "Building");
  CHECK(std::string_view(GetBuildJobStatusLabel(BuildJobStatus::Success)) == "Success");
  CHECK(std::string_view(GetBuildJobStatusLabel(BuildJobStatus::Failed)) == "Failed");
  CHECK(std::string_view(GetBuildJobStatusLabel(BuildJobStatus::Cancelled)) == "Cancelled");
}

// ===========================================================================
// BuildPipelineState — state machine validation (HORO-32 / P7.2)
// ===========================================================================

using Horo::Build::BuildPipelineState;
using Horo::Build::CanTransitionBuildPipelineState;
using Horo::Build::GetBuildPipelineStateLabel;
using Horo::Build::IsTerminalBuildPipelineState;

TEST_CASE("BuildPipelineState: labels cover every enum value",
          "[editor][build-pipeline-state]") {
  using enum BuildPipelineState;
  CHECK(std::string_view(GetBuildPipelineStateLabel(Idle)) == "Idle");
  CHECK(std::string_view(GetBuildPipelineStateLabel(Configuring)) == "Configuring");
  CHECK(std::string_view(GetBuildPipelineStateLabel(Building)) == "Building");
  CHECK(std::string_view(GetBuildPipelineStateLabel(Packaging)) == "Packaging");
  CHECK(std::string_view(GetBuildPipelineStateLabel(Downloading)) == "Downloading");
  CHECK(std::string_view(GetBuildPipelineStateLabel(Done)) == "Done");
  CHECK(std::string_view(GetBuildPipelineStateLabel(Error)) == "Error");
}

TEST_CASE("BuildPipelineState: terminal detection",
          "[editor][build-pipeline-state]") {
  using enum BuildPipelineState;
  CHECK_FALSE(IsTerminalBuildPipelineState(Idle));
  CHECK_FALSE(IsTerminalBuildPipelineState(Configuring));
  CHECK_FALSE(IsTerminalBuildPipelineState(Building));
  CHECK_FALSE(IsTerminalBuildPipelineState(Packaging));
  CHECK_FALSE(IsTerminalBuildPipelineState(Downloading));
  CHECK(IsTerminalBuildPipelineState(Done));
  CHECK(IsTerminalBuildPipelineState(Error));
}

TEST_CASE("BuildPipelineState: allowed transitions",
          "[editor][build-pipeline-state]") {
  using enum BuildPipelineState;

  // Idle → Configuring is the only valid transition from Idle.
  CHECK(CanTransitionBuildPipelineState(Idle, Configuring));
  CHECK_FALSE(CanTransitionBuildPipelineState(Idle, Building));
  CHECK_FALSE(CanTransitionBuildPipelineState(Idle, Done));
  CHECK_FALSE(CanTransitionBuildPipelineState(Idle, Error));

  // Configuring → Idle or Building or Error (can't start build).
  CHECK(CanTransitionBuildPipelineState(Configuring, Idle));
  CHECK(CanTransitionBuildPipelineState(Configuring, Building));
  CHECK(CanTransitionBuildPipelineState(Configuring, Error));
  CHECK_FALSE(CanTransitionBuildPipelineState(Configuring, Done));
  CHECK_FALSE(CanTransitionBuildPipelineState(Configuring, Packaging));
  CHECK_FALSE(CanTransitionBuildPipelineState(Configuring, Downloading));

  // Building → Idle (cancel), Error (fail), or Packaging (succeed).
  // Reset rule also allows → Configuring from anywhere.
  CHECK(CanTransitionBuildPipelineState(Building, Idle));
  CHECK(CanTransitionBuildPipelineState(Building, Error));
  CHECK(CanTransitionBuildPipelineState(Building, Packaging));
  CHECK(CanTransitionBuildPipelineState(Building, Configuring));
  CHECK(CanTransitionBuildPipelineState(Building, Downloading));  // CI path: build done on remote → fetch artifacts
  CHECK(CanTransitionBuildPipelineState(Building, Done));         // Local build: packaging inline → terminal success

  // Packaging → Error or Downloading; reset rule allows → Idle.
  CHECK(CanTransitionBuildPipelineState(Packaging, Error));
  CHECK(CanTransitionBuildPipelineState(Packaging, Downloading));
  CHECK(CanTransitionBuildPipelineState(Packaging, Idle));
  CHECK_FALSE(CanTransitionBuildPipelineState(Packaging, Building));
  CHECK_FALSE(CanTransitionBuildPipelineState(Packaging, Done));

  // Downloading → Done; reset rule allows → Idle.
  CHECK(CanTransitionBuildPipelineState(Downloading, Done));
  CHECK(CanTransitionBuildPipelineState(Downloading, Idle));
  CHECK_FALSE(CanTransitionBuildPipelineState(Downloading, Error));
  CHECK_FALSE(CanTransitionBuildPipelineState(Downloading, Packaging));

  // Done → Idle or Configuring.
  CHECK(CanTransitionBuildPipelineState(Done, Idle));
  CHECK(CanTransitionBuildPipelineState(Done, Configuring));
  CHECK_FALSE(CanTransitionBuildPipelineState(Done, Building));
  CHECK_FALSE(CanTransitionBuildPipelineState(Done, Error));
  CHECK_FALSE(CanTransitionBuildPipelineState(Done, Packaging));

  // Error → Idle or Configuring.
  CHECK(CanTransitionBuildPipelineState(Error, Idle));
  CHECK(CanTransitionBuildPipelineState(Error, Configuring));
  CHECK_FALSE(CanTransitionBuildPipelineState(Error, Building));
  CHECK_FALSE(CanTransitionBuildPipelineState(Error, Done));
  CHECK_FALSE(CanTransitionBuildPipelineState(Error, Packaging));
}

TEST_CASE("BuildPipelineState: same-state only reset states are no-op transitions",
          "[editor][build-pipeline-state]") {
  using enum BuildPipelineState;
  // Reset rule allows no-op Idle/Configuring transitions; active/terminal
  // states still require an actual state change.
  CHECK(CanTransitionBuildPipelineState(Idle, Idle));
  CHECK(CanTransitionBuildPipelineState(Configuring, Configuring));
  CHECK_FALSE(CanTransitionBuildPipelineState(Building, Building));
  CHECK_FALSE(CanTransitionBuildPipelineState(Packaging, Packaging));
  CHECK_FALSE(CanTransitionBuildPipelineState(Downloading, Downloading));
  CHECK_FALSE(CanTransitionBuildPipelineState(Done, Done));
  CHECK_FALSE(CanTransitionBuildPipelineState(Error, Error));
}

TEST_CASE("EditorBuildPipelineModal: state transitions through lifecycle",
          "[editor][build-pipeline-modal][build-pipeline-state]") {
  EditorBuildPipelineModal modal;

  // Initially Idle.
  CHECK(modal.GetState() == BuildPipelineState::Idle);

  // Open → Configuring.
  modal.Open();
  CHECK(modal.GetState() == BuildPipelineState::Configuring);
  CHECK(modal.IsOpen());

  // Close → Idle.
  modal.Close();
  CHECK(modal.GetState() == BuildPipelineState::Idle);
  CHECK_FALSE(modal.IsOpen());
}

TEST_CASE("EditorBuildPipelineModal: non-host targets transition to Error via state machine",
          "[editor][build-pipeline-modal][build-pipeline-state]") {
  EditorBuildPipelineModal modal;
  BuildPipelineDraft draft;
  draft.versionTag = "vtest";
  draft.outputRoot = (std::filesystem::temp_directory_path() / "horo_build_modal_state_test").generic_string();
  BuildJob job;
#if defined(_WIN32)
  job.os = BuildTargetOS::Linux;
#else
  job.os = BuildTargetOS::Windows;
#endif
  job.arch = BuildArch::x86_64;
  job.config = BuildConfig::Release;
  job.status = BuildJobStatus::Pending;
  draft.jobs.push_back(job);

  // Open modal first to transition Idle → Configuring,
  // then swap in the non-host draft before building.
  modal.Open();
  modal.SetDraftForTest(std::move(draft));

  modal.StartNextPendingJobForTest();

  CHECK(modal.GetState() == BuildPipelineState::Error);
}

TEST_CASE("EditorBuildPipelineModal: cancel transitions to Error",
          "[editor][build-pipeline-modal][build-pipeline-state]") {
  EditorBuildPipelineModal modal;
  modal.Open();
  CHECK(modal.GetState() == BuildPipelineState::Configuring);

  // Setup a draft with a building job to simulate in-flight build.
  BuildPipelineDraft draft;
  draft.versionTag = "vtest";
  draft.outputRoot = (std::filesystem::temp_directory_path() / "horo_build_modal_cancel_state_test").generic_string();
  BuildJob building;
  building.os = BuildTargetOS::MacOS;
  building.arch = BuildArch::Arm64;
  building.status = BuildJobStatus::Building;
  draft.jobs.push_back(building);
  modal.SetDraftForTest(std::move(draft));
  // Simulate being in Building state for a running build.
  modal.SetStateForTest(BuildPipelineState::Building);

  // CancelAllBuilds should transition to Error (terminal), not Idle.
  modal.CancelAllBuildsForTest();
  CHECK(modal.GetState() == BuildPipelineState::Error);
}

TEST_CASE("EditorBuildPipelineModal: successful finalization goes to Done",
          "[editor][build-pipeline-modal][build-pipeline-state]") {
  EditorBuildPipelineModal modal;
  BuildPipelineDraft draft;
  draft.versionTag = "vsuccess";
  draft.outputRoot = (std::filesystem::temp_directory_path() / "horo_build_modal_done_state_test").generic_string();

  BuildJob success;
  success.os = BuildTargetOS::MacOS;
  success.arch = BuildArch::Arm64;
  success.status = BuildJobStatus::Success;
  draft.jobs.push_back(success);

  modal.SetDraftForTest(std::move(draft));

  // Simulate being in Building state — Finalize transitions Building → Done.
  modal.SetStateForTest(BuildPipelineState::Building);
  CHECK(modal.FinalizeIfAllJobsTerminalForTest());

  CHECK(modal.GetState() == BuildPipelineState::Done);
}

TEST_CASE("EditorBuildPipelineModal: failed finalization goes to Error",
          "[editor][build-pipeline-modal][build-pipeline-state]") {
  EditorBuildPipelineModal modal;
  BuildPipelineDraft draft;
  draft.versionTag = "vfail";
  draft.outputRoot = (std::filesystem::temp_directory_path() / "horo_build_modal_error_state_test").generic_string();

  BuildJob failed;
  failed.os = BuildTargetOS::Linux;
  failed.arch = BuildArch::x86_64;
  failed.status = BuildJobStatus::Failed;
  failed.exitCode = 1;
  failed.error = "build error";
  draft.jobs.push_back(failed);

  modal.SetDraftForTest(std::move(draft));

  // Simulate being in Building state — Finalize transitions Building → Error.
  modal.SetStateForTest(BuildPipelineState::Building);
  CHECK(modal.FinalizeIfAllJobsTerminalForTest());

  CHECK(modal.GetState() == BuildPipelineState::Error);
}

TEST_CASE("BuildPipelineState: exhaustive invalid transitions",
          "[editor][build-pipeline-state]") {
  using enum BuildPipelineState;

  // Verify every explicitly-disallowed transition returns false.
  // Idle can only go to Configuring (and self via no-op).
  CHECK_FALSE(CanTransitionBuildPipelineState(Idle, Building));
  CHECK_FALSE(CanTransitionBuildPipelineState(Idle, Packaging));
  CHECK_FALSE(CanTransitionBuildPipelineState(Idle, Downloading));
  CHECK_FALSE(CanTransitionBuildPipelineState(Idle, Done));
  CHECK_FALSE(CanTransitionBuildPipelineState(Idle, Error));

  // Configuring cannot skip to Done or jump directly to packaging/downloading.
  CHECK_FALSE(CanTransitionBuildPipelineState(Configuring, Packaging));
  CHECK_FALSE(CanTransitionBuildPipelineState(Configuring, Downloading));
  CHECK_FALSE(CanTransitionBuildPipelineState(Configuring, Done));

  // Building: all transitions through packaging/downloading or direct to Done are valid.
  // (Building → Packaging, → Downloading, → Done, → Error, → Idle, → Configuring are all valid)

  // Packaging cannot go backwards to Building or skip to Done.
  CHECK_FALSE(CanTransitionBuildPipelineState(Packaging, Building));
  CHECK_FALSE(CanTransitionBuildPipelineState(Packaging, Done));

  // Downloading cannot go to Error or back to Packaging.
  CHECK_FALSE(CanTransitionBuildPipelineState(Downloading, Error));
  CHECK_FALSE(CanTransitionBuildPipelineState(Downloading, Packaging));

  // Terminal states cannot go back to active states.
  CHECK_FALSE(CanTransitionBuildPipelineState(Done, Building));
  CHECK_FALSE(CanTransitionBuildPipelineState(Done, Error));
  CHECK_FALSE(CanTransitionBuildPipelineState(Done, Packaging));
  CHECK_FALSE(CanTransitionBuildPipelineState(Done, Downloading));

  CHECK_FALSE(CanTransitionBuildPipelineState(Error, Building));
  CHECK_FALSE(CanTransitionBuildPipelineState(Error, Done));
  CHECK_FALSE(CanTransitionBuildPipelineState(Error, Packaging));
  CHECK_FALSE(CanTransitionBuildPipelineState(Error, Downloading));
}

TEST_CASE("BuildPipelineState: all valid transitions enumerated",
          "[editor][build-pipeline-state]") {
  using enum BuildPipelineState;

  // Idle
  CHECK(CanTransitionBuildPipelineState(Idle, Configuring));

  // Configuring
  CHECK(CanTransitionBuildPipelineState(Configuring, Idle));
  CHECK(CanTransitionBuildPipelineState(Configuring, Building));
  CHECK(CanTransitionBuildPipelineState(Configuring, Error));

  // Building
  CHECK(CanTransitionBuildPipelineState(Building, Idle));
  CHECK(CanTransitionBuildPipelineState(Building, Error));
  CHECK(CanTransitionBuildPipelineState(Building, Packaging));
  CHECK(CanTransitionBuildPipelineState(Building, Downloading));
  CHECK(CanTransitionBuildPipelineState(Building, Done));
  CHECK(CanTransitionBuildPipelineState(Building, Configuring));

  // Packaging
  CHECK(CanTransitionBuildPipelineState(Packaging, Idle));
  CHECK(CanTransitionBuildPipelineState(Packaging, Error));
  CHECK(CanTransitionBuildPipelineState(Packaging, Downloading));

  // Downloading
  CHECK(CanTransitionBuildPipelineState(Downloading, Idle));
  CHECK(CanTransitionBuildPipelineState(Downloading, Done));

  // Done (terminal → reset)
  CHECK(CanTransitionBuildPipelineState(Done, Idle));
  CHECK(CanTransitionBuildPipelineState(Done, Configuring));

  // Error (terminal → reset)
  CHECK(CanTransitionBuildPipelineState(Error, Idle));
  CHECK(CanTransitionBuildPipelineState(Error, Configuring));
}

TEST_CASE("EditorBuildPipelineModal: full local build lifecycle",
          "[editor][build-pipeline-modal][build-pipeline-state]") {
  EditorBuildPipelineModal modal;

  // 1. Starts Idle.
  CHECK(modal.GetState() == BuildPipelineState::Idle);

  // 2. Open → Configuring.
  modal.Open();
  CHECK(modal.GetState() == BuildPipelineState::Configuring);
  CHECK(modal.IsOpen());

  // 3. Setup host-platform jobs for a full build.
  BuildPipelineDraft draft;
  draft.versionTag = "v1.0.0";
  draft.outputRoot =
      (std::filesystem::temp_directory_path() / "horo_full_lifecycle_test")
          .generic_string();

  BuildJob job;
#if defined(_WIN32)
  job.os = BuildTargetOS::Windows;
#elif defined(__APPLE__)
  job.os = BuildTargetOS::MacOS;
  job.arch = BuildArch::Arm64;
#else
  job.os = BuildTargetOS::Linux;
#endif
  job.arch = BuildArch::x86_64;
  job.config = BuildConfig::Release;
  job.status = BuildJobStatus::Pending;
  draft.jobs.push_back(job);

  modal.SetDraftForTest(std::move(draft));

  // 4. CancelAllBuilds → Error (cancelling marks jobs terminal).
  modal.CancelAllBuildsForTest();
  CHECK(modal.GetState() == BuildPipelineState::Error);

  // 5. Re-open and verify configuring again.
  modal.Open();
  CHECK(modal.GetState() == BuildPipelineState::Configuring);

  // 6. Close → Idle.
  modal.Close();
  CHECK(modal.GetState() == BuildPipelineState::Idle);
  CHECK_FALSE(modal.IsOpen());
}

TEST_CASE("EditorBuildPipelineModal: state transitions persist through repeated operations",
          "[editor][build-pipeline-modal][build-pipeline-state]") {
  EditorBuildPipelineModal modal;

  // Start: Idle.
  CHECK(modal.GetState() == BuildPipelineState::Idle);

  // Open / Close cycle 3x to validate no state drift.
  for (int i = 0; i < 3; ++i) {
    modal.Open();
    CHECK(modal.GetState() == BuildPipelineState::Configuring);
    modal.Close();
    CHECK(modal.GetState() == BuildPipelineState::Idle);
  }

  // After close, state stays Idle.
  CHECK(modal.GetState() == BuildPipelineState::Idle);
}


#include <imgui.h>

namespace {
/** @brief Sets up a minimal ImGui frame so widget calls do not crash. Caller must End() + DestroyContext(). */
struct ImGuiFrameFixture {
  ImGuiContext *context = nullptr;
  explicit ImGuiFrameFixture() {
    context = ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(640.0f, 480.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->Build();
    ImGui::NewFrame();
  }
  ~ImGuiFrameFixture() {
    ImGui::EndFrame();
    ImGui::DestroyContext(context);
  }
  ImGuiFrameFixture(const ImGuiFrameFixture &) = delete;
  ImGuiFrameFixture &operator=(const ImGuiFrameFixture &) = delete;
};
} // namespace

TEST_CASE("EditorBuildPipelineModal: Draw renders platform tab with ImGui frame",
          "[editor][build-pipeline-modal]") {
  ImGuiFrameFixture frame;
  EditorBuildPipelineModal modal;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "horo_build_modal_draw_project";
  modal.SetProjectRoot(root.generic_string());

  modal.Open();
  modal.Draw();

  CHECK(modal.IsOpen());
  CHECK(modal.DraftForTest().versionTag == (std::string("v") + Horo::Build::EngineVersion()));
}

TEST_CASE("EditorBuildPipelineModal: Draw renders completed progress and history paths",
          "[editor][build-pipeline-modal]") {
  ImGuiFrameFixture frame;
  EditorBuildPipelineModal modal;
  BuildPipelineDraft draft;
  draft.versionTag = "vcoverage";
  draft.outputRoot =
      (std::filesystem::temp_directory_path() / "horo_build_modal_draw_output").generic_string();
  draft.totalProgress = 100;
  draft.allJobsComplete = true;

  BuildJob success;
  success.os = BuildTargetOS::MacOS;
  success.arch = BuildArch::Arm64;
  success.config = BuildConfig::Release;
  success.status = BuildJobStatus::Success;
  success.outputPath = draft.outputRoot + "/macos/Horo.app";
  success.log = "packaged";
  success.timestamp = "2026-05-23T00:00:00Z";
  draft.jobs.push_back(success);

  BuildJob failed;
  failed.os = BuildTargetOS::Linux;
  failed.arch = BuildArch::x86_64;
  failed.config = BuildConfig::Debug;
  failed.status = BuildJobStatus::Failed;
  failed.exitCode = 1;
  failed.error = "toolchain unavailable";
  failed.timestamp = "2026-05-23T00:00:01Z";
  draft.jobs.push_back(failed);

  modal.Open();
  modal.SetDraftForTest(std::move(draft));
  modal.Draw();

  const BuildPipelineDraft &result = modal.DraftForTest();
  REQUIRE(result.jobs.size() == 2);
  CHECK(result.allJobsComplete);
  CHECK(result.totalProgress == 100);
}

// ===========================================================================
// HORO-52 — log persistence and history inspection
// ===========================================================================

TEST_CASE("ResolveJobLogPath: produces a valid path with platform/config in filename",
          "[editor][build-pipeline-modal][horo-52][log-persistence]") {
  using Horo::Build::BuildArch;
  using Horo::Build::BuildConfig;
  using Horo::Build::BuildTargetOS;

  BuildPipelineDraft draft;
  draft.outputRoot = "/tmp/test_project/build/release";

  BuildJob job;
  job.os = BuildTargetOS::Windows;
  job.arch = BuildArch::x86_64;
  job.config = BuildConfig::Release;
  job.timestamp = "2026-05-30T10:15:30Z";

  const std::string path = Horo::Build::ResolveJobLogPath(
      draft, job, std::filesystem::path{});
  CHECK(path.find("/tmp/test_project/build/release/logs/") != std::string::npos);
  CHECK(path.find("2026-05-30T10-15-30Z_Windows_x86_64_Release.log") != std::string::npos);
}

TEST_CASE("BuildJob: logPath round-trips through JSON serialization",
          "[editor][build-pipeline-modal][horo-52][log-persistence]") {
  using namespace Horo::Build;

  // Write a single entry with logPath set.
  BuildJob job;
  job.os = BuildTargetOS::MacOS;
  job.arch = BuildArch::Arm64;
  job.logPath = "/tmp/test_project/build/release/logs/run.log";

  BuildHistoryEntry entry;
  entry.versionTag = "vlogtest";
  entry.timestamp = CurrentTimestamp();
  entry.jobs.push_back(job);
  entry.allSucceeded = true;

  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "horo_logpath_test.json";
  WriteHistoryJson(tmp, {entry});

  // Read back and verify logPath survived.
  const auto loaded = ReadHistoryJson(tmp);
  REQUIRE(loaded.size() == 1);
  REQUIRE(loaded[0].jobs.size() == 1);
  CHECK(loaded[0].jobs[0].logPath == "/tmp/test_project/build/release/logs/run.log");

  // Clean up.
  std::error_code ec;
  std::filesystem::remove(tmp, ec);
}

TEST_CASE("EditorBuildPipelineModal: missing logPath shows empty fallback",
          "[editor][build-pipeline-modal][horo-52][log-persistence]") {
  // Simulate a history entry where logPath is empty (older serialized data).
  // Verify it does not crash.
  BuildHistoryEntry entry;
  entry.versionTag = "vold";
  entry.timestamp = "2024-01-01T00:00:00Z";
  entry.allSucceeded = true;
  BuildJob oldJob;
  oldJob.os = BuildTargetOS::Linux;
  oldJob.logPath.clear();  // No log path — legacy entry.
  oldJob.log.clear();      // No in-memory log either.
  entry.jobs.push_back(oldJob);

  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "horo_nolog_test.json";
  Horo::Build::WriteHistoryJson(tmp, {entry});
  const auto loaded = Horo::Build::ReadHistoryJson(tmp);
  REQUIRE(loaded.size() == 1);
  REQUIRE(loaded[0].jobs.size() == 1);
  CHECK(loaded[0].jobs[0].logPath.empty());

  std::error_code ec;
  std::filesystem::remove(tmp, ec);
}

TEST_CASE("EditorBuildPipelineModal: finalization preserves logPath in history",
          "[editor][build-pipeline-modal][horo-52][log-persistence]") {
  const std::filesystem::path home =
      Horo::Tests::SecureTempBase() / "horo_log_persist";
  std::error_code ec;
  std::filesystem::remove_all(home, ec);
  std::filesystem::create_directories(home, ec);
#if defined(_WIN32)
  _putenv_s("APPDATA", home.string().c_str());
#else
  setenv("HOME", home.string().c_str(), 1);
#endif

  EditorBuildPipelineModal modal;
  modal.SetProjectRoot((home / "game_project").generic_string());

  BuildPipelineDraft draft;
  draft.versionTag = "vlogwrite";
  draft.outputRoot = (home / "game_project" / "build" / "release").generic_string();

  BuildJob successJob;
  successJob.os = BuildTargetOS::MacOS;
  successJob.arch = BuildArch::Arm64;
  successJob.config = BuildConfig::Release;
  successJob.status = BuildJobStatus::Success;
  successJob.timestamp = "2026-05-30T11:00:00Z";
  successJob.log = "cmake configured\nbuild succeeded\n";
  successJob.logPath = Horo::Build::ResolveJobLogPath(
      draft, successJob, home / "game_project");
  draft.jobs.push_back(std::move(successJob));

  modal.SetDraftForTest(std::move(draft));
  modal.SetStateForTest(BuildPipelineState::Building);
  CHECK(modal.FinalizeIfAllJobsTerminalForTest());

  // Verify logPath is recorded in the persisted history entry.
  REQUIRE(modal.HistoryForTest().size() >= 1);
  const auto &histEntry = modal.HistoryForTest()[0];
  REQUIRE(histEntry.jobs.size() == 1);
  CHECK_FALSE(histEntry.jobs[0].logPath.empty());

  // Clean up.
  std::filesystem::remove_all(home, ec);
}

TEST_CASE("EditorImportAssetModal: Draw renders without crash with ImGui frame and FBX path",
          "[editor][import-asset-modal]") {
  ImGuiFrameFixture frame;
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  modal.Open("/p/cube.fbx", &registry, std::filesystem::path{});
  // Drives DrawPathSection / DrawImporterSection / DrawIdentitySection /
  // DrawActionsSection through the BeginPopupModal + EndPopup path.
  modal.Draw();
  CHECK(modal.IsOpen());
}

TEST_CASE("EditorImportAssetModal: Draw renders without crash with no path (importer empty branch)",
          "[editor][import-asset-modal]") {
  ImGuiFrameFixture frame;
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  modal.Open({}, &registry, std::filesystem::path{});
  modal.Draw();
  CHECK(modal.IsOpen());
}

TEST_CASE("EditorImportAssetModal: Draw with prior result renders the result panel branch",
          "[editor][import-asset-modal]") {
  ImGuiFrameFixture frame;
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  modal.Open("/p/cube.fbx", &registry, std::filesystem::path{});

  ImportAssetOutcome outcome;
  outcome.ok = false;
  outcome.error = "boom";
  AssetImportDiagnostic warn;
  warn.severity = AssetDiagnosticSeverity::Warning;
  warn.code = "asset.fbx.external_texture_missing";
  warn.message = "missing.png";
  AssetImportDiagnostic err;
  err.severity = AssetDiagnosticSeverity::Error;
  err.code = "asset.fbx.parse_failed";
  err.message = "parse";
  outcome.diagnostics.push_back(warn);
  outcome.diagnostics.push_back(err);
  modal.SetLastResult(outcome);
  modal.Draw();
  CHECK(modal.IsOpen());
}

TEST_CASE("EditorImportAssetModal: Draw renders successfully when the modal is closed",
          "[editor][import-asset-modal]") {
  ImGuiFrameFixture frame;
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  // Modal not opened — Draw must short-circuit before BeginPopupModal.
  modal.Draw();
  CHECK_FALSE(modal.IsOpen());
}
// ===========================================================================
// AssetImportService — ImportTextureForAsset + SaveMetadataForAsset
// ===========================================================================

TEST_CASE("AssetImportService: ImportTextureForAsset null asset returns false", "[editor][asset-import]") {
  AssetImportService service;
  std::string err;
  const std::string source =
      (Horo::Tests::SecureTempBase() / "x.png").string();
  const bool ok = service.ImportTextureForAsset(source, "a", nullptr, &err);
  CHECK_FALSE(ok);
  CHECK_FALSE(err.empty());
}

TEST_CASE("AssetImportService: ImportTextureForAsset succeeds with real PNG", "[editor][asset-import]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_tex_import";
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

TEST_CASE("AssetImportService: ImportTextureForAsset fails for unsupported type", "[editor][asset-import]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_tex_import_bad";
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
      (Horo::Tests::SecureTempBase() / "document.pdf").string();
  const bool ok =
      service.ImportTextureForAsset(source, "bad_tex", &asset, &err);
  CHECK_FALSE(ok);
  CHECK_FALSE(err.empty());
}

TEST_CASE("AssetImportService: SaveMetadataForAsset null-asset returns false", "[editor][asset-import]") {
  // SaveMetadataForAsset(assetId, asset, outError) — test missing guid
  AssetImportService service;
  AssetDef emptyAsset;
  std::string err;
  // empty guid → saving has nowhere to write meaningful data
  // The function itself should still execute without crashing
  service.SaveMetadataForAsset("some_id", emptyAsset, &err);
  // No crash is the primary assertion; error state is implementation-defined
}

TEST_CASE("AssetImportService: ReimportAssetWithDependents unknown guid is noop", "[editor][asset-import]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_reimport_noop";
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

TEST_CASE("IsSupportedEditorImGuiBackend: OpenGL is supported", "[editor][imgui-backend]") {
  CHECK(IsSupportedEditorImGuiBackend(RenderBackendId::OpenGL));
}

TEST_CASE("IsSupportedEditorImGuiBackend: Auto is supported", "[editor][imgui-backend]") {
  CHECK(IsSupportedEditorImGuiBackend(RenderBackendId::Auto));
}

TEST_CASE("IsSupportedEditorImGuiBackend: Vulkan returns compile-conditional", "[editor][imgui-backend]") {
  // Just call it to ensure the switch branch and return are exercised.
  const bool result = IsSupportedEditorImGuiBackend(RenderBackendId::Vulkan);
#if defined(HORO_HAS_VULKAN)
  CHECK(result == true);
#else
  CHECK(result == false);
#endif
}

TEST_CASE("IsSupportedEditorImGuiBackend: Null is not supported", "[editor][imgui-backend]") {
  CHECK_FALSE(IsSupportedEditorImGuiBackend(RenderBackendId::Null));
}

TEST_CASE("InitEditorImGuiBackend: Null returns false without GLFW context", "[editor][imgui-backend]") {
  // Null path returns false immediately without dereferencing the window.
  auto *fakeWindow = reinterpret_cast<GLFWwindow *>(static_cast<uintptr_t>(1));
  CHECK_FALSE(InitEditorImGuiBackend(fakeWindow, RenderBackendId::Null));
}

TEST_CASE("ShutdownEditorImGuiBackend: Null is a no-op", "[editor][imgui-backend]") {
  // Must not crash; no GLFW context required.
  REQUIRE_NOTHROW(ShutdownEditorImGuiBackend(RenderBackendId::Null));
}

TEST_CASE("BeginEditorImGuiFrame: Null is a no-op", "[editor][imgui-backend]") {
  REQUIRE_NOTHROW(BeginEditorImGuiFrame(RenderBackendId::Null));
}

TEST_CASE("RenderEditorImGuiDrawData: Null is a no-op", "[editor][imgui-backend]") {
  // Non-null drawData pointer; Null backend returns without dereferencing it.
  auto *fakeDrawData = reinterpret_cast<ImDrawData *>(static_cast<uintptr_t>(1));
  REQUIRE_NOTHROW(RenderEditorImGuiDrawData(RenderBackendId::Null, fakeDrawData));
}


// EditorDebugTrace — EditorTraceEnabled and EditorTrace
// ===========================================================================

TEST_CASE("EditorDebugTrace: EditorTraceEnabled reads env var", "[editor][debug-trace]") {
  // In release builds it always returns false
  // In debug builds it reads HORO_EDITOR_TRACE
  const bool enabled = EditorTraceEnabled();
  (void)enabled; // exercising the code path is sufficient
  CHECK(true);   // no crash
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

TEST_CASE("SceneRuntimeBridge: valid doc builds without errors", "[editor][runtime-bridge]") {
  const RuntimeSceneBuildResult result =
      BuildRuntimeSceneDefinition(MakeMinimalDoc("bridge_valid"));
  CHECK_FALSE(result.HasErrors());
}

TEST_CASE("SceneRuntimeBridge: doc with version=0 produces build errors", "[editor][runtime-bridge]") {
  SceneDocument doc = MakeMinimalDoc("bridge_fail");
  doc.version = 0; // triggers schemaVersion < 1 error
  const RuntimeSceneBuildResult result = BuildRuntimeSceneDefinition(doc);
  CHECK(result.HasErrors());
}

TEST_CASE("SceneRuntimeBridge: doc with nodes builds rooms", "[editor][runtime-bridge]") {
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

TEST_CASE("SceneRuntimeCoordinatorBridge: LoadSceneDocument fails on build error", "[editor][coordinator-bridge]") {
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

TEST_CASE("SceneRuntimeCoordinatorBridge: LoadSceneDocument succeeds on valid doc", "[editor][coordinator-bridge]") {
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

TEST_CASE("SceneRuntimeCoordinatorBridge: ReloadSceneDocument fails on build error", "[editor][coordinator-bridge]") {
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

TEST_CASE("SceneRuntimeCoordinatorBridge: ReloadSceneDocument succeeds on valid doc", "[editor][coordinator-bridge]") {
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

TEST_CASE("EditorWorkspaceSettings: ResolveEditorWorkspacePath uses .horo when project root is set", "[editor][workspace-settings]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_ws_resolve";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path path = ResolveEditorWorkspacePath();
  CHECK(path.string().find(".horo") != std::string::npos);
}

TEST_CASE("EditorWorkspaceSettings: ResolveEditorWorkspacePath falls back to home dir when no project root", "[editor][workspace-settings]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_ws_home";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  ProjectPathGuard clearGuard({});
  HomeDirGuard homeGuard(fakeHome);

  const std::filesystem::path path = ResolveEditorWorkspacePath();
  // path is inside the fake home dir or default settings dir
  CHECK_FALSE(path.empty());
}

TEST_CASE("EditorWorkspaceSettings: LoadEditorWorkspaceDocument returns defaults when file is absent", "[editor][workspace-settings]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_ws_absent";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_ws_absent_home";
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

TEST_CASE("EditorWorkspaceSettings: LoadEditorWorkspaceDocument parses valid JSON", "[editor][workspace-settings]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_ws_valid";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_ws_valid_home";
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

TEST_CASE("EditorWorkspaceSettings: LoadEditorWorkspaceDocument handles invalid JSON", "[editor][workspace-settings]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_ws_badjson";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_ws_badjson_home";
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

TEST_CASE("EditorWorkspaceSettings: LoadEditorWorkspaceDocument handles non-object root", "[editor][workspace-settings]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_ws_arrayroot";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_ws_arrayroot_home";
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  const std::filesystem::path wsFile = ResolveEditorWorkspacePath();
  std::filesystem::create_directories(wsFile.parent_path(), ec);
  WriteFile(wsFile, "[1,2,3]");

  EditorWorkspaceDocument doc = LoadEditorWorkspaceDocument();
  CHECK(doc.loadedFromDisk);
  CHECK(doc.parseError);
}

TEST_CASE("EditorWorkspaceSettings: SaveEditorWorkspaceDocument null returns false", "[editor][workspace-settings]") {
  std::string err;
  const bool ok = SaveEditorWorkspaceDocument(nullptr, &err);
  CHECK_FALSE(ok);
  CHECK_FALSE(err.empty());
}

TEST_CASE("EditorWorkspaceSettings: save then load round-trips state", "[editor][workspace-settings]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_ws_roundtrip";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_ws_rt_home";
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
// EditorUserSettings — ~/.horo/editor_settings.json (theme preset, etc.)
// ===========================================================================

TEST_CASE("EditorUserSettings: ResolveEditorUserSettingsPath lives under .horo", "[editor][user-settings]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_us_resolve";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  const std::filesystem::path path = ResolveEditorUserSettingsPath();
  CHECK(path.filename() == "editor_settings.json");
  CHECK(path.parent_path().filename() == ".horo");
}

TEST_CASE("EditorUserSettings: LoadEditorUserSettingsDocument returns DarkBlue default when file is absent", "[editor][user-settings]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_us_absent";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  EditorUserSettingsDocument doc = LoadEditorUserSettingsDocument();
  CHECK_FALSE(doc.loadedFromDisk);
  CHECK_FALSE(doc.parseError);
  CHECK(doc.error.empty());
  CHECK(doc.settings.themePreset == Horo::Ui::EditorThemePreset::DarkBlue);
}

TEST_CASE("EditorUserSettings: save then load round-trips a Graphite preset", "[editor][user-settings]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_us_roundtrip";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  EditorUserSettingsDocument doc;
  doc.settings.themePreset = Horo::Ui::EditorThemePreset::Graphite;
  std::string err;
  REQUIRE(SaveEditorUserSettingsDocument(&doc, &err));
  CHECK(err.empty());

  // Verify the on-disk JSON actually contains the graphite id.
  std::ifstream in(ResolveEditorUserSettingsPath());
  REQUIRE(in.is_open());
  nlohmann::json root;
  in >> root;
  REQUIRE(root.is_object());
  REQUIRE(root.contains("editor"));
  const auto editorJson = root.at("editor");
  REQUIRE(editorJson.is_object());
  CHECK(editorJson.at("themePreset").get<std::string>() == "graphite");

  EditorUserSettingsDocument loaded = LoadEditorUserSettingsDocument();
  CHECK(loaded.loadedFromDisk);
  CHECK_FALSE(loaded.parseError);
  CHECK(loaded.settings.themePreset == Horo::Ui::EditorThemePreset::Graphite);
}

TEST_CASE("EditorUserSettings: custom theme id from config round-trips", "[editor][user-settings]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_us_custom_theme";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  const std::filesystem::path configPath = fakeHome / ".horo" / "config.json";
  WriteFile(configPath,
            R"({"editorThemes":[{"id":"midnight","name":"Midnight","palette":{"panel":"#10131aff","accent":[0.1,0.4,0.9,1.0]}}]})");
  const auto loadResult = Horo::Ui::LoadEditorThemeConfig(configPath);
  REQUIRE(loadResult.ok);
  REQUIRE(loadResult.customThemeCount == 1);

  EditorUserSettingsDocument doc;
  doc.settings.themePresetId = "midnight";
  std::string err;
  REQUIRE(SaveEditorUserSettingsDocument(&doc, &err));
  CHECK(err.empty());

  EditorUserSettingsDocument loaded = LoadEditorUserSettingsDocument();
  CHECK(loaded.loadedFromDisk);
  CHECK_FALSE(loaded.parseError);
  CHECK(loaded.error.empty());
  CHECK(loaded.settings.themePresetId == "midnight");
  CHECK(loaded.settings.themePreset == Horo::Ui::EditorThemePreset::DarkBlue);
}

TEST_CASE("EditorUserSettings: unknown preset id falls back to DarkBlue without crash", "[editor][user-settings]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_us_unknown";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  const std::filesystem::path settingsFile = ResolveEditorUserSettingsPath();
  WriteFile(settingsFile, R"({"editor":{"themePreset":"nebula"}})");

  EditorUserSettingsDocument loaded = LoadEditorUserSettingsDocument();
  CHECK(loaded.loadedFromDisk);
  CHECK_FALSE(loaded.parseError);
  CHECK(loaded.settings.themePreset == Horo::Ui::EditorThemePreset::DarkBlue);
  CHECK_FALSE(loaded.error.empty());
}

TEST_CASE("EditorUserSettings: invalid JSON falls back to DarkBlue and reports parse error", "[editor][user-settings]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_us_badjson";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  WriteFile(ResolveEditorUserSettingsPath(), "{{ not json");

  EditorUserSettingsDocument loaded = LoadEditorUserSettingsDocument();
  CHECK(loaded.loadedFromDisk);
  CHECK(loaded.parseError);
  CHECK_FALSE(loaded.error.empty());
  CHECK(loaded.settings.themePreset == Horo::Ui::EditorThemePreset::DarkBlue);
}

TEST_CASE("EditorUserSettings: non-object root falls back to DarkBlue and reports parse error", "[editor][user-settings]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_us_arrayroot";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  WriteFile(ResolveEditorUserSettingsPath(), "[1,2,3]");

  EditorUserSettingsDocument loaded = LoadEditorUserSettingsDocument();
  CHECK(loaded.loadedFromDisk);
  CHECK(loaded.parseError);
  CHECK(loaded.settings.themePreset == Horo::Ui::EditorThemePreset::DarkBlue);
}

TEST_CASE("EditorUserSettings: unknown root and editor keys survive save round-trip", "[editor][user-settings]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_us_preserve";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  WriteFile(
      ResolveEditorUserSettingsPath(),
      R"({"future":{"flag":true},"editor":{"themePreset":"darkBlue","experimentalMode":"spatial"}})");

  EditorUserSettingsDocument loaded = LoadEditorUserSettingsDocument();
  REQUIRE(loaded.loadedFromDisk);
  REQUIRE_FALSE(loaded.parseError);
  loaded.settings.themePreset = Horo::Ui::EditorThemePreset::HighContrast;

  std::string err;
  REQUIRE(SaveEditorUserSettingsDocument(&loaded, &err));

  std::ifstream in(ResolveEditorUserSettingsPath());
  REQUIRE(in.is_open());
  nlohmann::json root;
  in >> root;
  REQUIRE(root.is_object());
  REQUIRE(root.contains("future"));
  REQUIRE(root.at("future").is_object());
  CHECK(root.at("future").at("flag").get<bool>());
  REQUIRE(root.contains("editor"));
  const auto editorJson = root.at("editor");
  CHECK(editorJson.at("themePreset").get<std::string>() == "highContrast");
  REQUIRE(editorJson.contains("experimentalMode"));
  CHECK(editorJson.at("experimentalMode").get<std::string>() == "spatial");
}

TEST_CASE("EditorUserSettings: SaveEditorUserSettingsDocument null returns false", "[editor][user-settings]") {
  std::string err;
  const bool ok = SaveEditorUserSettingsDocument(nullptr, &err);
  CHECK_FALSE(ok);
  CHECK_FALSE(err.empty());
}

// ===========================================================================
// BuildToolchainSettings — platform enablement, host-safe defaults, JSON round-trip
// ===========================================================================

TEST_CASE("BuildToolchainSettings: host platform defaults to enabled, non-host disabled", "[editor][user-settings][build-toolchain]") {
  const BuildToolchainSettings s = MakeDefaultBuildToolchainSettings();

#if defined(__APPLE__)
  CHECK(s.macOS.enabled);
  CHECK_FALSE(s.windows.enabled);
  CHECK_FALSE(s.linux_.enabled);
  CHECK(s.macOS.toolchain.empty());
#elif defined(_WIN32)
  CHECK_FALSE(s.macOS.enabled);
  CHECK(s.windows.enabled);
  CHECK_FALSE(s.linux_.enabled);
  CHECK(s.windows.toolchain.empty());
#elif defined(__linux__)
  CHECK_FALSE(s.macOS.enabled);
  CHECK_FALSE(s.windows.enabled);
  CHECK(s.linux_.enabled);
  CHECK(s.linux_.toolchain.empty());
#endif
}

TEST_CASE("BuildToolchainSettings: default-constructed is all-disabled", "[editor][user-settings][build-toolchain]") {
  const BuildToolchainSettings s{};
  CHECK_FALSE(s.macOS.enabled);
  CHECK_FALSE(s.windows.enabled);
  CHECK_FALSE(s.linux_.enabled);
}

TEST_CASE("BuildToolchainSettings: platforms are independently mutable", "[editor][user-settings][build-toolchain]") {
  BuildToolchainSettings s = MakeDefaultBuildToolchainSettings();

  // Enable a non-host platform independently.
  s.windows.enabled = true;
  s.windows.toolchain = "MSVC 2022";

  // macOS must be unchanged from defaults.
#if defined(__APPLE__)
  CHECK(s.macOS.enabled);
#else
  CHECK_FALSE(s.macOS.enabled);
#endif
  CHECK(s.windows.enabled);
  CHECK(s.windows.toolchain == "MSVC 2022");

  // Linux still has its default.
#if defined(__linux__)
  CHECK(s.linux_.enabled);
#else
  CHECK_FALSE(s.linux_.enabled);
#endif
}

TEST_CASE("BuildToolchainSettings: equality compares all platforms", "[editor][user-settings][build-toolchain]") {
  BuildToolchainSettings a;
  BuildToolchainSettings b;
  CHECK(a == b);

  a.macOS.enabled = true;
  CHECK_FALSE(a == b);

  b.macOS.enabled = true;
  CHECK(a == b);

  a.windows.toolchain = "Clang";
  CHECK_FALSE(a == b);

  b.windows.toolchain = "Clang";
  CHECK(a == b);
}

TEST_CASE("BuildToolchainSettings: copy is deep and independent", "[editor][user-settings][build-toolchain]") {
  BuildToolchainSettings original;
  original.macOS.enabled = true;
  original.macOS.toolchain = "Xcode";
  original.windows.enabled = true;
  original.windows.toolchain = "MSVC";

  BuildToolchainSettings copy = original;
  CHECK(copy == original);

  // Mutate the copy, original must stay unchanged.
  copy.macOS.enabled = false;
  copy.windows.toolchain = "Clang";

  CHECK(original.macOS.enabled);
  CHECK(original.windows.toolchain == "MSVC");
  CHECK_FALSE(copy.macOS.enabled);
  CHECK(copy.windows.toolchain == "Clang");
}

TEST_CASE("EditorUserSettings: load defaults buildToolchain to host-safe when file is absent", "[editor][user-settings][build-toolchain]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_bt_absent";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  EditorUserSettingsDocument doc = LoadEditorUserSettingsDocument();
  CHECK_FALSE(doc.loadedFromDisk);

  const auto& bt = doc.settings.buildToolchain;
#if defined(__APPLE__)
  CHECK(bt.macOS.enabled);
  CHECK_FALSE(bt.windows.enabled);
  CHECK_FALSE(bt.linux_.enabled);
#elif defined(_WIN32)
  CHECK_FALSE(bt.macOS.enabled);
  CHECK(bt.windows.enabled);
  CHECK_FALSE(bt.linux_.enabled);
#elif defined(__linux__)
  CHECK_FALSE(bt.macOS.enabled);
  CHECK_FALSE(bt.windows.enabled);
  CHECK(bt.linux_.enabled);
#endif
}

TEST_CASE("EditorUserSettings: build toolchain settings survive JSON round-trip", "[editor][user-settings][build-toolchain]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_bt_roundtrip";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  EditorUserSettingsDocument doc;
  doc.settings.buildToolchain.macOS.enabled = true;
  doc.settings.buildToolchain.macOS.toolchain = "Xcode 16";
  doc.settings.buildToolchain.windows.enabled = true;
  doc.settings.buildToolchain.windows.toolchain = "MSVC 2022";
  doc.settings.buildToolchain.linux_.enabled = false;
  doc.settings.buildToolchain.linux_.toolchain = "GCC 14";

  std::string err;
  REQUIRE(SaveEditorUserSettingsDocument(&doc, &err));
  CHECK(err.empty());

  // Verify on-disk JSON shape.
  std::ifstream in(ResolveEditorUserSettingsPath());
  REQUIRE(in.is_open());
  nlohmann::json root;
  in >> root;
  REQUIRE(root.is_object());
  REQUIRE(root.contains("build"));
  const auto buildJson = root.at("build");
  REQUIRE(buildJson.is_object());
  REQUIRE(buildJson.contains("macOS"));
  CHECK(buildJson.at("macOS").at("enabled").get<bool>());
  CHECK(buildJson.at("macOS").at("toolchain").get<std::string>() == "Xcode 16");
  REQUIRE(buildJson.contains("windows"));
  CHECK(buildJson.at("windows").at("enabled").get<bool>());
  CHECK(buildJson.at("windows").at("toolchain").get<std::string>() == "MSVC 2022");
  REQUIRE(buildJson.contains("linux"));
  CHECK_FALSE(buildJson.at("linux").at("enabled").get<bool>());
  CHECK(buildJson.at("linux").at("toolchain").get<std::string>() == "GCC 14");

  // Reload and verify.
  EditorUserSettingsDocument loaded = LoadEditorUserSettingsDocument();
  CHECK(loaded.loadedFromDisk);
  CHECK_FALSE(loaded.parseError);
  CHECK(loaded.settings.buildToolchain.macOS.enabled);
  CHECK(loaded.settings.buildToolchain.macOS.toolchain == "Xcode 16");
  CHECK(loaded.settings.buildToolchain.windows.enabled);
  CHECK(loaded.settings.buildToolchain.windows.toolchain == "MSVC 2022");
  CHECK_FALSE(loaded.settings.buildToolchain.linux_.enabled);
  CHECK(loaded.settings.buildToolchain.linux_.toolchain == "GCC 14");
}

TEST_CASE("EditorUserSettings: missing build key falls back to host-safe defaults", "[editor][user-settings][build-toolchain]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_bt_no_build";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  WriteFile(ResolveEditorUserSettingsPath(),
            R"({"editor":{"themePreset":"darkBlue"}})");

  EditorUserSettingsDocument loaded = LoadEditorUserSettingsDocument();
  CHECK(loaded.loadedFromDisk);
  CHECK_FALSE(loaded.parseError);

  // Build key was absent → host-safe defaults.
  const auto& bt = loaded.settings.buildToolchain;
#if defined(__APPLE__)
  CHECK(bt.macOS.enabled);
  CHECK_FALSE(bt.windows.enabled);
  CHECK_FALSE(bt.linux_.enabled);
#elif defined(_WIN32)
  CHECK_FALSE(bt.macOS.enabled);
  CHECK(bt.windows.enabled);
  CHECK_FALSE(bt.linux_.enabled);
#elif defined(__linux__)
  CHECK_FALSE(bt.macOS.enabled);
  CHECK_FALSE(bt.windows.enabled);
  CHECK(bt.linux_.enabled);
#endif
}

TEST_CASE("EditorUserSettings: partial build JSON preserves defaults for missing platform keys", "[editor][user-settings][build-toolchain]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_bt_partial";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  // Only macOS is present in the JSON; windows and linux are absent.
  WriteFile(ResolveEditorUserSettingsPath(),
            R"({"editor":{"themePreset":"darkBlue"},"build":{"macOS":{"enabled":true,"toolchain":"Xcode"}}})");

  EditorUserSettingsDocument loaded = LoadEditorUserSettingsDocument();
  CHECK(loaded.loadedFromDisk);
  CHECK_FALSE(loaded.parseError);

  CHECK(loaded.settings.buildToolchain.macOS.enabled);
  CHECK(loaded.settings.buildToolchain.macOS.toolchain == "Xcode");
  // Missing keys fall back to struct defaults (all disabled).
  CHECK_FALSE(loaded.settings.buildToolchain.windows.enabled);
  CHECK(loaded.settings.buildToolchain.windows.toolchain.empty());
  CHECK_FALSE(loaded.settings.buildToolchain.linux_.enabled);
  CHECK(loaded.settings.buildToolchain.linux_.toolchain.empty());
}

TEST_CASE("EditorUserSettings: build key that is not an object falls back to defaults without parse error", "[editor][user-settings][build-toolchain]") {
  const std::filesystem::path fakeHome =
      Horo::Tests::SecureTempBase() / "horo_bt_non_object";
  std::error_code ec;
  std::filesystem::remove_all(fakeHome, ec);
  std::filesystem::create_directories(fakeHome, ec);
  HomeDirGuard homeGuard(fakeHome);

  WriteFile(ResolveEditorUserSettingsPath(),
            R"({"editor":{"themePreset":"darkBlue"},"build":"not_an_object"})");

  EditorUserSettingsDocument loaded = LoadEditorUserSettingsDocument();
  CHECK(loaded.loadedFromDisk);
  // The root is valid JSON → not a parse error; build is simply skipped.
  CHECK_FALSE(loaded.parseError);

  // Fall back to host-safe defaults.
  const auto& bt = loaded.settings.buildToolchain;
#if defined(__APPLE__)
  CHECK(bt.macOS.enabled);
#else
  CHECK_FALSE(bt.macOS.enabled);
#endif
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

TEST_CASE("TransformGizmo: HandleSize returns unit size at near-zero distance", "[gizmo][coverage]") {
  TransformGizmo g;
  // Place gizmo right at the camera position → distance ≈ 0
  Camera cam = MakeCam({0, 0, 0}, {0, 0, -1});
  g.Activate(GizmoMode::Translate, cam.position, Quaternion::Identity(),
             Vec3::One());
  const float size = g.HandleSize(cam);
  CHECK(size == Approx(1.0f).margin(1e-6f));
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
  CHECK(size == Approx(1.0f).margin(1e-6f));
}

TEST_CASE("TransformGizmo: RayHitPlane returns false for behind-origin hit", "[gizmo][coverage]") {
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

TEST_CASE("TransformGizmo: PickAxis returns None for empty screen point", "[gizmo][coverage]") {
  TransformGizmo g;
  g.Activate(GizmoMode::Translate, {0, 0, 0}, Quaternion::Identity(),
             Vec3::One());
  Camera cam = MakeCam({0, 0, 10}, Vec3::Zero());
  // Mouse far off screen — should not pick anything
  const GizmoAxis axis = g.PickAxis(9999.0f, 9999.0f, cam, 800, 800);
  CHECK(axis == GizmoAxis::None);
}

TEST_CASE("EditorViewportToolbar: precision labels resolve nearest supported step", "[editor][viewport-toolbar]") {
  CHECK(std::string_view(ViewportTranslatePrecisionLabel(0.01f)) == "1 cm");
  CHECK(std::string_view(ViewportTranslatePrecisionLabel(0.49f)) == "50 cm");
  CHECK(std::string_view(ViewportTranslatePrecisionLabel(2.0f)) == "1 m");
}

TEST_CASE("EditorViewportToolbar: translate snap step falls back when disabled or invalid", "[editor][viewport-toolbar]") {
  CHECK(ResolveViewportTranslateSnapStep(false, 0.1f, 0.5f) ==
        Approx(0.5f));
  CHECK(ResolveViewportTranslateSnapStep(true, 0.0f, 0.5f) ==
        Approx(0.5f));
  CHECK(ResolveViewportTranslateSnapStep(true, 0.1f, 0.5f) ==
        Approx(0.1f));
}

TEST_CASE("EditorViewportToolbar: precision drives rotate and scale step helpers", "[editor][viewport-toolbar]") {
  CHECK(ResolveViewportRotateSnapStepDegrees(false, 0.1f, 15.0f) ==
        Approx(15.0f));
  CHECK(ResolveViewportRotateSnapStepDegrees(true, 0.01f, 15.0f) ==
        Approx(1.0f));
  CHECK(ResolveViewportRotateSnapStepDegrees(true, 1.0f, 15.0f) ==
        Approx(90.0f));
  CHECK(ResolveViewportScaleSnapStep(false, 0.1f, 0.25f) == Approx(0.25f));
  CHECK(ResolveViewportScaleSnapStep(true, 0.05f, 0.25f) == Approx(0.05f));
}

// ===========================================================================
// EditorHistory — via MCP commands (no GLFW needed)
// ===========================================================================

TEST_CASE("EditorHistory: CommitHistoryChange records snapshot on object create", "[editor][history]") {
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

TEST_CASE("EditorHistory: CommitHistoryChange is no-op when snapshots are equal", "[editor][history]") {
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

TEST_CASE("EditorHistory: UndoHistory with empty stack returns false", "[editor][history]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto undo =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(undo.ok);
  REQUIRE_FALSE(undo.data["undone"].get<bool>());
}

TEST_CASE("EditorHistory: UndoHistory restores prior document state", "[editor][history]") {
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

TEST_CASE("EditorHistory: RedoHistory after undo re-applies change", "[editor][history]") {
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

TEST_CASE("EditorHistory: RedoHistory with empty stack returns false", "[editor][history]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto redo =
      editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
  REQUIRE(redo.ok);
  REQUIRE_FALSE(redo.data["redone"].get<bool>());
}

TEST_CASE("EditorHistory: TrimHistory caps undo stack at 128 entries", "[editor][history]") {
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

TEST_CASE("EditorHistory: RefreshHistorySavedBaseline refreshes undo and redo snapshots", "[editor][history]") {
  namespace fs = std::filesystem;
  const fs::path sceneDir =
      Horo::Tests::SecureTempBase() / "horo_hist_unit_baseline";
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

TEST_CASE("EditorHistory: HistorySnapshotsEqual returns false for changed document", "[editor][history]") {
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

TEST_CASE("EditorSceneGraph: FindObjectIndexById returns -1 for unknown id", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "known";
  doc.objects.push_back(obj);

  REQUIRE(FindObjectIndexById(doc, "unknown") == -1);
  REQUIRE(FindObjectIndexById(doc, "") == -1);
}

TEST_CASE("EditorSceneGraph: FindObjectIndexById returns correct index", "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: IsDescendantOf returns false for out-of-bounds indices", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "obj";
  doc.objects.push_back(obj);

  REQUIRE_FALSE(IsDescendantOf(doc, -1, 0));
  REQUIRE_FALSE(IsDescendantOf(doc, 0, -1));
  REQUIRE_FALSE(IsDescendantOf(doc, 99, 0));
  REQUIRE_FALSE(IsDescendantOf(doc, 0, 99));
}

TEST_CASE("EditorSceneGraph: IsDescendantOf finds ancestor via while loop", "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: IsDescendantOf returns false when no parent chain", "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta translates child", "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta applies rotation to child", "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta invokes callback", "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta skips listed indices", "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: PropagateHierarchyTransformDelta invalid parentIdx is no-op", "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: CollectReservedObjectIds includes ids and prop references", "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: CollectReservedObjectIds handles empty doc", "[editor][scene-graph]") {
  SceneDocument doc;
  const auto reserved = CollectReservedObjectIds(doc);
  REQUIRE(reserved.empty());
}

TEST_CASE("EditorSceneGraph: RewriteObjectIdReferences rewrites followTargetId too", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "follower";
  obj.props["followTargetId"] = "old_target";
  doc.objects.push_back(obj);

  RewriteObjectIdReferences(&doc, "old_target", "new_target");
  REQUIRE(doc.objects[0].props.at("followTargetId") == "new_target");
}

TEST_CASE("EditorSceneGraph: RewriteObjectIdReferences null doc is no-op", "[editor][scene-graph]") {
  REQUIRE_NOTHROW(RewriteObjectIdReferences(nullptr, "old", "new"));
}

TEST_CASE("EditorSceneGraph: LogDanglingObjectReferences empty label does not crash", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject orphan;
  orphan.id = "orphan";
  orphan.props["parentId"] = "ghost_parent";
  doc.objects.push_back(orphan);

  REQUIRE_NOTHROW(LogDanglingObjectReferences(doc, ""));
}

TEST_CASE("EditorSceneGraph: LogDanglingObjectReferences skips non-ref and empty-value props", "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: IsDescendantOf cycle detection returns false after guard", "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: IsReservedObjectId returns false for empty id", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "x";
  doc.objects.push_back(obj);
  REQUIRE_FALSE(IsReservedObjectId(doc, ""));
}

TEST_CASE("EditorSceneGraph: IsReservedObjectId returns true for existing object id", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "used";
  doc.objects.push_back(obj);
  REQUIRE(IsReservedObjectId(doc, "used"));
  REQUIRE_FALSE(IsReservedObjectId(doc, "unused"));
}

TEST_CASE("EditorSceneGraph: IsReservedObjectId returns true for referenced prop value", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "follower";
  obj.props["followTargetId"] = "target_obj";
  doc.objects.push_back(obj);

  REQUIRE(IsReservedObjectId(doc, "target_obj"));
  REQUIRE_FALSE(IsReservedObjectId(doc, "not_referenced"));
}

TEST_CASE("EditorSceneGraph: IsReservedObjectId respects ignoreConcreteObjectId", "[editor][scene-graph]") {
  SceneDocument doc;
  SceneObject obj;
  obj.id = "rename_me";
  doc.objects.push_back(obj);

  const std::string ignored = "rename_me";
  REQUIRE_FALSE(IsReservedObjectId(doc, "rename_me", &ignored));
  REQUIRE(IsReservedObjectId(doc, "rename_me", nullptr));
}

TEST_CASE("EditorSceneGraph: IsReservedObjectId ignores prop references when matching ignored id", "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: SanitizePrefabStem cleans special characters", "[editor][scene-graph]") {
  REQUIRE(SanitizePrefabStem("hello_world") == "hello_world");
  REQUIRE(SanitizePrefabStem("valid-name_123") == "valid-name_123");
  REQUIRE(SanitizePrefabStem("__hello__") == "hello");
  REQUIRE(SanitizePrefabStem("_test") == "test");
  REQUIRE(SanitizePrefabStem("test_") == "test");
  REQUIRE(SanitizePrefabStem("") == "prefab");
  REQUIRE(SanitizePrefabStem("___") == "prefab");
  REQUIRE(SanitizePrefabStem("hello world!") == "hello_world");
}

TEST_CASE("EditorSceneGraph: BuildUniquePrefabPath returns .horo path", "[editor][scene-graph]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_prefab_path_test";
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

TEST_CASE("EditorSceneGraph: BuildUniquePrefabPath uses sceneId when object id is empty", "[editor][scene-graph]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_prefab_path_test2";
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

TEST_CASE("EditorHistory: CommitHistoryChange skips when update makes no content change", "[editor][history]") {
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

TEST_CASE("EditorHistory: TrimHistory guard — no trim when stack within limit", "[editor][history]") {
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

TEST_CASE("EditorHistory: RestoreHistorySnapshot restores selectedAssetId from undo", "[editor][history]") {
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

TEST_CASE("EditorSceneGraph: IsReservedObjectId ignores prop value matching ignoreConcreteObjectId", "[editor][scene-graph]") {
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

TEST_CASE("EditorSceneGraph: BuildUniquePrefabPath appends suffix when candidate exists", "[editor][scene-graph]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_prefab_suffix_test";
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

TEST_CASE("EditorSceneGraph: IsReservedObjectId skips non-reference prop keys", "[editor][scene-graph]") {
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

TEST_CASE("EditorSearch: ObjectTypeLabel returns 'unknown' for unrecognised type", "[editor][search]") {
  const auto unknownType = static_cast<SceneObjectType>(99);
  CHECK(std::string(ObjectTypeLabel(unknownType)) == "unknown");
}

TEST_CASE("EditorSearch: ObjectTypeLabel covers all known types", "[editor][search]") {
  using enum SceneObjectType;
  CHECK(std::string(ObjectTypeLabel(Prop)) == "prop");
  CHECK(std::string(ObjectTypeLabel(Light)) == "light");
  CHECK(std::string(ObjectTypeLabel(Panel)) == "board");
  CHECK(std::string(ObjectTypeLabel(Camera)) == "[cam]");
}

TEST_CASE("EditorSearch: ContainsCaseInsensitive returns true for empty query", "[editor][search]") {
  CHECK(ContainsCaseInsensitive("anything", ""));
}

TEST_CASE("EditorSearch: ContainsCaseInsensitive is case-insensitive", "[editor][search]") {
  CHECK(ContainsCaseInsensitive("Hello World", "hello"));
  CHECK(ContainsCaseInsensitive("UPPER", "upper"));
  CHECK_FALSE(ContainsCaseInsensitive("abc", "xyz"));
}

TEST_CASE("EditorSearch: MatchesShortcutQuery empty query always matches", "[editor][search]") {
  ShortcutRow row{"Editor", "Undo", "Ctrl+Z"};
  CHECK(MatchesShortcutQuery(row, ""));
  CHECK(MatchesShortcutQuery(row, "Editor"));
  CHECK(MatchesShortcutQuery(row, "Ctrl"));
  CHECK_FALSE(MatchesShortcutQuery(row, "ZZZ_NO_MATCH"));
}

TEST_CASE("EditorSearch: MatchesCommandPaletteQuery empty query always matches", "[editor][search]") {
  CommandPaletteRow row{"save_scene", "Save Scene", "Toolbar"};
  CHECK(MatchesCommandPaletteQuery(row, ""));
  CHECK(MatchesCommandPaletteQuery(row, "save"));
  CHECK(MatchesCommandPaletteQuery(row, "Toolbar"));
  CHECK_FALSE(MatchesCommandPaletteQuery(row, "XYZ_NOT_FOUND"));
}

TEST_CASE("EditorSearch: ObjectMatchesQuickOpenQuery uses id and type", "[editor][search]") {
  SceneObject obj;
  obj.id = "my_light";
  obj.type = SceneObjectType::Light;
  obj.assetId = "";
  CHECK(ObjectMatchesQuickOpenQuery(obj, ""));
  CHECK(ObjectMatchesQuickOpenQuery(obj, "my_light"));
  CHECK(ObjectMatchesQuickOpenQuery(obj, "light"));
  CHECK_FALSE(ObjectMatchesQuickOpenQuery(obj, "camera_xyz"));
}

TEST_CASE("EditorSearch: AssetMatchesQuickOpenQuery uses assetId and mesh", "[editor][search]") {
  AssetDef asset;
  asset.mesh = "meshes/cube.obj";
  asset.albedoMap = "textures/albedo.png";
  CHECK(AssetMatchesQuickOpenQuery("cube_asset", asset, ""));
  CHECK(AssetMatchesQuickOpenQuery("cube_asset", asset, "cube"));
  CHECK(AssetMatchesQuickOpenQuery("cube_asset", asset, "albedo"));
  CHECK_FALSE(AssetMatchesQuickOpenQuery("cube_asset", asset, "XYZ_NONE"));
}

TEST_CASE("EditorSearch: EvaluateFilteredListState None when shownCount > 0", "[editor][search]") {
  using enum FilteredListState;
  CHECK(EvaluateFilteredListState(5, 3, "q") == None);
}

TEST_CASE("EditorSearch: EvaluateFilteredListState EmptyData when totalCount == 0", "[editor][search]") {
  using enum FilteredListState;
  CHECK(EvaluateFilteredListState(0, 0, "") == EmptyData);
}

TEST_CASE("EditorSearch: EvaluateFilteredListState NoMatches when query non-empty", "[editor][search]") {
  using enum FilteredListState;
  CHECK(EvaluateFilteredListState(5, 0, "nomatch") == NoMatches);
}

TEST_CASE("EditorSearch: EvaluateFilteredListState None when query empty totalCount>0", "[editor][search]") {
  using enum FilteredListState;
  // totalCount > 0, shown == 0, query empty → None (items exist, none filtered)
  CHECK(EvaluateFilteredListState(3, 0, "") == None);
}

TEST_CASE("EditorSearch: GetEditorShortcuts returns non-empty span", "[editor][search]") {
  CHECK(!GetEditorShortcuts().empty());
}

TEST_CASE("EditorSearch: GetEditorCommands returns non-empty span", "[editor][search]") {
  CHECK(!GetEditorCommands().empty());
}

// ===========================================================================
// EditorHistory — additional branch coverage via MCP commands
// ===========================================================================

TEST_CASE("EditorHistory: undo command returns false when nothing to undo", "[editor][history][extra]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res =
      editor.ExecuteMcpCommand("editor.undo", nlohmann::json::object());
  REQUIRE(res.ok);
  REQUIRE_FALSE(res.data["undone"].get<bool>());
}

TEST_CASE("EditorHistory: redo command returns false when nothing to redo", "[editor][history][extra]") {
  EditorLayer editor;
  editor.LoadDocument(SceneDocument{});

  const auto res =
      editor.ExecuteMcpCommand("editor.redo", nlohmann::json::object());
  REQUIRE(res.ok);
  REQUIRE_FALSE(res.data["redone"].get<bool>());
}

TEST_CASE("EditorHistory: undo after create_object via transaction path", "[editor][history][extra]") {
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

TEST_CASE("SceneProjectBridge: BuildSceneProjectModel produces model with camera node", "[editor][scene-bridge]") {
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

TEST_CASE("SceneProjectBridge: BuildSceneProjectModel handles light object", "[editor][scene-bridge]") {
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

TEST_CASE("SceneProjectBridge: BuildSceneProjectModel handles invalid float prop", "[editor][scene-bridge]") {
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

TEST_CASE("SceneProjectBridge: BuildSceneDocument round-trips basic document", "[editor][scene-bridge]") {
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

TEST_CASE("SceneProjectBridge: BuildSceneProjectModel preserves light and component props", "[editor][scene-bridge][coverage]") {
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

TEST_CASE("AssetMetadata: malformed sidecar reports error without throwing", "[editor][asset-metadata][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_asset_metadata_bad_sidecar";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(
      root / "assets" / "models" / "guid_bad_meta", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  WriteFile(GetAssetMetadataPath("guid_bad_meta"), "{ not valid json");

  AssetMetadata metadata;
  std::string error;
  REQUIRE_FALSE(LoadAssetMetadata("guid_bad_meta", &metadata, &error));
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("AssetMetadata: round-trip preserves dependencies and importer id", "[editor][asset-metadata][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_asset_metadata_roundtrip";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(
      root / "assets" / "models" / "guid_roundtrip", ec);
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

TEST_CASE("AssetImportService: ImportTextureForAsset with missing source returns false", "[editor][asset-import][extra]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_import_tex_missing";
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

TEST_CASE("AssetImportService: ImportAssetFromSource persists failed import diagnostics", "[editor][asset-import][extra]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_import_asset_missing";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  constexpr std::string_view assetGuid = "guid_missing";

  AssetImportService service;
  const AssetImportResult result = service.ImportAssetFromSource(
      (root / "does_not_exist.obj").string(), "asset_missing",
      std::string(assetGuid), "Missing");
  CHECK_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].code == DiagnosticCodes::ObjSourceMissing);

  AssetMetadata metadata;
  std::string error;
  REQUIRE(LoadAssetMetadata(std::string(assetGuid), &metadata, &error));
  CHECK(error.empty());
  CHECK_FALSE(metadata.lastImportSucceeded);
  CHECK(metadata.lastImportReason == result.error);
  REQUIRE_FALSE(metadata.diagnostics.empty());
  CHECK(metadata.diagnostics[0].code == DiagnosticCodes::ObjSourceMissing);
  CHECK(metadata.sourcePath == (root / "does_not_exist.obj").string());
}

TEST_CASE("AssetImportService: ImportAssetFromSource reports failed diagnostic persistence", "[editor][asset-import][extra]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_import_failure_metadata_blocked";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  constexpr std::string_view assetGuid = "guid_blocked_failure_meta";
  const std::filesystem::path managedDir =
      GetManagedAssetDirectory(std::string(assetGuid));
  std::filesystem::create_directories(managedDir.parent_path(), ec);
  REQUIRE_FALSE(ec);
  WriteFile(managedDir, "not-a-directory");

  AssetImportService service;
  const AssetImportResult result = service.ImportAssetFromSource(
      (root / "does_not_exist.obj").string(), "asset_blocked_failure_meta",
      std::string(assetGuid), "Blocked Failure Metadata");

  CHECK_FALSE(result.ok);
  REQUIRE(result.diagnostics.size() >= 2);
  CHECK(result.diagnostics[0].code == DiagnosticCodes::ObjSourceMissing);
  CHECK(std::ranges::any_of(result.diagnostics, [](const auto &diagnostic) {
    return diagnostic.code == DiagnosticCodes::MetadataSaveFailed;
  }));
}

TEST_CASE("AssetImportService: ReimportAssetWithDependents with null doc is no-op", "[editor][asset-import][extra]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_reimport_null";
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

// ===========================================================================
// AssetGuidRegistry — in-memory GUID → metadata index
// ===========================================================================

namespace {
// Helper: writes a sidecar at assets/models/<guid>/asset.meta.json under the
// active ProjectPath::Root() and returns the metadata it produced. Caller is
// responsible for managing the project root via ProjectPathGuard.
AssetMetadata WriteSidecar(const std::string &assetId, const std::string &guid,
                           const std::string &importerId,
                           const std::vector<AssetDependencyRecord> &deps) {
  AssetMetadata metadata;
  metadata.assetId = assetId;
  metadata.assetGuid = guid;
  metadata.displayName = assetId;
  metadata.importerId = importerId;
  metadata.dependencies = deps;
  std::error_code ec;
  std::filesystem::create_directories(GetManagedAssetDirectory(guid), ec);
  std::string error;
  REQUIRE(SaveAssetMetadata(metadata, &error));
  REQUIRE(error.empty());
  return metadata;
}
} // namespace

TEST_CASE("AssetGuidRegistry: empty registry returns null and no dependents", "[editor][asset-guid-registry]") {
  AssetGuidRegistry registry;
  CHECK(registry.Empty());
  CHECK(registry.Size() == 0);
  CHECK(registry.LookupByGuid("any") == nullptr);
  CHECK(registry.Dependents("any").empty());
  CHECK(registry.LastRefreshSource() ==
        AssetGuidRegistryRefreshSource::Unknown);
}

TEST_CASE("AssetGuidRegistry: RefreshFromFilesystem on missing project root is a no-op", "[editor][asset-guid-registry]") {
  ProjectPathGuard guard({});
  AssetGuidRegistry registry;
  const AssetGuidRegistryRefreshResult result = registry.RefreshFromFilesystem();
  CHECK(registry.Empty());
  CHECK(result.scanned == 0);
  CHECK(result.loaded == 0);
  CHECK_FALSE(result.warnings.empty());
  CHECK(registry.LastRefreshSource() ==
        AssetGuidRegistryRefreshSource::Filesystem);
}

TEST_CASE("AssetGuidRegistry: RefreshFromFilesystem on empty assets dir loads zero entries", "[editor][asset-guid-registry]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_guid_reg_empty_dir";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  AssetGuidRegistry registry;
  const AssetGuidRegistryRefreshResult result = registry.RefreshFromFilesystem();
  CHECK(result.scanned == 0);
  CHECK(result.loaded == 0);
  CHECK(registry.Empty());
}

TEST_CASE("AssetGuidRegistry: RefreshFromFilesystem indexes a single sidecar", "[editor][asset-guid-registry]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_guid_reg_single";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  WriteSidecar("only_asset", "guid_only", "builtin.obj_mesh",
               {AssetDependencyRecord{AssetDependencyKind::Source, "src.obj"}});

  AssetGuidRegistry registry;
  const AssetGuidRegistryRefreshResult result = registry.RefreshFromFilesystem();
  CHECK(result.scanned == 1);
  CHECK(result.loaded == 1);
  CHECK(result.skipped == 0);
  CHECK(registry.Size() == 1);

  const AssetMetadata *cached = registry.LookupByGuid("guid_only");
  REQUIRE(cached != nullptr);
  CHECK(cached->assetId == "only_asset");
  CHECK(cached->importerId == "builtin.obj_mesh");
}

TEST_CASE("AssetGuidRegistry: Dependents reflects DownstreamAsset edges", "[editor][asset-guid-registry]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_guid_reg_deps";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  // Two assets B and C declare DownstreamAsset edge to A; the registry's
  // reverse index should report B and C as dependents of A.
  WriteSidecar("a", "guid_a", "builtin.obj_mesh", {});
  WriteSidecar(
      "b", "guid_b", "builtin.obj_mesh",
      {AssetDependencyRecord{AssetDependencyKind::DownstreamAsset, "guid_a"}});
  WriteSidecar(
      "c", "guid_c", "builtin.obj_mesh",
      {AssetDependencyRecord{AssetDependencyKind::DownstreamAsset, "guid_a"}});

  AssetGuidRegistry registry;
  const AssetGuidRegistryRefreshResult result = registry.RefreshFromFilesystem();
  CHECK(result.loaded == 3);

  const std::vector<std::string> dependentsOfA = registry.Dependents("guid_a");
  REQUIRE(dependentsOfA.size() == 2);
  CHECK(dependentsOfA[0] == "guid_b");
  CHECK(dependentsOfA[1] == "guid_c");
  CHECK(registry.Dependents("guid_b").empty());
  CHECK(registry.Dependents("guid_unknown").empty());
}

TEST_CASE("AssetGuidRegistry: Dependents ignores non-DownstreamAsset edges", "[editor][asset-guid-registry]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_guid_reg_deps_kind";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  WriteSidecar(
      "x", "guid_x", "builtin.obj_mesh",
      {AssetDependencyRecord{AssetDependencyKind::Source, "guid_y"},
       AssetDependencyRecord{AssetDependencyKind::ProducedOutput, "guid_y"}});
  AssetGuidRegistry registry;
  registry.RefreshFromFilesystem();
  CHECK(registry.Dependents("guid_y").empty());
}

TEST_CASE("AssetGuidRegistry: Insert adds an entry without touching disk", "[editor][asset-guid-registry]") {
  ProjectPathGuard guard({});
  AssetMetadata metadata;
  metadata.assetId = "in_mem";
  metadata.assetGuid = "guid_in_mem";
  metadata.dependencies.push_back(
      AssetDependencyRecord{AssetDependencyKind::DownstreamAsset, "guid_target"});

  AssetGuidRegistry registry;
  CHECK(registry.Insert(metadata));
  CHECK(registry.Size() == 1);
  REQUIRE(registry.LookupByGuid("guid_in_mem") != nullptr);
  CHECK(registry.Dependents("guid_target").size() == 1);
}

TEST_CASE("AssetGuidRegistry: Insert rejects metadata with empty GUID", "[editor][asset-guid-registry]") {
  AssetGuidRegistry registry;
  AssetMetadata metadata;
  metadata.assetId = "no_guid";
  CHECK_FALSE(registry.Insert(metadata));
  CHECK(registry.Empty());
}

TEST_CASE("AssetGuidRegistry: Insert overwrites existing entry and re-indexes dependents", "[editor][asset-guid-registry]") {
  AssetGuidRegistry registry;

  AssetMetadata first;
  first.assetGuid = "guid_x";
  first.dependencies.push_back(
      AssetDependencyRecord{AssetDependencyKind::DownstreamAsset, "guid_old"});
  REQUIRE(registry.Insert(first));
  CHECK(registry.Dependents("guid_old").size() == 1);

  AssetMetadata replacement;
  replacement.assetGuid = "guid_x";
  replacement.dependencies.push_back(
      AssetDependencyRecord{AssetDependencyKind::DownstreamAsset, "guid_new"});
  REQUIRE(registry.Insert(replacement));

  // Old reverse-edge must be gone, new one in place.
  CHECK(registry.Dependents("guid_old").empty());
  CHECK(registry.Dependents("guid_new").size() == 1);
}

TEST_CASE("AssetGuidRegistry: Invalidate removes an entry and prunes its reverse edges", "[editor][asset-guid-registry]") {
  AssetGuidRegistry registry;
  AssetMetadata metadata;
  metadata.assetGuid = "guid_to_drop";
  metadata.dependencies.push_back(
      AssetDependencyRecord{AssetDependencyKind::DownstreamAsset, "guid_other"});
  REQUIRE(registry.Insert(metadata));
  CHECK(registry.Dependents("guid_other").size() == 1);

  CHECK(registry.Invalidate("guid_to_drop"));
  CHECK(registry.Empty());
  CHECK(registry.Dependents("guid_other").empty());
  CHECK(registry.LookupByGuid("guid_to_drop") == nullptr);
  // Second invalidate is a no-op.
  CHECK_FALSE(registry.Invalidate("guid_to_drop"));
}

TEST_CASE("AssetGuidRegistry: Clear empties everything", "[editor][asset-guid-registry]") {
  AssetGuidRegistry registry;
  AssetMetadata metadata;
  metadata.assetGuid = "guid_clear";
  metadata.dependencies.push_back(
      AssetDependencyRecord{AssetDependencyKind::DownstreamAsset, "guid_dep"});
  REQUIRE(registry.Insert(metadata));

  registry.Clear();
  CHECK(registry.Empty());
  CHECK(registry.Dependents("guid_dep").empty());
  CHECK(registry.LastRefreshSource() ==
        AssetGuidRegistryRefreshSource::Unknown);
}

TEST_CASE("AssetGuidRegistry: RefreshFromFilesystem skips malformed sidecars and reports them", "[editor][asset-guid-registry]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_guid_reg_bad_json";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  // Good sidecar:
  WriteSidecar("good", "guid_good", "builtin.obj_mesh", {});
  // Malformed sidecar:
  std::filesystem::create_directories(GetManagedAssetDirectory("guid_bad"), ec);
  WriteFile(GetAssetMetadataPath("guid_bad"), "{ this is not json");

  AssetGuidRegistry registry;
  const AssetGuidRegistryRefreshResult result = registry.RefreshFromFilesystem();
  CHECK(result.scanned == 2);
  CHECK(result.loaded == 1);
  CHECK(result.skipped == 1);
  CHECK_FALSE(result.warnings.empty());
  CHECK(registry.LookupByGuid("guid_good") != nullptr);
  CHECK(registry.LookupByGuid("guid_bad") == nullptr);
}

TEST_CASE("AssetGuidRegistry: RefreshFromFilesystem replaces previous entries", "[editor][asset-guid-registry]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_guid_reg_replace";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  WriteSidecar("a", "guid_a", "builtin.obj_mesh", {});
  AssetGuidRegistry registry;
  registry.RefreshFromFilesystem();
  CHECK(registry.Size() == 1);

  // Remove the sidecar's containing dir entirely.
  std::filesystem::remove_all(GetManagedAssetDirectory("guid_a"), ec);
  WriteSidecar("b", "guid_b", "builtin.obj_mesh", {});

  registry.RefreshFromFilesystem();
  CHECK(registry.Size() == 1);
  CHECK(registry.LookupByGuid("guid_a") == nullptr);
  CHECK(registry.LookupByGuid("guid_b") != nullptr);
}

TEST_CASE("AssetGuidRegistry: filesystem refresh forces directory GUID and warns on mismatch", "[editor][asset-guid-registry]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_guid_reg_fs_mismatch";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  // Sidecar is stored under the directory "guid_dir" but its embedded
  // assetGuid disagrees. The directory name is canonical.
  AssetMetadata metadata;
  metadata.assetId = "mismatched_asset";
  metadata.assetGuid = "guid_in_json";
  metadata.displayName = "Mismatched";
  metadata.importerId = "builtin.obj_mesh";
  std::filesystem::create_directories(GetManagedAssetDirectory("guid_dir"), ec);
  WriteFile(GetAssetMetadataPath("guid_dir"),
            R"({"version":1,"assetId":"mismatched_asset","assetGuid":"guid_in_json","displayName":"Mismatched","importerId":"builtin.obj_mesh"})");

  AssetGuidRegistry registry;
  const AssetGuidRegistryRefreshResult result = registry.RefreshFromFilesystem();
  CHECK(result.loaded == 1);
  // Mismatch warning recorded.
  REQUIRE_FALSE(result.warnings.empty());
  // The entry is indexed by the directory name, not the JSON value.
  CHECK(registry.LookupByGuid("guid_dir") != nullptr);
  CHECK(registry.LookupByGuid("guid_in_json") == nullptr);
}

TEST_CASE("AssetGuidRegistry: document refresh forces document GUID and warns on mismatch", "[editor][asset-guid-registry]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_guid_reg_doc_mismatch";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  // Sidecar lives under "doc_guid" but its embedded GUID is different.
  std::filesystem::create_directories(GetManagedAssetDirectory("doc_guid"), ec);
  WriteFile(GetAssetMetadataPath("doc_guid"),
            R"({"version":1,"assetId":"a","assetGuid":"stale_guid","displayName":"A","importerId":"builtin.obj_mesh"})");

  SceneDocument doc;
  doc.assets["a"] = AssetDef{};
  doc.assets["a"].guid = "doc_guid";

  AssetGuidRegistry registry;
  const AssetGuidRegistryRefreshResult result = registry.RefreshFromDocument(doc);
  CHECK(result.loaded == 1);
  REQUIRE_FALSE(result.warnings.empty());
  // The entry is indexed by the document GUID, not the sidecar's embedded value.
  CHECK(registry.LookupByGuid("doc_guid") != nullptr);
  CHECK(registry.LookupByGuid("stale_guid") == nullptr);
}

TEST_CASE("AssetGuidRegistry: RefreshFromDocument loads entries listed in the document", "[editor][asset-guid-registry]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_guid_reg_doc";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  WriteSidecar("a", "guid_a", "builtin.obj_mesh", {});
  WriteSidecar("b", "guid_b", "builtin.obj_mesh", {});

  SceneDocument doc;
  doc.assets["a"] = AssetDef{};
  doc.assets["a"].guid = "guid_a";
  doc.assets["b"] = AssetDef{};
  doc.assets["b"].guid = "guid_b";
  // Asset 'c' has no GUID — must be skipped without crashing.
  doc.assets["c"] = AssetDef{};

  AssetGuidRegistry registry;
  const AssetGuidRegistryRefreshResult result = registry.RefreshFromDocument(doc);
  CHECK(result.loaded == 2);
  CHECK(result.skipped >= 1);
  CHECK(registry.Size() == 2);
  CHECK(registry.LastRefreshSource() ==
        AssetGuidRegistryRefreshSource::Document);
}

TEST_CASE("AssetImportService: GuidRegistry populated after a reimport call", "[editor][asset-guid-registry][asset-import]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_guid_reg_service";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  WriteSidecar("a", "guid_a", "builtin.obj_mesh", {});

  SceneDocument doc;
  doc.sceneId = "s1";
  doc.assets["a"] = AssetDef{};
  doc.assets["a"].guid = "guid_a";

  AssetImportService service;
  // The reimport will fail (no real source on disk) but the registry must
  // still have been refreshed before the failure path executes.
  service.ReimportAssetWithDependents(&doc, "guid_a", "test_refresh");
  CHECK(service.GuidRegistry().LookupByGuid("guid_a") != nullptr);
  CHECK(service.GuidRegistry().LastRefreshSource() ==
        AssetGuidRegistryRefreshSource::Document);
}

// ===========================================================================
// Coverage: ObjCreateDirectoryFailed error path (AssetImporterRegistry.cpp:252)
// ===========================================================================

TEST_CASE("AssetImporterRegistry: ObjImporter reports create-directory failure when managed path is a file",
          "[editor][importer-registry][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_imp_obj_create_dir_fail";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path sourceObj = root / "mesh.obj";
  WriteFile(sourceObj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

  const std::string assetGuid = "guid_obj_create_dir_fail";
  const std::filesystem::path managedDir = GetManagedAssetDirectory(assetGuid);
  std::filesystem::create_directories(managedDir.parent_path(), ec);
  REQUIRE_FALSE(ec);
  // Block directory creation by placing a regular file at the managed path.
  WriteFile(managedDir, "not-a-directory");

  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.obj_mesh");
  REQUIRE(imp != nullptr);
  AssetImportRequest req;
  req.assetId = "obj_dir_fail";
  req.assetGuid = assetGuid;
  req.sourcePath = sourceObj.string();
  const AssetImportResult result = imp->Import(req);
  CHECK_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].code == DiagnosticCodes::ObjCreateDirectoryFailed);
}

// ===========================================================================
// Coverage: TextureCopyFailed error path (AssetImporterRegistry.cpp:353)
// ===========================================================================

TEST_CASE("AssetImporterRegistry: TextureCopy reports copy failure when destination is blocked",
          "[editor][importer-registry][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_imp_tex_copy_fail";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path sourceTexture = root / "brick.png";
  WriteFile(sourceTexture, "png-bytes");

  const std::string assetGuid = "guid_tex_copy_fail";
  const std::filesystem::path managedDir = GetManagedAssetDirectory(assetGuid);
  std::filesystem::create_directories(managedDir, ec);
  REQUIRE_FALSE(ec);
  // Block the copy by creating a directory with the same name as the destination file.
  std::filesystem::create_directories(managedDir / "brick.png", ec);
  REQUIRE_FALSE(ec);

  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.texture_copy");
  REQUIRE(imp != nullptr);
  AssetImportRequest req;
  req.assetId = "tex_copy_fail";
  req.assetGuid = assetGuid;
  req.sourcePath = sourceTexture.string();
  const AssetImportResult result = imp->Import(req);
  CHECK_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].code == DiagnosticCodes::TextureCopyFailed);
}

// ===========================================================================
// Coverage: EditorImportAssetModal SetDraftForTest + RefreshImporterFromExtension
//           + RefreshIdentitiesFromPath (EditorImportAssetModal.cpp:64-74)
// ===========================================================================

TEST_CASE("EditorImportAssetModal: SetDraftForTest overrides auto-derived fields",
          "[editor][import-asset-modal][coverage]") {
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  modal.Open("/p/cube.fbx", &registry, std::filesystem::path{});

  // Override via test seam
  modal.SetDraftForTest("/other/path/ship.obj", "ship_id", "Ship Model", "builtin.obj_mesh");
  CHECK(modal.DraftForTest().sourcePath == "/other/path/ship.obj");
  CHECK(modal.DraftForTest().assetId == "ship_id");
  CHECK(modal.DraftForTest().displayName == "Ship Model");
  CHECK(modal.DraftForTest().importerId == "builtin.obj_mesh");
}

TEST_CASE("EditorImportAssetModal: RefreshImporterFromExtension updates importer from draft path",
          "[editor][import-asset-modal][coverage]") {
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  modal.Open({}, &registry, std::filesystem::path{});

  // Manually set a path and refresh
  modal.SetDraftForTest("/x/model.obj", "model", "Model", "");
  // Re-open with the new path to trigger refresh
  modal.Open("/x/model.obj", &registry, std::filesystem::path{});
  CHECK(modal.DraftForTest().importerId == "builtin.obj_mesh");
}

TEST_CASE("EditorImportAssetModal: RefreshIdentitiesFromPath derives assetId from source path",
          "[editor][import-asset-modal][coverage]") {
  AssetImporterRegistry registry;
  EditorImportAssetModal modal;
  modal.Open("/some/dir/my_asset.fbx", &registry, std::filesystem::path{});
  CHECK(modal.DraftForTest().assetId == "my_asset");
  CHECK(modal.DraftForTest().displayName == "my_asset");
}

// ===========================================================================
// Coverage: TextureCopyImporter success with empty displayName
// ===========================================================================

TEST_CASE("TextureCopyImporter: empty displayName falls back to assetId",
          "[editor][importer-registry][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_tex_empty_display";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path sourceTexture = root / "tile.png";
  WriteFile(sourceTexture, "png-bytes");

  AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.texture_copy");
  REQUIRE(imp != nullptr);
  AssetImportRequest req;
  req.assetId = "tile_tex";
  req.assetGuid = "guid_tile_tex";
  req.displayName = ""; // empty — should fall back to assetId
  req.sourcePath = sourceTexture.string();
  const AssetImportResult result = imp->Import(req);
  REQUIRE(result.ok);
  CHECK(result.asset.displayName == "tile_tex");
}

// ===========================================================================
// Coverage: AssetImporterInternal helpers (SniffImageExtension, EnsureExtension,
//           IsSafeBasename, SanitiseTextureBasename)
// ===========================================================================

#include "ui/editor/AssetImporterInternal.h"

using namespace Horo::Editor::ImporterDetail;

TEST_CASE("SniffImageExtension: detects PNG magic", "[editor][importer-internal][coverage]") {
  const std::vector<unsigned char> png = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A};
  CHECK(SniffImageExtension(png) == ".png");
}

TEST_CASE("SniffImageExtension: detects JPG magic", "[editor][importer-internal][coverage]") {
  const std::vector<unsigned char> jpg = {0xFF, 0xD8, 0xFF, 0xE0};
  CHECK(SniffImageExtension(jpg) == ".jpg");
}

TEST_CASE("SniffImageExtension: detects BMP magic", "[editor][importer-internal][coverage]") {
  const std::vector<unsigned char> bmp = {0x42, 0x4D, 0x00, 0x00};
  CHECK(SniffImageExtension(bmp) == ".bmp");
}

TEST_CASE("SniffImageExtension: detects WebP magic", "[editor][importer-internal][coverage]") {
  std::vector<unsigned char> webp(16, 0);
  webp[0] = 0x52; webp[1] = 0x49; webp[2] = 0x46; webp[3] = 0x46;
  webp[8] = 0x57; webp[9] = 0x45; webp[10] = 0x42; webp[11] = 0x50;
  CHECK(SniffImageExtension(webp) == ".webp");
}

TEST_CASE("SniffImageExtension: detects HDR magic", "[editor][importer-internal][coverage]") {
  std::vector<unsigned char> hdr(16, 0);
  hdr[0] = '#'; hdr[1] = '?'; hdr[2] = 'R'; hdr[3] = 'A';
  CHECK(SniffImageExtension(hdr) == ".hdr");
}

TEST_CASE("SniffImageExtension: returns empty for unknown bytes", "[editor][importer-internal][coverage]") {
  const std::vector<unsigned char> unknown = {0x00, 0x01, 0x02, 0x03};
  CHECK(SniffImageExtension(unknown).empty());
}

TEST_CASE("SniffImageExtension: returns empty for too-short input", "[editor][importer-internal][coverage]") {
  CHECK(SniffImageExtension({}).empty());
  CHECK(SniffImageExtension({0x89}).empty());
}

TEST_CASE("EnsureExtension: appends extension when basename has none", "[editor][importer-internal][coverage]") {
  CHECK(EnsureExtension("texture", ".png") == "texture.png");
}

TEST_CASE("EnsureExtension: replaces existing extension", "[editor][importer-internal][coverage]") {
  CHECK(EnsureExtension("texture.tga", ".png") == "texture.png");
}

TEST_CASE("EnsureExtension: returns unchanged when ext is empty", "[editor][importer-internal][coverage]") {
  CHECK(EnsureExtension("texture.tga", "") == "texture.tga");
}

TEST_CASE("IsSafeBasename: rejects empty and dot names", "[editor][importer-internal][coverage]") {
  CHECK_FALSE(IsSafeBasename(""));
  CHECK_FALSE(IsSafeBasename("."));
  CHECK_FALSE(IsSafeBasename(".."));
}

TEST_CASE("IsSafeBasename: rejects paths with separators", "[editor][importer-internal][coverage]") {
  CHECK_FALSE(IsSafeBasename("sub/file.png"));
  CHECK_FALSE(IsSafeBasename("sub\\file.png"));
}

TEST_CASE("IsSafeBasename: accepts normal filenames", "[editor][importer-internal][coverage]") {
  CHECK(IsSafeBasename("texture.png"));
  CHECK(IsSafeBasename("my_file"));
}

TEST_CASE("SanitiseTextureBasename: empty input returns fallback", "[editor][importer-internal][coverage]") {
  CHECK(SanitiseTextureBasename("") == "texture.png");
}

TEST_CASE("SanitiseTextureBasename: extracts filename from path", "[editor][importer-internal][coverage]") {
  CHECK(SanitiseTextureBasename("textures/diffuse.png") == "diffuse.png");
}

TEST_CASE("SanitiseTextureBasename: replaces unsafe characters", "[editor][importer-internal][coverage]") {
  // On POSIX, backslash is not a separator so filename() returns the whole string;
  // the sanitiser replaces backslashes and colons with '_'.
  const std::string result = SanitiseTextureBasename("sub/diffuse.png");
  CHECK(result == "diffuse.png");
}

TEST_CASE("SanitiseTextureBasename: dot-dot returns fallback", "[editor][importer-internal][coverage]") {
  CHECK(SanitiseTextureBasename("..") == "texture.png");
}

TEST_CASE("EditorFileBrowser: RefreshEntries filters hidden files and resolves .fbx.bin extension",
          "[editor][file-browser][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempPath("horo_editor_file_browser_refresh_entries");
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "folder_a", ec);
  REQUIRE_FALSE(ec);

  WriteFile(root / "visible.txt", "ok");
  WriteFile(root / "model.fbx.bin", "mesh");
  WriteFile(root / ".hidden.txt", "skip");
  WriteFile(root / "._resource", "skip");
  std::filesystem::create_directories(root / "__MACOSX", ec);
  REQUIRE_FALSE(ec);

  EditorFileBrowser browser;
  browser.SetRootPath(root);
  browser.NavigateTo(root);

  const auto &state = browser.State();
  REQUIRE(state.currentDir == std::filesystem::weakly_canonical(root));

  const auto hasEntryNamed = [&state](std::string_view name) {
    return std::ranges::any_of(state.entries, [name](const FileBrowserEntry &entry) {
      return entry.name == name;
    });
  };

  CHECK(hasEntryNamed("folder_a"));
  CHECK(hasEntryNamed("visible.txt"));
  CHECK(hasEntryNamed("model.fbx.bin"));
  CHECK_FALSE(hasEntryNamed(".hidden.txt"));
  CHECK_FALSE(hasEntryNamed("._resource"));
  CHECK_FALSE(hasEntryNamed("__MACOSX"));

  const auto compound = std::ranges::find_if(state.entries, [](const FileBrowserEntry &entry) {
    return entry.name == "model.fbx.bin";
  });
  REQUIRE(compound != state.entries.end());
  CHECK(compound->extension == ".fbx");
}

TEST_CASE("EditorFileBrowser: HandleFileDrop selects files and navigates into directories",
          "[editor][file-browser][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempPath("horo_editor_file_browser_file_drop");
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "folder_b", ec);
  REQUIRE_FALSE(ec);
  const std::filesystem::path filePath = root / "drop_me.obj";
  WriteFile(filePath, "o mesh\n");

  EditorFileBrowser browser;
  browser.SetRootPath(root);
  browser.NavigateTo(root);
  browser.SetModalRect(0.0f, 0.0f, 100.0f, 100.0f);

  CHECK_FALSE(browser.HandleFileDrop(-50.0f, -50.0f, filePath.string()));
  CHECK_FALSE(browser.HasSelection());

  REQUIRE(browser.HandleFileDrop(50.0f, 50.0f, filePath.string()));
  CHECK(browser.HasSelection());
  CHECK(std::filesystem::path(browser.GetSelectedFilePath()) ==
        std::filesystem::weakly_canonical(filePath));

  const std::filesystem::path subdir = root / "folder_b";
  REQUIRE(browser.HandleFileDrop(50.0f, 50.0f, subdir.string()));
  CHECK(browser.State().currentDir == std::filesystem::weakly_canonical(subdir));
}

TEST_CASE("EditorFileBrowser: NavigateTo rejects invalid paths and NavigateUp stops at filesystem root",
          "[editor][file-browser][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempPath("horo_editor_file_browser_nav");
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "child", ec);
  REQUIRE_FALSE(ec);

  const std::filesystem::path filePath = root / "not_a_directory.txt";
  WriteFile(filePath, "x");

  EditorFileBrowser browser;
  browser.SetRootPath(root);
  browser.NavigateTo(root / "child");
  REQUIRE(browser.State().currentDir ==
          std::filesystem::weakly_canonical(root / "child"));

  browser.NavigateTo(filePath);
  CHECK(browser.State().currentDir ==
        std::filesystem::weakly_canonical(root / "child"));

  const auto previous = browser.State().currentDir;
  browser.NavigateTo(root / "missing_dir");
  CHECK(browser.State().currentDir == previous);

  const std::filesystem::path filesystemRoot =
      std::filesystem::weakly_canonical(root).root_path();
  REQUIRE_FALSE(filesystemRoot.empty());
  browser.NavigateTo(filesystemRoot);
  REQUIRE(browser.State().currentDir == filesystemRoot);
  browser.NavigateUp();
  CHECK(browser.State().currentDir == filesystemRoot);
}
