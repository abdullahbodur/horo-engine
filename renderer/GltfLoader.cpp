#include "renderer/GltfLoader.h"

// Must come before tiny_gltf.h when TINYGLTF_NO_INCLUDE_JSON is set globally.
#include <nlohmann/json.hpp>

// stb_image declarations — implementation lives in Texture.cpp (STB_IMAGE_IMPLEMENTATION
// is defined there).  Including without the define gives us the prototypes needed by
// tinygltf's LoadImageDataCallback below.
#include <stb_image.h>

// tinygltf single-file implementation — compiled exactly once in this TU.
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

// Engine headers
#include <algorithm>
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

namespace Monolith {

// ---------------------------------------------------------------------------
// Custom stb_image callback for tinygltf
// (TINYGLTF_NO_STB_IMAGE suppresses the built-in one)
// ---------------------------------------------------------------------------
static bool LoadImageDataCallback(tinygltf::Image* image,
                                   const int /*image_idx*/,
                                   std::string* err,
                                   std::string* /*warn*/,
                                   int /*req_width*/,
                                   int /*req_height*/,
                                   const unsigned char* bytes,
                                   int size,
                                   void* /*user_data*/) {
  int w = 0, h = 0, comp = 0;
  unsigned char* data =
      stbi_load_from_memory(bytes, size, &w, &h, &comp, 4);
  if (!data) {
    if (err) *err = "stbi_load_from_memory failed";
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

// ---------------------------------------------------------------------------
// Accessor helpers
// ---------------------------------------------------------------------------

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
    case TINYGLTF_TYPE_SCALAR: return 1;
    case TINYGLTF_TYPE_VEC2:   return 2;
    case TINYGLTF_TYPE_VEC3:   return 3;
    case TINYGLTF_TYPE_VEC4:   return 4;
    case TINYGLTF_TYPE_MAT2:   return 4;
    case TINYGLTF_TYPE_MAT3:   return 9;
    case TINYGLTF_TYPE_MAT4:   return 16;
    default:                   return 0;
  }
}

// Returns a pointer to the first byte of element 0 in the accessor's data.
static const unsigned char* GetAccessorData(const tinygltf::Model& model,
                                             int accessorIdx) {
  const tinygltf::Accessor&   acc  = model.accessors[accessorIdx];
  const tinygltf::BufferView& bv   = model.bufferViews[acc.bufferView];
  const tinygltf::Buffer&     buf  = model.buffers[bv.buffer];
  return buf.data.data() + bv.byteOffset + acc.byteOffset;
}

// Returns the element count (acc.count) for an accessor.
static size_t GetAccessorCount(const tinygltf::Model& model, int accessorIdx) {
  return model.accessors[accessorIdx].count;
}

// Returns the effective byte stride for traversing elements in an accessor.
// When bufferView.byteStride == 0 the data is tightly packed.
static size_t GetAccessorStride(const tinygltf::Model& model, int accessorIdx) {
  const tinygltf::Accessor&   acc = model.accessors[accessorIdx];
  const tinygltf::BufferView& bv  = model.bufferViews[acc.bufferView];
  if (bv.byteStride != 0)
    return bv.byteStride;
  // Tightly packed: stride = elementSize
  return static_cast<size_t>(GetComponentSize(acc.componentType) *
                              GetNumComponents(acc.type));
}

// ---------------------------------------------------------------------------
// Typed accessor read helpers
// ---------------------------------------------------------------------------

// Read a float scalar at element `i` from an accessor known to be SCALAR FLOAT.
static float ReadFloat(const unsigned char* base, size_t stride, size_t i) {
  float v;
  std::memcpy(&v, base + i * stride, sizeof(float));
  return v;
}

// Read a Vec3 at element `i` from a VEC3 FLOAT accessor.
static Vec3 ReadVec3(const unsigned char* base, size_t stride, size_t i) {
  float buf[3];
  std::memcpy(buf, base + i * stride, 3 * sizeof(float));
  return {buf[0], buf[1], buf[2]};
}

// Read a Vec4 at element `i` from a VEC4 FLOAT accessor.
static Vec4 ReadVec4(const unsigned char* base, size_t stride, size_t i) {
  float buf[4];
  std::memcpy(buf, base + i * stride, 4 * sizeof(float));
  return {buf[0], buf[1], buf[2], buf[3]};
}

// Read joint indices at element `i` — handles UNSIGNED_BYTE and UNSIGNED_SHORT.
// Returns four bone indices written into `out[4]`.
static void ReadJointIndices(const unsigned char* base,
                              size_t stride,
                              int componentType,
                              size_t i,
                              int out[4]) {
  const unsigned char* p = base + i * stride;
  if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    out[3] = p[3];
  } else {  // UNSIGNED_SHORT
    uint16_t tmp[4];
    std::memcpy(tmp, p, 4 * sizeof(uint16_t));
    out[0] = tmp[0];
    out[1] = tmp[1];
    out[2] = tmp[2];
    out[3] = tmp[3];
  }
}

// ---------------------------------------------------------------------------
// Topological sort of skin joints
// ---------------------------------------------------------------------------
// Returns a sorted list of indices into skin.joints such that for every joint
// its parent appears at a lower position.  Also fills jointToSorted: a map
// from original skin.joints position → position in the sorted output.
static std::vector<int> TopologicallySortJoints(
    const tinygltf::Model& model,
    const tinygltf::Skin& skin,
    std::vector<int>& outJointToSorted) {
  const int n = static_cast<int>(skin.joints.size());

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
    const tinygltf::Node& node = model.nodes[nodeIdx];
    for (int childNodeIdx : node.children) {
      auto it = nodeToJointIdx.find(childNodeIdx);
      if (it == nodeToJointIdx.end()) continue;
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
    const tinygltf::Node& node = model.nodes[nodeIdx];
    for (int childNodeIdx : node.children) {
      auto it = nodeToJointIdx.find(childNodeIdx);
      if (it == nodeToJointIdx.end()) continue;
      int childJi = it->second;
      if (--inDegree[childJi] == 0)
        queue.push(childJi);
    }
  }

  // Fill reverse map: original skin.joints slot → position in sorted order
  outJointToSorted.resize(static_cast<size_t>(n));
  for (int sortedPos = 0; sortedPos < static_cast<int>(sorted.size()); ++sortedPos)
    outJointToSorted[static_cast<size_t>(sorted[sortedPos])] = sortedPos;

  return sorted;  // each entry is an index into skin.joints[]
}

// ---------------------------------------------------------------------------
// GltfLoader::Load
// ---------------------------------------------------------------------------
GltfLoadResult GltfLoader::Load(const std::string& path) {
  GltfLoadResult result;

  tinygltf::TinyGLTF loader;
  loader.SetImageLoader(LoadImageDataCallback, nullptr);

  tinygltf::Model model;
  std::string err, warn;

  bool ok;
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".glb")
    ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
  else
    ok = loader.LoadASCIIFromFile(&model, &err, &warn, path);

  if (!warn.empty())
    std::cerr << "[GltfLoader] Warning: " << warn << "\n";
  if (!ok) {
    std::cerr << "[GltfLoader] Error loading '" << path << "': " << err << "\n";
    return result;
  }

  // -------------------------------------------------------------------------
  // Skeleton loading
  // -------------------------------------------------------------------------
  // jointToSorted[originalJointSlot] = skeletonBoneIndex
  std::vector<int> jointToSorted;
  // sortedSlots[skeletonBoneIndex] = originalJointSlot (index into skin.joints)
  std::vector<int> sortedSlots;

  auto skeleton = std::make_shared<Skeleton>();

  if (!model.skins.empty()) {
    const tinygltf::Skin& skin = model.skins[0];
    const int numJoints = static_cast<int>(skin.joints.size());

    sortedSlots = TopologicallySortJoints(model, skin, jointToSorted);

    // Read the inverse bind matrices accessor (one mat4 per joint, column-major).
    // We read them via raw pointer because they are tightly packed float data.
    const float* ibmData = nullptr;
    if (skin.inverseBindMatrices >= 0) {
      ibmData = reinterpret_cast<const float*>(
          GetAccessorData(model, skin.inverseBindMatrices));
    }

    for (int sortedPos = 0; sortedPos < numJoints; ++sortedPos) {
      int originalSlot = sortedSlots[static_cast<size_t>(sortedPos)];
      int nodeIdx = skin.joints[originalSlot];
      const tinygltf::Node& node = model.nodes[nodeIdx];

      Bone bone;
      bone.name = node.name;
      bone.parentIndex = -1;

      // Find the parent: look at every joint's children list and check if
      // this joint's node appears there.  After the topo sort we only need
      // to find which already-added bone is the parent.
      for (int pi = 0; pi < numJoints; ++pi) {
        int parentNodeIdx = skin.joints[sortedSlots[static_cast<size_t>(pi)]];
        const tinygltf::Node& parentNode = model.nodes[parentNodeIdx];
        bool found =
            std::find(parentNode.children.begin(), parentNode.children.end(),
                      nodeIdx) != parentNode.children.end();
        if (found) {
          bone.parentIndex = pi;
          break;
        }
      }

      // Inverse bind matrix — column-major float[16] in the accessor.
      if (ibmData) {
        // IBM accessor stores one 4x4 matrix per joint in their original
        // skin.joints order, so we index by originalSlot.
        const float* mat = ibmData + static_cast<size_t>(originalSlot) * 16;
        // Engine Mat4 is column-major: m[col][row]
        for (int col = 0; col < 4; ++col)
          for (int row = 0; row < 4; ++row)
            bone.inverseBindMatrix.m[col][row] = mat[col * 4 + row];
      } else {
        bone.inverseBindMatrix = Mat4::Identity();
      }

      skeleton->AddBone(bone);
    }

    result.skeleton = skeleton;
  }

  // -------------------------------------------------------------------------
  // Mesh loading (first mesh, all primitives merged)
  // -------------------------------------------------------------------------
  if (!model.meshes.empty()) {
    std::vector<SkinnedVertex> vertices;
    std::vector<unsigned int> indices;

    const tinygltf::Mesh& gltfMesh = model.meshes[0];
    for (const tinygltf::Primitive& prim : gltfMesh.primitives) {
      if (prim.mode != TINYGLTF_MODE_TRIANGLES &&
          prim.mode != -1 /* default = triangles */) {
        std::cerr << "[GltfLoader] Skipping non-triangle primitive\n";
        continue;
      }

      // ---- Attributes ----
      auto findAttr = [&](const std::string& name) -> int {
        auto it = prim.attributes.find(name);
        return (it != prim.attributes.end()) ? it->second : -1;
      };

      const int posIdx     = findAttr("POSITION");
      const int normIdx    = findAttr("NORMAL");
      const int uvIdx      = findAttr("TEXCOORD_0");
      const int jointsIdx  = findAttr("JOINTS_0");
      const int weightsIdx = findAttr("WEIGHTS_0");

      if (posIdx < 0) {
        std::cerr << "[GltfLoader] Primitive missing POSITION, skipping\n";
        continue;
      }

      // ---- Read accessor data pointers & strides ----
      const unsigned char* posData = GetAccessorData(model, posIdx);
      size_t posStride = GetAccessorStride(model, posIdx);
      const size_t vertCount = GetAccessorCount(model, posIdx);

      const unsigned char* normData   = nullptr;
      size_t normStride = 0;
      if (normIdx >= 0) {
        normData   = GetAccessorData(model, normIdx);
        normStride = GetAccessorStride(model, normIdx);
      }

      const unsigned char* uvData   = nullptr;
      size_t uvStride = 0;
      if (uvIdx >= 0) {
        uvData   = GetAccessorData(model, uvIdx);
        uvStride = GetAccessorStride(model, uvIdx);
      }

      const unsigned char* jointsData    = nullptr;
      size_t jointsStride                = 0;
      int    jointsComponentType         = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
      if (jointsIdx >= 0) {
        jointsData          = GetAccessorData(model, jointsIdx);
        jointsStride        = GetAccessorStride(model, jointsIdx);
        jointsComponentType = model.accessors[jointsIdx].componentType;
      }

      const unsigned char* weightsData = nullptr;
      size_t weightsStride             = 0;
      if (weightsIdx >= 0) {
        weightsData   = GetAccessorData(model, weightsIdx);
        weightsStride = GetAccessorStride(model, weightsIdx);
      }

      // ---- Build vertices ----
      const unsigned int baseVertex = static_cast<unsigned int>(vertices.size());
      vertices.reserve(vertices.size() + vertCount);

      for (size_t vi = 0; vi < vertCount; ++vi) {
        SkinnedVertex sv{};

        // Position (always present)
        sv.position = ReadVec3(posData, posStride, vi);

        // Normal
        if (normData) {
          sv.normal = ReadVec3(normData, normStride, vi);
        }
        // (flat normals generated after index loop if normData was null)

        // UV
        if (uvData) {
          float buf[2];
          std::memcpy(buf, uvData + vi * uvStride, 2 * sizeof(float));
          sv.uv = {buf[0], buf[1]};
        }

        // Bone indices (-1 = unused)
        sv.boneIndices[0] = -1;
        sv.boneIndices[1] = -1;
        sv.boneIndices[2] = -1;
        sv.boneIndices[3] = -1;
        sv.boneWeights    = Vec4{0.0f, 0.0f, 0.0f, 0.0f};

        if (jointsData && weightsIdx >= 0) {
          int rawJoints[4];
          ReadJointIndices(jointsData, jointsStride, jointsComponentType, vi,
                           rawJoints);

          Vec4 w = ReadVec4(weightsData, weightsStride, vi);
          const float weights[4] = {w.x, w.y, w.z, w.w};

          for (int slot = 0; slot < 4; ++slot) {
            if (weights[slot] <= 0.0f) continue;  // ignore zero-weight influences
            int rawJoint = rawJoints[slot];
            // Remap: rawJoint is an index into skin.joints[];
            // jointToSorted maps that to the skeleton bone index.
            int boneIdx = -1;
            if (!jointToSorted.empty() &&
                rawJoint >= 0 &&
                rawJoint < static_cast<int>(jointToSorted.size())) {
              boneIdx = jointToSorted[static_cast<size_t>(rawJoint)];
            }
            sv.boneIndices[slot] = boneIdx;
          }
          sv.boneWeights = w;
        }

        vertices.push_back(sv);
      }

      // ---- Build indices ----
      if (prim.indices >= 0) {
        const tinygltf::Accessor&   idxAcc = model.accessors[prim.indices];
        const unsigned char*        idxPtr = GetAccessorData(model, prim.indices);
        const size_t idxStride = GetAccessorStride(model, prim.indices);
        const size_t idxCount  = idxAcc.count;

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
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
              std::memcpy(&idx, idxPtr + ii * idxStride, 4);
              break;
            }
            default:
              break;
          }
          indices.push_back(baseVertex + idx);
        }
      } else {
        // No index buffer — generate sequential indices for this primitive.
        const unsigned int count = static_cast<unsigned int>(vertCount);
        indices.reserve(indices.size() + count);
        for (unsigned int ii = 0; ii < count; ++ii)
          indices.push_back(baseVertex + ii);
      }

      // ---- Auto-generate flat normals if NORMAL was absent ----
      if (!normData) {
        // Iterate over the triangles we just appended.
        const size_t idxStart = indices.size() -
            (prim.indices >= 0
                 ? model.accessors[prim.indices].count
                 : vertCount);
        for (size_t ti = idxStart; ti + 2 < indices.size(); ti += 3) {
          SkinnedVertex& v0 = vertices[indices[ti]];
          SkinnedVertex& v1 = vertices[indices[ti + 1]];
          SkinnedVertex& v2 = vertices[indices[ti + 2]];
          Vec3 e1 = v1.position - v0.position;
          Vec3 e2 = v2.position - v0.position;
          Vec3 n  = Vec3::Cross(e1, e2).Normalized();
          v0.normal = n;
          v1.normal = n;
          v2.normal = n;
        }
      }
    }  // for each primitive

    if (!vertices.empty()) {
      auto mesh = std::make_shared<SkinnedMesh>();
      mesh->SetData(vertices, indices);
      result.mesh = mesh;
    }
  }

  // -------------------------------------------------------------------------
  // Animation loading
  // -------------------------------------------------------------------------
  // Build a map: glTF node index → skeleton bone index (for quick lookup)
  std::unordered_map<int, int> nodeIndexToBoneIndex;
  if (result.skeleton && !model.skins.empty()) {
    const tinygltf::Skin& skin = model.skins[0];
    for (int sortedPos = 0;
         sortedPos < static_cast<int>(sortedSlots.size()); ++sortedPos) {
      int originalSlot = sortedSlots[static_cast<size_t>(sortedPos)];
      int nodeIdx      = skin.joints[originalSlot];
      nodeIndexToBoneIndex[nodeIdx] = sortedPos;
    }
  }

  result.clips.reserve(model.animations.size());

  for (const tinygltf::Animation& anim : model.animations) {
    auto clip = std::make_shared<AnimationClip>();
    clip->name = anim.name;
    clip->duration = 0.0f;

    // We accumulate tracks per bone index, then add them all at the end.
    std::unordered_map<int, BoneTrack> trackMap;

    for (const tinygltf::AnimationChannel& channel : anim.channels) {
      // Find the skeleton bone this channel targets.
      auto it = nodeIndexToBoneIndex.find(channel.target_node);
      if (it == nodeIndexToBoneIndex.end())
        continue;  // targets a non-skin node — ignore
      const int boneIdx = it->second;

      const tinygltf::AnimationSampler& sampler =
          anim.samplers[channel.sampler];

      // Input: time values (SCALAR FLOAT)
      const unsigned char* timeBase   = GetAccessorData(model, sampler.input);
      size_t               timeStride = GetAccessorStride(model, sampler.input);
      const size_t         keyCount   = GetAccessorCount(model, sampler.input);

      // Update clip duration
      if (keyCount > 0) {
        float lastTime = ReadFloat(timeBase, timeStride, keyCount - 1);
        if (lastTime > clip->duration)
          clip->duration = lastTime;
      }

      // Output: values (VEC3 FLOAT for translation/scale, VEC4 FLOAT for rotation)
      const unsigned char* valBase   = GetAccessorData(model, sampler.output);
      size_t               valStride = GetAccessorStride(model, sampler.output);

      // Find or create the BoneTrack for this bone.
      auto& track = trackMap.emplace(boneIdx, BoneTrack{boneIdx, {}, {}, {}, {}, {}, {}})
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
          Vec4 v = ReadVec4(valBase, valStride, ki);
          // glTF quaternion order is (x, y, z, w) — same as engine Quaternion.
          Quaternion q{v.x, v.y, v.z, v.w};
          track.rotations.push_back(q.Normalized());
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

    // Commit all bone tracks to the clip.
    for (auto& [boneIdx, track] : trackMap)
      clip->AddTrack(std::move(track));

    result.clips.push_back(std::move(clip));
  }

  // -------------------------------------------------------------------------
  // Texture loading (base color of first material, if available)
  // -------------------------------------------------------------------------
  // Texture::FromMemory does not exist on the engine's Texture class, so we
  // can only load textures that are already unpacked to disk via Texture::FromFile.
  // Embedded textures in GLB are decoded into tinygltf::Image::image[] but there
  // is no matching GPU upload path without adding a new Texture method.
  // We therefore check for a URI-based texture source and use Texture::FromFile
  // when the image has a filesystem path; embedded images are left as null.
  if (!model.materials.empty()) {
    const tinygltf::Material& mat = model.materials[0];
    const int texIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
    if (texIdx >= 0) {
      const tinygltf::Texture& tex = model.textures[texIdx];
      if (tex.source >= 0) {
        const tinygltf::Image& img = model.images[tex.source];
        if (!img.uri.empty()) {
          // URI path is relative to the .gltf file's directory.
          // Resolve it relative to the loader path.
          std::string dir = path;
          auto slash = dir.find_last_of("/\\");
          if (slash != std::string::npos)
            dir = dir.substr(0, slash + 1);
          else
            dir.clear();
          const std::string texPath = dir + img.uri;
          auto texture = std::make_shared<Texture>(Texture::FromFile(texPath));
          if (texture->IsValid())
            result.albedoTexture = std::move(texture);
        }
        // Embedded (uri empty, image data already in img.image[]): no GPU upload path
        // without adding a Texture::FromMemory factory — left as null per spec.
      }
    }
  }

  return result;
}

}  // namespace Monolith
