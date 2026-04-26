#pragma once

#include <memory>
#include <vector>

#include "math/Vec3.h"
#include "renderer/SkinnedVertex.h"

namespace Horo {
    // SkinnedMesh — GPU mesh variant that carries per-vertex bone influence data.
    //
    // VAO attribute layout:
    //   location 0 — position    (3 floats,  GL_FLOAT,  glVertexAttribPointer)
    //   location 1 — normal      (3 floats,  GL_FLOAT,  glVertexAttribPointer)
    //   location 2 — uv          (2 floats,  GL_FLOAT,  glVertexAttribPointer)
    //   location 3 — boneIndices (4 ints,    GL_INT,    glVertexAttribIPointer)
    //   location 4 — boneWeights (4 floats,  GL_FLOAT,  glVertexAttribPointer)
    //
    // All attributes share a single VBO with stride = sizeof(SkinnedVertex).
    // The corresponding GLSL skinning shader must declare:
    //   layout(location = 3) in ivec4 a_BoneIndices;
    //   layout(location = 4) in vec4  a_BoneWeights;
    //
    // Non-copyable (owns GPU resources).  Move-only, matching the Mesh convention.
    class SkinnedMesh {
    public:
        SkinnedMesh();

        ~SkinnedMesh();

        SkinnedMesh(const SkinnedMesh &) = delete;

        SkinnedMesh &operator=(const SkinnedMesh &) = delete;

        SkinnedMesh(SkinnedMesh &&o) noexcept;

        SkinnedMesh &operator=(SkinnedMesh &&o) noexcept;

        // Upload vertex and index data to the GPU.
        // Computes AABB extents from vertex positions.
        // Calling SetData() on an already-valid mesh first releases the old GPU
        // resources.
        void SetData(const std::vector<SkinnedVertex> &vertices,
                     const std::vector<unsigned int> &indices);

        // Bind the VAO and issue a single glDrawElements call.
        void Draw() const;

        bool IsValid() const;

        int GetIndexCount() const { return m_indexCount; }

        // Axis-aligned bounding box in local (bind-pose) space.
        Vec3 GetHalfExtents() const { return m_halfExtents; }
        Vec3 GetLocalAabbCenter() const { return m_localAabbCenter; }

    private:
        struct GpuStorage;
        std::unique_ptr<GpuStorage> m_gpu;
        int m_indexCount = 0;
        Vec3 m_halfExtents = {0.5f, 0.5f, 0.5f};
        Vec3 m_localAabbCenter = Vec3::Zero();

        // Create VAO/VBO/EBO and configure attribute pointers.
        void Upload(const std::vector<SkinnedVertex> &vertices,
                    const std::vector<unsigned int> &indices);

        // Delete GPU objects and zero all handles.
        void Release();
    };
} // namespace Horo
