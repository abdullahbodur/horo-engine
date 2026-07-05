#include "renderer/DebugDraw.h"

#include <array>
#include <vector>

#include "math/MathUtils.h"
#include "renderer/Renderer.h"

namespace Horo {
    std::vector<DebugDraw::LineVertex> DebugDraw::s_lines;
    std::vector<DebugDraw::LineVertex> DebugDraw::s_tris;
    std::unique_ptr<Shader> DebugDraw::s_shader;
    std::shared_ptr<IVertexArray>  DebugDraw::s_vao;
    std::shared_ptr<IVertexBuffer> DebugDraw::s_vbo;
    std::shared_ptr<IVertexArray>  DebugDraw::s_triVao;
    std::shared_ptr<IVertexBuffer> DebugDraw::s_triVbo;
    bool DebugDraw::s_initialized = false;

    // Inline GLSL source for debug lines/triangles (shared — position + color).
    static const char *const DEBUG_VERT = R"glsl(
#version 410 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_color;
uniform mat4 u_vp;
out vec4 v_color;
void main() {
    gl_Position = u_vp * vec4(a_pos, 1.0);
    v_color = a_color;
}
)glsl";

    static const char *const DEBUG_FRAG = R"glsl(
#version 410 core
in vec4 v_color;
out vec4 FragColor;
void main() { FragColor = v_color; }
)glsl";

    void DebugDraw::Init() {
        if (!Renderer::GetBackendCapabilities().supportsDebugDraw) {
            s_initialized = false;
            return;
        }

        s_shader =
                std::make_unique<Shader>(Shader::FromSource(DEBUG_VERT, DEBUG_FRAG));

        // Dynamic line vertex buffer — refilled every frame.
        s_vbo = Renderer::CreateVertexBuffer(
            static_cast<uint32_t>(sizeof(LineVertex) * 65536));
        s_vbo->SetLayout({
            {ShaderDataType::Float3, "a_pos"},
            {ShaderDataType::Float4, "a_color"},
        });

        s_vao = Renderer::CreateVertexArray();
        s_vao->AddVertexBuffer(s_vbo);

        // Dynamic triangle vertex buffer — separate so draw-call topology is clean.
        s_triVbo = Renderer::CreateVertexBuffer(
            static_cast<uint32_t>(sizeof(LineVertex) * 65536));
        s_triVbo->SetLayout({
            {ShaderDataType::Float3, "a_pos"},
            {ShaderDataType::Float4, "a_color"},
        });

        s_triVao = Renderer::CreateVertexArray();
        s_triVao->AddVertexBuffer(s_triVbo);

        s_initialized = true;
    }

    void DebugDraw::Shutdown() {
        s_vao.reset();
        s_vbo.reset();
        s_triVao.reset();
        s_triVbo.reset();
        s_shader.reset();
        s_initialized = false;
    }

    void DebugDraw::Line(const Vec3 &from, const Vec3 &to, const Vec4 &color) {
        if (!Renderer::GetBackendCapabilities().supportsDebugDraw)
            return;
        s_lines.emplace_back(from, color);
        s_lines.emplace_back(to, color);
    }

    void DebugDraw::Triangle(const Vec3 &a, const Vec3 &b, const Vec3 &c,
                             const Vec4 &color) {
        if (!Renderer::GetBackendCapabilities().supportsDebugDraw)
            return;
        // Double-sided: emit the triangle twice with reversed winding so the
        // primitive stays visible from both sides regardless of the global
        // GL_CULL_FACE state owned by the render backend.
        s_tris.emplace_back(a, color);
        s_tris.emplace_back(b, color);
        s_tris.emplace_back(c, color);
        s_tris.emplace_back(a, color);
        s_tris.emplace_back(c, color);
        s_tris.emplace_back(b, color);
    }

    void DebugDraw::Sphere(const Vec3 &center, float radius, const Vec4 &color,
                           int segs) {
        // 3 great circles (XY, XZ, YZ)
        for (int axis = 0; axis < 3; axis++) {
            Vec3 prev;
            for (int i = 0; i <= segs; i++) {
                float t = TWO_PI * static_cast<float>(i) / static_cast<float>(segs);
                float cx = Cos(t) * radius;
                float cy = Sin(t) * radius;
                Vec3 p = center;
                if (axis == 0) {
                    p.x += cx;
                    p.y += cy;
                }
                if (axis == 1) {
                    p.x += cx;
                    p.z += cy;
                }
                if (axis == 2) {
                    p.y += cx;
                    p.z += cy;
                }
                if (i > 0)
                    Line(prev, p, color);
                prev = p;
            }
        }
    }

    void DebugDraw::Box(const Vec3 &center, const Vec3 &h, const Vec4 &color) {
        std::array<Vec3, 8> c{};
        c[0] = center + Vec3{-h.x, -h.y, -h.z};
        c[1] = center + Vec3{h.x, -h.y, -h.z};
        c[2] = center + Vec3{h.x, h.y, -h.z};
        c[3] = center + Vec3{-h.x, h.y, -h.z};
        c[4] = center + Vec3{-h.x, -h.y, h.z};
        c[5] = center + Vec3{h.x, -h.y, h.z};
        c[6] = center + Vec3{h.x, h.y, h.z};
        c[7] = center + Vec3{-h.x, h.y, h.z};

        // Bottom
        Line(c[0], c[1], color);
        Line(c[1], c[2], color);
        Line(c[2], c[3], color);
        Line(c[3], c[0], color);
        // Top
        Line(c[4], c[5], color);
        Line(c[5], c[6], color);
        Line(c[6], c[7], color);
        Line(c[7], c[4], color);
        // Pillars
        Line(c[0], c[4], color);
        Line(c[1], c[5], color);
        Line(c[2], c[6], color);
        Line(c[3], c[7], color);
    }

    void DebugDraw::OrientedBox(const Vec3 &center, const Vec3 &h,
                                const Quaternion &rotation,
                                const Vec4 &color) {
        const std::array<Vec3, 8> local = {
            Vec3{-h.x, -h.y, -h.z}, Vec3{h.x, -h.y, -h.z},
            Vec3{h.x, h.y, -h.z},   Vec3{-h.x, h.y, -h.z},
            Vec3{-h.x, -h.y, h.z},  Vec3{h.x, -h.y, h.z},
            Vec3{h.x, h.y, h.z},    Vec3{-h.x, h.y, h.z},
        };
        std::array<Vec3, 8> c{};
        for (size_t i = 0; i < c.size(); ++i)
            c[i] = center + rotation * local[i];

        // Bottom
        Line(c[0], c[1], color);
        Line(c[1], c[2], color);
        Line(c[2], c[3], color);
        Line(c[3], c[0], color);
        // Top
        Line(c[4], c[5], color);
        Line(c[5], c[6], color);
        Line(c[6], c[7], color);
        Line(c[7], c[4], color);
        // Pillars
        Line(c[0], c[4], color);
        Line(c[1], c[5], color);
        Line(c[2], c[6], color);
        Line(c[3], c[7], color);
    }

    void DebugDraw::SolidBox(const Vec3 &center, const Vec3 &h, const Vec4 &color) {
        // Corner layout (same index scheme as the wireframe Box above):
        //   7---6       +Y
        //  /|  /|       |
        // 3---2 |       +---+X
        // | 4-|-5      /
        // |/  |/      +Z
        // 0---1
        std::array<Vec3, 8> c{};
        c[0] = center + Vec3{-h.x, -h.y, -h.z};
        c[1] = center + Vec3{h.x, -h.y, -h.z};
        c[2] = center + Vec3{h.x, h.y, -h.z};
        c[3] = center + Vec3{-h.x, h.y, -h.z};
        c[4] = center + Vec3{-h.x, -h.y, h.z};
        c[5] = center + Vec3{h.x, -h.y, h.z};
        c[6] = center + Vec3{h.x, h.y, h.z};
        c[7] = center + Vec3{-h.x, h.y, h.z};

        // Emit each face as two triangles (double-sided via Triangle()).
        Triangle(c[0], c[1], c[2], color); // back
        Triangle(c[0], c[2], c[3], color);
        Triangle(c[4], c[5], c[6], color); // front
        Triangle(c[4], c[6], c[7], color);
        Triangle(c[0], c[3], c[7], color); // left
        Triangle(c[0], c[7], c[4], color);
        Triangle(c[1], c[2], c[6], color); // right
        Triangle(c[1], c[6], c[5], color);
        Triangle(c[0], c[1], c[5], color); // bottom
        Triangle(c[0], c[5], c[4], color);
        Triangle(c[3], c[2], c[6], color); // top
        Triangle(c[3], c[6], c[7], color);
    }

    void DebugDraw::SolidSphere(const Vec3 &center, float radius, const Vec4 &color, int segs) {
        int stacks = segs / 2;
        int slices = segs;
        
        for (int i = 0; i < stacks; i++) {
            float phi0 = PI * static_cast<float>(i) / static_cast<float>(stacks);
            float phi1 = PI * static_cast<float>(i + 1) / static_cast<float>(stacks);
            float y0 = radius * Cos(phi0);
            float r0 = radius * Sin(phi0);
            float y1 = radius * Cos(phi1);
            float r1 = radius * Sin(phi1);

            for (int j = 0; j < slices; j++) {
                float theta0 = TWO_PI * static_cast<float>(j) / static_cast<float>(slices);
                float theta1 = TWO_PI * static_cast<float>(j + 1) / static_cast<float>(slices);

                float x00 = r0 * Cos(theta0), z00 = r0 * Sin(theta0);
                float x01 = r0 * Cos(theta1), z01 = r0 * Sin(theta1);
                float x10 = r1 * Cos(theta0), z10 = r1 * Sin(theta0);
                float x11 = r1 * Cos(theta1), z11 = r1 * Sin(theta1);

                Vec3 p00 = center + Vec3{x00, y0, z00};
                Vec3 p01 = center + Vec3{x01, y0, z01};
                Vec3 p10 = center + Vec3{x10, y1, z10};
                Vec3 p11 = center + Vec3{x11, y1, z11};

                Triangle(p00, p10, p01, color);
                Triangle(p01, p10, p11, color);
            }
        }
    }

    void DebugDraw::Flush(const Camera &camera, float lineWidth) {
        if (!Renderer::GetBackendCapabilities().supportsDebugDraw) {
            s_lines.clear();
            s_tris.clear();
            return;
        }

        if (!s_initialized) {
            s_lines.clear();
            s_tris.clear();
            return;
        }

        const bool hasLines = !s_lines.empty();
        const bool hasTris = !s_tris.empty();
        if (!hasLines && !hasTris)
            return;

        Renderer::BeginDebugBlend();

        s_shader->Bind();
        s_shader->SetMat4("u_vp", camera.GetViewProjection());

        if (hasTris) {
            s_triVbo->SetData(
                s_tris.data(),
                static_cast<uint32_t>(s_tris.size() * sizeof(LineVertex)));
            s_triVao->Bind();
            s_triVao->DrawArrays(static_cast<uint32_t>(s_tris.size()));
            s_triVao->Unbind();
        }

        if (hasLines) {
            s_vbo->SetData(
                s_lines.data(),
                static_cast<uint32_t>(s_lines.size() * sizeof(LineVertex)));
            s_vao->Bind();
            s_vao->DrawArraysLines(static_cast<uint32_t>(s_lines.size()),
                                   lineWidth);
            s_vao->Unbind();
        }

        Renderer::EndDebugBlend();

        s_lines.clear();
        s_tris.clear();
    }
} // namespace Horo
