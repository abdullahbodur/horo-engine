#pragma once
#include <cstdint>
#include <vector>

#include "math/Mat4.h"
#include "math/Vec3.h"
#include "renderer/Shader.h"

namespace Monolith {

struct HUDStats {
  float fps, frameTimeMs;
  float posX, posY, posZ;
  float velY;
  bool onGround;
  float speed;
  float camYaw, camPitch;
  int collisionBlockCount;
  int entityCount;
  int drawCallCount;
};

class DebugHUD {
 public:
  static void Init(int screenW, int screenH);
  static void Shutdown();
  static void Update(float dt, const HUDStats& stats);
  static void Render();
  static void SetScreenSize(int w, int h);

  static bool IsVisible() { return s_visible; }
  static bool IsCollisionBoxesOn() { return s_showCollisionBoxes; }
#ifndef NDEBUG
  static bool IsLabelsOn() { return s_labelsVisible; }
  static void SubmitWorldLabel(const Vec3& worldPos, const Mat4& vp, const char* text);
#else
  static bool IsLabelsOn() { return false; }
  static void SubmitWorldLabel(const Vec3&, const Mat4&, const char*) {}
#endif

 private:
  struct GlyphVertex {
    float x, y, u, v, r, g, b, a;
  };

  static unsigned int s_vao, s_vbo, s_fontTex;
  static Shader* s_shader;
  static bool s_initialized, s_visible, s_showCollisionBoxes;

#ifndef NDEBUG
  static bool s_labelsVisible;
  static bool s_settingsOpen;
  static bool s_occlusionCulling;
  struct WorldLabel {
    float x, y, ndcZ;
    char text[32];
  };
  static constexpr int MAX_LABELS = 512;
  static WorldLabel s_labels[MAX_LABELS];
  static int s_labelCount;
  static std::vector<float> s_depthBuf;
#endif
  static int s_screenW, s_screenH;
  static float s_smoothFps, s_smoothFrameMs;
  static HUDStats s_stats;

  static constexpr int MAX_GLYPHS = 1024;
  static GlyphVertex s_glyphBuf[MAX_GLYPHS * 6];
  static int s_glyphCount;

  static void BuildFontAtlas();
  static void DrawText(
      const char* text, float x, float y, float r, float g, float b, float scale = 2.0f);
  static void FlushGlyphs();
};

}  // namespace Monolith
