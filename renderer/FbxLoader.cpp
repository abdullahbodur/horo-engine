/** @file FbxLoader.cpp
 *  @brief Static-mesh FBX loader backed by the vendored ufbx parser.
 *
 *  Walks every node referencing an @c ufbx_mesh, triangulates each face via
 *  @c ufbx_triangulate_face, and copies position / normal / UV through the
 *  attribute indirection helpers. Geometry from all mesh nodes is concatenated
 *  into a single combined output; per-instance world transforms are intentionally
 *  ignored at this stage because the static-mesh asset is the bind-pose / local
 *  geometry source. Subsequent subtasks (HORO-107 skeletal, HORO-108 animation)
 *  introduce node-level handling.
 */
#include "renderer/FbxLoader.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "ufbx.h"

namespace Horo::FbxLoader {
    namespace {
        /** @brief Triangulation buffer growth heuristic — most FBX faces are tris/quads. */
        constexpr std::size_t kInitialTriBufferSize = 32;

        /** @brief Returns true when @p mesh has at least one face and a vertex_position channel. */
        bool MeshHasGeometry(const ufbx_mesh *mesh) {
            return mesh != nullptr && mesh->num_faces > 0 &&
                   mesh->vertex_position.exists;
        }

        /** @brief Reads the per-index normal, falling back to a generated one when missing. */
        Vec3 SampleNormal(const ufbx_mesh *mesh, std::uint32_t index) {
            if (mesh->vertex_normal.exists) {
                const ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, index);
                return {static_cast<float>(n.x), static_cast<float>(n.y),
                        static_cast<float>(n.z)};
            }
            return {0.0f, 0.0f, 1.0f};
        }

        /** @brief Reads the per-index UV, defaulting to (0, 0) when absent. */
        Vec2 SampleUv(const ufbx_mesh *mesh, std::uint32_t index) {
            if (mesh->vertex_uv.exists) {
                const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                return {static_cast<float>(uv.x), static_cast<float>(uv.y)};
            }
            return {0.0f, 0.0f};
        }

        /** @brief Appends one triangulated face into @p outVertices / @p outIndices. */
        void EmitFace(const ufbx_mesh *mesh, const ufbx_face &face,
                      std::vector<std::uint32_t> &triBuffer,
                      std::vector<Vertex> &outVertices,
                      std::vector<std::uint32_t> &outIndices,
                      std::uint32_t &outTriCount) {
            const std::uint32_t triCount = ufbx_triangulate_face(
                triBuffer.data(), triBuffer.size(), mesh, face);
            if (triCount == 0)
                return;

            for (std::uint32_t triIdx = 0; triIdx < triCount; ++triIdx) {
                for (std::uint32_t corner = 0; corner < 3; ++corner) {
                    const std::uint32_t fbxIndex = triBuffer[triIdx * 3 + corner];
                    Vertex vertex{};
                    const ufbx_vec3 pos =
                            ufbx_get_vertex_vec3(&mesh->vertex_position, fbxIndex);
                    vertex.position = {static_cast<float>(pos.x),
                                       static_cast<float>(pos.y),
                                       static_cast<float>(pos.z)};
                    vertex.normal = SampleNormal(mesh, fbxIndex);
                    vertex.uv = SampleUv(mesh, fbxIndex);

                    outIndices.push_back(static_cast<std::uint32_t>(outVertices.size()));
                    outVertices.push_back(vertex);
                }
                ++outTriCount;
            }
        }

        /** @brief Walks @p scene and concatenates triangulated geometry from every mesh node. */
        void ExtractAllMeshes(const ufbx_scene *scene, FbxLoadResult &result) {
            std::vector<std::uint32_t> triBuffer(kInitialTriBufferSize);
            for (std::size_t i = 0; i < scene->nodes.count; ++i) {
                const ufbx_node *node = scene->nodes.data[i];
                if (node == nullptr || node->mesh == nullptr)
                    continue;
                const ufbx_mesh *mesh = node->mesh;
                if (!MeshHasGeometry(mesh))
                    continue;

                ++result.meshNodeCount;

                // Resize the triangulation buffer once per mesh to its maximum
                // needs (3 indices per triangle, max_face_triangles per face).
                const std::size_t requiredSize =
                        static_cast<std::size_t>(mesh->max_face_triangles) * 3;
                if (triBuffer.size() < requiredSize)
                    triBuffer.resize(requiredSize);

                for (std::size_t faceIdx = 0; faceIdx < mesh->num_faces; ++faceIdx) {
                    EmitFace(mesh, mesh->faces.data[faceIdx], triBuffer,
                             result.vertices, result.indices, result.triangleCount);
                }
            }
        }
    } // namespace

    /** @copydoc Horo::FbxLoader::LoadStaticMesh */
    FbxLoadResult LoadStaticMesh(const std::string &sourcePath) {
        FbxLoadResult result;

        ufbx_load_opts opts{};
        opts.target_axes = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters = 1.0f;
        opts.generate_missing_normals = true;
        opts.evaluate_skinning = false;
        opts.load_external_files = false;

        ufbx_error error{};
        ufbx_scene *scene = ufbx_load_file(sourcePath.c_str(), &opts, &error);
        if (scene == nullptr) {
            result.errorCode = "fbx.parse_failed";
            result.error = std::string("ufbx parse failed: ") +
                           (error.description.length > 0
                                ? std::string(error.description.data, error.description.length)
                                : std::string("unknown error"));
            return result;
        }

        ExtractAllMeshes(scene, result);
        ufbx_free_scene(scene);

        if (result.vertices.empty() || result.indices.empty()) {
            result.errorCode = "fbx.no_geometry";
            result.error = "FBX parsed but contained no triangulable mesh data.";
            return result;
        }

        result.aabbMin = {std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::infinity()};
        result.aabbMax = {-std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()};
        for (const Vertex &vertex: result.vertices) {
            result.aabbMin.x = std::min(result.aabbMin.x, vertex.position.x);
            result.aabbMin.y = std::min(result.aabbMin.y, vertex.position.y);
            result.aabbMin.z = std::min(result.aabbMin.z, vertex.position.z);
            result.aabbMax.x = std::max(result.aabbMax.x, vertex.position.x);
            result.aabbMax.y = std::max(result.aabbMax.y, vertex.position.y);
            result.aabbMax.z = std::max(result.aabbMax.z, vertex.position.z);
        }

        result.ok = true;
        return result;
    }
} // namespace Horo::FbxLoader
