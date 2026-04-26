#pragma once
#include <array>

#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Vec4.h"

namespace Horo {
    // SkinnedVertex — same memory layout as Vertex with bone influence data
    // appended.
    //
    // boneIndices: up to 4 bone slot indices that affect this vertex; -1 = unused
    // slot. boneWeights: corresponding per-bone weights; the active weights must
    // sum to 1.0.
    //
    // Memory layout (matches the VAO attribute setup in SkinnedMesh):
    //   location 0 — position (Vec3, 12 bytes)
    //   location 1 — normal (Vec3, 12 bytes)
    //   location 2 — uv (Vec2, 8 bytes)
    //   location 3 — boneIndices(int[4],16 bytes)  — uploaded via
    //   glVertexAttribIPointer location 4 — boneWeights(Vec4,  16 bytes)
    struct SkinnedVertex {
        Vec3 position;
        Vec3 normal;
        Vec2 uv;
        std::array<int, 4> boneIndices{}; // bone slot indices; -1 = unused
        Vec4 boneWeights; // corresponding weights (active weights must sum to 1.0)
    };
} // namespace Horo
