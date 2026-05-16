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
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ufbx.h"
#include "core/StringHash.h"
#include "renderer/BinaryMeshIoShared.h"

namespace Horo::FbxLoader {
    namespace {
        /** @brief Triangulation buffer growth heuristic — most FBX faces are tris/quads. */
        constexpr std::size_t kInitialTriBufferSize = 32;
        using SceneHandle = std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)>;

        /** @brief Builds shared ufbx loading options for static/skeletal/animation paths. */
        ufbx_load_opts MakeLoadOptions(bool generateMissingNormals) {
            ufbx_load_opts opts{};
            opts.target_axes = ufbx_axes_right_handed_y_up;
            opts.target_unit_meters = 1.0f;
            opts.generate_missing_normals = generateMissingNormals;
            opts.evaluate_skinning = false;
            opts.load_external_files = false;
            return opts;
        }

        /** @brief Formats a stable parse error from ufbx diagnostics. */
        std::string MakeParseError(const ufbx_error &error) {
            const std::string detail = error.description.length > 0
                                           ? std::string(error.description.data,
                                                         error.description.length)
                                           : std::string("unknown error");
            return "ufbx parse failed: " + detail;
        }

        /** @brief Loads an FBX scene and reports parse failures into result fields. */
        SceneHandle LoadSceneOrFail(const std::string &sourcePath, const ufbx_load_opts &opts,
                                    std::string *errorCodeOut,
                                    std::string *errorOut) {
            ufbx_error error{};
            ufbx_scene *scene = ufbx_load_file(sourcePath.c_str(), &opts, &error);
            if (scene == nullptr) {
                *errorCodeOut = "fbx.parse_failed";
                *errorOut = MakeParseError(error);
                return {nullptr, &ufbx_free_scene};
            }
            return {scene, &ufbx_free_scene};
        }

        template <typename VertexT>
        void ComputeResultAabb(const std::vector<VertexT> &vertices, Vec3 *outMin,
                               Vec3 *outMax) {
            BinaryMeshIoShared::ComputePositionAabb(vertices, outMin, outMax);
        }

        bool SceneHasSkinning(const ufbx_scene *scene) {
            for (std::size_t i = 0; i < scene->nodes.count; ++i) {
                const ufbx_node *node = scene->nodes.data[i];
                if (node != nullptr && node->mesh != nullptr &&
                    node->mesh->skin_deformers.count > 0)
                    return true;
            }
            return false;
        }

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

        std::string ToLowerAscii(std::string value) {
            std::ranges::transform(value, value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        /** @brief Maps ufbx material property names to engine material texture slots. */
        FbxTextureSlot SlotFromMaterialProp(std::string_view prop) {
            const std::string normalized = ToLowerAscii(std::string{prop});
            if (normalized == "diffusecolor" || normalized == "diffuse" ||
                normalized == "diffuse_color" || normalized == "basecolor" ||
                normalized == "base_color" || normalized == "pbr|basecolor")
                return FbxTextureSlot::Albedo;
            if (normalized == "normalmap" || normalized == "normal" ||
                normalized == "bump" || normalized == "bumpmap")
                return FbxTextureSlot::Normal;
            if (normalized == "metallicroughness" ||
                normalized == "metallicroughnessmap" ||
                normalized == "metalness" || normalized == "metallic" ||
                normalized == "roughness")
                return FbxTextureSlot::MetallicRoughness;
            if (normalized == "emissivecolor" || normalized == "emissive" ||
                normalized == "emissioncolor" || normalized == "emission")
                return FbxTextureSlot::Emissive;
            if (normalized == "ambientcolor" || normalized == "ambient" ||
                normalized == "ambientocclusion" || normalized == "occlusion")
                return FbxTextureSlot::Occlusion;
            return FbxTextureSlot::Unknown;
        }

        /** @brief Filename / element-name fallback heuristic for material-slot identification. */
        FbxTextureSlot SlotFromTextureName(std::string_view name) {
            const std::string normalized = ToLowerAscii(std::string{name});
            const auto contains = [&](std::string_view needle) {
                return normalized.find(needle) != std::string::npos;
            };
            if (contains("normal") || contains("_nrm") || contains("-nrm"))
                return FbxTextureSlot::Normal;
            if (contains("metallicroughness") || contains("metal_rough") ||
                contains("metalrough") || contains("orm") || contains("_mr") ||
                contains("-mr") || contains("metallic") || contains("roughness"))
                return FbxTextureSlot::MetallicRoughness;
            if (contains("emissive") || contains("emission"))
                return FbxTextureSlot::Emissive;
            if (contains("occlusion") || contains("ambientocclusion") ||
                contains("_ao") || contains("-ao"))
                return FbxTextureSlot::Occlusion;
            if (contains("diffuse") || contains("albedo") ||
                contains("base_color") || contains("basecolor"))
                return FbxTextureSlot::Albedo;
            return FbxTextureSlot::Unknown;
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
            std::unordered_map<std::uint32_t, FbxTextureSlot> videoSlots;
            for (std::size_t mi = 0; mi < scene->materials.count; ++mi) {
                const ufbx_material *material = scene->materials.data[mi];
                if (material == nullptr)
                    continue;
                for (std::size_t ti = 0; ti < material->textures.count; ++ti) {
                    const ufbx_material_texture &mtex = material->textures.data[ti];
                    if (mtex.texture == nullptr || mtex.texture->video == nullptr)
                        continue;
                    const FbxTextureSlot slot =
                            SlotFromMaterialProp(MakeString(mtex.material_prop));
                    if (slot == FbxTextureSlot::Unknown)
                        continue;
                    videoSlots.try_emplace(mtex.texture->video->element_id, slot);
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
                if (const auto slotIt = videoSlots.find(video->element_id);
                    slotIt != videoSlots.end())
                    record.slot = slotIt->second;
                if (record.slot == FbxTextureSlot::Unknown)
                    record.slot = SlotFromTextureName(MakeString(video->name));
                if (record.slot == FbxTextureSlot::Unknown)
                    record.slot = SlotFromTextureName(MakeString(video->filename));
                result.textures.push_back(std::move(record));
            }

            if (!result.textures.empty()) {
                const bool anyAlbedo = std::ranges::any_of(
                    result.textures,
                    [](const FbxTextureRecord &r) {
                        return r.slot == FbxTextureSlot::Albedo;
                    });
                if (!anyAlbedo)
                    result.textures.front().slot = FbxTextureSlot::Albedo;
            }
        }
    } // namespace

    /** @copydoc Horo::FbxLoader::LoadStaticMesh */
    FbxLoadResult LoadStaticMesh(const std::string &sourcePath) {
        FbxLoadResult result;

        const ufbx_load_opts opts = MakeLoadOptions(true);
        SceneHandle scene =
            LoadSceneOrFail(sourcePath, opts, &result.errorCode, &result.error);
        if (!scene)
            return result;

        ExtractAllMeshes(scene.get(), result);
        ExtractAllTextures(scene.get(), result);
        result.hasSkinning = SceneHasSkinning(scene.get());

        if (result.vertices.empty() || result.indices.empty()) {
            result.errorCode = "fbx.no_geometry";
            result.error = "FBX parsed but contained no triangulable mesh data.";
            return result;
        }

        ComputeResultAabb(result.vertices, &result.aabbMin, &result.aabbMax);

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

        /** @brief Seeds @p outBoneNodeById with every cluster bone_node and walks parent chains
         *         so detached parents are included.
         *  @return False if any cluster is null or has no bone_node.
         */
        bool CollectBoneNodes(const ufbx_skin_cluster_list &clusters,
                              std::unordered_map<std::uint32_t, const ufbx_node *> &outBoneNodeById) {
            for (std::size_t ci = 0; ci < clusters.count; ++ci) {
                const ufbx_skin_cluster *cluster = clusters.data[ci];
                if (cluster == nullptr || cluster->bone_node == nullptr)
                    return false;
                outBoneNodeById.emplace(cluster->bone_node->element_id, cluster->bone_node);
            }
            std::vector<const ufbx_node *> seeds;
            seeds.reserve(outBoneNodeById.size());
            for (const auto &[id, node]: outBoneNodeById)
                seeds.push_back(node);
            for (const ufbx_node *node: seeds) {
                for (const ufbx_node *cur = node->parent;
                     cur != nullptr && cur->bone != nullptr;
                     cur = cur->parent) {
                    outBoneNodeById.emplace(cur->element_id, cur);
                }
            }
            return true;
        }

        /** @brief Resolves @p node's parentIndex against the partial @p outBoneIndexByNodeId.
         *  @return -1 for orphan/no-parent, the parent's bone index when known,
         *          or -2 when the parent is in the bone set but not yet emitted.
         */
        int ResolveParentIndex(const ufbx_node *node,
                               const std::unordered_map<std::uint32_t, const ufbx_node *> &boneNodeById,
                               const std::unordered_map<std::uint32_t, int> &outBoneIndexByNodeId) {
            if (const bool parentInSet =
                    node->parent != nullptr && node->parent->bone != nullptr &&
                    boneNodeById.contains(node->parent->element_id);
                !parentInSet)
                return -1;
            const auto it = outBoneIndexByNodeId.find(node->parent->element_id);
            return it != outBoneIndexByNodeId.end() ? it->second : -2;
        }

        /** @brief Topologically emits bones from @p boneNodeById, populating @p outBoneIndexByNodeId
         *         and @p outBones in parent-before-child order.
         *  @return False on cycle or detached subtree.
         */
        bool EmitBonesTopological(
            const std::unordered_map<std::uint32_t, const ufbx_node *> &boneNodeById,
            std::unordered_map<std::uint32_t, int> &outBoneIndexByNodeId,
            std::vector<Bone> &outBones) {
            std::vector<const ufbx_node *> remaining;
            remaining.reserve(boneNodeById.size());
            for (const auto &[id, node]: boneNodeById)
                remaining.push_back(node);

            while (!remaining.empty()) {
                bool madeProgress = false;
                for (auto it = remaining.begin(); it != remaining.end();) {
                    const ufbx_node *node = *it;
                    const int parentIndex = ResolveParentIndex(node, boneNodeById, outBoneIndexByNodeId);
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
                    return false;
            }
            return true;
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
            std::unordered_map<std::uint32_t, const ufbx_node *> boneNodeById;
            if (!CollectBoneNodes(clusters, boneNodeById))
                return false;
            return EmitBonesTopological(boneNodeById, outBoneIndexByNodeId, outBones);
        }

        /** @brief Returns true when @p mesh is a renderable skinned mesh (has skin clusters,
         *         positions, and at least one face).
         */
        bool IsSkinnedRenderableMesh(const ufbx_mesh *mesh) {
            if (mesh == nullptr || mesh->skin_deformers.count == 0 ||
                mesh->num_faces == 0 || !mesh->vertex_position.exists)
                return false;
            const ufbx_skin_deformer *skin = mesh->skin_deformers.data[0];
            return skin != nullptr && skin->clusters.count > 0;
        }

        /** @brief Resolves each cluster's geometry_to_bone into the matching bone's inverseBindMatrix. */
        void ResolveBindMatrices(const ufbx_skin_deformer &skin,
                                  const std::unordered_map<std::uint32_t, int> &boneIndexByNodeId,
                                  std::vector<Bone> &bones) {
            for (std::size_t ci = 0; ci < skin.clusters.count; ++ci) {
                const ufbx_skin_cluster *cluster = skin.clusters.data[ci];
                if (cluster == nullptr || cluster->bone_node == nullptr)
                    continue;
                const auto it = boneIndexByNodeId.find(cluster->bone_node->element_id);
                if (it == boneIndexByNodeId.end())
                    continue;
                bones[static_cast<std::size_t>(it->second)].inverseBindMatrix =
                    UfbxMatrixToMat4(cluster->geometry_to_bone);
            }
        }

        /** @brief Returns the bone influences for skin vertex @p vertexIndex. */
        std::vector<VertexInfluence> GatherInfluences(
            const ufbx_skin_deformer &skin,
            const std::unordered_map<std::uint32_t, int> &boneIndexByNodeId,
            std::uint32_t vertexIndex) {
            std::vector<VertexInfluence> influences;
            if (vertexIndex >= skin.vertices.count)
                return influences;
            const ufbx_skin_vertex sv = skin.vertices.data[vertexIndex];
            for (std::uint32_t w = 0; w < sv.num_weights; ++w) {
                const ufbx_skin_weight &sw = skin.weights.data[sv.weight_begin + w];
                if (sw.cluster_index >= skin.clusters.count)
                    continue;
                const ufbx_skin_cluster *cluster = skin.clusters.data[sw.cluster_index];
                if (cluster == nullptr || cluster->bone_node == nullptr)
                    continue;
                const auto it = boneIndexByNodeId.find(cluster->bone_node->element_id);
                if (it == boneIndexByNodeId.end())
                    continue;
                influences.push_back({it->second, static_cast<float>(sw.weight)});
            }
            return influences;
        }

        /** @brief Builds a single skinned vertex from FBX corner @p fbxIndex. */
        SkinnedVertex MakeSkinnedCorner(const ufbx_mesh &mesh,
                                         const ufbx_skin_deformer &skin,
                                         const std::unordered_map<std::uint32_t, int> &boneIndexByNodeId,
                                         std::uint32_t fbxIndex) {
            Vertex base{};
            const ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh.vertex_position, fbxIndex);
            base.position = {static_cast<float>(pos.x),
                              static_cast<float>(pos.y),
                              static_cast<float>(pos.z)};
            base.normal = SampleNormal(&mesh, fbxIndex);
            base.uv = SampleUv(&mesh, fbxIndex);
            const std::uint32_t vertexIndex = mesh.vertex_indices.data[fbxIndex];
            const auto influences = GatherInfluences(skin, boneIndexByNodeId, vertexIndex);
            return BuildSkinnedVertex(base, influences);
        }

        /** @brief Triangulates @p mesh and pushes per-corner skinned vertices + indices into @p result. */
        void EmitSkinnedTriangles(const ufbx_mesh &mesh,
                                   const ufbx_skin_deformer &skin,
                                   const std::unordered_map<std::uint32_t, int> &boneIndexByNodeId,
                                   FbxSkeletalLoadResult &result) {
            std::vector<std::uint32_t> triBuffer(
                static_cast<std::size_t>(mesh.max_face_triangles) * 3 + 16);
            for (std::size_t faceIdx = 0; faceIdx < mesh.num_faces; ++faceIdx) {
                const ufbx_face &face = mesh.faces.data[faceIdx];
                const std::uint32_t triCount = ufbx_triangulate_face(
                    triBuffer.data(), triBuffer.size(), &mesh, face);
                const std::uint32_t cornerCount = triCount * 3;
                for (std::uint32_t cornerIdx = 0; cornerIdx < cornerCount; ++cornerIdx) {
                    const SkinnedVertex skinnedVertex =
                        MakeSkinnedCorner(mesh, skin, boneIndexByNodeId, triBuffer[cornerIdx]);
                    result.indices.push_back(
                        static_cast<std::uint32_t>(result.vertices.size()));
                    result.vertices.push_back(skinnedVertex);
                }
            }
        }

        /** @brief Walks @p scene and extracts the first skinned mesh into @p result. */
        bool ExtractSkeletalMesh(const ufbx_scene *scene, FbxSkeletalLoadResult &result) {
            for (std::size_t i = 0; i < scene->nodes.count; ++i) {
                const ufbx_node *node = scene->nodes.data[i];
                if (node == nullptr || !IsSkinnedRenderableMesh(node->mesh))
                    continue;
                const ufbx_mesh &mesh = *node->mesh;
                const ufbx_skin_deformer &skin = *mesh.skin_deformers.data[0];

                std::unordered_map<std::uint32_t, int> boneIndexByNodeId;
                if (!BuildBoneHierarchy(skin.clusters, boneIndexByNodeId, result.bones)) {
                    result.errorCode = "fbx.skeleton_missing";
                    result.error =
                        "FBX skin clusters reference bones that could not be linked to nodes.";
                    return false;
                }

                ResolveBindMatrices(skin, boneIndexByNodeId, result.bones);
                EmitSkinnedTriangles(mesh, skin, boneIndexByNodeId, result);
                return true;
            }
            return false;
        }
    } // namespace

    /** @brief Assigns skeletal @c no_geometry diagnostics when extraction yields nothing drawable. */
    static void SetSkinnedMeshMissingGeometryError(FbxSkeletalLoadResult &out) {
        out.errorCode = "fbx.no_geometry";
        out.error = "FBX parsed but contained no skinned mesh data.";
    }

    /** @copydoc Horo::FbxLoader::LoadSkeletalMesh */
    FbxSkeletalLoadResult LoadSkeletalMesh(const std::string &sourcePath) {
        FbxSkeletalLoadResult result;

        const ufbx_load_opts opts = MakeLoadOptions(true);
        SceneHandle scene =
            LoadSceneOrFail(sourcePath, opts, &result.errorCode, &result.error);
        if (!scene)
            return result;

        if (const bool extracted = ExtractSkeletalMesh(scene.get(), result);
            !extracted) {
            if (result.errorCode.empty())
                SetSkinnedMeshMissingGeometryError(result);
            return result;
        }
        if (result.vertices.empty() || result.indices.empty()) {
            SetSkinnedMeshMissingGeometryError(result);
            return result;
        }

        ComputeResultAabb(result.vertices, &result.aabbMin, &result.aabbMax);

        result.ok = true;
        return result;
    }
} // namespace Horo::FbxLoader

namespace Horo::FbxLoader {
    /** @copydoc Horo::FbxLoader::LoadAnimations */
    FbxAnimLoadResult LoadAnimations(const std::string &sourcePath,
                                      const std::vector<std::string> &boneNames) {
        FbxAnimLoadResult result;

        ufbx_load_opts opts{};
        opts.target_axes = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters = 1.0f;
        opts.evaluate_skinning = false;
        opts.load_external_files = false;

        ufbx_error error{};
        SceneHandle scene{ufbx_load_file(sourcePath.c_str(), &opts, &error),
                          &ufbx_free_scene};
        if (!scene) {
            result.errorCode = "fbx.parse_failed";
            result.error = MakeParseError(error);
            return result;
        }

        // Resolve bone-name -> ufbx_node*.
        std::unordered_map<std::string, const ufbx_node *, Horo::StringHash, std::equal_to<>> nodeByName;
        for (std::size_t i = 0; i < scene->nodes.count; ++i) {
            const ufbx_node *node = scene->nodes.data[i];
            if (node == nullptr || node->name.length == 0)
                continue;
            nodeByName.try_emplace(std::string(node->name.data, node->name.length), node);
        }

        constexpr float kSampleRateHz = 30.0f;
        constexpr float kFrameDt = 1.0f / kSampleRateHz;

        for (std::size_t si = 0; si < scene->anim_stacks.count; ++si) {
            const ufbx_anim_stack *stack = scene->anim_stacks.data[si];
            if (stack == nullptr || stack->anim == nullptr)
                continue;
            const auto t0 = static_cast<float>(stack->time_begin);
            const auto t1 = static_cast<float>(stack->time_end);
            if (t1 <= t0)
                continue;

            AnimationClip clip;
            clip.name =
                std::string(stack->name.data, static_cast<std::size_t>(stack->name.length));
            clip.duration = t1 - t0;

            // Determine number of samples (inclusive of both endpoints).
            const float duration = t1 - t0;
            const std::size_t sampleCount =
                std::max<std::size_t>(2, static_cast<std::size_t>(
                                              std::ceil(duration * kSampleRateHz)) + 1);

            for (std::size_t bi = 0; bi < boneNames.size(); ++bi) {
                const auto it = nodeByName.find(boneNames[bi]);
                if (it == nodeByName.end())
                    continue;
                const ufbx_node *node = it->second;

                BoneTrack track;
                track.boneIndex = static_cast<int>(bi);
                track.positionTimes.reserve(sampleCount);
                track.positions.reserve(sampleCount);
                track.rotationTimes.reserve(sampleCount);
                track.rotations.reserve(sampleCount);
                track.scaleTimes.reserve(sampleCount);
                track.scales.reserve(sampleCount);

                for (std::size_t s = 0; s < sampleCount; ++s) {
                    const float t = std::min(duration, static_cast<float>(s) * kFrameDt);
                    const ufbx_transform xform =
                        ufbx_evaluate_transform(stack->anim, node,
                                                static_cast<double>(t0 + t));
                    track.positionTimes.push_back(t);
                    track.positions.push_back({static_cast<float>(xform.translation.x),
                                                static_cast<float>(xform.translation.y),
                                                static_cast<float>(xform.translation.z)});
                    track.rotationTimes.push_back(t);
                    track.rotations.push_back(
                        Quaternion{static_cast<float>(xform.rotation.x),
                                    static_cast<float>(xform.rotation.y),
                                    static_cast<float>(xform.rotation.z),
                                    static_cast<float>(xform.rotation.w)});
                    track.scaleTimes.push_back(t);
                    track.scales.push_back({static_cast<float>(xform.scale.x),
                                              static_cast<float>(xform.scale.y),
                                              static_cast<float>(xform.scale.z)});
                }
                clip.AddTrack(std::move(track));
            }
            result.clips.push_back(std::move(clip));
        }

        result.ok = true;
        return result;
    }
} // namespace Horo::FbxLoader
