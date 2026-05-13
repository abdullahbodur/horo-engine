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
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
                if (const auto requiredSize =
                            static_cast<std::size_t>(mesh->max_face_triangles) * 3;
                    triBuffer.size() < requiredSize)
                    triBuffer.resize(requiredSize);

                for (std::size_t faceIdx = 0; faceIdx < mesh->num_faces; ++faceIdx) {
                    EmitFace(mesh, mesh->faces.data[faceIdx], triBuffer,
                             result.vertices, result.indices, result.triangleCount);
                }
            }
        }

        /** @brief Converts a ufbx string to a std::string, skipping null/empty inputs. */
        std::string MakeString(const ufbx_string &s) {
            return (s.data != nullptr && s.length > 0)
                       ? std::string(s.data, s.length)
                       : std::string{};
        }

        /** @brief Returns a basename suitable for the on-disk texture filename in managed storage.
         *
         *  Prefers (in order): the basename of @c filename, the basename of
         *  @c relative_filename, the basename of @c absolute_filename, and finally
         *  the element @c name with a generic @c .png extension. The result never
         *  contains a directory separator and is safe to use as a filename inside
         *  the per-asset managed directory.
         *
         *  Templated so the same logic applies to both @c ufbx_texture and
         *  @c ufbx_video, whose filename surfaces share the same shape.
         */
        template <typename HasFilenames>
        std::string MakeTextureBasename(const HasFilenames *src) {
            namespace fs = std::filesystem;
            const std::array<const ufbx_string *, 3> candidates = {
                &src->filename, &src->relative_filename, &src->absolute_filename};
            for (const ufbx_string *raw: candidates) {
                if (raw->data == nullptr || raw->length == 0)
                    continue;
                const fs::path candidate(MakeString(*raw));
                if (!candidate.empty()) {
                    const std::string base = candidate.filename().string();
                    if (!base.empty())
                        return base;
                }
            }
            std::string baseName = MakeString(src->name);
            if (baseName.empty())
                baseName = "texture";
            return baseName + ".png";
        }

        /** @brief True when @p prop names a diffuse / albedo channel across legacy FBX and PBR. */
        bool IsDiffuseChannelProp(std::string_view prop) {
            return prop == "DiffuseColor" || prop == "Diffuse" ||
                   prop == "diffuse_color" || prop == "BaseColor" ||
                   prop == "base_color";
        }

        /** @brief Filename / element-name fallback heuristic for diffuse identification. */
        bool NameHintsDiffuse(std::string_view name) {
            const auto contains = [&](std::string_view needle) {
                return name.find(needle) != std::string_view::npos;
            };
            return contains("diffuse") || contains("Diffuse") ||
                   contains("albedo") || contains("Albedo") ||
                   contains("base_color") || contains("BaseColor") ||
                   contains("basecolor");
        }

        /** @brief Walks @p scene and captures every texture image source.
         *
         *  Walks @c scene->videos because that is the canonical FBX image-source
         *  container and is populated by every exporter, including FBX 5800-era
         *  files that do not expose @c scene->textures at all. For each video we
         *  record the filename trio (for external resolution) and the embedded
         *  byte blob if present.
         *
         *  Diffuse classification uses @c material->textures bindings when
         *  available (every modern FBX exposes them) and falls back to a
         *  filename-substring heuristic. If no record is flagged as diffuse, the
         *  first record is promoted so single-texture assets always resolve an
         *  @c albedoMap.
         */
        void ExtractAllTextures(const ufbx_scene *scene, FbxLoadResult &result) {
            std::unordered_set<std::uint32_t> diffuseVideoIds;
            for (std::size_t mi = 0; mi < scene->materials.count; ++mi) {
                const ufbx_material *material = scene->materials.data[mi];
                if (material == nullptr)
                    continue;
                for (std::size_t ti = 0; ti < material->textures.count; ++ti) {
                    const ufbx_material_texture &mtex = material->textures.data[ti];
                    if (mtex.texture == nullptr || mtex.texture->video == nullptr)
                        continue;
                    if (!IsDiffuseChannelProp(MakeString(mtex.material_prop)))
                        continue;
                    diffuseVideoIds.insert(mtex.texture->video->element_id);
                }
            }

            for (std::size_t vi = 0; vi < scene->videos.count; ++vi) {
                const ufbx_video *video = scene->videos.data[vi];
                if (video == nullptr)
                    continue;
                FbxTextureRecord record;
                record.filename = MakeTextureBasename(video);
                record.absolutePath = MakeString(video->absolute_filename);
                record.relativePath = MakeString(video->relative_filename);
                if (video->content.data != nullptr && video->content.size > 0) {
                    const auto *src =
                            static_cast<const unsigned char *>(video->content.data);
                    record.embeddedBytes.assign(src, src + video->content.size);
                }
                if (diffuseVideoIds.contains(video->element_id))
                    record.isDiffuseAlbedo = true;
                else if (NameHintsDiffuse(MakeString(video->name)) ||
                         NameHintsDiffuse(MakeString(video->filename)))
                    record.isDiffuseAlbedo = true;
                result.textures.push_back(std::move(record));
            }

            if (!result.textures.empty()) {
                const bool anyDiffuse = std::ranges::any_of(
                    result.textures,
                    [](const FbxTextureRecord &r) { return r.isDiffuseAlbedo; });
                if (!anyDiffuse)
                    result.textures.front().isDiffuseAlbedo = true;
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
        ExtractAllTextures(scene, result);
        // Probe for skinning so the importer can branch into the skeletal path.
        for (std::size_t i = 0; i < scene->nodes.count && !result.hasSkinning; ++i) {
            const ufbx_node *node = scene->nodes.data[i];
            if (node == nullptr || node->mesh == nullptr)
                continue;
            if (node->mesh->skin_deformers.count > 0)
                result.hasSkinning = true;
        }
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

    namespace {
        /** @brief Converts an ufbx_matrix (3 columns of 3 floats + translation) into engine Mat4. */
        Mat4 UfbxMatrixToMat4(const ufbx_matrix &m) {
            Mat4 out = Mat4::Identity();
            out(0, 0) = static_cast<float>(m.cols[0].x);
            out(1, 0) = static_cast<float>(m.cols[0].y);
            out(2, 0) = static_cast<float>(m.cols[0].z);
            out(0, 1) = static_cast<float>(m.cols[1].x);
            out(1, 1) = static_cast<float>(m.cols[1].y);
            out(2, 1) = static_cast<float>(m.cols[1].z);
            out(0, 2) = static_cast<float>(m.cols[2].x);
            out(1, 2) = static_cast<float>(m.cols[2].y);
            out(2, 2) = static_cast<float>(m.cols[2].z);
            out(0, 3) = static_cast<float>(m.cols[3].x);
            out(1, 3) = static_cast<float>(m.cols[3].y);
            out(2, 3) = static_cast<float>(m.cols[3].z);
            return out;
        }

        /** @brief Single per-vertex influence used while normalising the top-4 weights. */
        struct VertexInfluence {
            int boneIndex = -1;
            float weight = 0.0f;
        };

        /** @brief Picks up to 4 highest-weighted influences for one vertex and renormalises their weights to sum 1.0.
         *
         *  When more than 4 clusters affect a vertex (rare for game-ready exports
         *  but legal in DCC tools), the smallest weights are dropped. When the
         *  surviving weights sum to zero (vertex untouched by skin), the importer
         *  defaults to bone 0 with full weight so the GPU shader does not see
         *  an undefined transform.
         */
        SkinnedVertex BuildSkinnedVertex(const Vertex &base,
                                          const std::vector<VertexInfluence> &raw) {
            SkinnedVertex sv;
            sv.position = base.position;
            sv.normal = base.normal;
            sv.uv = base.uv;
            sv.boneIndices = {-1, -1, -1, -1};
            sv.boneWeights = {0.0f, 0.0f, 0.0f, 0.0f};

            std::vector<VertexInfluence> sorted = raw;
            std::ranges::sort(sorted, [](const VertexInfluence &a,
                                          const VertexInfluence &b) {
                return a.weight > b.weight;
            });

            float sum = 0.0f;
            for (std::size_t slot = 0; slot < std::min<std::size_t>(4, sorted.size());
                 ++slot) {
                if (sorted[slot].weight <= 0.0f)
                    break;
                sv.boneIndices[slot] = sorted[slot].boneIndex;
                if (slot == 0) sv.boneWeights.x = sorted[slot].weight;
                else if (slot == 1) sv.boneWeights.y = sorted[slot].weight;
                else if (slot == 2) sv.boneWeights.z = sorted[slot].weight;
                else sv.boneWeights.w = sorted[slot].weight;
                sum += sorted[slot].weight;
            }
            if (sum < 1e-6f) {
                sv.boneIndices = {0, -1, -1, -1};
                sv.boneWeights = {1.0f, 0.0f, 0.0f, 0.0f};
            } else if (sum > 0.0f && std::abs(sum - 1.0f) > 1e-4f) {
                sv.boneWeights.x /= sum;
                sv.boneWeights.y /= sum;
                sv.boneWeights.z /= sum;
                sv.boneWeights.w /= sum;
            }
            return sv;
        }

        /** @brief Collects the bone hierarchy reachable from @p clusters in topological order.
         *  @param clusters First-deformer clusters from the source mesh.
         *  @param outBoneIndexByNodeId Output map ufbx_node element_id → engine bone index.
         *  @param outBones Topologically sorted Bone records.
         *  @return False if any cluster has no @c bone_node — reported as @c "fbx.skeleton_missing".
         */
        bool BuildBoneHierarchy(const ufbx_skin_cluster_list &clusters,
                                std::unordered_map<std::uint32_t, int> &outBoneIndexByNodeId,
                                std::vector<Bone> &outBones) {
            // Walk all bone_nodes and their parent chain so the resulting flat
            // array is in topological order.
            std::unordered_map<std::uint32_t, const ufbx_node *> boneNodeById;
            for (std::size_t ci = 0; ci < clusters.count; ++ci) {
                const ufbx_skin_cluster *cluster = clusters.data[ci];
                if (cluster == nullptr || cluster->bone_node == nullptr)
                    return false;
                boneNodeById.emplace(cluster->bone_node->element_id, cluster->bone_node);
            }
            // Walk parent chains to seed missing parents (the FBX may reference
            // bones whose parents do not appear as cluster bone nodes).
            std::vector<const ufbx_node *> work;
            work.reserve(boneNodeById.size());
            for (const auto &kv: boneNodeById)
                work.push_back(kv.second);
            for (const ufbx_node *node: work) {
                const ufbx_node *cur = node->parent;
                while (cur != nullptr && cur->bone != nullptr) {
                    if (boneNodeById.emplace(cur->element_id, cur).second) {
                        // newly inserted; continue up
                    }
                    cur = cur->parent;
                }
            }

            // Topological sort: emit roots first, then bones whose parent is already emitted.
            std::vector<const ufbx_node *> remaining;
            remaining.reserve(boneNodeById.size());
            for (const auto &kv: boneNodeById)
                remaining.push_back(kv.second);

            while (!remaining.empty()) {
                bool madeProgress = false;
                for (auto it = remaining.begin(); it != remaining.end();) {
                    const ufbx_node *node = *it;
                    const bool parentInSet =
                        node->parent != nullptr && node->parent->bone != nullptr &&
                        boneNodeById.count(node->parent->element_id) > 0;
                    const int parentIndex =
                        parentInSet
                            ? outBoneIndexByNodeId.count(node->parent->element_id) > 0
                                  ? outBoneIndexByNodeId.at(node->parent->element_id)
                                  : -2
                            : -1;
                    if (parentIndex == -2) {
                        ++it;
                        continue;
                    }
                    Bone bone;
                    bone.parentIndex = parentIndex;
                    bone.name = MakeString(node->name);
                    bone.inverseBindMatrix = Mat4::Identity();
                    outBoneIndexByNodeId[node->element_id] =
                        static_cast<int>(outBones.size());
                    outBones.push_back(std::move(bone));
                    it = remaining.erase(it);
                    madeProgress = true;
                }
                if (!madeProgress)
                    return false; // cycle or detached subtree
            }

            return true;
        }

        /** @brief Walks @p scene and extracts the first skinned mesh into @p result. */
        bool ExtractSkeletalMesh(const ufbx_scene *scene, FbxSkeletalLoadResult &result) {
            for (std::size_t i = 0; i < scene->nodes.count; ++i) {
                const ufbx_node *node = scene->nodes.data[i];
                if (node == nullptr || node->mesh == nullptr)
                    continue;
                const ufbx_mesh *mesh = node->mesh;
                if (mesh->skin_deformers.count == 0 || mesh->num_faces == 0 ||
                    !mesh->vertex_position.exists)
                    continue;

                const ufbx_skin_deformer *skin = mesh->skin_deformers.data[0];
                if (skin == nullptr || skin->clusters.count == 0)
                    continue;

                std::unordered_map<std::uint32_t, int> boneIndexByNodeId;
                if (!BuildBoneHierarchy(skin->clusters, boneIndexByNodeId, result.bones)) {
                    result.errorCode = "fbx.skeleton_missing";
                    result.error =
                        "FBX skin clusters reference bones that could not be linked to nodes.";
                    return false;
                }

                // Now fill inverseBindMatrix from each cluster's geometry_to_bone.
                for (std::size_t ci = 0; ci < skin->clusters.count; ++ci) {
                    const ufbx_skin_cluster *cluster = skin->clusters.data[ci];
                    if (cluster == nullptr || cluster->bone_node == nullptr)
                        continue;
                    const auto it = boneIndexByNodeId.find(cluster->bone_node->element_id);
                    if (it == boneIndexByNodeId.end())
                        continue;
                    result.bones[static_cast<std::size_t>(it->second)].inverseBindMatrix =
                        UfbxMatrixToMat4(cluster->geometry_to_bone);
                }

                // Triangulate and emit per-corner SkinnedVertex.
                std::vector<std::uint32_t> triBuffer(
                    static_cast<std::size_t>(mesh->max_face_triangles) * 3 + 16);
                for (std::size_t faceIdx = 0; faceIdx < mesh->num_faces; ++faceIdx) {
                    const ufbx_face &face = mesh->faces.data[faceIdx];
                    const std::uint32_t triCount = ufbx_triangulate_face(
                        triBuffer.data(), triBuffer.size(), mesh, face);
                    for (std::uint32_t tri = 0; tri < triCount; ++tri) {
                        for (std::uint32_t corner = 0; corner < 3; ++corner) {
                            const std::uint32_t fbxIndex = triBuffer[tri * 3 + corner];
                            Vertex base{};
                            const ufbx_vec3 pos =
                                ufbx_get_vertex_vec3(&mesh->vertex_position, fbxIndex);
                            base.position = {static_cast<float>(pos.x),
                                              static_cast<float>(pos.y),
                                              static_cast<float>(pos.z)};
                            base.normal = SampleNormal(mesh, fbxIndex);
                            base.uv = SampleUv(mesh, fbxIndex);

                            const std::uint32_t vertexIndex =
                                mesh->vertex_indices.data[fbxIndex];
                            std::vector<VertexInfluence> influences;
                            if (vertexIndex < skin->vertices.count) {
                                const ufbx_skin_vertex sv = skin->vertices.data[vertexIndex];
                                for (std::uint32_t w = 0; w < sv.num_weights; ++w) {
                                    const ufbx_skin_weight &sw =
                                        skin->weights.data[sv.weight_begin + w];
                                    if (sw.cluster_index >= skin->clusters.count)
                                        continue;
                                    const ufbx_skin_cluster *cluster =
                                        skin->clusters.data[sw.cluster_index];
                                    if (cluster == nullptr || cluster->bone_node == nullptr)
                                        continue;
                                    const auto it = boneIndexByNodeId.find(
                                        cluster->bone_node->element_id);
                                    if (it == boneIndexByNodeId.end())
                                        continue;
                                    influences.push_back(
                                        {it->second, static_cast<float>(sw.weight)});
                                }
                            }
                            const SkinnedVertex skinnedVertex =
                                BuildSkinnedVertex(base, influences);
                            result.indices.push_back(
                                static_cast<std::uint32_t>(result.vertices.size()));
                            result.vertices.push_back(skinnedVertex);
                        }
                    }
                }
                return true;
            }
            return false;
        }
    } // namespace

    /** @copydoc Horo::FbxLoader::LoadSkeletalMesh */
    FbxSkeletalLoadResult LoadSkeletalMesh(const std::string &sourcePath) {
        FbxSkeletalLoadResult result;

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

        const bool extracted = ExtractSkeletalMesh(scene, result);
        ufbx_free_scene(scene);

        if (!extracted) {
            if (result.errorCode.empty()) {
                result.errorCode = "fbx.no_geometry";
                result.error = "FBX parsed but contained no skinned mesh data.";
            }
            return result;
        }
        if (result.vertices.empty() || result.indices.empty()) {
            result.errorCode = "fbx.no_geometry";
            result.error = "FBX parsed but contained no skinned mesh data.";
            return result;
        }

        result.aabbMin = {std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::infinity()};
        result.aabbMax = {-std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()};
        for (const SkinnedVertex &v: result.vertices) {
            result.aabbMin.x = std::min(result.aabbMin.x, v.position.x);
            result.aabbMin.y = std::min(result.aabbMin.y, v.position.y);
            result.aabbMin.z = std::min(result.aabbMin.z, v.position.z);
            result.aabbMax.x = std::max(result.aabbMax.x, v.position.x);
            result.aabbMax.y = std::max(result.aabbMax.y, v.position.y);
            result.aabbMax.z = std::max(result.aabbMax.z, v.position.z);
        }

        result.ok = true;
        return result;
    }
} // namespace Horo::FbxLoader
