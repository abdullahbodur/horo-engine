/** @file SkinnedMeshBin.h
 *  @brief Engine-native binary serialization for skinned meshes + skeletons.
 *
 *  The skinned-mesh-bin format is the runtime artifact produced by FBX skeletal
 *  asset import (HORO-107). It pairs vertex/index data with a compact bone
 *  hierarchy so the runtime can reconstruct @ref SkinnedMesh and @ref Skeleton
 *  in a single read.
 *
 *  Layout (all multi-byte integers are little-endian; matrices are stored
 *  column-major as 16 float32s):
 *  @code
 *  offset  size  field
 *  0       4     magic        'H','R','S','K' (HoroSkinnedBin)
 *  4       4     version      uint32_t, currently @ref kSkinnedMeshBinVersion
 *  8       4     vertexCount  uint32_t
 *  12      4     indexCount   uint32_t
 *  16      4     boneCount    uint32_t
 *  20      4     vertexStride uint32_t, must equal sizeof(SkinnedVertex)
 *  24      4     reserved0    uint32_t, write 0
 *  28      4     reserved1    uint32_t, write 0
 *  32      12    aabbMin      three float32 (x, y, z)
 *  44      12    aabbMax      three float32 (x, y, z)
 *  56      4     reserved2    uint32_t, write 0 (header padding to 64 bytes)
 *  60      4     reserved3    uint32_t, write 0
 *  64      V*64  vertices     V SkinnedVertex records
 *  ...     I*4   indices      I uint32_t indices
 *  ...     B*?   bones        per-bone records, see below
 *  @endcode
 *
 *  Per-bone record:
 *  @code
 *  4   parentIndex     int32_t (-1 for roots)
 *  64  inverseBind     16 float32 (column-major Mat4)
 *  4   nameLength      uint32_t
 *  N   name            ASCII bytes, no terminator
 *  @endcode
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "renderer/Skeleton.h"
#include "renderer/SkinnedVertex.h"

namespace Horo::SkinnedMeshBin {
    /** @brief Format magic bytes ('H','R','S','K'). */
    inline constexpr uint32_t kSkinnedMeshBinMagic =
        (static_cast<uint32_t>('H')) |
        (static_cast<uint32_t>('R') << 8) |
        (static_cast<uint32_t>('S') << 16) |
        (static_cast<uint32_t>('K') << 24);

    /** @brief Current on-disk schema version. */
    inline constexpr uint32_t kSkinnedMeshBinVersion = 1;

    /** @brief Outcome of a read operation. */
    struct ReadResult {
        bool ok = false;                       /**< True on success. */
        std::vector<SkinnedVertex> vertices;   /**< Decoded vertex array. */
        std::vector<uint32_t> indices;         /**< Decoded index array. */
        std::vector<Bone> bones;               /**< Decoded bone hierarchy (topologically sorted). */
        Vec3 aabbMin = {};                     /**< AABB minimum, as recorded by the writer. */
        Vec3 aabbMax = {};                     /**< AABB maximum, as recorded by the writer. */
        std::string error;                     /**< Human-readable diagnostic on failure; empty on success. */
    };

    /** @brief Outcome of a write operation. */
    struct WriteResult {
        bool ok = false;       /**< True on success. */
        std::string error;     /**< Human-readable diagnostic on failure; empty on success. */
    };

    /** @brief Writes a skinned mesh + skeleton to disk in the engine-native format.
     *  @param destPath  Absolute or project-relative destination file path.
     *  @param vertices  CPU vertex data; must not be empty.
     *  @param indices   CPU index data; must be a non-empty multiple of three.
     *  @param bones     Bone array; must not be empty. Parent indices must reference earlier entries (topologically sorted).
     *  @return WriteResult with @c ok = true on success or a populated @c error.
     */
    WriteResult WriteSkinnedMesh(const std::string &destPath,
                                  const std::vector<SkinnedVertex> &vertices,
                                  const std::vector<uint32_t> &indices,
                                  const std::vector<Bone> &bones);

    /** @brief Reads a skinned mesh + skeleton previously produced by @ref WriteSkinnedMesh.
     *  @param sourcePath Absolute or project-relative source file path.
     *  @return ReadResult populated with vertices/indices/bones/aabb on success.
     */
    ReadResult ReadSkinnedMesh(const std::string &sourcePath);
} // namespace Horo::SkinnedMeshBin
