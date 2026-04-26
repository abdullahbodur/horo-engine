#include "renderer/DebugDraw.h"

#include <glad/glad.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "math/MathUtils.h"
#include "renderer/Renderer.h"

namespace Horo {
    std::vector<DebugDraw::LineVertex> DebugDraw::s_lines;
    std::unique_ptr<Shader> DebugDraw::s_shader;
    unsigned int DebugDraw::s_vao = 0;
    unsigned int DebugDraw::s_vbo = 0;
    bool DebugDraw::s_initialized = false;

    // Inline GLSL source for debug lines
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

        glGenVertexArrays(1, &s_vao);
        glBindVertexArray(s_vao);

        glGenBuffers(1, &s_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        // Allocate a large buffer — will be refilled each frame
        glBufferData(GL_ARRAY_BUFFER, sizeof(LineVertex) * 65536, nullptr,
                     GL_DYNAMIC_DRAW);

        // pos (location 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
            static_cast<const void *>(static_cast<const std::byte *>(nullptr) +
                                      offsetof(LineVertex, pos)));
        // color (location 1)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
            static_cast<const void *>(static_cast<const std::byte *>(nullptr) +
                                      offsetof(LineVertex, col)));

        glBindVertexArray(0);
        s_initialized = true;
    }

    void DebugDraw::Shutdown() {
        if (s_vbo)
            glDeleteBuffers(1, &s_vbo);
        if (s_vao)
            glDeleteVertexArrays(1, &s_vao);
        s_shader.reset();
        s_initialized = false;
    }

    void DebugDraw::Line(const Vec3 &from, const Vec3 &to, const Vec4 &color) {
        if (!Renderer::GetBackendCapabilities().supportsDebugDraw)
            return;
        s_lines.emplace_back(from, color);
        s_lines.emplace_back(to, color);
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

    void DebugDraw::Flush(const Camera &camera) {
        if (!Renderer::GetBackendCapabilities().supportsDebugDraw) {
            s_lines.clear();
            return;
        }

        if (!s_initialized || s_lines.empty())
            return;

        s_shader->Bind();
        s_shader->SetMat4("u_vp", camera.GetViewProjection());

        glBindVertexArray(s_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(s_lines.size() * sizeof(LineVertex)),
                        s_lines.data());

        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(s_lines.size()));

        glBindVertexArray(0);
        s_lines.clear();
    }
} // namespace Horo
