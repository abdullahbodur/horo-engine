/** @file MeshBin.h
 *  @brief Engine-native binary serialization for static @ref Mesh data.
 *
 *  The mesh-bin format is a small, versioned, host-endian binary container that
 *  asset importers (currently the FBX importer) use as the runtime-loadable
 *  artifact for static meshes living in @c assets/&lt;guid&gt;/. The file holds
 *  a fixed-size header followed by tightly packed @ref Vertex and index arrays.
 *
 *  Format layout (all multi-byte integers are little-endian):
 *  @code
 *  offset  size  field
 *  0       4     magic       'H','R','M','B' (HoroMeshBin)
 *  4       4     version     uint32_t, currently @ref kMeshBinVersion
 *  8       4     vertexCount uint32_t
 *  12      4     indexCount  uint32_t
 *  16      4     vertexStride uint32_t, must equal sizeof(Vertex)
 *  20      4     reserved0   uint32_t, write 0
 *  24      12    aabbMin     three float32 (x, y, z)
 *  36      12    aabbMax     three float32 (x, y, z)
 *  48      V*32  vertices    V Vertex records
 *  ...     I*4   indices     I uint32_t indices
 *  @endcode
 *
 *  Errors are reported via @ref MeshBinResult::error string with a short
 *  diagnostic-style prefix that mirrors the asset diagnostic taxonomy; the
 *  asset importer translates these into @ref AssetImportDiagnostic rows.
 *
 *  Targeted at runtime-side mesh loading (HORO-100). The reader returns CPU
 *  vertex/index arrays so callers can construct @ref Mesh themselves; this
 *  keeps the format free of GPU coupling and trivially testable.
 */
#pragma once

#include <string>
#include <vector>

#include "renderer/Mesh.h"

namespace Horo::MeshBin {
    /** @brief Format magic bytes ('H','R','M','B'). */
    inline constexpr uint32_t kMeshBinMagic =
        (static_cast<uint32_t>('H')) |
        (static_cast<uint32_t>('R') << 8) |
        (static_cast<uint32_t>('M') << 16) |
        (static_cast<uint32_t>('B') << 24);

    /** @brief Current on-disk schema version for static meshes.
     *
     *  Increment when the on-disk layout changes. Readers must reject mismatched
     *  versions rather than silently accept them.
     */
    inline constexpr uint32_t kMeshBinVersion = 1;

    /** @brief Outcome of a read operation. */
    struct ReadResult {
        bool ok = false;                  /**< True on success. */
        std::vector<Vertex> vertices;     /**< Decoded vertex array. */
        std::vector<uint32_t> indices;    /**< Decoded index array. */
        Vec3 aabbMin = {};                /**< AABB minimum, as recorded by the writer. */
        Vec3 aabbMax = {};                /**< AABB maximum, as recorded by the writer. */
        std::string error;                /**< Human-readable diagnostic on failure; empty on success. */
    };

    /** @brief Outcome of a write operation. */
    struct WriteResult {
        bool ok = false;       /**< True on success. */
        std::string error;     /**< Human-readable diagnostic on failure; empty on success. */
    };

    /** @brief Writes a static mesh to disk in the engine-native format.
     *  @param destPath  Absolute or project-relative destination file path.
     *  @param vertices  CPU vertex data; must not be empty.
     *  @param indices   CPU index data; must be a non-empty multiple of three.
     *  @return WriteResult with @c ok = true on success or a populated @c error.
     *
     *  Computes the AABB from @p vertices and embeds it in the header for cheap
     *  bounding-volume queries without re-parsing the body.
     */
    WriteResult WriteStaticMesh(const std::string &destPath,
                                const std::vector<Vertex> &vertices,
                                const std::vector<uint32_t> &indices);

    /** @brief Reads a static mesh previously produced by @ref WriteStaticMesh.
     *  @param sourcePath Absolute or project-relative source file path.
     *  @return ReadResult populated with vertices/indices/aabb on success.
     *
     *  Validates magic, version, declared sizes, and that vertex stride equals
     *  @c sizeof(Vertex). The reader does not allocate GPU resources; callers
     *  pass the decoded arrays to @c Mesh::SetData() if a renderable mesh is
     *  required.
     */
    ReadResult ReadStaticMesh(const std::string &sourcePath);
} // namespace Horo::MeshBin
