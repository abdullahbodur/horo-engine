#pragma once
#include <memory>
#include <vector>

#include "math/Quaternion.h"
#include "math/Vec3.h"
#include "math/Vec4.h"
#include "renderer/Camera.h"
#include "renderer/IVertexArray.h"
#include "renderer/IVertexBuffer.h"
#include "renderer/Shader.h"

namespace Horo {
    class DebugDraw {
    public:
        static void Init();

        static void Shutdown();

        // Queue a line for this frame
        static void Line(const Vec3 &from, const Vec3 &to,
                         const Vec4 &color = {1, 1, 0, 1});

        // Queue a filled triangle for this frame (vertices in CCW order recommended).
        static void Triangle(const Vec3 &a, const Vec3 &b, const Vec3 &c,
                             const Vec4 &color = {1, 1, 0, 1});

        // Queue a wireframe sphere
        static void Sphere(const Vec3 &center, float radius,
                           const Vec4 &color = {0, 1, 0, 1}, int segments = 16);

        // Queue a wireframe box
        static void Box(const Vec3 &center, const Vec3 &halfExtents,
                        const Vec4 &color = {1, 0.5f, 0, 1});

        // Queue a wireframe box rotated by an arbitrary quaternion (all 12
        // edges drawn as lines). Used by the selection highlight to reflect
        // object rotation on non-mesh objects such as Panels.
        static void OrientedBox(const Vec3 &center, const Vec3 &halfExtents,
                                const Quaternion &rotation,
                                const Vec4 &color = {1, 0.5f, 0, 1});

        // Queue a filled box (12 triangles, double-sided via Triangle()).
        static void SolidBox(const Vec3 &center, const Vec3 &halfExtents,
                             const Vec4 &color = {1, 0.5f, 0, 1});

        // Flush all queued primitives to the GPU.
        // lineWidth is forwarded to glLineWidth (1.0 = default thin lines).
        static void Flush(const Camera &camera, float lineWidth = 1.0f);

    private:
        struct LineVertex {
            Vec3 pos;
            Vec4 col;
        };

        static std::vector<LineVertex> s_lines;
        static std::vector<LineVertex> s_tris;
        static std::unique_ptr<Shader> s_shader;
        static std::shared_ptr<IVertexArray>  s_vao;
        static std::shared_ptr<IVertexBuffer> s_vbo;
        static std::shared_ptr<IVertexArray>  s_triVao;
        static std::shared_ptr<IVertexBuffer> s_triVbo;
        static bool s_initialized;
    };
} // namespace Horo
