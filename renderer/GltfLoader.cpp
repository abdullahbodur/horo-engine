#include "renderer/GltfLoader.h"

// Must come before tiny_gltf.h when TINYGLTF_NO_INCLUDE_JSON is set globally.
#include <nlohmann/json.hpp>

// stb_image declarations — implementation lives in Texture.cpp
// (STB_IMAGE_IMPLEMENTATION is defined there).  Including without the define
// gives us the prototypes needed by tinygltf's LoadImageDataCallback below.
#include <stb_image.h>

// tinygltf single-file implementation — compiled exactly once in this TU.
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

// Engine headers
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <queue>
#include <unordered_map>

#include "math/Mat4.h"
#include "math/Quaternion.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Vec4.h"
#include "renderer/SkinnedVertex.h"
#include "renderer/Texture.h"

namespace Horo {
    // Custom stb_image callback for tinygltf
    // (TINYGLTF_NO_STB_IMAGE suppresses the built-in one)
    static bool LoadImageDataCallback( // NOSONAR: cpp:S107 callback signature
        // required by tinygltf API
        tinygltf::Image *image, const int /*image_idx*/, std::string *err,
        std::string * /*warn*/, int /*req_width*/, int /*req_height*/,
        const unsigned char *bytes, int size, void * /*user_data*/) {
        int w = 0;
        int h = 0;
        int comp = 0;
        unsigned char *data = stbi_load_from_memory(bytes, size, &w, &h, &comp, 4);
        if (!data) {
            if (err)
                *err = "stbi_load_from_memory failed";
            return false;
        }
        image->width = w;
        image->height = h;
        image->component = 4;
        image->bits = 8;
        image->image.resize(static_cast<size_t>(w * h * 4));
        std::memcpy(image->image.data(), data, image->image.size());
        stbi_image_free(data);
        return true;
    }

    // Accessor helpers

    // Returns the byte stride for a given accessor, falling back to the tightly-
    // packed size when bufferView.byteStride == 0.
    static int GetComponentSize(int componentType) {
        switch (componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE:
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                return 1;
            case TINYGLTF_COMPONENT_TYPE_SHORT:
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                return 2;
            case TINYGLTF_COMPONENT_TYPE_INT:
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                return 4;
            case TINYGLTF_COMPONENT_TYPE_DOUBLE:
                return 8;
            default:
                return 0;
        }
    }

    // Returns the number of components in one element for the given accessor type.
    static int GetNumComponents(int type) {
        switch (type) {
            case TINYGLTF_TYPE_SCALAR:
                return 1;
            case TINYGLTF_TYPE_VEC2:
                return 2;
            case TINYGLTF_TYPE_VEC3:
                return 3;
            case TINYGLTF_TYPE_VEC4:
                return 4;
            case TINYGLTF_TYPE_MAT2:
                return 4;
            case TINYGLTF_TYPE_MAT3:
                return 9;
            case TINYGLTF_TYPE_MAT4:
                return 16;
            default:
                return 0;
        }
    }

    // Returns a pointer to the first byte of element 0 in the accessor's data.
    static const unsigned char *GetAccessorData(const tinygltf::Model &model,
                                                int accessorIdx) {
        const tinygltf::Accessor &acc = model.accessors[accessorIdx];
        const tinygltf::BufferView &bv = model.bufferViews[acc.bufferView];
        const tinygltf::Buffer &buf = model.buffers[bv.buffer];
        return buf.data.data() + bv.byteOffset + acc.byteOffset;
    }

    // Returns the element count (acc.count) for an accessor.
    static size_t GetAccessorCount(const tinygltf::Model &model, int accessorIdx) {
        return model.accessors[accessorIdx].count;
    }

    // Returns the effective byte stride for traversing elements in an accessor.
    // When bufferView.byteStride == 0 the data is tightly packed.
    static size_t GetAccessorStride(const tinygltf::Model &model, int accessorIdx) {
        const tinygltf::Accessor &acc = model.accessors[accessorIdx];
        if (const tinygltf::BufferView &bv = model.bufferViews[acc.bufferView];
            bv.byteStride != 0)
            return bv.byteStride;
        // Tightly packed: stride = elementSize
        return static_cast<size_t>(GetComponentSize(acc.componentType) *
                                   GetNumComponents(acc.type));
    }

    // Typed accessor read helpers

    // Read a float scalar at element `i` from an accessor known to be SCALAR FLOAT.
    static float ReadFloat(const unsigned char *base, size_t stride, size_t i) {
        float v;
        std::memcpy(&v, base + i * stride, sizeof(float));
        return v;
    }

    // Read a Vec3 at element `i` from a VEC3 FLOAT accessor.
    static Vec3 ReadVec3(const unsigned char *base, size_t stride, size_t i) {
        std::array<float, 3> buf{};
        std::memcpy(buf.data(), base + i * stride, 3 * sizeof(float));
        return {buf[0], buf[1], buf[2]};
    }

    // Read a Vec4 at element `i` from a VEC4 FLOAT accessor.
    static Vec4 ReadVec4(const unsigned char *base, size_t stride, size_t i) {
        std::array<float, 4> buf{};
        std::memcpy(buf.data(), base + i * stride, 4 * sizeof(float));
        return {buf[0], buf[1], buf[2], buf[3]};
    }

    // Read joint indices at element `i` — handles UNSIGNED_BYTE and UNSIGNED_SHORT.
    // Returns four bone indices written into `out`.
    static void ReadJointIndices(const unsigned char *base, size_t stride,
                                 int componentType, size_t i,
                                 std::array<int, 4> &out) {
        const unsigned char *p = base + i * stride;
        if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            out[0] = p[0];
            out[1] = p[1];
            out[2] = p[2];
            out[3] = p[3];
        } else {
            // UNSIGNED_SHORT
            std::array<uint16_t, 4> tmp{};
            std::memcpy(tmp.data(), p, 4 * sizeof(uint16_t));
            out[0] = tmp[0];
            out[1] = tmp[1];
            out[2] = tmp[2];
            out[3] = tmp[3];
        }
    }

    // Read a column-major Mat4 at element `i` from a MAT4 FLOAT accessor.
    static Mat4 ReadMat4(const unsigned char *base, size_t stride, size_t i) {
        std::array<float, 16> buf{};
        std::memcpy(buf.data(), base + i * stride, 16 * sizeof(float));
        Mat4 m;
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                m.m[col][row] = buf[static_cast<size_t>(col * 4 + row)];
        return m;
    }

    // Topological sort of skin joints
    // Returns a sorted list of indices into skin.joints such that for every joint
    // its parent appears at a lower position.  Also fills jointToSorted: a map
    // from original skin.joints position → position in the sorted output.
    static std::vector<int>
    TopologicallySortJoints(const tinygltf::Model &model,
                            const tinygltf::Skin &skin,
                            std::vector<int> &outJointToSorted) {
        const auto n = static_cast<int>(skin.joints.size());

        // Build a mapping: glTF node index → index in skin.joints
        std::unordered_map<int, int> nodeToJointIdx;
        nodeToJointIdx.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            nodeToJointIdx[skin.joints[i]] = i;

        // Build adjacency (parent index in skin.joints[] for each joint slot).
        std::vector<int> parentInSkin(static_cast<size_t>(n), -1);
        // in-degree for Kahn's algorithm
        std::vector<int> inDegree(static_cast<size_t>(n), 0);

        for (int ji = 0; ji < n; ++ji) {
            int nodeIdx = skin.joints[ji];
            const tinygltf::Node &node = model.nodes[nodeIdx];
            for (int childNodeIdx: node.children) {
                auto it = nodeToJointIdx.find(childNodeIdx);
                if (it == nodeToJointIdx.end())
                    continue;
                int childJi = it->second;
                parentInSkin[childJi] = ji;
                inDegree[childJi]++;
            }
        }

        // Kahn's BFS topological sort
        std::queue<int> queue;
        for (int i = 0; i < n; ++i) {
            if (inDegree[i] == 0)
                queue.push(i);
        }

        std::vector<int> sorted;
        sorted.reserve(static_cast<size_t>(n));
        while (!queue.empty()) {
            int ji = queue.front();
            queue.pop();
            sorted.push_back(ji);
            int nodeIdx = skin.joints[ji];
            const tinygltf::Node &node = model.nodes[nodeIdx];
            for (int childNodeIdx: node.children) {
                auto it = nodeToJointIdx.find(childNodeIdx);
                if (it == nodeToJointIdx.end())
                    continue;
                int childJi = it->second;
                if (--inDegree[childJi] == 0)
                    queue.push(childJi);
            }
        }

        // Fill reverse map: original skin.joints slot → position in sorted order
        outJointToSorted.resize(static_cast<size_t>(n));
        for (int sortedPos = 0; sortedPos < static_cast<int>(sorted.size());
             ++sortedPos)
            outJointToSorted[static_cast<size_t>(sorted[sortedPos])] = sortedPos;

        return sorted; // each entry is an index into skin.joints[]
    }

    // Parent-bone lookup — finds which already-sorted bone is the parent of nodeIdx
    static int FindParentBoneIndex(const tinygltf::Model &model,
                                   const tinygltf::Skin &skin,
                                   const std::vector<int> &sortedSlots, int nodeIdx,
                                   int numProcessed) {
        for (int pi = 0; pi < numProcessed; ++pi) {
            const int parentNodeIdx = skin.joints[sortedSlots[static_cast<size_t>(pi)]];
            const tinygltf::Node &parentNode = model.nodes[parentNodeIdx];
            if (std::ranges::find(parentNode.children, nodeIdx) !=
                parentNode.children.end())
                return pi;
        }
        return -1;
    }

    // Skeleton section of Load()
    static void LoadGltfSkeleton(const tinygltf::Model &model,
                                 GltfLoadResult &result,
                                 std::vector<int> &outSortedSlots,
                                 std::vector<int> &outJointToSorted) {
        if (model.skins.empty())
            return;

        const tinygltf::Skin &skin = model.skins[0];
        const auto numJoints = static_cast<int>(skin.joints.size());

        outSortedSlots = TopologicallySortJoints(model, skin, outJointToSorted);

        const unsigned char *ibmBase = nullptr;
        size_t ibmStride = 0;
        if (skin.inverseBindMatrices >= 0) {
            ibmBase = GetAccessorData(model, skin.inverseBindMatrices);
            ibmStride = GetAccessorStride(model, skin.inverseBindMatrices);
        }

        auto skeleton = std::make_shared<Skeleton>();
        for (int sortedPos = 0; sortedPos < numJoints; ++sortedPos) {
            const int originalSlot = outSortedSlots[static_cast<size_t>(sortedPos)];
            const int nodeIdx = skin.joints[originalSlot];
            const tinygltf::Node &node = model.nodes[nodeIdx];

            Bone bone;
            bone.name = node.name;
            bone.parentIndex =
                    FindParentBoneIndex(model, skin, outSortedSlots, nodeIdx, sortedPos);

            bone.inverseBindMatrix =
                    ibmBase
                        ? ReadMat4(ibmBase, ibmStride, static_cast<size_t>(originalSlot))
                        : Mat4::Identity();

            skeleton->AddBone(bone);
        }

        result.skeleton = skeleton;
    }

    // Mesh section helpers
    struct VertexAccessors {
        const unsigned char *posData = nullptr;
        size_t posStride = 0;
        const unsigned char *normData = nullptr;
        size_t normStride = 0;
        const unsigned char *uvData = nullptr;
        size_t uvStride = 0;
        const unsigned char *jointsData = nullptr;
        size_t jointsStride = 0;
        int jointsComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
        const unsigned char *weightsData = nullptr;
        size_t weightsStride = 0;
    };

    static VertexAccessors BuildVertexAccessors(const tinygltf::Model &model,
                                                int posIdx, int normIdx, int uvIdx,
                                                int jointsIdx, int weightsIdx) {
        VertexAccessors acc{};
        acc.posData = GetAccessorData(model, posIdx);
        acc.posStride = GetAccessorStride(model, posIdx);
        if (normIdx >= 0) {
            acc.normData = GetAccessorData(model, normIdx);
            acc.normStride = GetAccessorStride(model, normIdx);
        }
        if (uvIdx >= 0) {
            acc.uvData = GetAccessorData(model, uvIdx);
            acc.uvStride = GetAccessorStride(model, uvIdx);
        }
        if (jointsIdx >= 0) {
            acc.jointsData = GetAccessorData(model, jointsIdx);
            acc.jointsStride = GetAccessorStride(model, jointsIdx);
            acc.jointsComponentType = model.accessors[jointsIdx].componentType;
        }
        if (weightsIdx >= 0) {
            acc.weightsData = GetAccessorData(model, weightsIdx);
            acc.weightsStride = GetAccessorStride(model, weightsIdx);
        }
        return acc;
    }

    static SkinnedVertex BuildSkinnedVertex(size_t vi, const VertexAccessors &acc,
                                            const std::vector<int> &jointToSorted) {
        SkinnedVertex sv{};
        sv.position = ReadVec3(acc.posData, acc.posStride, vi);
        if (acc.normData)
            sv.normal = ReadVec3(acc.normData, acc.normStride, vi);
        if (acc.uvData) {
            std::array<float, 2> buf{};
            std::memcpy(buf.data(), acc.uvData + vi * acc.uvStride, 2 * sizeof(float));
            sv.uv = {buf[0], buf[1]};
        }
        sv.boneIndices[0] = sv.boneIndices[1] = sv.boneIndices[2] =
                                                sv.boneIndices[3] = -1;
        sv.boneWeights = Vec4{0.0f, 0.0f, 0.0f, 0.0f};
        if (acc.jointsData && acc.weightsData) {
            std::array<int, 4> rawJoints{};
            ReadJointIndices(acc.jointsData, acc.jointsStride, acc.jointsComponentType,
                             vi, rawJoints);
            const Vec4 w = ReadVec4(acc.weightsData, acc.weightsStride, vi);
            const std::array<float, 4> weights = {w.x, w.y, w.z, w.w};
            for (int slot = 0; slot < 4; ++slot) {
                if (weights[static_cast<size_t>(slot)] <= 0.0f)
                    continue;
                const int rawJoint = rawJoints[static_cast<size_t>(slot)];
                int boneIdx = -1;
                if (!jointToSorted.empty() && rawJoint >= 0 &&
                    rawJoint < static_cast<int>(jointToSorted.size()))
                    boneIdx = jointToSorted[static_cast<size_t>(rawJoint)];
                sv.boneIndices[slot] = boneIdx;
            }
            sv.boneWeights = w;
        }
        return sv;
    }

    static void GenerateFlatNormals(std::vector<SkinnedVertex> &vertices,
                                    const std::vector<unsigned int> &indices,
                                    size_t idxStart) {
        for (size_t ti = idxStart; ti + 2 < indices.size(); ti += 3) {
            SkinnedVertex &v0 = vertices[indices[ti]];
            SkinnedVertex &v1 = vertices[indices[ti + 1]];
            SkinnedVertex &v2 = vertices[indices[ti + 2]];
            const Vec3 e1 = v1.position - v0.position;
            const Vec3 e2 = v2.position - v0.position;
            v0.normal = v1.normal = v2.normal = Vec3::Cross(e1, e2).Normalized();
        }
    }

    static void AppendPrimitiveIndices(const tinygltf::Model &model,
                                       const tinygltf::Primitive &prim,
                                       unsigned int baseVertex, size_t vertCount,
                                       std::vector<unsigned int> &indices) {
        if (prim.indices >= 0) {
            const tinygltf::Accessor &idxAcc = model.accessors[prim.indices];
            const unsigned char *idxPtr = GetAccessorData(model, prim.indices);
            const size_t idxStride = GetAccessorStride(model, prim.indices);
            const size_t idxCount = idxAcc.count;
            indices.reserve(indices.size() + idxCount);
            for (size_t ii = 0; ii < idxCount; ++ii) {
                unsigned int idx = 0;
                switch (idxAcc.componentType) {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                        uint8_t v;
                        std::memcpy(&v, idxPtr + ii * idxStride, 1);
                        idx = v;
                        break;
                    }
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                        uint16_t v;
                        std::memcpy(&v, idxPtr + ii * idxStride, 2);
                        idx = v;
                        break;
                    }
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                        std::memcpy(&idx, idxPtr + ii * idxStride, 4);
                        break;
                    default:
                        break;
                }
                indices.push_back(baseVertex + idx);
            }
        } else {
            const auto count = static_cast<unsigned int>(vertCount);
            indices.reserve(indices.size() + count);
            for (unsigned int ii = 0; ii < count; ++ii)
                indices.push_back(baseVertex + ii);
        }
    }

    // Mesh section of Load()
    static void LoadGltfMesh(const tinygltf::Model &model,
                             const std::vector<int> &jointToSorted,
                             GltfLoadResult &result) {
        if (model.meshes.empty())
            return;

        std::vector<SkinnedVertex> vertices;
        std::vector<unsigned int> indices;

        const tinygltf::Mesh &gltfMesh = model.meshes[0];
        for (const tinygltf::Primitive &prim: gltfMesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1) {
                std::cerr << "[GltfLoader] Skipping non-triangle primitive\n";
                continue;
            }

            auto findAttr = [&](const std::string &name) {
                auto it = prim.attributes.find(name);
                return (it != prim.attributes.end()) ? it->second : -1;
            };

            const int posIdx = findAttr("POSITION");
            const int normIdx = findAttr("NORMAL");
            const int uvIdx = findAttr("TEXCOORD_0");
            const int jointsIdx = findAttr("JOINTS_0");
            const int weightsIdx = findAttr("WEIGHTS_0");

            if (posIdx < 0) {
                std::cerr << "[GltfLoader] Primitive missing POSITION, skipping\n";
                continue;
            }

            const size_t vertCount = GetAccessorCount(model, posIdx);
            const VertexAccessors acc = BuildVertexAccessors(
                model, posIdx, normIdx, uvIdx, jointsIdx, weightsIdx);

            const auto baseVertex = static_cast<unsigned int>(vertices.size());
            vertices.reserve(vertices.size() + vertCount);

            for (size_t vi = 0; vi < vertCount; ++vi)
                vertices.push_back(BuildSkinnedVertex(vi, acc, jointToSorted));

            const size_t idxBefore = indices.size();
            AppendPrimitiveIndices(model, prim, baseVertex, vertCount, indices);

            if (!acc.normData)
                GenerateFlatNormals(vertices, indices, idxBefore);
        }

        if (!vertices.empty()) {
            auto mesh = std::make_shared<SkinnedMesh>();
            mesh->SetData(vertices, indices);
            result.mesh = mesh;
        }
    }

    // Per-channel animation processing
    static void ProcessAnimationChannel(
        const tinygltf::Model &model, const tinygltf::AnimationChannel &channel,
        const tinygltf::AnimationSampler &sampler, int boneIdx, float &clipDuration,
        std::unordered_map<int, BoneTrack> &trackMap) {
        const unsigned char *timeBase = GetAccessorData(model, sampler.input);
        const size_t timeStride = GetAccessorStride(model, sampler.input);
        const size_t keyCount = GetAccessorCount(model, sampler.input);

        if (keyCount > 0) {
            const float lastTime = ReadFloat(timeBase, timeStride, keyCount - 1);
            if (lastTime > clipDuration)
                clipDuration = lastTime;
        }

        const unsigned char *valBase = GetAccessorData(model, sampler.output);
        const size_t valStride = GetAccessorStride(model, sampler.output);

        auto &track =
                trackMap.try_emplace(boneIdx, BoneTrack{boneIdx, {}, {}, {}, {}, {}, {}})
                .first->second;

        if (channel.target_path == "translation") {
            track.positionTimes.reserve(keyCount);
            track.positions.reserve(keyCount);
            for (size_t ki = 0; ki < keyCount; ++ki) {
                track.positionTimes.push_back(ReadFloat(timeBase, timeStride, ki));
                track.positions.push_back(ReadVec3(valBase, valStride, ki));
            }
        } else if (channel.target_path == "rotation") {
            track.rotationTimes.reserve(keyCount);
            track.rotations.reserve(keyCount);
            for (size_t ki = 0; ki < keyCount; ++ki) {
                track.rotationTimes.push_back(ReadFloat(timeBase, timeStride, ki));
                const Vec4 v = ReadVec4(valBase, valStride, ki);
                track.rotations.push_back(Quaternion{v.x, v.y, v.z, v.w}.Normalized());
            }
        } else if (channel.target_path == "scale") {
            track.scaleTimes.reserve(keyCount);
            track.scales.reserve(keyCount);
            for (size_t ki = 0; ki < keyCount; ++ki) {
                track.scaleTimes.push_back(ReadFloat(timeBase, timeStride, ki));
                track.scales.push_back(ReadVec3(valBase, valStride, ki));
            }
        }
        // "weights" (morph targets) are intentionally ignored.
    }

    // Animation section of Load()
    static void LoadGltfAnimations(const tinygltf::Model &model,
                                   const std::vector<int> &sortedSlots,
                                   GltfLoadResult &result) {
        if (!result.skeleton || model.skins.empty())
            return;

        std::unordered_map<int, int> nodeIndexToBoneIndex;
        const tinygltf::Skin &skin = model.skins[0];
        for (int sortedPos = 0; sortedPos < static_cast<int>(sortedSlots.size());
             ++sortedPos) {
            const int originalSlot = sortedSlots[static_cast<size_t>(sortedPos)];
            nodeIndexToBoneIndex[skin.joints[originalSlot]] = sortedPos;
        }

        result.clips.reserve(model.animations.size());
        for (const tinygltf::Animation &anim: model.animations) {
            auto clip = std::make_shared<AnimationClip>();
            clip->name = anim.name;
            clip->duration = 0.0f;

            std::unordered_map<int, BoneTrack> trackMap;
            for (const tinygltf::AnimationChannel &channel: anim.channels) {
                const auto it = nodeIndexToBoneIndex.find(channel.target_node);
                if (it == nodeIndexToBoneIndex.end())
                    continue;
                ProcessAnimationChannel(model, channel, anim.samplers[channel.sampler],
                                        it->second, clip->duration, trackMap);
            }

            for (auto &[boneIdx, track]: trackMap)
                clip->AddTrack(std::move(track));
            result.clips.push_back(std::move(clip));
        }
    }

    // Texture section of Load()
    static std::shared_ptr<Texture> LoadGltfTextureByIndex(
        const tinygltf::Model &model, const std::string &path, int texIdx) {
        if (texIdx < 0)
            return nullptr;
        const tinygltf::Texture &tex = model.textures[texIdx];
        if (tex.source < 0)
            return nullptr;
        const tinygltf::Image &img = model.images[tex.source];
        if (img.uri.empty())
            return nullptr;

        std::string dir = path;
        if (const auto slash = dir.find_last_of("/\\"); slash != std::string::npos)
            dir = dir.substr(0, slash + 1);
        else
            dir.clear();

        auto texture = std::make_shared<Texture>(Texture::FromFile(dir + img.uri));
        if (texture->IsValid())
            return texture;
        return nullptr;
    }

    static void LoadGltfMaterialTextures(const tinygltf::Model &model,
                                         const std::string &path,
                                         GltfLoadResult &result) {
        if (model.materials.empty())
            return;
        const tinygltf::Material &material = model.materials[0];
        result.albedoTexture = LoadGltfTextureByIndex(
            model, path, material.pbrMetallicRoughness.baseColorTexture.index);
        result.normalTexture =
            LoadGltfTextureByIndex(model, path, material.normalTexture.index);
        result.metallicRoughnessTexture = LoadGltfTextureByIndex(
            model, path, material.pbrMetallicRoughness.metallicRoughnessTexture.index);
        result.emissiveTexture =
            LoadGltfTextureByIndex(model, path, material.emissiveTexture.index);
        result.occlusionTexture =
            LoadGltfTextureByIndex(model, path, material.occlusionTexture.index);
    }

    // GltfLoader::Load
    GltfLoadResult GltfLoader::Load(const std::string &path) {
        GltfLoadResult result;

        tinygltf::TinyGLTF loader;
        loader.SetImageLoader(LoadImageDataCallback, nullptr);

        tinygltf::Model model;
        std::string err;
        std::string warn;

        const bool ok = path.ends_with(".glb")
                            ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
                            : loader.LoadASCIIFromFile(&model, &err, &warn, path);

        if (!warn.empty())
            std::cerr << "[GltfLoader] Warning: " << warn << "\n";
        if (!ok) {
            std::cerr << "[GltfLoader] Error loading '" << path << "': " << err << "\n";
            return result;
        }

        std::vector<int> jointToSorted;
        std::vector<int> sortedSlots;
        LoadGltfSkeleton(model, result, sortedSlots, jointToSorted);
        LoadGltfMesh(model, jointToSorted, result);
        LoadGltfAnimations(model, sortedSlots, result);
        LoadGltfMaterialTextures(model, path, result);

        return result;
    }
} // namespace Horo
