// Coverage for renderer/FbxLoader, renderer/MeshBin, and the FbxAssetImporter
// in ui/editor/AssetImporterRegistry. The fixture is the upstream ufbx test
// data file `max2009_cube_anim_5800_binary.fbx`, vendored under
// tests/fixtures/fbx/cube_5800_binary.fbx.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "core/ProjectPath.h"
#include "renderer/FbxLoader.h"
#include "renderer/Mesh.h"
#include "renderer/MeshBin.h"
#include "renderer/MeshCache.h"
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

// ===========================================================================
// HORO-100 — end-to-end: imported FBX asset is loadable through MeshCache
// ===========================================================================
// The FBX importer in HORO-94 produces a managed assets/<guid>/<stem>.mesh.bin
// artefact. HORO-99 made MeshCache route by extension. HORO-100 wires .mesh.bin
// into the runtime side. This test pins the full loop:
//
//   AssetImportService::ImportAssetFromSource(<cube.fbx>) ->
//       AssetDef.mesh = "assets/models/<guid>/cube.mesh.bin"
//   MeshCache::Get(asset.mesh) ->
//       Real Mesh with vertex/index data matching the imported geometry.

TEST_CASE("AssetImportService + MeshCache: imported FBX is loadable end-to-end",
          "[editor][asset-import][fbx][meshbin][runtime]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_runtime_e2e";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const AssetImportService service;
  const std::string source = FixturePath("cube_5800_binary.fbx");
  REQUIRE(std::filesystem::exists(source));

  const AssetImportResult import = service.ImportAssetFromSource(
      source, "cube_e2e", "guid_cube_e2e", "Cube E2E", {});
  REQUIRE(import.ok);
  REQUIRE(import.asset.mesh.ends_with(".mesh.bin"));

  // The asset.mesh string is project-relative; resolve it against root for
  // MeshCache, which expects an absolute or cwd-relative path.
  const std::filesystem::path absoluteMeshPath =
      root / std::filesystem::path(import.asset.mesh);
  REQUIRE(std::filesystem::exists(absoluteMeshPath));

  MeshCache cache;
  const std::shared_ptr<Mesh> mesh = cache.Get(absoluteMeshPath.string());
  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() > 0);
  REQUIRE(!mesh->GetVertices().empty());
  REQUIRE(!mesh->GetIndices().empty());
  // Repeat hits return the same cached pointer.
  const std::shared_ptr<Mesh> mesh2 = cache.Get(absoluteMeshPath.string());
  REQUIRE(mesh == mesh2);
}

// ===========================================================================
// HORO-95 — external texture reference resolution
// ===========================================================================

TEST_CASE("FbxLoader: cube_texture fixture exposes a diffuse texture record", "[fbx][loader][textures]") {
  // Note: cube_texture_5800_binary.fbx stores its texture as an embedded video
  // blob (FBX 5800 era). The loader still produces a texture record; the
  // diffuse-by-name heuristic flags the only entry.
  const std::string path = FixturePath("cube_texture_5800_binary.fbx");
  REQUIRE(std::filesystem::exists(path));

  const FbxLoader::FbxLoadResult result = FbxLoader::LoadStaticMesh(path);
  REQUIRE(result.ok);
  REQUIRE_FALSE(result.textures.empty());
  CHECK(std::ranges::any_of(result.textures, [](const auto &t) {
    return t.isDiffuseAlbedo;
  }));
}

TEST_CASE("FbxLoader: external-only fixture exposes paths but no embedded bytes",
          "[fbx][loader][textures]") {
  const std::string path = FixturePath("external_texture_6100_binary.fbx");
  REQUIRE(std::filesystem::exists(path));

  const FbxLoader::FbxLoadResult result = FbxLoader::LoadStaticMesh(path);
  REQUIRE(result.ok);
  REQUIRE_FALSE(result.textures.empty());
  const auto &record = result.textures.front();
  CHECK(record.embeddedBytes.empty());
  CHECK_FALSE(record.filename.empty());
  CHECK(record.isDiffuseAlbedo);
}

TEST_CASE("FbxAssetImporter: copies sibling external texture into managed storage and sets albedoMap",
          "[editor][asset-import][fbx][textures]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_external_tex";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  // Stage the external-only FBX next to a sibling PNG named to match the
  // basename the FBX references.
  const std::filesystem::path stagingDir = root / "source";
  std::filesystem::create_directories(stagingDir, ec);
  const std::filesystem::path stagedFbx = stagingDir / "ext.fbx";
  std::filesystem::copy_file(FixturePath("external_texture_6100_binary.fbx"),
                             stagedFbx,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  REQUIRE_FALSE(ec);

  // Discover the texture filename via the loader so the test stays correct
  // even if the upstream fixture changes.
  const FbxLoader::FbxLoadResult loaded =
      FbxLoader::LoadStaticMesh(stagedFbx.string());
  REQUIRE(loaded.ok);
  REQUIRE_FALSE(loaded.textures.empty());
  const std::string textureBasename = loaded.textures.front().filename;
  REQUIRE_FALSE(textureBasename.empty());

  // Drop a tiny PNG next to the FBX with the basename the FBX expects.
  const unsigned char png1x1[] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
      0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
      0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, 0x33, 0x00, 0x00, 0x00,
      0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
  {
    const std::filesystem::path texPath = stagingDir / textureBasename;
    std::ofstream f(texPath, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char *>(png1x1), sizeof(png1x1));
  }

  const AssetImportService service;
  const AssetImportResult result = service.ImportAssetFromSource(
      stagedFbx.string(), "extimg", "guid_extimg", "ExtImg", {});
  REQUIRE(result.ok);
  REQUIRE_FALSE(result.asset.albedoMap.empty());
  CHECK(result.asset.albedoMap.find("assets/models/guid_extimg/") !=
        std::string::npos);

  // Diffuse albedo path must resolve to a real file under managed storage.
  const std::filesystem::path absoluteAlbedo = root / result.asset.albedoMap;
  REQUIRE(std::filesystem::is_regular_file(absoluteAlbedo));

  // Metadata sidecar must list both the mesh.bin and the texture.
  AssetMetadata metadata;
  std::string metaError;
  REQUIRE(LoadAssetMetadata("guid_extimg", &metadata, &metaError));
  CHECK(std::ranges::any_of(metadata.producedFiles, [&](const std::string &p) {
    return p.ends_with(".mesh.bin");
  }));
  CHECK(std::ranges::any_of(metadata.producedFiles,
                            [&](const std::string &p) {
                              return p == result.asset.albedoMap;
                            }));
}

TEST_CASE("FbxAssetImporter: missing external texture emits FbxExternalTextureMissing warning",
          "[editor][asset-import][fbx][textures]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_external_tex_missing";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  // Stage the external-only FBX without any sibling texture file.
  const std::filesystem::path stagingDir = root / "source";
  std::filesystem::create_directories(stagingDir, ec);
  const std::filesystem::path stagedFbx = stagingDir / "ext.fbx";
  std::filesystem::copy_file(FixturePath("external_texture_6100_binary.fbx"),
                             stagedFbx,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  REQUIRE_FALSE(ec);

  const AssetImportService service;
  const AssetImportResult result = service.ImportAssetFromSource(
      stagedFbx.string(), "extimg_m", "guid_extimg_m", "ExtImg M", {});
  // The mesh import must still succeed; texture failure is just a warning.
  REQUIRE(result.ok);
  CHECK(result.asset.albedoMap.empty());

  AssetMetadata metadata;
  std::string metaError;
  REQUIRE(LoadAssetMetadata("guid_extimg_m", &metadata, &metaError));
  CHECK(std::ranges::any_of(metadata.diagnostics, [](const auto &d) {
    return d.code == DiagnosticCodes::FbxExternalTextureMissing;
  }));
}

// ===========================================================================
// HORO-96 — embedded texture extraction
// ===========================================================================

TEST_CASE("FbxLoader: embedded fixture exposes textures with non-empty content blobs",
          "[fbx][loader][textures]") {
  const std::string path = FixturePath("embedded_textures_7400_binary.fbx");
  REQUIRE(std::filesystem::exists(path));

  const FbxLoader::FbxLoadResult result = FbxLoader::LoadStaticMesh(path);
  REQUIRE(result.ok);
  REQUIRE_FALSE(result.textures.empty());
  CHECK(std::ranges::any_of(result.textures, [](const auto &t) {
    return !t.embeddedBytes.empty();
  }));
}

TEST_CASE("FbxAssetImporter: extracts embedded texture into managed storage",
          "[editor][asset-import][fbx][textures]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_embedded_tex";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const AssetImportService service;
  const std::string source = FixturePath("embedded_textures_7400_binary.fbx");
  REQUIRE(std::filesystem::exists(source));

  const AssetImportResult result = service.ImportAssetFromSource(
      source, "embed", "guid_embed", "Embed", {});
  REQUIRE(result.ok);

  AssetMetadata metadata;
  std::string metaError;
  REQUIRE(LoadAssetMetadata("guid_embed", &metadata, &metaError));
  // Multiple producedFiles: at least the .mesh.bin and one texture.
  CHECK(metadata.producedFiles.size() >= 2);
  CHECK(std::ranges::any_of(metadata.producedFiles, [&](const std::string &p) {
    return p.ends_with(".mesh.bin");
  }));

  // At least one produced file must be a real texture file under managed storage.
  bool foundExtractedTexture = false;
  for (const std::string &p: metadata.producedFiles) {
    if (p.ends_with(".mesh.bin"))
      continue;
    const std::filesystem::path abs = root / p;
    if (std::filesystem::is_regular_file(abs) &&
        std::filesystem::file_size(abs) > 0) {
      foundExtractedTexture = true;
      break;
    }
  }
  CHECK(foundExtractedTexture);
}

// ===========================================================================
// HORO-97 — persisted produced files and dependency metadata
// ===========================================================================
// AssetMetadata::dependencies is the contract the reimport propagator (HORO-98)
// reads to decide which downstream assets to refresh. The FBX importer must:
// - record the .fbx as a Source dependency;
// - record every produced file as a ProducedOutput dependency;
// - record every resolved external texture source path as an extra Source
//   dependency so reimport sees changes to those files.

TEST_CASE("FbxAssetImporter: external texture is recorded as both Source and ProducedOutput dependencies",
          "[editor][asset-import][fbx][metadata]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_deps_external";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path stagingDir = root / "source";
  std::filesystem::create_directories(stagingDir, ec);
  const std::filesystem::path stagedFbx = stagingDir / "ext.fbx";
  std::filesystem::copy_file(FixturePath("external_texture_6100_binary.fbx"),
                             stagedFbx,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  REQUIRE_FALSE(ec);

  const FbxLoader::FbxLoadResult loaded =
      FbxLoader::LoadStaticMesh(stagedFbx.string());
  REQUIRE(loaded.ok);
  REQUIRE_FALSE(loaded.textures.empty());
  const std::string textureBasename = loaded.textures.front().filename;
  REQUIRE_FALSE(textureBasename.empty());

  const unsigned char png1x1[] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
      0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
      0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, 0x33, 0x00, 0x00, 0x00,
      0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
  const std::filesystem::path externalTexture = stagingDir / textureBasename;
  {
    std::ofstream f(externalTexture, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char *>(png1x1), sizeof(png1x1));
  }

  const AssetImportService service;
  const AssetImportResult result = service.ImportAssetFromSource(
      stagedFbx.string(), "deps_e", "guid_deps_e", "Deps E", {});
  REQUIRE(result.ok);

  AssetMetadata metadata;
  std::string metaError;
  REQUIRE(LoadAssetMetadata("guid_deps_e", &metadata, &metaError));

  // Source dependencies: FBX itself + resolved external texture.
  const auto isSource = [](const AssetDependencyRecord &d) {
    return d.kind == AssetDependencyKind::Source;
  };
  const auto isProduced = [](const AssetDependencyRecord &d) {
    return d.kind == AssetDependencyKind::ProducedOutput;
  };
  CHECK(std::ranges::any_of(metadata.dependencies, [&](const auto &d) {
    return isSource(d) && d.value == stagedFbx.string();
  }));
  CHECK(std::ranges::any_of(metadata.dependencies, [&](const auto &d) {
    return isSource(d) && d.value == externalTexture.string();
  }));
  // ProducedOutput dependencies: mesh.bin and the copied texture.
  CHECK(std::ranges::any_of(metadata.dependencies, [&](const auto &d) {
    return isProduced(d) && d.value.ends_with(".mesh.bin");
  }));
  CHECK(std::ranges::any_of(metadata.dependencies, [&](const auto &d) {
    return isProduced(d) && d.value == result.asset.albedoMap;
  }));
}

TEST_CASE("FbxAssetImporter: embedded texture only adds the FBX as a Source dependency",
          "[editor][asset-import][fbx][metadata]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_deps_embedded";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const AssetImportService service;
  const std::string source = FixturePath("embedded_textures_7400_binary.fbx");
  const AssetImportResult result = service.ImportAssetFromSource(
      source, "deps_emb", "guid_deps_emb", "Deps Emb", {});
  REQUIRE(result.ok);

  AssetMetadata metadata;
  std::string metaError;
  REQUIRE(LoadAssetMetadata("guid_deps_emb", &metadata, &metaError));

  const std::size_t sourceCount = std::ranges::count_if(
      metadata.dependencies, [](const AssetDependencyRecord &d) {
        return d.kind == AssetDependencyKind::Source;
      });
  // Exactly one Source dep: the FBX itself. Embedded textures live inside it,
  // so they do not get their own Source row.
  CHECK(sourceCount == 1);
  CHECK(std::ranges::any_of(metadata.dependencies, [&](const auto &d) {
    return d.kind == AssetDependencyKind::Source && d.value == source;
  }));
  // ProducedOutput count: at least mesh.bin + 1 texture.
  const std::size_t producedCount = std::ranges::count_if(
      metadata.dependencies, [](const AssetDependencyRecord &d) {
        return d.kind == AssetDependencyKind::ProducedOutput;
      });
  CHECK(producedCount >= 2);
}
