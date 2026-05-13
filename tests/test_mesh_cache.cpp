// test_mesh_cache.cpp
//
// Unit tests for renderer/MeshCache.
//
// Coverage:
//   - MeshCache::Get: cache miss (fallback box mesh on ObjLoader failure),
//     cache hit (same shared_ptr for the same path), unsupported runtime
//     formats fall back to a box mesh with an explicit warning, and each
//     distinct path gets its own cache entry (HORO-99: removed the silent
//     .fbx/.glb/.gltf → .obj rewrite hack).
//   - MeshCache::Clear: invalidates all entries.
//
// Constraints:
//   - No OpenGL context required: Mesh::Upload() skips GPU calls when
//     glfwGetCurrentContext() == nullptr but still populates CPU vertex data
//     and m_indexCount.
//   - No file system files need to exist; missing or unsupported paths
//     trigger the fallback box-mesh code path.

#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "renderer/Mesh.h"
#include "renderer/MeshCache.h"

using namespace Horo;

// ===========================================================================
// MeshCache — cache miss: non-existent OBJ falls back to box mesh
// ===========================================================================

TEST_CASE("MeshCache: Get with non-existent OBJ returns a valid fallback mesh", "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> mesh = cache.Get("/nonexistent/path/no_such_file.obj");

  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() > 0);
}

TEST_CASE("MeshCache: fallback mesh from non-existent OBJ has CPU vertex data", "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> mesh = cache.Get("/nonexistent/box.obj");

  REQUIRE(!mesh->GetVertices().empty());
  REQUIRE(!mesh->GetIndices().empty());
}

// ===========================================================================
// MeshCache — cache hit: same path returns identical shared_ptr
// ===========================================================================

TEST_CASE("MeshCache: Get the same path twice returns the same shared_ptr", "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> a = cache.Get("/does/not/exist/crate.obj");
  std::shared_ptr<Mesh> b = cache.Get("/does/not/exist/crate.obj");

  REQUIRE(a != nullptr);
  REQUIRE(a == b);
}

// ===========================================================================
// MeshCache — Clear: post-clear Get creates a new mesh instance
// ===========================================================================

TEST_CASE("MeshCache: Clear invalidates cached entries so next Get is fresh", "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> before = cache.Get("/does/not/exist/sphere.obj");
  cache.Clear();
  std::shared_ptr<Mesh> after = cache.Get("/does/not/exist/sphere.obj");

  // Different allocation after clear
  REQUIRE(before != after);
  REQUIRE(after != nullptr);
}

TEST_CASE("MeshCache: Clear leaves cache empty so subsequent Get is a miss", "[renderer][mesh_cache]") {
  MeshCache cache;
  cache.Get("virtual/any.obj"); // populate
  cache.Clear();                // clear

  // New Get after clear must still succeed (returns fallback box)
  std::shared_ptr<Mesh> mesh = cache.Get("virtual/any.obj");
  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() > 0);
}

// ===========================================================================
// MeshCache — unsupported runtime formats fall back to box mesh (HORO-99)
// ===========================================================================
// Before HORO-99: .fbx / .glb / .gltf were silently rewritten to .obj and
// shared a cache entry with the rewritten path. After HORO-99 the runtime is
// honest: anything that is not currently a supported runtime format gets the
// fallback box and its own cache entry.

TEST_CASE("MeshCache: .fbx falls back to box mesh without rewriting the path", "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> mesh = cache.Get("/assets/models/crate.fbx");

  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() > 0);
}

TEST_CASE("MeshCache: .glb falls back to box mesh without rewriting the path", "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> mesh = cache.Get("/assets/models/crate.glb");

  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() > 0);
}

TEST_CASE("MeshCache: .gltf falls back to box mesh without rewriting the path", "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> mesh = cache.Get("/assets/models/crate.gltf");

  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() > 0);
}

TEST_CASE("MeshCache: .mesh.bin falls back to box mesh until HORO-100 wires it in", "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> mesh = cache.Get("/assets/models/crate.mesh.bin");

  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() > 0);
}

// ===========================================================================
// MeshCache — distinct paths must not share cache entries (HORO-99)
// ===========================================================================
// The pre-HORO-99 rewrite hack made .fbx and .obj share a slot. After HORO-99
// each path gets its own slot regardless of extension.

TEST_CASE("MeshCache: .fbx and .obj with the same stem do NOT share a cache entry", "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> via_fbx = cache.Get("/assets/models/rock.fbx");
  std::shared_ptr<Mesh> via_obj = cache.Get("/assets/models/rock.obj");

  REQUIRE(via_fbx != nullptr);
  REQUIRE(via_obj != nullptr);
  REQUIRE(via_fbx != via_obj);
}

TEST_CASE("MeshCache: .glb and .obj with the same stem do NOT share a cache entry", "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> via_glb = cache.Get("/assets/models/barrel.glb");
  std::shared_ptr<Mesh> via_obj = cache.Get("/assets/models/barrel.obj");

  REQUIRE(via_glb != via_obj);
}

TEST_CASE("MeshCache: .gltf and .obj with the same stem do NOT share a cache entry", "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> via_gltf = cache.Get("/assets/models/pillar.gltf");
  std::shared_ptr<Mesh> via_obj = cache.Get("/assets/models/pillar.obj");

  REQUIRE(via_gltf != via_obj);
}
