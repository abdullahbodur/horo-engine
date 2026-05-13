// Coverage for renderer/FbxLoader, renderer/MeshBin, and the FbxAssetImporter
// in ui/editor/AssetImporterRegistry. The fixture is the upstream ufbx test
// data file `max2009_cube_anim_5800_binary.fbx`, vendored under
// tests/fixtures/fbx/cube_5800_binary.fbx.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "core/ProjectPath.h"
#include "renderer/FbxLoader.h"
#include "renderer/Mesh.h"
#include "renderer/MeshBin.h"
#include "tests/TestTempPaths.h"
#include "ui/editor/AssetImportDiagnosticCodes.h"
#include "ui/editor/AssetImportService.h"
#include "ui/editor/AssetImporterRegistry.h"
#include "ui/editor/AssetMetadata.h"
#include "ui/editor/EditorAssetImport.h"

using namespace Horo;
using namespace Horo::Editor;
using Catch::Approx;

namespace {

#ifndef HORO_TEST_SOURCE_DIR
#error "HORO_TEST_SOURCE_DIR must be defined for test_fbx_import to locate fixtures."
#endif

// Returns the absolute path to the vendored binary FBX cube fixture.
std::string FixturePath(const std::string &relative) {
  return (std::filesystem::path(HORO_TEST_SOURCE_DIR) / "fixtures" / "fbx" /
          relative)
      .string();
}

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

void WriteFile(const std::filesystem::path &path, std::string_view content) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << content;
}

} // namespace

// ===========================================================================
// IsFbxFilePath
// ===========================================================================

TEST_CASE("IsFbxFilePath returns true for .fbx files", "[editor][import][fbx]") {
  REQUIRE(IsFbxFilePath("model.fbx"));
  REQUIRE(IsFbxFilePath("/absolute/path/to/model.FBX"));
  REQUIRE(IsFbxFilePath("relative/asset.Fbx"));
  REQUIRE(IsFbxFilePath("C:\\Users\\user\\model.fbx"));
}

TEST_CASE("IsFbxFilePath returns false for non-fbx files", "[editor][import][fbx]") {
  REQUIRE_FALSE(IsFbxFilePath("model.obj"));
  REQUIRE_FALSE(IsFbxFilePath("model.glb"));
  REQUIRE_FALSE(IsFbxFilePath("model.gltf"));
  REQUIRE_FALSE(IsFbxFilePath("model.fbx.bak"));
  REQUIRE_FALSE(IsFbxFilePath("model"));
  REQUIRE_FALSE(IsFbxFilePath(""));
}

// ===========================================================================
// FbxLoader::LoadStaticMesh — error paths
// ===========================================================================

TEST_CASE("FbxLoader: rejects nonexistent file with fbx.parse_failed",
          "[fbx][loader]") {
  const FbxLoader::FbxLoadResult result =
      FbxLoader::LoadStaticMesh("/nonexistent/path/no_such_file.fbx");
  REQUIRE_FALSE(result.ok);
  CHECK(result.errorCode == "fbx.parse_failed");
  CHECK_FALSE(result.error.empty());
}

TEST_CASE("FbxLoader: rejects corrupt file with fbx.parse_failed",
          "[fbx][loader]") {
  const std::string path =
      (Horo::Tests::SecureTempBase() / "horo_fbx_corrupt.fbx").string();
  WriteFile(path, "this is not an FBX file at all");
  const FbxLoader::FbxLoadResult result = FbxLoader::LoadStaticMesh(path);
  REQUIRE_FALSE(result.ok);
  CHECK(result.errorCode == "fbx.parse_failed");
}

// ===========================================================================
// FbxLoader::LoadStaticMesh — happy path against the vendored cube fixture
// ===========================================================================

TEST_CASE("FbxLoader: loads vendored binary cube fixture", "[fbx][loader]") {
  const std::string path = FixturePath("cube_5800_binary.fbx");
  REQUIRE(std::filesystem::exists(path));

  const FbxLoader::FbxLoadResult result = FbxLoader::LoadStaticMesh(path);
  REQUIRE(result.ok);
  REQUIRE(result.error.empty());
  CHECK(result.meshNodeCount >= 1);
  CHECK(result.triangleCount >= 12); // a cube triangulates to at least 12 tris
  REQUIRE(result.vertices.size() == result.indices.size());
  CHECK(result.indices.size() % 3 == 0);

  // AABB sanity: a cube has non-degenerate extent on all axes.
  CHECK(result.aabbMax.x - result.aabbMin.x > 0.0f);
  CHECK(result.aabbMax.y - result.aabbMin.y > 0.0f);
  CHECK(result.aabbMax.z - result.aabbMin.z > 0.0f);
}

// ===========================================================================
// MeshBin: round-trip
// ===========================================================================

TEST_CASE("MeshBin: WriteStaticMesh + ReadStaticMesh round-trip preserves data",
          "[meshbin]") {
  const std::string path =
      (Horo::Tests::SecureTempBase() / "horo_meshbin_roundtrip.mesh.bin")
          .string();

  std::vector<Vertex> vertices = {
      {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
      {{0.0f, 2.0f, -3.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
  };
  std::vector<uint32_t> indices = {0, 1, 2};

  const MeshBin::WriteResult wr =
      MeshBin::WriteStaticMesh(path, vertices, indices);
  REQUIRE(wr.ok);
  REQUIRE(wr.error.empty());

  const MeshBin::ReadResult rr = MeshBin::ReadStaticMesh(path);
  REQUIRE(rr.ok);
  REQUIRE(rr.error.empty());
  REQUIRE(rr.vertices.size() == vertices.size());
  REQUIRE(rr.indices == indices);
  CHECK(rr.aabbMin.x == Approx(0.0f));
  CHECK(rr.aabbMin.y == Approx(0.0f));
  CHECK(rr.aabbMin.z == Approx(-3.0f));
  CHECK(rr.aabbMax.x == Approx(1.0f));
  CHECK(rr.aabbMax.y == Approx(2.0f));
  CHECK(rr.aabbMax.z == Approx(0.0f));
}

TEST_CASE("MeshBin: WriteStaticMesh rejects empty vertex array",
          "[meshbin]") {
  const std::string path =
      (Horo::Tests::SecureTempBase() / "horo_meshbin_empty.mesh.bin").string();
  const MeshBin::WriteResult wr =
      MeshBin::WriteStaticMesh(path, {}, {0, 1, 2});
  REQUIRE_FALSE(wr.ok);
  CHECK_FALSE(wr.error.empty());
}

TEST_CASE("MeshBin: WriteStaticMesh rejects index count not multiple of 3",
          "[meshbin]") {
  const std::string path =
      (Horo::Tests::SecureTempBase() / "horo_meshbin_bad_indices.mesh.bin")
          .string();
  std::vector<Vertex> vertices = {
      {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}};
  const MeshBin::WriteResult wr =
      MeshBin::WriteStaticMesh(path, vertices, {0, 1});
  REQUIRE_FALSE(wr.ok);
  CHECK_FALSE(wr.error.empty());
}

TEST_CASE("MeshBin: ReadStaticMesh rejects bad magic", "[meshbin]") {
  const std::string path =
      (Horo::Tests::SecureTempBase() / "horo_meshbin_bad_magic.mesh.bin")
          .string();
  // Write 80 bytes of zero — header size is 72 + we add some padding.
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  const std::array<char, 80> zeros{};
  stream.write(zeros.data(), zeros.size());
  stream.close();

  const MeshBin::ReadResult rr = MeshBin::ReadStaticMesh(path);
  REQUIRE_FALSE(rr.ok);
  CHECK_FALSE(rr.error.empty());
}

TEST_CASE("MeshBin: ReadStaticMesh rejects nonexistent file", "[meshbin]") {
  const MeshBin::ReadResult rr =
      MeshBin::ReadStaticMesh("/this/path/does/not/exist.mesh.bin");
  REQUIRE_FALSE(rr.ok);
}

// ===========================================================================
// AssetImporterRegistry: FbxAssetImporter is registered
// ===========================================================================

TEST_CASE("AssetImporterRegistry: FBX extension resolves to FbxAssetImporter",
          "[editor][importer-registry][fbx]") {
  const AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindByExtension("model.fbx");
  REQUIRE(imp != nullptr);
  REQUIRE(std::string(imp->ImporterId()) == "builtin.fbx_static_mesh");
  REQUIRE(std::string(imp->AssetKind()) == "static_mesh");
  REQUIRE(registry.FindByExtension("model.FBX") != nullptr);

  const auto ids = registry.RegisteredImporterIds();
  REQUIRE(std::ranges::find(ids, "builtin.fbx_static_mesh") != ids.end());
}

// ===========================================================================
// FbxAssetImporter: error paths
// ===========================================================================

TEST_CASE("FbxAssetImporter: rejects unsupported source type",
          "[editor][asset-import][fbx]") {
  const AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.fbx_static_mesh");
  REQUIRE(imp != nullptr);

  AssetImportRequest req;
  req.assetId = "x";
  req.assetGuid = "guid_x";
  req.sourcePath =
      (Horo::Tests::SecureTempBase() / "totally_not_fbx.txt").string();

  const AssetImportResult result = imp->Import(req);
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].code == DiagnosticCodes::FbxUnsupportedType);
}

TEST_CASE("FbxAssetImporter: emits fbx.source_missing for nonexistent path",
          "[editor][asset-import][fbx]") {
  const AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.fbx_static_mesh");
  REQUIRE(imp != nullptr);

  AssetImportRequest req;
  req.assetId = "x";
  req.assetGuid = "guid_x";
  req.sourcePath = "/nope/not/here.fbx";

  const AssetImportResult result = imp->Import(req);
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].code == DiagnosticCodes::FbxSourceMissing);
}

TEST_CASE("FbxAssetImporter: emits fbx.parse_failed for malformed FBX",
          "[editor][asset-import][fbx]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_parse_fail";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path bad = root / "broken.fbx";
  WriteFile(bad, "not actually fbx bytes");

  const AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.fbx_static_mesh");
  REQUIRE(imp != nullptr);

  AssetImportRequest req;
  req.assetId = "broken";
  req.assetGuid = "guid_broken";
  req.sourcePath = bad.string();

  const AssetImportResult result = imp->Import(req);
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].code == DiagnosticCodes::FbxParseFailed);
}

// ===========================================================================
// FbxAssetImporter: happy path round-trip
// ===========================================================================

TEST_CASE("AssetImportService: imports vendored cube FBX into managed mesh.bin",
          "[editor][asset-import][fbx]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_import_happy";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const AssetImportService service;
  const std::string source = FixturePath("cube_5800_binary.fbx");
  REQUIRE(std::filesystem::exists(source));

  const AssetImportResult result = service.ImportAssetFromSource(
      source, "cube", "guid_cube", "Cube", {{"preset", "default"}});
  REQUIRE(result.ok);
  CHECK(result.asset.guid == "guid_cube");
  CHECK(result.asset.displayName == "Cube");
  CHECK(result.asset.mesh.find("assets/models/guid_cube/") != std::string::npos);
  CHECK(result.asset.mesh.ends_with(".mesh.bin"));

  // The produced mesh.bin must be readable and yield non-empty geometry.
  const std::filesystem::path meshBinPath = root / result.asset.mesh;
  REQUIRE(std::filesystem::exists(meshBinPath));
  const MeshBin::ReadResult rr =
      MeshBin::ReadStaticMesh(meshBinPath.string());
  REQUIRE(rr.ok);
  CHECK_FALSE(rr.vertices.empty());
  CHECK_FALSE(rr.indices.empty());

  // Metadata sidecar must point at the correct importer + record producedFiles.
  AssetMetadata metadata;
  std::string metaError;
  REQUIRE(LoadAssetMetadata("guid_cube", &metadata, &metaError));
  CHECK(metadata.importerId == "builtin.fbx_static_mesh");
  CHECK(metadata.sourcePath == source);
  CHECK(metadata.settings.at("preset") == "default");
  REQUIRE_FALSE(metadata.producedFiles.empty());
  CHECK(std::ranges::any_of(metadata.producedFiles, [&](const std::string &p) {
    return p == result.asset.mesh;
  }));
  CHECK(metadata.lastImportSucceeded);
}
