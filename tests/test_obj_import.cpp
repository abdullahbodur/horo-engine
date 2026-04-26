#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "editor/EditorAssetImport.h"
#include "renderer/ObjLoader.h"
#include "tests/TestTempPaths.h"

using namespace Monolith;
using namespace Monolith::Editor;
using namespace Monolith::ObjLoader;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string TmpPath(const std::string &name) {
  return (Monolith::Tests::SecureTempBase() / name).string();
}

static void WriteFile(const std::string &path, const std::string &content) {
  std::ofstream f(path);
  f << content;
}

// Parse a "x,y,z" string into three floats.
static bool ParseVec3String(const std::string &s, float &x, float &y,
                            float &z) {
  std::istringstream ss(s);
  char comma1;
  char comma2;
  return static_cast<bool>(ss >> x >> comma1 >> y >> comma2 >> z);
}

// ===========================================================================
// ObjLoader::ComputeAABB
// ===========================================================================

TEST_CASE("ObjLoader::ComputeAABB returns invalid for nonexistent file",
          "[objloader][aabb]") {
  ObjAABB result = ObjLoader::ComputeAABB("/nonexistent/path/no_such_file.obj");
  REQUIRE_FALSE(result.valid);
}

TEST_CASE("ObjLoader::Load throws typed exception for missing OBJ",
          "[objloader][load]") {
  REQUIRE_THROWS_AS(ObjLoader::Load("/nonexistent/path/no_such_file.obj"),
                    ObjLoaderException);
}

TEST_CASE("ObjLoader::Load throws typed exception for OBJ without geometry",
          "[objloader][load]") {
  const std::string path = TmpPath("obj_no_geometry.obj");
  WriteFile(path, "# comment only\n"
                  "vt 0.0 1.0\n"
                  "vn 0.0 1.0 0.0\n");

  REQUIRE_THROWS_AS(ObjLoader::Load(path), ObjLoaderException);
}

TEST_CASE("ObjLoader::Load parses a valid triangle and returns CPU mesh data",
          "[objloader][load][renderer][obj][coverage]") {
  const std::string path = TmpPath("obj_valid_triangle.obj");
  WriteFile(path, "v 0.0 0.0 0.0\n"
                  "v 1.0 0.0 0.0\n"
                  "v 0.0 1.0 0.0\n"
                  "vt 0.0 0.0\n"
                  "vt 1.0 0.0\n"
                  "vt 0.0 1.0\n"
                  "vn 0.0 0.0 1.0\n"
                  "f 1/1/1 2/2/1 3/3/1\n");

  Mesh mesh;
  REQUIRE_NOTHROW(mesh = ObjLoader::Load(path));
  REQUIRE(mesh.GetIndexCount() == 3);
  REQUIRE(mesh.GetVertices().size() == 3);
  REQUIRE(mesh.GetIndices().size() == 3);
}

TEST_CASE("ObjLoader::Load auto-generates normals when OBJ has no vn lines",
          "[objloader][load]") {
  const std::string path = TmpPath("obj_auto_normals.obj");
  WriteFile(path, "v 0.0 0.0 0.0\n"
                  "v 1.0 0.0 0.0\n"
                  "v 0.0 1.0 0.0\n"
                  "f 1 2 3\n");

  const Mesh mesh = ObjLoader::Load(path);
  REQUIRE(mesh.GetVertices().size() == 3);
  for (const Vertex &vertex : mesh.GetVertices()) {
    REQUIRE(vertex.normal.z == Approx(1.0f).epsilon(0.001f));
  }
}

TEST_CASE("ObjLoader::ComputeAABB computes correct bounds for simple triangle",
          "[objloader][aabb]") {
  const std::string path = TmpPath("aabb_triangle.obj");
  WriteFile(path, "# simple triangle\n"
                  "v 0.0 0.0 0.0\n"
                  "v 1.0 2.0 0.5\n"
                  "v -1.0 0.5 0.3\n"
                  "f 1 2 3\n");

  ObjAABB result = ObjLoader::ComputeAABB(path);

  REQUIRE(result.valid);
  REQUIRE(result.min.x == Approx(-1.0f).epsilon(0.001f));
  REQUIRE(result.min.y == Approx(0.0f).epsilon(0.001f));
  REQUIRE(result.min.z == Approx(0.0f).epsilon(0.001f));
  REQUIRE(result.max.x == Approx(1.0f).epsilon(0.001f));
  REQUIRE(result.max.y == Approx(2.0f).epsilon(0.001f));
  REQUIRE(result.max.z == Approx(0.5f).epsilon(0.001f));
}

TEST_CASE("ObjLoader::ComputeAABB ignores non-vertex lines",
          "[objloader][aabb]") {
  const std::string path = TmpPath("aabb_mixed.obj");
  WriteFile(path, "# comment line\n"
                  "mtllib material.mtl\n"
                  "v 2.0 3.0 1.0\n"
                  "vt 0.0 1.0\n"
                  "vn 0.0 1.0 0.0\n"
                  "v -2.0 -3.0 -1.0\n"
                  "usemtl Mat\n"
                  "f 1/1/1 2/1/1\n");

  ObjAABB result = ObjLoader::ComputeAABB(path);

  REQUIRE(result.valid);
  // Only the two "v" lines should influence the AABB.
  REQUIRE(result.min.x == Approx(-2.0f).epsilon(0.001f));
  REQUIRE(result.min.y == Approx(-3.0f).epsilon(0.001f));
  REQUIRE(result.min.z == Approx(-1.0f).epsilon(0.001f));
  REQUIRE(result.max.x == Approx(2.0f).epsilon(0.001f));
  REQUIRE(result.max.y == Approx(3.0f).epsilon(0.001f));
  REQUIRE(result.max.z == Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("ObjLoader::ComputeAABB handles single vertex", "[objloader][aabb]") {
  const std::string path = TmpPath("aabb_single.obj");
  WriteFile(path, "v 5.0 -3.0 7.5\n");

  ObjAABB result = ObjLoader::ComputeAABB(path);

  REQUIRE(result.valid);
  REQUIRE(result.min.x == Approx(5.0f).epsilon(0.001f));
  REQUIRE(result.min.y == Approx(-3.0f).epsilon(0.001f));
  REQUIRE(result.min.z == Approx(7.5f).epsilon(0.001f));
  REQUIRE(result.max.x == Approx(5.0f).epsilon(0.001f));
  REQUIRE(result.max.y == Approx(-3.0f).epsilon(0.001f));
  REQUIRE(result.max.z == Approx(7.5f).epsilon(0.001f));
}

// ===========================================================================
// EditorAssetImport — IsObjFilePath
// ===========================================================================

TEST_CASE("IsObjFilePath returns true for .obj files", "[editor][import]") {
  REQUIRE(IsObjFilePath("model.obj"));
  REQUIRE(IsObjFilePath("/absolute/path/to/mesh.obj"));
  REQUIRE(IsObjFilePath("relative/path/asset.obj"));
  REQUIRE(IsObjFilePath("C:\\Users\\user\\model.obj"));
}

TEST_CASE("IsObjFilePath returns false for non-obj files", "[editor][import]") {
  REQUIRE_FALSE(IsObjFilePath("model.fbx"));
  REQUIRE_FALSE(IsObjFilePath("model.glb"));
  REQUIRE_FALSE(IsObjFilePath("model.gltf"));
  REQUIRE_FALSE(IsObjFilePath("model.obj.bak"));
  REQUIRE_FALSE(IsObjFilePath("model"));
  REQUIRE_FALSE(IsObjFilePath(""));
}

// ===========================================================================
// EditorAssetImport — AssetIdFromImportedPath
// ===========================================================================

TEST_CASE("AssetIdFromImportedPath extracts stem", "[editor][import]") {
  // The stem is the filename without the extension.
  REQUIRE(AssetIdFromImportedPath("/some/path/barrel.obj") == "barrel");
  REQUIRE(AssetIdFromImportedPath("crate.obj") == "crate");
  REQUIRE(AssetIdFromImportedPath("/deep/nested/dir/door_south.obj") ==
          "door_south");
}

// ===========================================================================
// EditorAssetImport — MeshTagFromImportedPath
// ===========================================================================

TEST_CASE("MeshTagFromImportedPath returns assets/models relative path",
          "[editor][import]") {
  // Unix-style path — only the filename is kept and placed under
  // assets/models/.
  REQUIRE(MeshTagFromImportedPath("/some/path/model.obj") ==
          "assets/models/model.obj");
}

TEST_CASE("MeshTagFromImportedPath handles nested Unix path",
          "[editor][import]") {
  // Deeply nested path — only the filename portion should be used.
  REQUIRE(MeshTagFromImportedPath("/a/b/c/d/e/mymodel.obj") ==
          "assets/models/mymodel.obj");
}

// ===========================================================================
// EditorAssetImport — SuggestRenderScale
// ===========================================================================

TEST_CASE("SuggestRenderScale returns 1,1,1 for nonexistent file",
          "[editor][import]") {
  // The mesh tag refers to an OBJ that does not exist on disk.
  // The function should fall back gracefully and return identity scale.
  const std::string result =
      SuggestRenderScale("assets/models/no_such_file.obj", 2.0f);

  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  REQUIRE(ParseVec3String(result, x, y, z));
  REQUIRE(x == Approx(1.0f).epsilon(0.01f));
  REQUIRE(y == Approx(1.0f).epsilon(0.01f));
  REQUIRE(z == Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("ObjLoader::FindDiffuseTexture returns empty for nonexistent OBJ",
          "[objloader][texture]") {
  REQUIRE(
      ObjLoader::FindDiffuseTexture("/nonexistent/path/no_such.obj").empty());
}

TEST_CASE("ObjLoader::FindDiffuseTexture returns empty for OBJ without mtllib",
          "[objloader][texture]") {
  const std::string path =
      Monolith::Tests::SecureTempBase().string() + "/test_no_mtllib.obj";
  WriteFile(path, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  REQUIRE(ObjLoader::FindDiffuseTexture(path).empty());
  std::remove(path.c_str());
}

TEST_CASE("ObjLoader::FindDiffuseTexture returns empty when MTL file missing",
          "[objloader][texture]") {
  const std::string path =
      Monolith::Tests::SecureTempBase().string() + "/test_missing_mtl.obj";
  WriteFile(path, "mtllib missing_material.mtl\nv 0 0 0\n");
  REQUIRE(ObjLoader::FindDiffuseTexture(path).empty());
  std::remove(path.c_str());
}

TEST_CASE("ObjLoader::FindDiffuseTexture returns empty when MTL has no map_Kd",
          "[objloader][texture]") {
  namespace fs = std::filesystem;
  const std::string dir = Monolith::Tests::SecureTempBase().string();
  const std::string objPath = dir + "/test_no_diffuse.obj";
  const std::string mtlPath = dir + "/test_no_diffuse.mtl";
  WriteFile(objPath, "mtllib test_no_diffuse.mtl\nv 0 0 0\n");
  WriteFile(mtlPath, "newmtl MyMaterial\nKa 1.0 1.0 1.0\n");
  REQUIRE(ObjLoader::FindDiffuseTexture(objPath).empty());
  std::remove(objPath.c_str());
  std::remove(mtlPath.c_str());
}

TEST_CASE("ObjLoader::FindDiffuseTexture resolves map_Kd from MTL",
          "[objloader][texture]") {
  namespace fs = std::filesystem;
  const std::string dir = Monolith::Tests::SecureTempBase().string();
  const std::string objPath = dir + "/test_diffuse.obj";
  const std::string mtlPath = dir + "/test_diffuse.mtl";
  WriteFile(objPath, "mtllib test_diffuse.mtl\nv 0 0 0\n");
  WriteFile(mtlPath, "newmtl MyMaterial\nmap_Kd albedo.png\n");
  const std::string result = ObjLoader::FindDiffuseTexture(objPath);
  REQUIRE_FALSE(result.empty());
  REQUIRE(result.find("albedo.png") != std::string::npos);
  std::remove(objPath.c_str());
  std::remove(mtlPath.c_str());
}

TEST_CASE("SuggestRenderScale scales mesh to target height",
          "[editor][import]") {
  // Write an OBJ spanning y = 0.0 .. 0.5  →  height = 0.5 units.
  // With targetHeight = 2.0, the expected uniform scale is 2.0 / 0.5 = 4.0.
  const std::string objPath = TmpPath("scale_test.obj");
  WriteFile(objPath, "v  0.0 0.0 0.0\n"
                     "v  1.0 0.5 0.0\n"
                     "v -1.0 0.5 0.0\n"
                     "f 1 2 3\n");

  // SuggestRenderScale receives a mesh tag which is resolved to an OBJ path.
  // Pass the temp path directly as the tag so the implementation can locate it.
  const std::string result = SuggestRenderScale(objPath, 2.0f);

  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  REQUIRE(ParseVec3String(result, x, y, z));
  REQUIRE(x == Approx(4.0f).epsilon(0.01f));
  REQUIRE(y == Approx(4.0f).epsilon(0.01f));
  REQUIRE(z == Approx(4.0f).epsilon(0.01f));
}
