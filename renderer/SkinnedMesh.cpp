#include "renderer/SkinnedMesh.h"

#include <glad/glad.h>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace Monolith {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

SkinnedMesh::~SkinnedMesh() {
  Release();
}

SkinnedMesh::SkinnedMesh(SkinnedMesh&& o) noexcept
    : m_vao(o.m_vao),
      m_vbo(o.m_vbo),
      m_ebo(o.m_ebo),
      m_indexCount(o.m_indexCount),
      m_halfExtents(o.m_halfExtents),
      m_localAabbCenter(o.m_localAabbCenter) {
  o.m_vao = o.m_vbo = o.m_ebo = 0;
  o.m_indexCount = 0;
}

SkinnedMesh& SkinnedMesh::operator=(SkinnedMesh&& o) noexcept {
  if (this != &o) {
    Release();
    m_vao              = o.m_vao;
    m_vbo              = o.m_vbo;
    m_ebo              = o.m_ebo;
    m_indexCount       = o.m_indexCount;
    m_halfExtents      = o.m_halfExtents;
    m_localAabbCenter  = o.m_localAabbCenter;
    o.m_vao = o.m_vbo = o.m_ebo = 0;
    o.m_indexCount = 0;
  }
  return *this;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SkinnedMesh::SetData(const std::vector<SkinnedVertex>& vertices,
                          const std::vector<unsigned int>& indices) {
  Release();
  Upload(vertices, indices);
}

void SkinnedMesh::Draw() const {
  glBindVertexArray(m_vao);
  glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void SkinnedMesh::Upload(const std::vector<SkinnedVertex>& vertices,
                         const std::vector<unsigned int>& indices) {
  m_indexCount = static_cast<int>(indices.size());

  glGenVertexArrays(1, &m_vao);
  glBindVertexArray(m_vao);

  // --- VBO ---
  glGenBuffers(1, &m_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(SkinnedVertex)),
               vertices.data(),
               GL_STATIC_DRAW);

  // --- EBO ---
  glGenBuffers(1, &m_ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
               indices.data(),
               GL_STATIC_DRAW);

  const GLsizei stride = static_cast<GLsizei>(sizeof(SkinnedVertex));

  // Location 0 — position (vec3)
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0,
                        3,
                        GL_FLOAT,
                        GL_FALSE,
                        stride,
                        reinterpret_cast<void*>(offsetof(SkinnedVertex, position)));

  // Location 1 — normal (vec3)
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1,
                        3,
                        GL_FLOAT,
                        GL_FALSE,
                        stride,
                        reinterpret_cast<void*>(offsetof(SkinnedVertex, normal)));

  // Location 2 — uv (vec2)
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        stride,
                        reinterpret_cast<void*>(offsetof(SkinnedVertex, uv)));

  // Location 3 — boneIndices (ivec4)
  // Must use glVertexAttribIPointer so the integers are not converted to float.
  glEnableVertexAttribArray(3);
  glVertexAttribIPointer(3,
                         4,
                         GL_INT,
                         stride,
                         reinterpret_cast<void*>(offsetof(SkinnedVertex, boneIndices)));

  // Location 4 — boneWeights (vec4)
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4,
                        4,
                        GL_FLOAT,
                        GL_FALSE,
                        stride,
                        reinterpret_cast<void*>(offsetof(SkinnedVertex, boneWeights)));

  glBindVertexArray(0);

  // Compute AABB from bind-pose vertex positions.
  if (!vertices.empty()) {
    Vec3 lo = vertices[0].position;
    Vec3 hi = vertices[0].position;
    for (const auto& v : vertices) {
      lo.x = std::min(lo.x, v.position.x);
      lo.y = std::min(lo.y, v.position.y);
      lo.z = std::min(lo.z, v.position.z);
      hi.x = std::max(hi.x, v.position.x);
      hi.y = std::max(hi.y, v.position.y);
      hi.z = std::max(hi.z, v.position.z);
    }
    m_halfExtents     = (hi - lo) * 0.5f;
    m_localAabbCenter = (lo + hi) * 0.5f;
  }
}

void SkinnedMesh::Release() {
  if (m_ebo)
    glDeleteBuffers(1, &m_ebo);
  if (m_vbo)
    glDeleteBuffers(1, &m_vbo);
  if (m_vao)
    glDeleteVertexArrays(1, &m_vao);
  m_vao = m_vbo = m_ebo = 0;
  m_indexCount = 0;
}

}  // namespace Monolith
