#include "renderer/Mesh.h"

// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>

#include "math/MathUtils.h"

namespace Monolith {
    namespace {
        bool HasCurrentGlContext() { return glfwGetCurrentContext() != nullptr; }
    } // namespace

    struct Mesh::GpuStorage {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        unsigned int ebo = 0;
    };

    Mesh::Mesh() = default;

    Mesh::~Mesh() { Release(); }

    Mesh::Mesh(Mesh &&o) noexcept
        : m_gpu(std::move(o.m_gpu)), m_indexCount(o.m_indexCount),
          m_cpuVertices(std::move(o.m_cpuVertices)),
          m_cpuIndices(std::move(o.m_cpuIndices)), m_halfExtents(o.m_halfExtents),
          m_localAabbCenter(o.m_localAabbCenter) {
        o.m_indexCount = 0;
    }

    Mesh &Mesh::operator=(Mesh &&o) noexcept {
        if (this != &o) {
            Release();
            m_gpu = std::move(o.m_gpu);
            m_indexCount = o.m_indexCount;
            m_cpuVertices = std::move(o.m_cpuVertices);
            m_cpuIndices = std::move(o.m_cpuIndices);
            m_halfExtents = o.m_halfExtents;
            m_localAabbCenter = o.m_localAabbCenter;
            o.m_indexCount = 0;
        }
        return *this;
    }

    bool Mesh::IsValid() const { return m_gpu && m_gpu->vao != 0; }

    void Mesh::Release() {
        if (m_gpu) {
            if (HasCurrentGlContext()) {
                if (m_gpu->ebo)
                    glDeleteBuffers(1, &m_gpu->ebo);
                if (m_gpu->vbo)
                    glDeleteBuffers(1, &m_gpu->vbo);
                if (m_gpu->vao)
                    glDeleteVertexArrays(1, &m_gpu->vao);
            }
            m_gpu.reset();
        }
        m_indexCount = 0;
        m_cpuVertices.clear();
        m_cpuIndices.clear();
    }

    void Mesh::SetData(const std::vector<Vertex> &vertices,
                       const std::vector<unsigned int> &indices) {
        Release();
        Upload(vertices, indices);

        // Compute bounding half-extents from vertex positions
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
    }

    void Mesh::Upload(const std::vector<Vertex> &vertices,
                      const std::vector<unsigned int> &indices) {
        m_cpuVertices = vertices;
        m_cpuIndices.assign(indices.begin(), indices.end());
        m_indexCount = static_cast<int>(indices.size());
        if (!HasCurrentGlContext())
            return;

        m_gpu = std::make_unique<GpuStorage>();

        glGenVertexArrays(1, &m_gpu->vao);
        glBindVertexArray(m_gpu->vao);

        glGenBuffers(1, &m_gpu->vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_gpu->vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                     vertices.data(), GL_STATIC_DRAW);

        glGenBuffers(1, &m_gpu->ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_gpu->ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                     indices.data(), GL_STATIC_DRAW);

        // Position (location 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              static_cast<const void *>(static_cast<const std::byte *>(nullptr) + offsetof(
                                                            Vertex, position)));

        // Normal (location 1)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              static_cast<const void *>(static_cast<const std::byte *>(nullptr) + offsetof(
                                                            Vertex, normal)));

        // UV (location 2)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              static_cast<const void *>(static_cast<const std::byte *>(nullptr) + offsetof(
                                                            Vertex, uv)));

        glBindVertexArray(0);

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
    }

    void Mesh::Draw() const {
        if (!m_gpu)
            return;
        glBindVertexArray(m_gpu->vao);
        glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    void Mesh::DrawWireframe() const {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        Draw();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    // ---- Procedural generators ----

    Mesh Mesh::CreateSphere(float radius, int stacks, int slices) {
        std::vector<Vertex> verts;
        std::vector<unsigned int> inds;

        for (int s = 0; s <= stacks; s++) {
            float phi = PI * (static_cast<float>(s) / static_cast<float>(stacks));
            for (int sl = 0; sl <= slices; sl++) {
                float theta = TWO_PI * (static_cast<float>(sl) / static_cast<float>(slices));
                Vec3 normal{Sin(phi) * Cos(theta), Cos(phi), Sin(phi) * Sin(theta)};
                verts.push_back(
                    {
                        normal * radius,
                        normal,
                        {
                            static_cast<float>(sl) / static_cast<float>(slices),
                            static_cast<float>(s) / static_cast<float>(stacks)
                        }
                    });
            }
        }

        for (int s = 0; s < stacks; s++) {
            for (int sl = 0; sl < slices; sl++) {
                auto a = static_cast<unsigned int>(s * (slices + 1) + sl);
                auto b = static_cast<unsigned int>((s + 1) * (slices + 1) + sl);
                unsigned int c = b + 1;
                unsigned int d = a + 1;
                inds.push_back(a);
                inds.push_back(b);
                inds.push_back(d);
                inds.push_back(b);
                inds.push_back(c);
                inds.push_back(d);
            }
        }

        Mesh m;
        m.Upload(verts, inds);
        return m;
    }

    Mesh Mesh::CreateBox(float hx, float hy, float hz) {
        std::vector<Vertex> verts;
        std::vector<unsigned int> inds;

        // 6 faces, each = 2 triangles
        struct Face {
            Vec3 n;
            Vec3 t;
            Vec3 b;
            Vec3 origin;
        };
        std::array<Face, 6> faces = {
            {
                {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {hx, 0, 0}}, // +X
                {{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}, {-hx, 0, 0}}, // -X
                {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, {0, hy, 0}}, // +Y
                {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, {0, -hy, 0}}, // -Y
                {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {0, 0, hz}}, // +Z
                {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, {0, 0, -hz}}, // -Z
            }
        };

        Vec3 extents{hx, hy, hz};
        for (const auto &f: faces) {
            auto base = static_cast<unsigned int>(verts.size());
            // 4 corners: origin ± tangent*e ± bitangent*e
            float te = Vec3::Dot(extents, {Abs(f.t.x), Abs(f.t.y), Abs(f.t.z)});
            float be = Vec3::Dot(extents, {Abs(f.b.x), Abs(f.b.y), Abs(f.b.z)});
            verts.push_back({f.origin - f.t * te - f.b * be, f.n, {0, 0}});
            verts.push_back({f.origin + f.t * te - f.b * be, f.n, {1, 0}});
            verts.push_back({f.origin + f.t * te + f.b * be, f.n, {1, 1}});
            verts.push_back({f.origin - f.t * te + f.b * be, f.n, {0, 1}});
            inds.push_back(base + 0);
            inds.push_back(base + 1);
            inds.push_back(base + 2);
            inds.push_back(base + 0);
            inds.push_back(base + 2);
            inds.push_back(base + 3);
        }

        Mesh m;
        m.Upload(verts, inds);
        return m;
    }

    Mesh Mesh::CreateCylinder(float radius, float halfHeight, int slices) {
        slices = std::max(slices, 3);

        std::vector<Vertex> verts;
        std::vector<unsigned int> inds;
        verts.reserve(static_cast<size_t>(slices) * 4 + 2);
        inds.reserve(static_cast<size_t>(slices) * 12);

        const float topY = halfHeight;
        const float bottomY = -halfHeight;

        // Side ring
        for (int i = 0; i <= slices; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(slices);
            const float a = t * TWO_PI;
            const float c = Cos(a);
            const float s = Sin(a);
            const auto n = Vec3{c, 0.0f, s};

            verts.push_back({{c * radius, topY, s * radius}, n, {t, 0.0f}});
            verts.push_back({{c * radius, bottomY, s * radius}, n, {t, 1.0f}});
        }

        for (int i = 0; i < slices; ++i) {
            const auto a = static_cast<unsigned int>(i * 2);
            const unsigned int b = a + 1;
            const unsigned int c = a + 2;
            const unsigned int d = a + 3;
            inds.push_back(a);
            inds.push_back(b);
            inds.push_back(c);
            inds.push_back(c);
            inds.push_back(b);
            inds.push_back(d);
        }

        const auto topCenter = static_cast<unsigned int>(verts.size());
        verts.push_back({{0.0f, topY, 0.0f}, Vec3::Up(), {0.5f, 0.5f}});
        const auto bottomCenter = static_cast<unsigned int>(verts.size());
        verts.push_back(
            {{0.0f, bottomY, 0.0f}, Vec3{0.0f, -1.0f, 0.0f}, {0.5f, 0.5f}});

        for (int i = 0; i < slices; ++i) {
            const float t0 = static_cast<float>(i) / static_cast<float>(slices);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(slices);
            const float a0 = t0 * TWO_PI;
            const float a1 = t1 * TWO_PI;

            const Vec3 top0 = {Cos(a0) * radius, topY, Sin(a0) * radius};
            const Vec3 top1 = {Cos(a1) * radius, topY, Sin(a1) * radius};
            const Vec3 bot0 = {Cos(a0) * radius, bottomY, Sin(a0) * radius};
            const Vec3 bot1 = {Cos(a1) * radius, bottomY, Sin(a1) * radius};

            const auto ti0 = static_cast<unsigned int>(verts.size());
            verts.push_back(
                {top0, Vec3::Up(), {(Cos(a0) + 1.0f) * 0.5f, (Sin(a0) + 1.0f) * 0.5f}});
            const auto ti1 = static_cast<unsigned int>(verts.size());
            verts.push_back(
                {top1, Vec3::Up(), {(Cos(a1) + 1.0f) * 0.5f, (Sin(a1) + 1.0f) * 0.5f}});

            inds.push_back(topCenter);
            inds.push_back(ti1);
            inds.push_back(ti0);

            const auto bi0 = static_cast<unsigned int>(verts.size());
            verts.push_back({
                bot0,
                Vec3{0.0f, -1.0f, 0.0f},
                {(Cos(a0) + 1.0f) * 0.5f, (Sin(a0) + 1.0f) * 0.5f}
            });
            const auto bi1 = static_cast<unsigned int>(verts.size());
            verts.push_back({
                bot1,
                Vec3{0.0f, -1.0f, 0.0f},
                {(Cos(a1) + 1.0f) * 0.5f, (Sin(a1) + 1.0f) * 0.5f}
            });

            inds.push_back(bottomCenter);
            inds.push_back(bi0);
            inds.push_back(bi1);
        }

        Mesh m;
        m.SetData(verts, inds);
        return m;
    }

    Mesh Mesh::CreatePyramid(float halfBase, float halfHeight) {
        const Vec3 p0{-halfBase, -halfHeight, -halfBase};
        const Vec3 p1{halfBase, -halfHeight, -halfBase};
        const Vec3 p2{halfBase, -halfHeight, halfBase};
        const Vec3 p3{-halfBase, -halfHeight, halfBase};
        const Vec3 apex{0.0f, halfHeight, 0.0f};

        std::vector<Vertex> verts;
        std::vector<unsigned int> inds;
        verts.reserve(16);
        inds.reserve(18);

        // Base (two triangles)
        const auto base = static_cast<unsigned int>(verts.size());
        verts.push_back({p0, Vec3{0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}});
        verts.push_back({p1, Vec3{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}});
        verts.push_back({p2, Vec3{0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}});
        verts.push_back({p3, Vec3{0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}});
        inds.push_back(base + 0);
        inds.push_back(base + 2);
        inds.push_back(base + 1);
        inds.push_back(base + 0);
        inds.push_back(base + 3);
        inds.push_back(base + 2);

        auto addSide = [&](Vec3 a, Vec3 b, Vec3 c) {
            const Vec3 n = Vec3::Cross(b - a, c - a).Normalized();
            const auto i = static_cast<unsigned int>(verts.size());
            verts.push_back({a, n, {0.0f, 1.0f}});
            verts.push_back({b, n, {1.0f, 1.0f}});
            verts.push_back({c, n, {0.5f, 0.0f}});
            inds.push_back(i + 0);
            inds.push_back(i + 1);
            inds.push_back(i + 2);
        };

        addSide(p0, p1, apex);
        addSide(p1, p2, apex);
        addSide(p2, p3, apex);
        addSide(p3, p0, apex);

        Mesh m;
        m.SetData(verts, inds);
        return m;
    }

    Mesh Mesh::CreatePlane(float halfSize) {
        std::vector<Vertex> verts = {
            {{-halfSize, 0, -halfSize}, Vec3::Up(), {0, 0}},
            {{halfSize, 0, -halfSize}, Vec3::Up(), {1, 0}},
            {{halfSize, 0, halfSize}, Vec3::Up(), {1, 1}},
            {{-halfSize, 0, halfSize}, Vec3::Up(), {0, 1}},
        };
        std::vector<unsigned int> inds = {0, 1, 2, 0, 2, 3};
        Mesh m;
        m.Upload(verts, inds);
        return m;
    }

    Mesh Mesh::CreateQuad() {
        std::vector<Vertex> verts = {
            {{-1, -1, 0}, Vec3::Back(), {0, 0}},
            {{1, -1, 0}, Vec3::Back(), {1, 0}},
            {{1, 1, 0}, Vec3::Back(), {1, 1}},
            {{-1, 1, 0}, Vec3::Back(), {0, 1}},
        };
        std::vector<unsigned int> inds = {0, 1, 2, 0, 2, 3};
        Mesh m;
        m.Upload(verts, inds);
        return m;
    }
} // namespace Monolith
