#include "renderer/SkinnedMesh.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "renderer/IVertexArray.h"
#include "renderer/IVertexBuffer.h"
#include "renderer/opengl/OpenGLIndexBuffer.h"
#include "renderer/opengl/OpenGLVertexArray.h"
#include "renderer/opengl/OpenGLVertexBuffer.h"

namespace Horo {
    namespace {
        bool HasCurrentGlContext() { return glfwGetCurrentContext() != nullptr; }
    } // namespace

    struct SkinnedMesh::GpuStorage {
        std::shared_ptr<IVertexArray> vao;
    };

    SkinnedMesh::SkinnedMesh() = default;

    // Lifecycle

    SkinnedMesh::~SkinnedMesh() { Release(); }

    SkinnedMesh::SkinnedMesh(SkinnedMesh &&o) noexcept
        : m_gpu(std::move(o.m_gpu)), m_indexCount(o.m_indexCount),
          m_halfExtents(o.m_halfExtents), m_localAabbCenter(o.m_localAabbCenter) {
        o.m_indexCount = 0;
    }

    SkinnedMesh &SkinnedMesh::operator=(SkinnedMesh &&o) noexcept {
        if (this != &o) {
            Release();
            m_gpu = std::move(o.m_gpu);
            m_indexCount = o.m_indexCount;
            m_halfExtents = o.m_halfExtents;
            m_localAabbCenter = o.m_localAabbCenter;
            o.m_indexCount = 0;
        }
        return *this;
    }

    bool SkinnedMesh::IsValid() const { return m_gpu && m_gpu->vao != nullptr; }

    // Public API

    void SkinnedMesh::SetData(const std::vector<SkinnedVertex> &vertices,
                              const std::vector<unsigned int> &indices) {
        Release();
        Upload(vertices, indices);
    }

    void SkinnedMesh::Draw() const {
        if (!m_gpu || !m_gpu->vao)
            return;
        m_gpu->vao->Bind();
        m_gpu->vao->DrawIndexed(static_cast<uint32_t>(m_indexCount));
        m_gpu->vao->Unbind();
    }

    // Private helpers

    void SkinnedMesh::Upload(const std::vector<SkinnedVertex> &vertices,
                             const std::vector<unsigned int> &indices) {
        m_indexCount = static_cast<int>(indices.size());

        // Compute AABB from bind-pose vertex positions even in headless mode.
        if (!vertices.empty()) {
            Vec3 lo = vertices[0].position;
            Vec3 hi = vertices[0].position;
            for (const auto &v: vertices) {
                lo.x = std::min(lo.x, v.position.x);
                lo.y = std::min(lo.y, v.position.y);
                lo.z = std::min(lo.z, v.position.z);
                hi.x = std::max(hi.x, v.position.x);
                hi.y = std::max(hi.y, v.position.y);
                hi.z = std::max(hi.z, v.position.z);
            }
            m_halfExtents = (hi - lo) * 0.5f;
            m_localAabbCenter = (lo + hi) * 0.5f;
        }

        if (!HasCurrentGlContext())
            return;

        m_gpu = std::make_unique<GpuStorage>();

        // SkinnedVertex layout:
        //   location 0 — position     (Float3)
        //   location 1 — normal       (Float3)
        //   location 2 — uv           (Float2)
        //   location 3 — boneIndices  (Int4)
        //   location 4 — boneWeights  (Float4)
        BufferLayout layout = {
            {ShaderDataType::Float3, "a_position"},
            {ShaderDataType::Float3, "a_normal"},
            {ShaderDataType::Float2, "a_uv"},
            {ShaderDataType::Int4,   "a_boneIndices"},
            {ShaderDataType::Float4, "a_boneWeights"},
        };

        auto vbo = std::make_shared<OpenGLVertexBuffer>(
            vertices.data(),
            static_cast<uint32_t>(vertices.size() * sizeof(SkinnedVertex)));
        vbo->SetLayout(layout);

        auto ibo = std::make_shared<OpenGLIndexBuffer>(
            indices.data(),
            static_cast<uint32_t>(indices.size()));

        auto vao = std::make_shared<OpenGLVertexArray>();
        vao->AddVertexBuffer(vbo);
        vao->SetIndexBuffer(ibo);
        m_gpu->vao = std::move(vao);
    }

    void SkinnedMesh::Release() {
        m_gpu.reset();
        m_indexCount = 0;
    }
} // namespace Horo
