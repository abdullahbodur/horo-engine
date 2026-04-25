// test_mesh_cache.cpp
//
// Unit tests for renderer/MeshCache.
//
// Coverage:
//   - MeshCache::Get: cache miss (fallback box mesh), cache hit (same ptr),
//     .fbx/.glb/.gltf extension redirected to .obj with warning,
//     .fbx and its resolved .obj path share the same cached entry.
//   - MeshCache::Clear: invalidates all entries.
//
// Constraints:
//   - No OpenGL context required: Mesh::Upload() skips GPU calls when
//     glfwGetCurrentContext()==nullptr but still populates CPU vertex data
//     and m_indexCount.
//   - No file system files need to exist; missing .obj files trigger the
//     fallback box-mesh code path.

#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "renderer/Mesh.h"
#include "renderer/MeshCache.h"

using namespace Monolith;

// ===========================================================================
// MeshCache — cache miss: non-existent OBJ falls back to box mesh
// ===========================================================================

TEST_CASE("MeshCache: Get with non-existent OBJ returns a valid fallback mesh",
          "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> mesh = cache.Get("/nonexistent/path/no_such_file.obj");

  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() > 0);
}

TEST_CASE("MeshCache: fallback mesh from non-existent OBJ has CPU vertex data",
          "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> mesh = cache.Get("/nonexistent/box.obj");

  REQUIRE(!mesh->GetVertices().empty());
  REQUIRE(!mesh->GetIndices().empty());
}

// ===========================================================================
// MeshCache — cache hit: same path returns identical shared_ptr
// ===========================================================================

TEST_CASE("MeshCache: Get the same path twice returns the same shared_ptr",
          "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> a = cache.Get("/does/not/exist/crate.obj");
  std::shared_ptr<Mesh> b = cache.Get("/does/not/exist/crate.obj");

  REQUIRE(a != nullptr);
  REQUIRE(a == b);
}

// ===========================================================================
// MeshCache — Clear: post-clear Get creates a new mesh instance
// ===========================================================================

TEST_CASE("MeshCache: Clear invalidates cached entries so next Get is fresh",
          "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> before = cache.Get("/does/not/exist/sphere.obj");
  cache.Clear();
  std::shared_ptr<Mesh> after = cache.Get("/does/not/exist/sphere.obj");

  // Different allocation after clear
  REQUIRE(before != after);
  REQUIRE(after != nullptr);
}

TEST_CASE("MeshCache: Clear leaves cache empty so subsequent Get is a miss",
          "[renderer][mesh_cache]") {
  MeshCache cache;
  cache.Get("virtual/any.obj"); // populate
  cache.Clear();                // clear

  // New Get after clear must still succeed (returns fallback box)
  std::shared_ptr<Mesh> mesh = cache.Get("virtual/any.obj");
  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() > 0);
}

// ===========================================================================
// MeshCache — extension redirects: .fbx / .glb / .gltf → .obj
// ===========================================================================

TEST_CASE("MeshCache: Get with .fbx extension succeeds (redirected to .obj)",
          "[renderer][mesh_cache]") {
  MeshCache cache;
  // .fbx triggers ResolveRuntimeMeshPath which rewrites to .obj.
  // The redirected .obj is also missing, so the fallback box is returned.
  std::shared_ptr<Mesh> mesh = cache.Get("/assets/models/crate.fbx");

  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() > 0);
}

TEST_CASE("MeshCache: Get with .glb extension succeeds (redirected to .obj)",
          "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> mesh = cache.Get("/assets/models/crate.glb");

  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() > 0);
}

TEST_CASE("MeshCache: Get with .gltf extension succeeds (redirected to .obj)",
          "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> mesh = cache.Get("/assets/models/crate.gltf");

  REQUIRE(mesh != nullptr);
  REQUIRE(mesh->GetIndexCount() > 0);
}

TEST_CASE(
    "MeshCache: .fbx and equivalent .obj path share the same cached entry",
    "[renderer][mesh_cache]") {
  // .fbx → /assets/models/rock.obj (resolved internally)
  // Direct .obj request should hit the same cache slot.
  MeshCache cache;
  std::shared_ptr<Mesh> via_fbx = cache.Get("/assets/models/rock.fbx");
  std::shared_ptr<Mesh> via_obj = cache.Get("/assets/models/rock.obj");

  REQUIRE(via_fbx == via_obj);
}

TEST_CASE(
    "MeshCache: .glb and equivalent .obj path share the same cached entry",
    "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> via_glb = cache.Get("/assets/models/barrel.glb");
  std::shared_ptr<Mesh> via_obj = cache.Get("/assets/models/barrel.obj");

  REQUIRE(via_glb == via_obj);
}

TEST_CASE(
    "MeshCache: .gltf and equivalent .obj path share the same cached entry",
    "[renderer][mesh_cache]") {
  MeshCache cache;
  std::shared_ptr<Mesh> via_gltf = cache.Get("/assets/models/pillar.gltf");
  std::shared_ptr<Mesh> via_obj = cache.Get("/assets/models/pillar.obj");

  REQUIRE(via_gltf == via_obj);
}
