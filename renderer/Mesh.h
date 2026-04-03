#pragma once
#include <vector>

#include "math/Vec2.h"
#include "math/Vec3.h"

namespace Monolith {

struct Vertex {
  Vec3 position;
  Vec3 normal;
  Vec2 uv;
};

class Mesh {
 public:
  Mesh() = default;
  ~Mesh();

  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;
  Mesh(Mesh&& o) noexcept;
  Mesh& operator=(Mesh&& o) noexcept;

  void SetData(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
  void Draw() const;
  void DrawWireframe() const;

  bool IsValid() const { return m_vao != 0; }
  int GetIndexCount() const { return m_indexCount; }
  Vec3 GetHalfExtents() const { return m_halfExtents; }
  Vec3 GetLocalAabbCenter() const { return m_localAabbCenter; }

  // --- Procedural generators ---
  static Mesh CreateSphere(float radius = 1.0f, int stacks = 16, int slices = 16);
  static Mesh CreateBox(float halfX = 0.5f, float halfY = 0.5f, float halfZ = 0.5f);
  static Mesh CreateCylinder(float radius = 0.5f, float halfHeight = 0.5f, int slices = 20);
  static Mesh CreatePyramid(float halfBase = 0.5f, float halfHeight = 0.5f);
  static Mesh CreatePlane(float halfSize = 10.0f);
  static Mesh CreateQuad();

 private:
  unsigned int m_vao = 0;
  unsigned int m_vbo = 0;
  unsigned int m_ebo = 0;
  int m_indexCount = 0;
  Vec3 m_halfExtents = {0.5f, 0.5f, 0.5f};
  Vec3 m_localAabbCenter = Vec3::Zero();

  void Upload(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
  void Release();
};

}  // namespace Monolith
