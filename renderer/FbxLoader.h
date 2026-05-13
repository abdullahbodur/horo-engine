/** @file FbxLoader.h
 *  @brief Static-mesh extraction from FBX files using the vendored ufbx parser.
 *
 *  Public surface intentionally avoids any @c ufbx_* type so consumers do not
 *  pull the parser implementation through their headers. The loader produces
 *  the engine's @ref Vertex / index representation, which the FBX importer
 *  then writes out via @ref MeshBin::WriteStaticMesh into managed asset storage.
 *
 *  Scope (PRs HORO-94, HORO-95, HORO-96):
 *  - Parses static geometry from a single FBX scene.
 *  - Concatenates every triangulated face from every renderable mesh node into
 *    one combined output. Multi-mesh splitting is intentionally deferred until
 *    a downstream subtask requires it (see HORO-101 thumbnails / HORO-107
 *    skeletal pipeline for likely follow-ups).
 *  - Synthesises normals via ufbx's @c generate_missing_normals option when the
 *    FBX has no normal attribute. UVs default to (0, 0) when absent.
 *  - Collects diffuse-channel texture references from every material reachable
 *    from the rendered meshes, capturing both embedded byte blobs (HORO-96) and
 *    the candidate external paths to try (HORO-95). Bytes/strings are copied
 *    out before the underlying @c ufbx_scene is freed.
 *  - Emits zero diagnostics on the happy path; populates @ref FbxLoadResult::error
 *    with a short prefix that mirrors the asset diagnostic taxonomy
 *    (@c "fbx.parse_failed:" / @c "fbx.no_geometry:" / etc.) so the FBX importer
 *    can map them to typed diagnostic codes.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "renderer/Mesh.h"

namespace Horo::FbxLoader {
    /** @brief Single texture reference captured during FBX scene extraction.
     *
     *  When @c embeddedBytes is non-empty the texture is embedded inside the FBX
     *  and the importer should write the bytes to managed storage. Otherwise the
     *  importer should resolve one of the candidate paths in order and copy the
     *  resolved file. Exactly one of the following is true on success:
     *  - @c embeddedBytes.empty() == false  → embedded;
     *  - any of @c absolutePath / @c relativePath / @c filename is non-empty → external.
     */
    struct FbxTextureRecord {
        std::string filename;     /**< Basename derived from the FBX texture's filename or texture name; used for the on-disk file in managed storage. */
        std::string absolutePath; /**< Absolute path captured by ufbx (may be empty). */
        std::string relativePath; /**< Path relative to the FBX file, captured by ufbx (may be empty). */
        std::vector<unsigned char> embeddedBytes; /**< Embedded byte blob; empty when the texture is external. */
        bool isDiffuseAlbedo = false; /**< True when this record represents the diffuse / base-colour map. */
    };

    /** @brief Result of a single static-mesh load operation. */
    struct FbxLoadResult {
        bool ok = false;                    /**< True on success. */
        std::vector<Vertex> vertices;       /**< Combined vertex array. */
        std::vector<uint32_t> indices;      /**< Combined triangle index array. */
        std::uint32_t meshNodeCount = 0;    /**< Number of distinct mesh nodes that contributed geometry. */
        std::uint32_t triangleCount = 0;    /**< Total emitted triangles after concatenation. */
        Vec3 aabbMin = {};                  /**< Per-component minimum over @c vertices, populated on success. */
        Vec3 aabbMax = {};                  /**< Per-component maximum over @c vertices, populated on success. */
        std::vector<FbxTextureRecord> textures; /**< Diffuse texture references captured from materials reachable from the rendered meshes. */
        std::string error;                  /**< Human-readable diagnostic on failure; empty on success. */
        std::string errorCode;              /**< Short tag (e.g. @c "fbx.parse_failed") used by the importer to pick a diagnostic code. */
    };

    /** @brief Loads static geometry from an FBX file at @p sourcePath.
     *  @param sourcePath Absolute path to the FBX source file.
     *  @return @ref FbxLoadResult populated with combined vertex/index data on success.
     *
     *  Failure modes (mapped to @ref Horo::Editor::DiagnosticCodes):
     *  - @c errorCode == "fbx.parse_failed" — ufbx rejected the file (corrupt, unsupported variant).
     *  - @c errorCode == "fbx.no_geometry"  — file parsed cleanly but contained no triangulable mesh data.
     */
    FbxLoadResult LoadStaticMesh(const std::string &sourcePath);
} // namespace Horo::FbxLoader
