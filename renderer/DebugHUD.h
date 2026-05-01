#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "math/Mat4.h"
#include "math/Vec3.h"
#include "renderer/ITexture.h"
#include "renderer/IVertexArray.h"
#include "renderer/IVertexBuffer.h"
#include "renderer/Shader.h"

namespace Horo {
    struct HUDStats {
        float fps;
        float frameTimeMs;
        float posX;
        float posY;
        float posZ;
        float velY;
        bool onGround;
        bool sceneCameraOn;
        bool showNoCameraOverlay;
        float speed;
        float camYaw;
        float camPitch;
        int collisionBlockCount;
        int entityCount;
        int drawCallCount;
    };

    class DebugHUD {
    public:
        static void Init(int screenW, int screenH);

        static void Shutdown();

        static void Update(float dt, const HUDStats &stats);

        static void Render();

        static void SetScreenSize(int w, int h);

        static bool IsVisible() { return s_visible; }
        static bool IsCollisionBoxesOn() { return s_showCollisionBoxes; }
#ifndef NDEBUG
        static bool IsLabelsOn() { return s_labelsVisible; }

        static void SubmitWorldLabel(const Vec3 &worldPos, const Mat4 &vp,
                                     const char *text);
#else
        static bool IsLabelsOn() { return false; }
        static void SubmitWorldLabel(const Vec3 &, const Mat4 &, const char *) {
        }
#endif

    private:
        struct GlyphVertex {
            float x;
            float y;
            float u;
            float v;
            float r;
            float g;
            float b;
            float a;
        };

        static std::shared_ptr<IVertexArray>  s_vao;
        static std::shared_ptr<IVertexBuffer> s_vbo;
        static std::shared_ptr<ITexture>      s_fontTex;
        static std::unique_ptr<Shader> s_shader;
        static bool s_initialized;
        static bool s_visible;
        static bool s_showCollisionBoxes;

#ifndef NDEBUG
        static bool s_labelsVisible;
        static bool s_settingsOpen;
        static bool s_occlusionCulling;

        struct WorldLabel {
            float x;
            float y;
            float ndcZ;
            std::string text;
        };

        static constexpr int MAX_LABELS = 512;
        static std::array<WorldLabel, MAX_LABELS> s_labels;
        static int s_labelCount;
        static std::vector<float> s_depthBuf;
#endif
        static int s_screenW;
        static int s_screenH;
        static float s_smoothFps;
        static float s_smoothFrameMs;
        static HUDStats s_stats;

        static constexpr int MAX_GLYPHS = 1024;
        static std::array<GlyphVertex, MAX_GLYPHS * 6> s_glyphBuf;
        static int s_glyphCount;

        static void BuildFontAtlas();

        static void DrawText(const char *text, float x, float y, float r, float g,
                             float b, float scale = 2.0f);

        static void FlushGlyphs();
#ifndef NDEBUG
        static void RenderWorldLabels();

        static void DrawLabelsWithDepthCull();
#endif
    };
} // namespace Horo
