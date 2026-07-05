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
#include <iterator>
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
    return t.slot == FbxLoader::FbxTextureSlot::Albedo;
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
  CHECK(record.slot == FbxLoader::FbxTextureSlot::Albedo);
  CHECK(record.slot == FbxLoader::FbxTextureSlot::Albedo);
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

TEST_CASE("FbxAssetImporter: texture override is copied and wired as albedoMap",
          "[editor][asset-import][fbx][textures]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_texture_override";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path overrideTexture = root / "override_albedo.png";
  WriteFile(overrideTexture, "png bytes are not decoded by the importer");

  const AssetImportService service;
  const AssetImportResult result = service.ImportAssetFromSource(
      FixturePath("cube_5800_binary.fbx"), "cube_override",
      "guid_cube_override", "Cube Override", {},
      TextureOverrides{.albedoMap = overrideTexture.string()});

  REQUIRE(result.ok);
  REQUIRE_FALSE(result.asset.albedoMap.empty());
  CHECK(result.asset.albedoMap ==
        "assets/models/guid_cube_override/override_albedo.png");
  CHECK(std::filesystem::is_regular_file(root / result.asset.albedoMap));
  CHECK(std::ranges::find(result.metadata.producedFiles,
                          result.asset.albedoMap) !=
        result.metadata.producedFiles.end());
}

TEST_CASE("FbxAssetImporter: texture overrides with matching basenames do not collide",
          "[editor][asset-import][fbx][textures]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_texture_override_collision";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path albedoDir = root / "source_a";
  const std::filesystem::path normalDir = root / "source_b";
  std::filesystem::create_directories(albedoDir, ec);
  std::filesystem::create_directories(normalDir, ec);
  const std::filesystem::path albedoOverride = albedoDir / "shared.png";
  const std::filesystem::path normalOverride = normalDir / "shared.png";
  WriteFile(albedoOverride, "albedo override bytes");
  WriteFile(normalOverride, "normal override bytes");

  const AssetImportService service;
  const AssetImportResult result = service.ImportAssetFromSource(
      FixturePath("cube_5800_binary.fbx"), "cube_override_collision",
      "guid_cube_override_collision", "Cube Override Collision", {},
      TextureOverrides{.albedoMap = albedoOverride.string(),
                       .normalMap = normalOverride.string()});

  REQUIRE(result.ok);
  REQUIRE_FALSE(result.asset.albedoMap.empty());
  REQUIRE_FALSE(result.asset.normalMap.empty());
  CHECK(result.asset.albedoMap != result.asset.normalMap);
  CHECK(result.asset.albedoMap ==
        "assets/models/guid_cube_override_collision/shared.png");
  CHECK(result.asset.normalMap ==
        "assets/models/guid_cube_override_collision/shared_1.png");
  CHECK(std::filesystem::is_regular_file(root / result.asset.albedoMap));
  CHECK(std::filesystem::is_regular_file(root / result.asset.normalMap));

  std::ifstream albedo(root / result.asset.albedoMap, std::ios::binary);
  std::ifstream normal(root / result.asset.normalMap, std::ios::binary);
  const std::string albedoContents((std::istreambuf_iterator<char>(albedo)),
                                   std::istreambuf_iterator<char>());
  const std::string normalContents((std::istreambuf_iterator<char>(normal)),
                                   std::istreambuf_iterator<char>());
  CHECK(albedoContents == "albedo override bytes");
  CHECK(normalContents == "normal override bytes");
  CHECK(std::ranges::find(result.metadata.producedFiles,
                          result.asset.albedoMap) !=
        result.metadata.producedFiles.end());
  CHECK(std::ranges::find(result.metadata.producedFiles,
                          result.asset.normalMap) !=
        result.metadata.producedFiles.end());
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

// ===========================================================================
// HORO-98 — reimport propagation: an FBX root reimport refreshes downstream
// assets that record it as a DownstreamAsset dependency.
// ===========================================================================
//
// Wires through AssetImportService::ReimportAssetWithDependents which already
// existed for OBJ; this is a regression pin for FBX participation. We import
// an FBX cube as the root, manually attach a small OBJ asset to the document
// whose metadata declares the FBX guid as a DownstreamAsset dependency, run
// reimport on the FBX root, and verify both assets appear in the result order
// with successful records and freshly-touched metadata sidecars.

TEST_CASE("AssetImportService: reimporting an FBX asset propagates to dependent OBJ asset",
          "[editor][asset-import][fbx][reimport]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_reimport";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  AssetImportService service;
  SceneDocument doc;

  // Root asset: FBX cube (no textures = simplest happy path).
  const std::string fbxSource = FixturePath("cube_5800_binary.fbx");
  const AssetImportResult fbxImport = service.ImportAssetFromSource(
      fbxSource, "fbx_root", "guid_fbx_root", "FBX Root", {});
  REQUIRE(fbxImport.ok);
  doc.assets["fbx_root"] = fbxImport.asset;

  // Downstream asset: a synthetic OBJ asset that declares the FBX as a
  // DownstreamAsset dependency. The OBJ source is written into the project so
  // ObjAssetImporter can re-run during reimport.
  const std::filesystem::path objSource = root / "downstream.obj";
  WriteFile(objSource, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  const AssetImportResult objImport = service.ImportAssetFromSource(
      objSource.string(), "obj_dep", "guid_obj_dep", "OBJ Dep", {});
  REQUIRE(objImport.ok);
  doc.assets["obj_dep"] = objImport.asset;

  // Wire the dependency edge: obj_dep declares fbx_root as a DownstreamAsset.
  // (Naming is from the perspective of the root: "asset has guid_fbx_root as
  //  a downstream dependency that fans out reimports".)
  AssetMetadata objMeta;
  std::string err;
  REQUIRE(LoadAssetMetadata("guid_obj_dep", &objMeta, &err));
  objMeta.dependencies.emplace_back(AssetDependencyKind::DownstreamAsset,
                                    "guid_fbx_root");
  REQUIRE(SaveAssetMetadata(objMeta, &err));

  const AssetReimportResult result = service.ReimportAssetWithDependents(
      &doc, "guid_fbx_root", "fbx source touched");
  REQUIRE(result.ok);
  CHECK(result.error.empty());

  // Both guids must appear in the topological order, root first.
  REQUIRE(result.order.size() == 2);
  CHECK(result.order[0] == "guid_fbx_root");
  CHECK(result.order[1] == "guid_obj_dep");

  // Each record must be a success.
  REQUIRE(result.records.size() == 2);
  for (const AssetReimportRecord &rec: result.records) {
    INFO("record assetId=" << rec.assetId << " guid=" << rec.assetGuid
                           << " ok=" << rec.ok << " error=" << rec.error);
    CHECK(rec.ok);
  }

  // Sidecars exist and reflect the reimport reason.
  AssetMetadata fbxMetaAfter;
  AssetMetadata objMetaAfter;
  REQUIRE(LoadAssetMetadata("guid_fbx_root", &fbxMetaAfter, &err));
  REQUIRE(LoadAssetMetadata("guid_obj_dep", &objMetaAfter, &err));
  CHECK(fbxMetaAfter.lastImportSucceeded);
  CHECK(objMetaAfter.lastImportSucceeded);
  CHECK(fbxMetaAfter.lastImportReason == "fbx source touched");
  CHECK(objMetaAfter.lastImportReason ==
        "Dependency changed: guid_fbx_root");
}

TEST_CASE("AssetImportService: reimporting an FBX asset alone keeps producedFiles consistent",
          "[editor][asset-import][fbx][reimport]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_reimport_alone";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  AssetImportService service;
  SceneDocument doc;

  const std::string source = FixturePath("embedded_textures_7400_binary.fbx");
  const AssetImportResult firstImport = service.ImportAssetFromSource(
      source, "embed_r", "guid_embed_r", "Embed R", {});
  REQUIRE(firstImport.ok);
  doc.assets["embed_r"] = firstImport.asset;

  AssetMetadata before;
  std::string err;
  REQUIRE(LoadAssetMetadata("guid_embed_r", &before, &err));

  const AssetReimportResult result = service.ReimportAssetWithDependents(
      &doc, "guid_embed_r", "manual reimport");
  REQUIRE(result.ok);
  REQUIRE(result.records.size() == 1);
  REQUIRE(result.records[0].ok);

  AssetMetadata after;
  REQUIRE(LoadAssetMetadata("guid_embed_r", &after, &err));
  CHECK(after.lastImportSucceeded);
  CHECK(after.lastImportReason == "manual reimport");
  // Same set of producedFiles up to ordering.
  std::vector<std::string> beforeSorted = before.producedFiles;
  std::vector<std::string> afterSorted = after.producedFiles;
  std::ranges::sort(beforeSorted);
  std::ranges::sort(afterSorted);
  CHECK(beforeSorted == afterSorted);
}

// ===========================================================================
// HORO-107 — skeletal mesh import (FbxLoader::LoadSkeletalMesh + .skinned.bin)
// ===========================================================================

#include "renderer/SkinnedMeshBin.h"

TEST_CASE("FbxLoader::LoadStaticMesh: skinned fixture sets hasSkinning=true",
          "[fbx][loader][skeletal]") {
  const std::string path = FixturePath("skinned_7400_binary.fbx");
  REQUIRE(std::filesystem::exists(path));
  const FbxLoader::FbxLoadResult result = FbxLoader::LoadStaticMesh(path);
  REQUIRE(result.ok);
  CHECK(result.hasSkinning);
}

TEST_CASE("FbxLoader::LoadSkeletalMesh: extracts vertices, indices, and a topologically sorted bone array",
          "[fbx][loader][skeletal]") {
  const std::string path = FixturePath("skinned_7400_binary.fbx");
  REQUIRE(std::filesystem::exists(path));
  const FbxLoader::FbxSkeletalLoadResult result =
      FbxLoader::LoadSkeletalMesh(path);
  REQUIRE(result.ok);
  CHECK(result.error.empty());
  CHECK_FALSE(result.vertices.empty());
  CHECK_FALSE(result.indices.empty());
  REQUIRE(result.indices.size() % 3 == 0);
  REQUIRE_FALSE(result.bones.empty());

  // Topological order: each non-root parent index must reference an earlier slot.
  for (size_t i = 0; i < result.bones.size(); ++i) {
    INFO("bone[" << i << "] name='" << result.bones[i].name
                   << "' parent=" << result.bones[i].parentIndex);
    if (result.bones[i].parentIndex >= 0) {
      REQUIRE(result.bones[i].parentIndex < static_cast<int>(i));
    }
  }
  // Every vertex's first bone index must be valid.
  for (const SkinnedVertex &sv: result.vertices) {
    REQUIRE(sv.boneIndices[0] >= 0);
    REQUIRE(sv.boneIndices[0] < static_cast<int>(result.bones.size()));
  }
}

TEST_CASE("SkinnedMeshBin: round-trip preserves vertices, indices, and bone hierarchy",
          "[skinned-mesh-bin]") {
  const std::filesystem::path path =
      Horo::Tests::SecureTempBase() / "horo_skinnedbin_roundtrip.skinned.bin";

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
  Bone child;
  child.parentIndex = 0;
  child.name = "child";
  child.inverseBindMatrix = Mat4::Identity();
  bones.push_back(child);

  REQUIRE(SkinnedMeshBin::WriteSkinnedMesh(path.string(), vertices, indices, bones).ok);

  const SkinnedMeshBin::ReadResult rr =
      SkinnedMeshBin::ReadSkinnedMesh(path.string());
  REQUIRE(rr.ok);
  REQUIRE(rr.vertices.size() == 3);
  REQUIRE(rr.indices == indices);
  REQUIRE(rr.bones.size() == 2);
  CHECK(rr.bones[0].name == "root");
  CHECK(rr.bones[0].parentIndex == -1);
  CHECK(rr.bones[1].name == "child");
  CHECK(rr.bones[1].parentIndex == 0);
}

TEST_CASE("SkinnedMeshBin: ReadSkinnedMesh rejects bad magic", "[skinned-mesh-bin]") {
  const std::filesystem::path path =
      Horo::Tests::SecureTempBase() / "horo_skinnedbin_badmagic.skinned.bin";
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  const std::array<char, 80> zeros{};
  stream.write(zeros.data(), zeros.size());
  stream.close();
  const SkinnedMeshBin::ReadResult rr =
      SkinnedMeshBin::ReadSkinnedMesh(path.string());
  REQUIRE_FALSE(rr.ok);
}

TEST_CASE("FbxAssetImporter: skinned FBX produces a managed .skinned.bin",
          "[editor][asset-import][fbx][skeletal]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_skinned_import";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const AssetImportService service;
  const std::string source = FixturePath("skinned_7400_binary.fbx");
  REQUIRE(std::filesystem::exists(source));
  const AssetImportResult result = service.ImportAssetFromSource(
      source, "skinned_a", "guid_skinned_a", "Skinned A", {});
  REQUIRE(result.ok);
  CHECK(result.asset.mesh.ends_with(".skinned.bin"));
  CHECK(result.asset.mesh.find("assets/models/guid_skinned_a/") !=
        std::string::npos);

  const std::filesystem::path absoluteSkinned = root / result.asset.mesh;
  REQUIRE(std::filesystem::is_regular_file(absoluteSkinned));
  const SkinnedMeshBin::ReadResult rr =
      SkinnedMeshBin::ReadSkinnedMesh(absoluteSkinned.string());
  REQUIRE(rr.ok);
  CHECK_FALSE(rr.vertices.empty());
  CHECK_FALSE(rr.bones.empty());

  AssetMetadata metadata;
  std::string metaError;
  REQUIRE(LoadAssetMetadata("guid_skinned_a", &metadata, &metaError));
  CHECK(metadata.importerId == "builtin.fbx_static_mesh");
  CHECK(metadata.sourcePath == source);
  CHECK(std::ranges::any_of(metadata.producedFiles, [&](const std::string &p) {
    return p == result.asset.mesh;
  }));
}

// ===========================================================================
// HORO-108 — animation clip extraction + AnimBin round-trip
// ===========================================================================

#include "renderer/AnimBin.h"

TEST_CASE("FbxLoader::LoadAnimations: animated cube fixture exposes a non-empty clip",
          "[fbx][loader][animation]") {
  // The static-cube fixture happens to ship with a 'Take 001' animation stack
  // covering 0..2s; sampling the cube node by name produces tracks with
  // multiple keyframes per channel.
  const std::string path = FixturePath("cube_5800_binary.fbx");
  REQUIRE(std::filesystem::exists(path));
  const std::vector<std::string> boneNames = {"Box01"};
  const FbxLoader::FbxAnimLoadResult result =
      FbxLoader::LoadAnimations(path, boneNames);
  REQUIRE(result.ok);
  REQUIRE_FALSE(result.clips.empty());
  const AnimationClip &clip = result.clips.front();
  CHECK(clip.duration > 0.0f);
  REQUIRE_FALSE(clip.GetTracks().empty());
  const BoneTrack &track = clip.GetTracks().front();
  CHECK(track.boneIndex == 0);
  CHECK(track.positionTimes.size() >= 2);
  CHECK(track.rotationTimes.size() == track.positionTimes.size());
  CHECK(track.scaleTimes.size() == track.positionTimes.size());
}

TEST_CASE("FbxLoader::LoadAnimations: file with no anim_stacks returns ok with empty clips",
          "[fbx][loader][animation]") {
  const std::string path = FixturePath("skinned_7400_binary.fbx");
  const std::vector<std::string> boneNames = {"Bone"};
  const FbxLoader::FbxAnimLoadResult result =
      FbxLoader::LoadAnimations(path, boneNames);
  REQUIRE(result.ok);
  CHECK(result.clips.empty());
}

TEST_CASE("AnimBin: round-trip preserves clip name, duration, and track keys",
          "[anim-bin]") {
  const std::filesystem::path path =
      Horo::Tests::SecureTempBase() / "horo_animbin_roundtrip.anim.bin";

  AnimationClip clip;
  clip.name = "Walk";
  clip.duration = 1.5f;
  BoneTrack track;
  track.boneIndex = 2;
  track.positionTimes = {0.0f, 0.5f, 1.0f};
  track.positions = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
  track.rotationTimes = {0.0f, 0.5f, 1.0f};
  track.rotations = {Quaternion{0, 0, 0, 1}, Quaternion{0, 0, 0, 1},
                      Quaternion{0, 0, 0, 1}};
  track.scaleTimes = {0.0f, 1.0f};
  track.scales = {{1, 1, 1}, {1, 1, 1}};
  clip.AddTrack(std::move(track));

  REQUIRE(AnimBin::WriteClips(path.string(), {clip}).ok);
  const AnimBin::ReadResult rr = AnimBin::ReadClips(path.string());
  REQUIRE(rr.ok);
  REQUIRE(rr.clips.size() == 1);
  CHECK(rr.clips[0].name == "Walk");
  CHECK(rr.clips[0].duration == 1.5f);
  REQUIRE(rr.clips[0].GetTracks().size() == 1);
  const BoneTrack &back = rr.clips[0].GetTracks().front();
  CHECK(back.boneIndex == 2);
  REQUIRE(back.positions.size() == 3);
  CHECK(back.positions[2].x == 2.0f);
  CHECK(back.rotations.size() == 3);
  CHECK(back.scales.size() == 2);
}

TEST_CASE("AnimBin: ReadClips rejects bad magic", "[anim-bin]") {
  const std::filesystem::path path =
      Horo::Tests::SecureTempBase() / "horo_animbin_badmagic.anim.bin";
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  const std::array<char, 64> zeros{};
  stream.write(zeros.data(), zeros.size());
  stream.close();
  const AnimBin::ReadResult rr = AnimBin::ReadClips(path.string());
  REQUIRE_FALSE(rr.ok);
}

TEST_CASE("FbxAssetImporter: skinned FBX without animation does NOT produce .anim.bin",
          "[editor][asset-import][fbx][animation]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_skinned_no_anim";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const AssetImportService service;
  const std::string source = FixturePath("skinned_7400_binary.fbx");
  const AssetImportResult result = service.ImportAssetFromSource(
      source, "skinned_b", "guid_skinned_b", "Skinned B", {});
  REQUIRE(result.ok);

  AssetMetadata metadata;
  std::string metaError;
  REQUIRE(LoadAssetMetadata("guid_skinned_b", &metadata, &metaError));
  // No anim.bin should appear in producedFiles when the FBX has zero anim_stacks.
  CHECK_FALSE(std::ranges::any_of(metadata.producedFiles,
                                   [](const std::string &p) {
                                     return p.ends_with(".anim.bin");
                                   }));
}

// ===========================================================================
// HORO-104 + HORO-105 + HORO-110 — bundled FBX import test sweep
// ===========================================================================
// HORO-104 (importer fixtures), HORO-105 (editor integration), and HORO-110
// (skinned + animation regression) were planned as a final bundled test pass.
// The per-feature PRs in this stack already shipped most of the coverage; this
// section adds the end-to-end consolidations that tie everything together
// across the public surface.

#include "ui/editor/components/EditorImportAssetModal.h"

TEST_CASE("E2E: Import Asset modal drives FBX import all the way to producedFiles",
          "[editor][asset-import][fbx][e2e]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_e2e_modal_fbx";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  AssetImporterRegistry registry;
  EditorImportAssetModal modal;

  // 1. User opens the modal seeded with an FBX path; importer is auto-selected.
  modal.Open(FixturePath("cube_5800_binary.fbx"), &registry, std::filesystem::path{});
  REQUIRE(modal.IsOpen());
  REQUIRE(modal.DraftForTest().importerId == "builtin.fbx_static_mesh");
  REQUIRE(modal.DraftForTest().assetId == "cube_5800_binary");

  // 2. User clicks Import → caller drives the actual import via AssetImportService.
  modal.RequestImportForTest();
  REQUIRE(modal.HasPendingRequest());
  const ImportAssetRequest req = modal.ConsumePendingRequest();

  AssetImportService service;
  const AssetImportResult result = service.ImportAssetFromSource(
      req.sourcePath, req.assetId, "guid_e2e_cube",
      req.displayName.empty() ? req.assetId : req.displayName);
  REQUIRE(result.ok);

  // 3. Caller pushes the outcome back into the modal; clean import auto-closes it.
  ImportAssetOutcome outcome;
  outcome.ok = result.ok;
  outcome.error = result.error;
  outcome.assetMesh = result.asset.mesh;
  outcome.assetAlbedoMap = result.asset.albedoMap;
  outcome.diagnostics = result.diagnostics;
  modal.SetLastResult(outcome);
  CHECK_FALSE(modal.IsOpen());

  // 4. The produced .mesh.bin is on disk and loadable via MeshCache.
  const std::filesystem::path meshAbs = root / result.asset.mesh;
  REQUIRE(std::filesystem::is_regular_file(meshAbs));
  MeshCache cache;
  const std::shared_ptr<Mesh> mesh = cache.Get(meshAbs.string());
  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() > 0);
  REQUIRE_FALSE(mesh->GetVertices().empty());
}

TEST_CASE("E2E: skinned + animated FBX yields .skinned.bin and (when present) .anim.bin",
          "[editor][asset-import][fbx][skeletal][animation][e2e]") {
  // Pin the regression: the skinned fixture has no anim_stacks, so the import
  // must produce .skinned.bin but not a .anim.bin. This confirms HORO-107 +
  // HORO-108 cooperate without false positives.
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_e2e_skinned_no_anim";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  AssetImportService service;
  const std::string source = FixturePath("skinned_7400_binary.fbx");
  const AssetImportResult result = service.ImportAssetFromSource(
      source, "skinned_e2e", "guid_skinned_e2e", "Skinned E2E", {});
  REQUIRE(result.ok);
  CHECK(result.asset.mesh.ends_with(".skinned.bin"));

  AssetMetadata metadata;
  std::string metaError;
  REQUIRE(LoadAssetMetadata("guid_skinned_e2e", &metadata, &metaError));

  bool sawSkinnedBin = false;
  bool sawAnimBin = false;
  for (const std::string &p: metadata.producedFiles) {
    if (p.ends_with(".skinned.bin")) sawSkinnedBin = true;
    if (p.ends_with(".anim.bin")) sawAnimBin = true;
  }
  CHECK(sawSkinnedBin);
  CHECK_FALSE(sawAnimBin); // fixture has no anim_stacks
}

TEST_CASE("E2E: drag-drop predicate accepts both .obj and .fbx",
          "[editor][asset-import][fbx][drag-drop][e2e]") {
  // ProcessPendingMeshDrops dispatches by IsObjFilePath || IsFbxFilePath.
  // Pin both predicates here so a future widening that re-introduces an
  // .fbx-only or .obj-only path breaks this test loudly.
  REQUIRE(IsObjFilePath("/x/y/cube.obj"));
  REQUIRE(IsFbxFilePath("/x/y/cube.fbx"));
  REQUIRE_FALSE(IsObjFilePath("/x/y/cube.fbx"));
  REQUIRE_FALSE(IsFbxFilePath("/x/y/cube.obj"));
}

TEST_CASE("E2E: every vendored FBX fixture loads without parse error",
          "[fbx][loader][e2e]") {
  // Coverage map for the fixtures vendored across this stack:
  // - cube_5800_binary           HORO-94 + HORO-108 (animation extraction).
  // - cube_texture_5800_binary   HORO-95/96 (embedded video texture).
  // - embedded_textures_7400     HORO-95/96 (multi-channel embedded textures).
  // - external_texture_6100      HORO-95   (external reference, no embedded).
  // - skinned_7400_binary        HORO-107  (skinned mesh + skeleton).
  const std::vector<std::string> fixtures = {
      "cube_5800_binary.fbx",
      "cube_texture_5800_binary.fbx",
      "embedded_textures_7400_binary.fbx",
      "external_texture_6100_binary.fbx",
      "skinned_7400_binary.fbx",
  };
  for (const std::string &f: fixtures) {
    INFO("fixture: " << f);
    const std::string path = FixturePath(f);
    REQUIRE(std::filesystem::exists(path));
    const FbxLoader::FbxLoadResult result = FbxLoader::LoadStaticMesh(path);
    REQUIRE(result.ok);
    CHECK(result.error.empty());
  }
}

// ===========================================================================
// Coverage: FbxCreateDirectoryFailed error path (AssetImporterRegistry.cpp:800)
// ===========================================================================

TEST_CASE("FbxAssetImporter: emits FbxCreateDirectoryFailed when managed dir is blocked",
          "[editor][asset-import][fbx][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_create_dir_fail";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::string source = FixturePath("cube_5800_binary.fbx");
  REQUIRE(std::filesystem::exists(source));

  const std::string assetGuid = "guid_fbx_dir_fail";
  const std::filesystem::path managedDir = GetManagedAssetDirectory(assetGuid);
  std::filesystem::create_directories(managedDir.parent_path(), ec);
  REQUIRE_FALSE(ec);
  // Block directory creation by placing a regular file at the managed path.
  WriteFile(managedDir, "not-a-directory");

  const AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.fbx_static_mesh");
  REQUIRE(imp != nullptr);

  AssetImportRequest req;
  req.assetId = "fbx_dir_fail";
  req.assetGuid = assetGuid;
  req.sourcePath = source;
  const AssetImportResult result = imp->Import(req);
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].code == DiagnosticCodes::FbxCreateDirectoryFailed);
}

// ===========================================================================
// Coverage: CopyFileReplacing same-file branch (AssetImporterRegistry.cpp:61)
// ===========================================================================

TEST_CASE("ObjImporter: importing OBJ already inside managed dir succeeds (same-file copy path)",
          "[editor][asset-import][obj][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_obj_same_file";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::string assetGuid = "guid_obj_same";
  const std::filesystem::path managedDir = GetManagedAssetDirectory(assetGuid);
  std::filesystem::create_directories(managedDir, ec);
  REQUIRE_FALSE(ec);

  // Place the OBJ directly inside the managed directory (source == destination).
  const std::filesystem::path objInManaged = managedDir / "cube.obj";
  WriteFile(objInManaged, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

  const AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.obj_mesh");
  REQUIRE(imp != nullptr);

  AssetImportRequest req;
  req.assetId = "same_file";
  req.assetGuid = assetGuid;
  req.sourcePath = objInManaged.string();
  const AssetImportResult result = imp->Import(req);
  REQUIRE(result.ok);
  CHECK_FALSE(result.asset.mesh.empty());
}

// ===========================================================================
// Coverage: ScanMtlForTextureNames branches (AssetImporterRegistry.cpp:89-103)
// ===========================================================================

TEST_CASE("ObjImporter: OBJ with MTL containing multiple map_ directives copies all textures",
          "[editor][asset-import][obj][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_obj_mtl_multi_tex";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path srcDir = root / "source";
  std::filesystem::create_directories(srcDir, ec);

  const std::filesystem::path objPath = srcDir / "model.obj";
  WriteFile(objPath, "mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

  // MTL with multiple map_ directives including short lines and no-space lines
  const std::filesystem::path mtlPath = srcDir / "model.mtl";
  WriteFile(mtlPath, "newmtl Mat\n"
                     "map_Kd diffuse.png\n"
                     "map_Ks specular.png\n"
                     "short\n"           // line < 8 chars, should be skipped
                     "notmap_ x.png\n"); // prefix != "map_", should be skipped

  WriteFile(srcDir / "diffuse.png", "PNG");
  WriteFile(srcDir / "specular.png", "PNG");

  const AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.obj_mesh");
  REQUIRE(imp != nullptr);

  AssetImportRequest req;
  req.assetId = "multi_tex";
  req.assetGuid = "guid_multi_tex";
  req.sourcePath = objPath.string();
  const AssetImportResult result = imp->Import(req);
  REQUIRE(result.ok);
}

// ===========================================================================
// Coverage: OBJ with MTL that has no texture references (empty scan result)
// ===========================================================================

TEST_CASE("ObjImporter: OBJ with MTL containing no map_ lines still succeeds",
          "[editor][asset-import][obj][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_obj_mtl_no_tex";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path srcDir = root / "source";
  std::filesystem::create_directories(srcDir, ec);

  const std::filesystem::path objPath = srcDir / "plain.obj";
  WriteFile(objPath, "mtllib plain.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

  const std::filesystem::path mtlPath = srcDir / "plain.mtl";
  WriteFile(mtlPath, "newmtl Material\nKd 0.8 0.8 0.8\n");

  const AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.obj_mesh");
  REQUIRE(imp != nullptr);

  AssetImportRequest req;
  req.assetId = "plain_obj";
  req.assetGuid = "guid_plain_obj";
  req.sourcePath = objPath.string();
  const AssetImportResult result = imp->Import(req);
  REQUIRE(result.ok);
}

// ===========================================================================
// Coverage: FbxMeshWriteFailed error path (AssetImporterRegistry.cpp:889)
// ===========================================================================

TEST_CASE("FbxAssetImporter: emits FbxMeshWriteFailed when mesh.bin destination is blocked",
          "[editor][asset-import][fbx][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_mesh_write_fail";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::string source = FixturePath("cube_5800_binary.fbx");
  REQUIRE(std::filesystem::exists(source));

  const std::string assetGuid = "guid_fbx_mesh_write_fail";
  const std::filesystem::path managedDir = GetManagedAssetDirectory(assetGuid);
  std::filesystem::create_directories(managedDir, ec);
  REQUIRE_FALSE(ec);
  // Block the mesh.bin write by creating a directory with the same name.
  std::filesystem::create_directories(managedDir / "cube_5800_binary.mesh.bin", ec);
  REQUIRE_FALSE(ec);

  const AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.fbx_static_mesh");
  REQUIRE(imp != nullptr);

  AssetImportRequest req;
  req.assetId = "mesh_write_fail";
  req.assetGuid = assetGuid;
  req.sourcePath = source;
  const AssetImportResult result = imp->Import(req);
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].code == DiagnosticCodes::FbxMeshWriteFailed);
}

// ===========================================================================
// Coverage: FbxSkeletonWriteFailed error path (AssetImporterRegistry.cpp:949)
// ===========================================================================

TEST_CASE("FbxAssetImporter: emits FbxSkeletonWriteFailed when skinned.bin destination is blocked",
          "[editor][asset-import][fbx][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_fbx_skinned_write_fail";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "assets" / "models", ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::string source = FixturePath("skinned_7400_binary.fbx");
  REQUIRE(std::filesystem::exists(source));

  const std::string assetGuid = "guid_fbx_skinned_write_fail";
  const std::filesystem::path managedDir = GetManagedAssetDirectory(assetGuid);
  std::filesystem::create_directories(managedDir, ec);
  REQUIRE_FALSE(ec);
  // Block the skinned.bin write by creating a directory with the same name.
  std::filesystem::create_directories(
      managedDir / "skinned_7400_binary.skinned.bin", ec);
  REQUIRE_FALSE(ec);

  const AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.fbx_static_mesh");
  REQUIRE(imp != nullptr);

  AssetImportRequest req;
  req.assetId = "skinned_write_fail";
  req.assetGuid = assetGuid;
  req.sourcePath = source;
  const AssetImportResult result = imp->Import(req);
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics[0].code == DiagnosticCodes::FbxSkeletonWriteFailed);
}

// ===========================================================================
// Coverage: OBJ with mtllib referencing a non-existent MTL file
// ===========================================================================

TEST_CASE("ObjImporter: OBJ referencing missing MTL file still imports successfully",
          "[editor][asset-import][obj][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_obj_missing_mtl";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path srcDir = root / "source";
  std::filesystem::create_directories(srcDir, ec);

  // OBJ references an MTL that does not exist on disk
  const std::filesystem::path objPath = srcDir / "ghost_mtl.obj";
  WriteFile(objPath, "mtllib nonexistent.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

  const AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.obj_mesh");
  REQUIRE(imp != nullptr);

  AssetImportRequest req;
  req.assetId = "ghost_mtl";
  req.assetGuid = "guid_ghost_mtl";
  req.sourcePath = objPath.string();
  const AssetImportResult result = imp->Import(req);
  REQUIRE(result.ok);
}

// ===========================================================================
// Coverage: OBJ with MTL that has map_ line without space separator
// ===========================================================================

TEST_CASE("ObjImporter: OBJ with MTL having malformed map_ lines still imports",
          "[editor][asset-import][obj][coverage]") {
  const std::filesystem::path root =
      Horo::Tests::SecureTempBase() / "horo_obj_mtl_malformed";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  WriteFile(root / "CMakePresets.json", "{}");
  ProjectPathGuard guard(root);

  const std::filesystem::path srcDir = root / "source";
  std::filesystem::create_directories(srcDir, ec);

  const std::filesystem::path objPath = srcDir / "malformed.obj";
  WriteFile(objPath, "mtllib malformed.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

  // MTL with map_ line that has no space (malformed) — triggers spacePos == npos branch
  const std::filesystem::path mtlPath = srcDir / "malformed.mtl";
  WriteFile(mtlPath, "newmtl Mat\n"
                     "map_KdNoSpace\n"  // no space after map_Kd — triggers continue
                     "map_Kd trailing_whitespace.png  \r\n"); // trailing whitespace + \r

  WriteFile(srcDir / "trailing_whitespace.png", "PNG");

  const AssetImporterRegistry registry;
  const AssetImporter *imp = registry.FindById("builtin.obj_mesh");
  REQUIRE(imp != nullptr);

  AssetImportRequest req;
  req.assetId = "malformed_mtl";
  req.assetGuid = "guid_malformed_mtl";
  req.sourcePath = objPath.string();
  const AssetImportResult result = imp->Import(req);
  REQUIRE(result.ok);
}
