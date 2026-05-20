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
 *  - Collects material texture references from every material reachable
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

#include "renderer/AnimationClip.h"
#include "renderer/Mesh.h"
#include "renderer/Skeleton.h"
#include "renderer/SkinnedVertex.h"

namespace Horo::FbxLoader {
    /** @brief Material texture slot identified while scanning FBX material bindings. */
    enum class FbxTextureSlot {
        Unknown,
        Albedo,
        Normal,
        MetallicRoughness,
        Emissive,
        Occlusion
    };

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
        FbxTextureSlot slot = FbxTextureSlot::Unknown; /**< Material slot this texture feeds, if known. */
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
        bool hasSkinning = false;           /**< True when at least one walked mesh has a non-empty @c skin_deformers list — caller should follow up with @ref LoadSkeletalMesh. */
        std::string error;                  /**< Human-readable diagnostic on failure; empty on success. */
        std::string errorCode;              /**< Short tag (e.g. @c "fbx.parse_failed") used by the importer to pick a diagnostic code. */
    };

    /** @brief Result of a skeletal-mesh load operation. */
    struct FbxSkeletalLoadResult {
        bool ok = false;                  /**< True on success. */
        std::vector<SkinnedVertex> vertices; /**< Combined skinned vertex array. */
        std::vector<uint32_t> indices;    /**< Combined triangle index array. */
        std::vector<Bone> bones;          /**< Bone hierarchy in topological order (parent index < self for non-root bones). */
        Vec3 aabbMin = {};                /**< Per-component minimum over @c vertices. */
        Vec3 aabbMax = {};                /**< Per-component maximum over @c vertices. */
        std::string error;                /**< Human-readable diagnostic on failure; empty on success. */
        std::string errorCode;            /**< Short tag (e.g. @c "fbx.skeleton_missing") used by the importer to pick a diagnostic code. */
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

    /** @brief Loads skeletal mesh geometry + skeleton from an FBX file at @p sourcePath.
     *  @param sourcePath Absolute path to the FBX source file.
     *  @return @ref FbxSkeletalLoadResult populated with skinned vertex / index / bone data on success.
     *
     *  Walks every renderable mesh that has a non-empty @c skin_deformers list, takes the
     *  first deformer per mesh, and emits @ref SkinnedVertex records using up to the first
     *  4 cluster influences per vertex sorted by descending weight. Bones are topologically
     *  sorted so each parent index is strictly less than the bone's own index.
     *
     *  Failure modes:
     *  - @c errorCode == "fbx.parse_failed"     — ufbx rejected the file.
     *  - @c errorCode == "fbx.no_geometry"      — no triangulable skinned mesh data.
     *  - @c errorCode == "fbx.skeleton_missing" — file has skin clusters but no bone nodes.
     */
    FbxSkeletalLoadResult LoadSkeletalMesh(const std::string &sourcePath);

    /** @brief Result of an animation extraction operation. */
    struct FbxAnimLoadResult {
        bool ok = false;                  /**< True on success (a successful parse with zero clips is still ok). */
        std::vector<AnimationClip> clips; /**< Decoded animation clips, one per FBX anim_stack. */
        std::string error;                /**< Diagnostic on parse failure. */
        std::string errorCode;            /**< Short tag e.g. @c "fbx.parse_failed". */
    };

    /** @brief Loads animation clips from an FBX file using uniform 30 fps sampling.
     *  @param sourcePath Absolute path to the FBX source file.
     *  @param boneNames  Bone names that match the engine-side skeleton order;
     *                    each track's @c boneIndex matches the index of the bone
     *                    name in this vector.
     *
     *  Tracks are produced only for bones whose names match an FBX node. Each
     *  track is uniformly sampled at 30 fps via @c ufbx_evaluate_transform over
     *  the stack's @c [time_begin, time_end] range and split into separate
     *  position / rotation / scale arrays so the engine sampler can SLERP
     *  rotations and LERP positions / scales without re-deriving them.
     */
    FbxAnimLoadResult LoadAnimations(const std::string &sourcePath,
                                      const std::vector<std::string> &boneNames);
} // namespace Horo::FbxLoader
