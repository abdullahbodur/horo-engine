#include "renderer/DebugHUD.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "input/Input.h"
#include "renderer/Renderer.h"
#include "input/KeyCodes.h"

// Windows headers (pulled in by glad) define DrawText as DrawTextA — undefine to avoid collision
#ifdef DrawText
#undef DrawText
#endif

namespace Monolith {

// ---- Static member definitions ----

unsigned int DebugHUD::s_vao = 0;
unsigned int DebugHUD::s_vbo = 0;
unsigned int DebugHUD::s_fontTex = 0;
std::unique_ptr<Shader> DebugHUD::s_shader;
bool DebugHUD::s_initialized = false;
bool DebugHUD::s_visible = false;
bool DebugHUD::s_showCollisionBoxes = false;
#ifndef NDEBUG
bool DebugHUD::s_labelsVisible = false;
bool DebugHUD::s_settingsOpen = false;
bool DebugHUD::s_occlusionCulling = true;
DebugHUD::WorldLabel DebugHUD::s_labels[DebugHUD::MAX_LABELS] = {};
int DebugHUD::s_labelCount = 0;
std::vector<float> DebugHUD::s_depthBuf;
#endif
int DebugHUD::s_screenW = 1280;
int DebugHUD::s_screenH = 720;
float DebugHUD::s_smoothFps = 0.0f;
float DebugHUD::s_smoothFrameMs = 0.0f;
HUDStats DebugHUD::s_stats = {};
DebugHUD::GlyphVertex DebugHUD::s_glyphBuf[DebugHUD::MAX_GLYPHS * 6] = {};
int DebugHUD::s_glyphCount = 0;

// ---- Public-domain 8x8 IBM BIOS font (ASCII 0-127) ----
// Each char = 8 bytes, one per row, bit 7 = leftmost pixel.
static const uint8_t FONT_DATA[128 * 8] = {
    // 0x00 (NUL)
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    // 0x01
    0x7E,
    0x81,
    0xA5,
    0x81,
    0xBD,
    0x99,
    0x81,
    0x7E,
    // 0x02
    0x7E,
    0xFF,
    0xDB,
    0xFF,
    0xC3,
    0xE7,
    0xFF,
    0x7E,
    // 0x03
    0x6C,
    0xFE,
    0xFE,
    0xFE,
    0x7C,
    0x38,
    0x10,
    0x00,
    // 0x04
    0x10,
    0x38,
    0x7C,
    0xFE,
    0x7C,
    0x38,
    0x10,
    0x00,
    // 0x05
    0x38,
    0x7C,
    0x38,
    0xFE,
    0xFE,
    0x10,
    0x10,
    0x7C,
    // 0x06
    0x10,
    0x10,
    0x38,
    0x7C,
    0xFE,
    0x7C,
    0x10,
    0x7C,
    // 0x07
    0x00,
    0x00,
    0x18,
    0x3C,
    0x3C,
    0x18,
    0x00,
    0x00,
    // 0x08
    0xFF,
    0xFF,
    0xE7,
    0xC3,
    0xC3,
    0xE7,
    0xFF,
    0xFF,
    // 0x09
    0x00,
    0x3C,
    0x66,
    0x42,
    0x42,
    0x66,
    0x3C,
    0x00,
    // 0x0A
    0xFF,
    0xC3,
    0x99,
    0xBD,
    0xBD,
    0x99,
    0xC3,
    0xFF,
    // 0x0B
    0x0F,
    0x07,
    0x0F,
    0x7D,
    0xCC,
    0xCC,
    0xCC,
    0x78,
    // 0x0C
    0x3C,
    0x66,
    0x66,
    0x66,
    0x3C,
    0x18,
    0x7E,
    0x18,
    // 0x0D
    0x3F,
    0x33,
    0x3F,
    0x30,
    0x30,
    0x70,
    0xF0,
    0xE0,
    // 0x0E
    0x7F,
    0x63,
    0x7F,
    0x63,
    0x63,
    0x67,
    0xE6,
    0xC0,
    // 0x0F
    0x99,
    0x5A,
    0x3C,
    0xE7,
    0xE7,
    0x3C,
    0x5A,
    0x99,
    // 0x10
    0x80,
    0xE0,
    0xF8,
    0xFE,
    0xF8,
    0xE0,
    0x80,
    0x00,
    // 0x11
    0x02,
    0x0E,
    0x3E,
    0xFE,
    0x3E,
    0x0E,
    0x02,
    0x00,
    // 0x12
    0x18,
    0x3C,
    0x7E,
    0x18,
    0x18,
    0x7E,
    0x3C,
    0x18,
    // 0x13
    0x66,
    0x66,
    0x66,
    0x66,
    0x66,
    0x00,
    0x66,
    0x00,
    // 0x14
    0x7F,
    0xDB,
    0xDB,
    0x7B,
    0x1B,
    0x1B,
    0x1B,
    0x00,
    // 0x15
    0x3E,
    0x63,
    0x38,
    0x6C,
    0x6C,
    0x38,
    0xCC,
    0x78,
    // 0x16
    0x00,
    0x00,
    0x00,
    0x00,
    0x7E,
    0x7E,
    0x7E,
    0x00,
    // 0x17
    0x18,
    0x3C,
    0x7E,
    0x18,
    0x7E,
    0x3C,
    0x18,
    0xFF,
    // 0x18
    0x18,
    0x3C,
    0x7E,
    0x18,
    0x18,
    0x18,
    0x18,
    0x00,
    // 0x19
    0x18,
    0x18,
    0x18,
    0x18,
    0x7E,
    0x3C,
    0x18,
    0x00,
    // 0x1A
    0x00,
    0x18,
    0x0C,
    0xFE,
    0x0C,
    0x18,
    0x00,
    0x00,
    // 0x1B
    0x00,
    0x30,
    0x60,
    0xFE,
    0x60,
    0x30,
    0x00,
    0x00,
    // 0x1C
    0x00,
    0x00,
    0xC0,
    0xC0,
    0xC0,
    0xFE,
    0x00,
    0x00,
    // 0x1D
    0x00,
    0x24,
    0x66,
    0xFF,
    0x66,
    0x24,
    0x00,
    0x00,
    // 0x1E
    0x00,
    0x18,
    0x3C,
    0x7E,
    0xFF,
    0xFF,
    0x00,
    0x00,
    // 0x1F
    0x00,
    0xFF,
    0xFF,
    0x7E,
    0x3C,
    0x18,
    0x00,
    0x00,
    // 0x20 (space)
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    // 0x21 (!)
    0x18,
    0x3C,
    0x3C,
    0x18,
    0x18,
    0x00,
    0x18,
    0x00,
    // 0x22 (")
    0x6C,
    0x6C,
    0x6C,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    // 0x23 (#)
    0x6C,
    0x6C,
    0xFE,
    0x6C,
    0xFE,
    0x6C,
    0x6C,
    0x00,
    // 0x24 ($)
    0x18,
    0x7E,
    0xC0,
    0x7C,
    0x06,
    0xFC,
    0x18,
    0x00,
    // 0x25 (%)
    0x00,
    0xC6,
    0xCC,
    0x18,
    0x30,
    0x66,
    0xC6,
    0x00,
    // 0x26 (&)
    0x38,
    0x6C,
    0x38,
    0x76,
    0xDC,
    0xCC,
    0x76,
    0x00,
    // 0x27 (')
    0x30,
    0x30,
    0x60,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    // 0x28 (()
    0x18,
    0x30,
    0x60,
    0x60,
    0x60,
    0x30,
    0x18,
    0x00,
    // 0x29 ())
    0x60,
    0x30,
    0x18,
    0x18,
    0x18,
    0x30,
    0x60,
    0x00,
    // 0x2A (*)
    0x00,
    0x66,
    0x3C,
    0xFF,
    0x3C,
    0x66,
    0x00,
    0x00,
    // 0x2B (+)
    0x00,
    0x18,
    0x18,
    0x7E,
    0x18,
    0x18,
    0x00,
    0x00,
    // 0x2C (,)
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x18,
    0x18,
    0x30,
    // 0x2D (-)
    0x00,
    0x00,
    0x00,
    0x7E,
    0x00,
    0x00,
    0x00,
    0x00,
    // 0x2E (.)
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x18,
    0x18,
    0x00,
    // 0x2F (/)
    0x06,
    0x0C,
    0x18,
    0x30,
    0x60,
    0xC0,
    0x80,
    0x00,
    // 0x30 (0)
    0x7C,
    0xC6,
    0xCE,
    0xDE,
    0xF6,
    0xE6,
    0x7C,
    0x00,
    // 0x31 (1)
    0x30,
    0x70,
    0x30,
    0x30,
    0x30,
    0x30,
    0xFC,
    0x00,
    // 0x32 (2)
    0x78,
    0xCC,
    0x0C,
    0x38,
    0x60,
    0xCC,
    0xFC,
    0x00,
    // 0x33 (3)
    0x78,
    0xCC,
    0x0C,
    0x38,
    0x0C,
    0xCC,
    0x78,
    0x00,
    // 0x34 (4)
    0x1C,
    0x3C,
    0x6C,
    0xCC,
    0xFE,
    0x0C,
    0x1E,
    0x00,
    // 0x35 (5)
    0xFC,
    0xC0,
    0xF8,
    0x0C,
    0x0C,
    0xCC,
    0x78,
    0x00,
    // 0x36 (6)
    0x38,
    0x60,
    0xC0,
    0xF8,
    0xCC,
    0xCC,
    0x78,
    0x00,
    // 0x37 (7)
    0xFC,
    0xCC,
    0x0C,
    0x18,
    0x30,
    0x30,
    0x30,
    0x00,
    // 0x38 (8)
    0x78,
    0xCC,
    0xCC,
    0x78,
    0xCC,
    0xCC,
    0x78,
    0x00,
    // 0x39 (9)
    0x78,
    0xCC,
    0xCC,
    0x7C,
    0x0C,
    0x18,
    0x70,
    0x00,
    // 0x3A (:)
    0x00,
    0x18,
    0x18,
    0x00,
    0x00,
    0x18,
    0x18,
    0x00,
    // 0x3B (;)
    0x00,
    0x18,
    0x18,
    0x00,
    0x00,
    0x18,
    0x18,
    0x30,
    // 0x3C (<)
    0x18,
    0x30,
    0x60,
    0xC0,
    0x60,
    0x30,
    0x18,
    0x00,
    // 0x3D (=)
    0x00,
    0x00,
    0xFC,
    0x00,
    0x00,
    0xFC,
    0x00,
    0x00,
    // 0x3E (>)
    0x60,
    0x30,
    0x18,
    0x0C,
    0x18,
    0x30,
    0x60,
    0x00,
    // 0x3F (?)
    0x78,
    0xCC,
    0x0C,
    0x18,
    0x30,
    0x00,
    0x30,
    0x00,
    // 0x40 (@)
    0x7C,
    0xC6,
    0xDE,
    0xDE,
    0xDE,
    0xC0,
    0x78,
    0x00,
    // 0x41 (A)
    0x30,
    0x78,
    0xCC,
    0xCC,
    0xFC,
    0xCC,
    0xCC,
    0x00,
    // 0x42 (B)
    0xFC,
    0x66,
    0x66,
    0x7C,
    0x66,
    0x66,
    0xFC,
    0x00,
    // 0x43 (C)
    0x3C,
    0x66,
    0xC0,
    0xC0,
    0xC0,
    0x66,
    0x3C,
    0x00,
    // 0x44 (D)
    0xF8,
    0x6C,
    0x66,
    0x66,
    0x66,
    0x6C,
    0xF8,
    0x00,
    // 0x45 (E)
    0xFE,
    0x62,
    0x68,
    0x78,
    0x68,
    0x62,
    0xFE,
    0x00,
    // 0x46 (F)
    0xFE,
    0x62,
    0x68,
    0x78,
    0x68,
    0x60,
    0xF0,
    0x00,
    // 0x47 (G)
    0x3C,
    0x66,
    0xC0,
    0xC0,
    0xCE,
    0x66,
    0x3E,
    0x00,
    // 0x48 (H)
    0xCC,
    0xCC,
    0xCC,
    0xFC,
    0xCC,
    0xCC,
    0xCC,
    0x00,
    // 0x49 (I)
    0x78,
    0x30,
    0x30,
    0x30,
    0x30,
    0x30,
    0x78,
    0x00,
    // 0x4A (J)
    0x1E,
    0x0C,
    0x0C,
    0x0C,
    0xCC,
    0xCC,
    0x78,
    0x00,
    // 0x4B (K)
    0xE6,
    0x66,
    0x6C,
    0x78,
    0x6C,
    0x66,
    0xE6,
    0x00,
    // 0x4C (L)
    0xF0,
    0x60,
    0x60,
    0x60,
    0x62,
    0x66,
    0xFE,
    0x00,
    // 0x4D (M)
    0xC6,
    0xEE,
    0xFE,
    0xFE,
    0xD6,
    0xC6,
    0xC6,
    0x00,
    // 0x4E (N)
    0xC6,
    0xE6,
    0xF6,
    0xDE,
    0xCE,
    0xC6,
    0xC6,
    0x00,
    // 0x4F (O)
    0x38,
    0x6C,
    0xC6,
    0xC6,
    0xC6,
    0x6C,
    0x38,
    0x00,
    // 0x50 (P)
    0xFC,
    0x66,
    0x66,
    0x7C,
    0x60,
    0x60,
    0xF0,
    0x00,
    // 0x51 (Q)
    0x78,
    0xCC,
    0xCC,
    0xCC,
    0xDC,
    0x78,
    0x1C,
    0x00,
    // 0x52 (R)
    0xFC,
    0x66,
    0x66,
    0x7C,
    0x6C,
    0x66,
    0xE6,
    0x00,
    // 0x53 (S)
    0x78,
    0xCC,
    0xE0,
    0x70,
    0x1C,
    0xCC,
    0x78,
    0x00,
    // 0x54 (T)
    0xFC,
    0xB4,
    0x30,
    0x30,
    0x30,
    0x30,
    0x78,
    0x00,
    // 0x55 (U)
    0xCC,
    0xCC,
    0xCC,
    0xCC,
    0xCC,
    0xCC,
    0xFC,
    0x00,
    // 0x56 (V)
    0xCC,
    0xCC,
    0xCC,
    0xCC,
    0xCC,
    0x78,
    0x30,
    0x00,
    // 0x57 (W)
    0xC6,
    0xC6,
    0xC6,
    0xD6,
    0xFE,
    0xEE,
    0xC6,
    0x00,
    // 0x58 (X)
    0xC6,
    0xC6,
    0x6C,
    0x38,
    0x38,
    0x6C,
    0xC6,
    0x00,
    // 0x59 (Y)
    0xCC,
    0xCC,
    0xCC,
    0x78,
    0x30,
    0x30,
    0x78,
    0x00,
    // 0x5A (Z)
    0xFE,
    0xC6,
    0x8C,
    0x18,
    0x32,
    0x66,
    0xFE,
    0x00,
    // 0x5B ([)
    0x78,
    0x60,
    0x60,
    0x60,
    0x60,
    0x60,
    0x78,
    0x00,
    // 0x5C (\)
    0xC0,
    0x60,
    0x30,
    0x18,
    0x0C,
    0x06,
    0x02,
    0x00,
    // 0x5D (])
    0x78,
    0x18,
    0x18,
    0x18,
    0x18,
    0x18,
    0x78,
    0x00,
    // 0x5E (^)
    0x10,
    0x38,
    0x6C,
    0xC6,
    0x00,
    0x00,
    0x00,
    0x00,
    // 0x5F (_)
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0xFF,
    // 0x60 (`)
    0x30,
    0x30,
    0x18,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    // 0x61 (a)
    0x00,
    0x00,
    0x78,
    0x0C,
    0x7C,
    0xCC,
    0x76,
    0x00,
    // 0x62 (b)
    0xE0,
    0x60,
    0x60,
    0x7C,
    0x66,
    0x66,
    0xDC,
    0x00,
    // 0x63 (c)
    0x00,
    0x00,
    0x78,
    0xCC,
    0xC0,
    0xCC,
    0x78,
    0x00,
    // 0x64 (d)
    0x1C,
    0x0C,
    0x0C,
    0x7C,
    0xCC,
    0xCC,
    0x76,
    0x00,
    // 0x65 (e)
    0x00,
    0x00,
    0x78,
    0xCC,
    0xFC,
    0xC0,
    0x78,
    0x00,
    // 0x66 (f)
    0x38,
    0x6C,
    0x60,
    0xF0,
    0x60,
    0x60,
    0xF0,
    0x00,
    // 0x67 (g)
    0x00,
    0x00,
    0x76,
    0xCC,
    0xCC,
    0x7C,
    0x0C,
    0xF8,
    // 0x68 (h)
    0xE0,
    0x60,
    0x6C,
    0x76,
    0x66,
    0x66,
    0xE6,
    0x00,
    // 0x69 (i)
    0x30,
    0x00,
    0x70,
    0x30,
    0x30,
    0x30,
    0x78,
    0x00,
    // 0x6A (j)
    0x0C,
    0x00,
    0x0C,
    0x0C,
    0x0C,
    0xCC,
    0xCC,
    0x78,
    // 0x6B (k)
    0xE0,
    0x60,
    0x66,
    0x6C,
    0x78,
    0x6C,
    0xE6,
    0x00,
    // 0x6C (l)
    0x70,
    0x30,
    0x30,
    0x30,
    0x30,
    0x30,
    0x78,
    0x00,
    // 0x6D (m)
    0x00,
    0x00,
    0xCC,
    0xFE,
    0xFE,
    0xD6,
    0xC6,
    0x00,
    // 0x6E (n)
    0x00,
    0x00,
    0xF8,
    0xCC,
    0xCC,
    0xCC,
    0xCC,
    0x00,
    // 0x6F (o)
    0x00,
    0x00,
    0x78,
    0xCC,
    0xCC,
    0xCC,
    0x78,
    0x00,
    // 0x70 (p)
    0x00,
    0x00,
    0xDC,
    0x66,
    0x66,
    0x7C,
    0x60,
    0xF0,
    // 0x71 (q)
    0x00,
    0x00,
    0x76,
    0xCC,
    0xCC,
    0x7C,
    0x0C,
    0x1E,
    // 0x72 (r)
    0x00,
    0x00,
    0xDC,
    0x76,
    0x66,
    0x60,
    0xF0,
    0x00,
    // 0x73 (s)
    0x00,
    0x00,
    0x7C,
    0xC0,
    0x78,
    0x0C,
    0xF8,
    0x00,
    // 0x74 (t)
    0x10,
    0x30,
    0x7C,
    0x30,
    0x30,
    0x34,
    0x18,
    0x00,
    // 0x75 (u)
    0x00,
    0x00,
    0xCC,
    0xCC,
    0xCC,
    0xCC,
    0x76,
    0x00,
    // 0x76 (v)
    0x00,
    0x00,
    0xCC,
    0xCC,
    0xCC,
    0x78,
    0x30,
    0x00,
    // 0x77 (w)
    0x00,
    0x00,
    0xC6,
    0xD6,
    0xFE,
    0xFE,
    0x6C,
    0x00,
    // 0x78 (x)
    0x00,
    0x00,
    0xC6,
    0x6C,
    0x38,
    0x6C,
    0xC6,
    0x00,
    // 0x79 (y)
    0x00,
    0x00,
    0xCC,
    0xCC,
    0xCC,
    0x7C,
    0x0C,
    0xF8,
    // 0x7A (z)
    0x00,
    0x00,
    0xFC,
    0x98,
    0x30,
    0x64,
    0xFC,
    0x00,
    // 0x7B ({)
    0x1C,
    0x30,
    0x30,
    0xE0,
    0x30,
    0x30,
    0x1C,
    0x00,
    // 0x7C (|)
    0x18,
    0x18,
    0x18,
    0x00,
    0x18,
    0x18,
    0x18,
    0x00,
    // 0x7D (})
    0xE0,
    0x30,
    0x30,
    0x1C,
    0x30,
    0x30,
    0xE0,
    0x00,
    // 0x7E (~)
    0x76,
    0xDC,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    // 0x7F (DEL)
    0x00,
    0x10,
    0x38,
    0x6C,
    0xC6,
    0xC6,
    0xFE,
    0x00,
};

// ---- Inline GLSL shaders ----

static const char* HUD_VERT = R"glsl(
#version 410 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform vec2 u_screenSize;
out vec2 v_uv;
out vec4 v_color;
void main() {
    vec2 ndc = (a_pos / u_screenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv   = a_uv;
    v_color = a_color;
}
)glsl";

static const char* HUD_FRAG = R"glsl(
#version 410 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_font;
out vec4 FragColor;
void main() {
    float alpha = texture(u_font, v_uv).r;
    FragColor = vec4(v_color.rgb, v_color.a * alpha);
}
)glsl";

// ---- Init / Shutdown ----

void DebugHUD::Init(int screenW, int screenH) {
  s_screenW = screenW;
  s_screenH = screenH;

  if (!Renderer::GetBackendCapabilities().supportsDebugHud) {
    s_initialized = false;
    s_visible = false;
    return;
  }

  s_shader = std::make_unique<Shader>(Shader::FromSource(HUD_VERT, HUD_FRAG));

  BuildFontAtlas();

  glGenVertexArrays(1, &s_vao);
  glBindVertexArray(s_vao);

  glGenBuffers(1, &s_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(GlyphVertex) * MAX_GLYPHS * 6, nullptr, GL_DYNAMIC_DRAW);

  // a_pos  (location 0): 2 floats
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex), reinterpret_cast<void*>(0));
  // a_uv   (location 1): 2 floats
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(
      1, 2, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex), reinterpret_cast<void*>(2 * sizeof(float)));
  // a_color (location 2): 4 floats
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(
      2, 4, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex), reinterpret_cast<void*>(4 * sizeof(float)));

  glBindVertexArray(0);

  s_initialized = true;
}

void DebugHUD::Shutdown() {
  if (s_fontTex) {
    glDeleteTextures(1, &s_fontTex);
    s_fontTex = 0;
  }
  if (s_vbo) {
    glDeleteBuffers(1, &s_vbo);
    s_vbo = 0;
  }
  if (s_vao) {
    glDeleteVertexArrays(1, &s_vao);
    s_vao = 0;
  }
  s_shader.reset();
  s_initialized = false;
}

void DebugHUD::SetScreenSize(int w, int h) {
  s_screenW = w;
  s_screenH = h;
}

// ---- Font atlas ----

void DebugHUD::BuildFontAtlas() {
  // 128 chars × 8 pixels wide = 1024 wide, 8 tall, GL_R8
  constexpr int ATLAS_W = 1024;
  constexpr int ATLAS_H = 8;

  uint8_t pixels[ATLAS_W * ATLAS_H] = {};

  for (int ch = 0; ch < 128; ++ch) {
    for (int row = 0; row < 8; ++row) {
      uint8_t bits = FONT_DATA[ch * 8 + row];
      for (int col = 0; col < 8; ++col) {
        bool set = (bits >> (7 - col)) & 1;
        pixels[row * ATLAS_W + ch * 8 + col] = set ? 0xFF : 0x00;
      }
    }
  }

  glGenTextures(1, &s_fontTex);
  glBindTexture(GL_TEXTURE_2D, s_fontTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, ATLAS_W, ATLAS_H, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
}

#ifndef NDEBUG
// ---- World label submission ----

void DebugHUD::SubmitWorldLabel(const Vec3& worldPos, const Mat4& vp, const char* text) {
  if (!s_labelsVisible || s_labelCount >= MAX_LABELS)
    return;

  Vec4 clip = vp * Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
  if (clip.w <= 0.0f)
    return;

  float nx = clip.x / clip.w;
  float ny = clip.y / clip.w;
  if (nx < -1.0f || nx > 1.0f || ny < -1.0f || ny > 1.0f)
    return;

  auto& lbl = s_labels[s_labelCount++];
  lbl.x = (nx * 0.5f + 0.5f) * static_cast<float>(s_screenW);
  lbl.y = (1.0f - (ny * 0.5f + 0.5f)) * static_cast<float>(s_screenH);
  lbl.ndcZ = clip.z / clip.w;
  snprintf(lbl.text, sizeof(lbl.text), "%s", text);
}
#endif

// ---- Update ----

void DebugHUD::Update(float dt, const HUDStats& stats) {
  if (!Renderer::GetBackendCapabilities().supportsDebugHud)
    return;

  if (Input::IsKeyPressed(Key::F3))
    s_visible = !s_visible;
  if (Input::IsKeyPressed(Key::F4))
    s_showCollisionBoxes = !s_showCollisionBoxes;
#ifndef NDEBUG
  if (Input::IsKeyPressed(Key::F5))
    s_settingsOpen = !s_settingsOpen;
  if (s_settingsOpen) {
    if (Input::IsKeyPressed(Key::L))
      s_labelsVisible = !s_labelsVisible;
    if (Input::IsKeyPressed(Key::O))
      s_occlusionCulling = !s_occlusionCulling;
  }
#endif

  if (dt > 0.0f) {
    float instFps = 1.0f / dt;
    float instMs = dt * 1000.0f;
    if (s_smoothFps < 1.0f) {
      // cold start
      s_smoothFps = instFps;
      s_smoothFrameMs = instMs;
    } else {
      s_smoothFps = 0.1f * instFps + 0.9f * s_smoothFps;
      s_smoothFrameMs = 0.1f * instMs + 0.9f * s_smoothFrameMs;
    }
  }

  s_stats = stats;
}

// ---- Render ----

void DebugHUD::Render() {
  if (!Renderer::GetBackendCapabilities().supportsDebugHud)
    return;
  if (!s_initialized)
    return;
  const bool forceNoCameraOverlay = s_stats.showNoCameraOverlay;
#ifndef NDEBUG
  if (!s_visible && !s_labelsVisible && !s_settingsOpen && !forceNoCameraOverlay)
    return;
#else
  if (!s_visible && !forceNoCameraOverlay)
    return;
#endif

  // Save GL state
  GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
  GLboolean blendWas = glIsEnabled(GL_BLEND);
  GLboolean cullWas = glIsEnabled(GL_CULL_FACE);

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);

  s_glyphCount = 0;

  if (s_visible) {
    // Layout constants
    constexpr float X0 = 8.0f;
    constexpr float Y0 = 8.0f;
    constexpr float LINE = 20.0f;
    constexpr float SCALE = 2.0f;

    float y = Y0;

    // --- PERFORMANCE ---
    DrawText("--- PERFORMANCE ---", X0, y, 1.0f, 0.9f, 0.1f, SCALE);
    y += LINE;
    {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "FPS  : %.1f", s_smoothFps);
      DrawText(buf, X0, y, 1.0f, 1.0f, 1.0f, SCALE);
      y += LINE;
      std::snprintf(buf, sizeof(buf), "FRAME: %.2f ms", s_smoothFrameMs);
      DrawText(buf, X0, y, 1.0f, 1.0f, 1.0f, SCALE);
      y += LINE;
    }
    y += 4.0f;

    // --- PLAYER ---
    DrawText("--- PLAYER ---", X0, y, 0.2f, 1.0f, 0.3f, SCALE);
    y += LINE;
    {
      char buf[64];
      std::snprintf(
          buf, sizeof(buf), "POS  : %.2f  %.2f  %.2f", s_stats.posX, s_stats.posY, s_stats.posZ);
      DrawText(buf, X0, y, 1.0f, 1.0f, 1.0f, SCALE);
      y += LINE;
      std::snprintf(buf, sizeof(buf), "VELY : %.2f", s_stats.velY);
      DrawText(buf, X0, y, 1.0f, 1.0f, 1.0f, SCALE);
      y += LINE;
      std::snprintf(buf, sizeof(buf), "GND  : %s", s_stats.onGround ? "YES" : "NO");
      DrawText(buf, X0, y, 1.0f, 1.0f, 1.0f, SCALE);
      y += LINE;
      std::snprintf(buf, sizeof(buf), "SPD  : %.2f", s_stats.speed);
      DrawText(buf, X0, y, 1.0f, 1.0f, 1.0f, SCALE);
      y += LINE;
    }
    y += 4.0f;

    // --- CAMERA ---
    DrawText("--- CAMERA ---", X0, y, 0.3f, 0.9f, 1.0f, SCALE);
    y += LINE;
    {
      char buf[64];
      if (!s_stats.sceneCameraOn) {
        DrawText("CAMERA OFF", X0, y, 1.0f, 0.35f, 0.35f, SCALE);
        y += LINE;
      } else {
        std::snprintf(buf, sizeof(buf), "YAW  : %.1f", s_stats.camYaw);
        DrawText(buf, X0, y, 1.0f, 1.0f, 1.0f, SCALE);
        y += LINE;
        std::snprintf(buf, sizeof(buf), "PITCH: %.1f", s_stats.camPitch);
        DrawText(buf, X0, y, 1.0f, 1.0f, 1.0f, SCALE);
        y += LINE;
      }
    }
    y += 4.0f;

    // --- WORLD ---
    DrawText("--- WORLD ---", X0, y, 1.0f, 0.6f, 0.1f, SCALE);
    y += LINE;
    {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "BLOCKS   : %d", s_stats.collisionBlockCount);
      DrawText(buf, X0, y, 1.0f, 1.0f, 1.0f, SCALE);
      y += LINE;
      std::snprintf(buf, sizeof(buf), "ENTITIES : %d", s_stats.entityCount);
      DrawText(buf, X0, y, 1.0f, 1.0f, 1.0f, SCALE);
      y += LINE;
      std::snprintf(buf, sizeof(buf), "DRAWCALLS: %d", s_stats.drawCallCount);
      DrawText(buf, X0, y, 1.0f, 1.0f, 1.0f, SCALE);
      y += LINE;
    }
    y += 4.0f;

    // --- Toggle hints ---
    {
      char buf[64];
#ifndef NDEBUG
      std::snprintf(
          buf, sizeof(buf), "[F4] BOXES: %s  [F5] SETTINGS", s_showCollisionBoxes ? "ON" : "OFF");
#else
      std::snprintf(buf, sizeof(buf), "[F4] BOXES: %s", s_showCollisionBoxes ? "ON" : "OFF");
#endif
      DrawText(buf, X0, y, 0.6f, 0.6f, 0.6f, SCALE);
    }
  }

  if (forceNoCameraOverlay) {
    constexpr float SCALE = 3.0f;
    const float x = static_cast<float>(s_screenW) * 0.5f - 108.0f;
    const float y = static_cast<float>(s_screenH) * 0.5f - 10.0f;
    DrawText("NO CAMERA", x, y, 1.0f, 0.25f, 0.25f, SCALE);
  }

#ifndef NDEBUG
  // --- Debug settings panel (top-right) ---
  if (s_settingsOpen) {
    constexpr float SCALE = 2.0f;
    constexpr float LINE = 20.0f;
    constexpr float PW = 230.0f;
    float px = static_cast<float>(s_screenW) - PW;
    float py = 8.0f;
    char buf[48];

    DrawText("--- DEBUG SETTINGS ---", px, py, 1.0f, 0.8f, 0.2f, SCALE);
    py += LINE;
    std::snprintf(buf, sizeof(buf), "[L] LABELS  : %s", s_labelsVisible ? "ON" : "OFF");
    DrawText(buf, px, py, 1.0f, 1.0f, 1.0f, SCALE);
    py += LINE;
    std::snprintf(buf, sizeof(buf), "[O] OCCLUDE : %s", s_occlusionCulling ? "ON" : "OFF");
    DrawText(buf, px, py, 1.0f, 1.0f, 1.0f, SCALE);
    py += LINE;
    DrawText("[F5] CLOSE", px, py, 0.5f, 0.5f, 0.5f, SCALE);
  }

  // --- World-space object labels (independent of s_visible / s_settingsOpen) ---
  if (s_labelsVisible && s_labelCount > 0) {
    if (s_occlusionCulling && Renderer::GetBackendCapabilities().supportsDepthReadback) {
      int total = s_screenW * s_screenH;
      s_depthBuf.resize(static_cast<std::vector<float>::size_type>(total));
      glReadPixels(0, 0, s_screenW, s_screenH, GL_DEPTH_COMPONENT, GL_FLOAT, s_depthBuf.data());

      for (int i = 0; i < s_labelCount; ++i) {
        int px = std::clamp(static_cast<int>(s_labels[i].x), 0, s_screenW - 1);
        int py = std::clamp(s_screenH - 1 - static_cast<int>(s_labels[i].y), 0, s_screenH - 1);
        float bufDepth =
            s_depthBuf[static_cast<std::vector<float>::size_type>(py * s_screenW + px)];
        float labelDepth = (s_labels[i].ndcZ + 1.0f) * 0.5f;
        if (labelDepth > bufDepth + 0.005f)
          continue;
        DrawText(s_labels[i].text, s_labels[i].x, s_labels[i].y, 0.2f, 1.0f, 0.4f, 1.5f);
      }
    } else {
      for (int i = 0; i < s_labelCount; ++i)
        DrawText(s_labels[i].text, s_labels[i].x, s_labels[i].y, 0.2f, 1.0f, 0.4f, 1.5f);
    }
  }
  s_labelCount = 0;
#endif

  FlushGlyphs();

  // Restore GL state
  if (depthWas)
    glEnable(GL_DEPTH_TEST);
  else
    glDisable(GL_DEPTH_TEST);
  if (blendWas)
    glEnable(GL_BLEND);
  else
    glDisable(GL_BLEND);
  if (cullWas)
    glEnable(GL_CULL_FACE);
  else
    glDisable(GL_CULL_FACE);
}

// ---- DrawText (CPU batching only) ----

void DebugHUD::DrawText(
    const char* text, float x, float y, float r, float g, float b, float scale) {
  constexpr float GLYPH_SRC_W = 8.0f;
  constexpr float GLYPH_SRC_H = 8.0f;
  constexpr float ATLAS_W = 1024.0f;
  constexpr float ATLAS_H = 8.0f;

  float cx = x;
  float gw = GLYPH_SRC_W * scale;
  float gh = GLYPH_SRC_H * scale;

  for (const char* p = text; *p; ++p) {
    if (s_glyphCount >= MAX_GLYPHS)
      break;

    unsigned char c = static_cast<unsigned char>(*p);
    if (c >= 128)
      c = '?';

    float u0 = (c * GLYPH_SRC_W) / ATLAS_W;
    float u1 = (c * GLYPH_SRC_W + GLYPH_SRC_W) / ATLAS_W;
    float v0 = 0.0f;
    float v1 = GLYPH_SRC_H / ATLAS_H;  // = 1.0

    float x0 = cx, y0 = y;
    float x1 = cx + gw, y1 = y + gh;

    // Two triangles (CCW)
    int base = s_glyphCount * 6;
    s_glyphBuf[base + 0] = {x0, y0, u0, v0, r, g, b, 1.0f};
    s_glyphBuf[base + 1] = {x1, y0, u1, v0, r, g, b, 1.0f};
    s_glyphBuf[base + 2] = {x1, y1, u1, v1, r, g, b, 1.0f};
    s_glyphBuf[base + 3] = {x0, y0, u0, v0, r, g, b, 1.0f};
    s_glyphBuf[base + 4] = {x1, y1, u1, v1, r, g, b, 1.0f};
    s_glyphBuf[base + 5] = {x0, y1, u0, v1, r, g, b, 1.0f};

    ++s_glyphCount;
    cx += gw;
  }
}

// ---- FlushGlyphs ----

void DebugHUD::FlushGlyphs() {
  if (s_glyphCount == 0)
    return;

  s_shader->Bind();
  s_shader->SetVec2("u_screenSize", static_cast<float>(s_screenW), static_cast<float>(s_screenH));

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, s_fontTex);
  s_shader->SetInt("u_font", 0);

  glBindVertexArray(s_vao);
  glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
  glBufferSubData(GL_ARRAY_BUFFER,
                  0,
                  static_cast<GLsizeiptr>(s_glyphCount * 6 * sizeof(GlyphVertex)),
                  s_glyphBuf);

  glDrawArrays(GL_TRIANGLES, 0, s_glyphCount * 6);

  glBindVertexArray(0);
  s_glyphCount = 0;
}

}  // namespace Monolith
