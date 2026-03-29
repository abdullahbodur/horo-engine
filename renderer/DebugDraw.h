#pragma once
#include <vector>

#include "math/Vec3.h"
#include "math/Vec4.h"
#include "renderer/Camera.h"
#include "renderer/Shader.h"

namespace Monolith {

class DebugDraw {
 public:
  static void Init();
  static void Shutdown();

  // Queue a line for this frame
  static void Line(const Vec3& from, const Vec3& to, const Vec4& color = {1, 1, 0, 1});

  // Queue a wireframe sphere
  static void Sphere(const Vec3& center,
                     float radius,
                     const Vec4& color = {0, 1, 0, 1},
                     int segments = 16);

  // Queue a wireframe box
  static void Box(const Vec3& center, const Vec3& halfExtents, const Vec4& color = {1, 0.5f, 0, 1});

  // Flush all queued primitives to the GPU
  static void Flush(const Camera& camera);

 private:
  struct LineVertex {
    Vec3 pos;
    Vec4 col;
  };

  static std::vector<LineVertex> s_lines;
  static Shader* s_shader;
  static unsigned int s_vao, s_vbo;
  static bool s_initialized;
};

}  // namespace Monolith
