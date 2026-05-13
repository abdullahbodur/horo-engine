/** @file SkinnedMeshBin.cpp
 *  @brief Skinned-mesh + skeleton binary writer / reader. See SkinnedMeshBin.h.
 */
#include "renderer/SkinnedMeshBin.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

#include "core/Logger.h"

namespace Horo::SkinnedMeshBin {
    namespace {
        /** @brief Fixed 64-byte header. */
        struct Header {
            uint32_t magic;
            uint32_t version;
            uint32_t vertexCount;
            uint32_t indexCount;
            uint32_t boneCount;
            uint32_t vertexStride;
            uint32_t reserved0;
            uint32_t reserved1;
            float aabbMin[3];
            float aabbMax[3];
            uint32_t reserved2;
            uint32_t reserved3;
        };

        static_assert(sizeof(Header) == 64,
                      "SkinnedMeshBin header layout must remain 64 bytes; bump kSkinnedMeshBinVersion before changing it.");

        bool WriteBytes(std::ofstream &stream, const void *data, std::size_t size) {
            stream.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
            return stream.good();
        }

        bool ReadBytes(std::ifstream &stream, void *data, std::size_t size) {
            stream.read(static_cast<char *>(data), static_cast<std::streamsize>(size));
            return stream.good();
        }

        /** @brief Computes the per-component AABB over @p vertices. */
        void ComputeAabb(const std::vector<SkinnedVertex> &vertices, Vec3 *outMin,
                         Vec3 *outMax) {
            *outMin = {std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::infinity()};
            *outMax = {-std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity()};
            for (const SkinnedVertex &v: vertices) {
                outMin->x = std::min(outMin->x, v.position.x);
                outMin->y = std::min(outMin->y, v.position.y);
                outMin->z = std::min(outMin->z, v.position.z);
                outMax->x = std::max(outMax->x, v.position.x);
                outMax->y = std::max(outMax->y, v.position.y);
                outMax->z = std::max(outMax->z, v.position.z);
            }
        }

        /** @brief Writes a single Mat4 as 16 column-major float32s. */
        bool WriteMatrix(std::ofstream &stream, const Mat4 &m) {
            float buf[16];
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                    buf[col * 4 + row] = m(row, col);
            return WriteBytes(stream, buf, sizeof(buf));
        }

        /** @brief Reads a Mat4 from 16 column-major float32s. */
        bool ReadMatrix(std::ifstream &stream, Mat4 &out) {
            float buf[16];
            if (!ReadBytes(stream, buf, sizeof(buf)))
                return false;
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                    out(row, col) = buf[col * 4 + row];
            return true;
        }
    } // namespace

    /** @copydoc Horo::SkinnedMeshBin::WriteSkinnedMesh */
    WriteResult WriteSkinnedMesh(const std::string &destPath,
                                  const std::vector<SkinnedVertex> &vertices,
                                  const std::vector<uint32_t> &indices,
                                  const std::vector<Bone> &bones) {
        WriteResult result;
        if (vertices.empty()) {
            result.error = "SkinnedMeshBin write: empty vertex array.";
            return result;
        }
        if (indices.empty() || indices.size() % 3 != 0) {
            result.error = "SkinnedMeshBin write: index count must be a non-zero multiple of 3.";
            return result;
        }
        if (bones.empty()) {
            result.error = "SkinnedMeshBin write: empty bone array.";
            return result;
        }

        std::error_code ec;
        const std::filesystem::path path(destPath);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                result.error =
                        "SkinnedMeshBin write: cannot create destination directory: " +
                        ec.message();
                return result;
            }
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            result.error = "SkinnedMeshBin write: cannot open destination file for writing.";
            return result;
        }

        Vec3 aabbMin{};
        Vec3 aabbMax{};
        ComputeAabb(vertices, &aabbMin, &aabbMax);

        Header header{};
        header.magic = kSkinnedMeshBinMagic;
        header.version = kSkinnedMeshBinVersion;
        header.vertexCount = static_cast<uint32_t>(vertices.size());
        header.indexCount = static_cast<uint32_t>(indices.size());
        header.boneCount = static_cast<uint32_t>(bones.size());
        header.vertexStride = static_cast<uint32_t>(sizeof(SkinnedVertex));
        header.aabbMin[0] = aabbMin.x;
        header.aabbMin[1] = aabbMin.y;
        header.aabbMin[2] = aabbMin.z;
        header.aabbMax[0] = aabbMax.x;
        header.aabbMax[1] = aabbMax.y;
        header.aabbMax[2] = aabbMax.z;

        if (!WriteBytes(stream, &header, sizeof(header))) {
            result.error = "SkinnedMeshBin write: failed writing header.";
            return result;
        }
        if (!WriteBytes(stream, vertices.data(),
                        vertices.size() * sizeof(SkinnedVertex))) {
            result.error = "SkinnedMeshBin write: failed writing vertex array.";
            return result;
        }
        if (!WriteBytes(stream, indices.data(),
                        indices.size() * sizeof(uint32_t))) {
            result.error = "SkinnedMeshBin write: failed writing index array.";
            return result;
        }
        for (const Bone &bone: bones) {
            const int32_t parent = static_cast<int32_t>(bone.parentIndex);
            if (!WriteBytes(stream, &parent, sizeof(parent))) {
                result.error = "SkinnedMeshBin write: failed writing bone parent.";
                return result;
            }
            if (!WriteMatrix(stream, bone.inverseBindMatrix)) {
                result.error = "SkinnedMeshBin write: failed writing bone matrix.";
                return result;
            }
            const uint32_t nameLength = static_cast<uint32_t>(bone.name.size());
            if (!WriteBytes(stream, &nameLength, sizeof(nameLength))) {
                result.error = "SkinnedMeshBin write: failed writing bone name length.";
                return result;
            }
            if (nameLength > 0 &&
                !WriteBytes(stream, bone.name.data(), nameLength)) {
                result.error = "SkinnedMeshBin write: failed writing bone name.";
                return result;
            }
        }

        stream.flush();
        if (!stream.good()) {
            result.error = "SkinnedMeshBin write: stream entered fail state during flush.";
            return result;
        }
        result.ok = true;
        return result;
    }

    /** @copydoc Horo::SkinnedMeshBin::ReadSkinnedMesh */
    ReadResult ReadSkinnedMesh(const std::string &sourcePath) {
        ReadResult result;

        std::error_code ec;
        if (!std::filesystem::is_regular_file(sourcePath, ec) || ec) {
            result.error = "SkinnedMeshBin read: source path is not a regular file.";
            return result;
        }

        std::ifstream stream(sourcePath, std::ios::binary);
        if (!stream.is_open()) {
            result.error = "SkinnedMeshBin read: cannot open source file.";
            return result;
        }

        Header header{};
        if (!ReadBytes(stream, &header, sizeof(header))) {
            result.error = "SkinnedMeshBin read: file too short for header.";
            return result;
        }
        if (header.magic != kSkinnedMeshBinMagic) {
            result.error = "SkinnedMeshBin read: bad magic bytes; not a HoroSkinnedBin file.";
            return result;
        }
        if (header.version != kSkinnedMeshBinVersion) {
            result.error = "SkinnedMeshBin read: unsupported version " +
                           std::to_string(header.version) + " (expected " +
                           std::to_string(kSkinnedMeshBinVersion) + ").";
            return result;
        }
        if (header.vertexStride != sizeof(SkinnedVertex)) {
            result.error = "SkinnedMeshBin read: vertex stride mismatch; expected " +
                           std::to_string(sizeof(SkinnedVertex)) + ", got " +
                           std::to_string(header.vertexStride) + ".";
            return result;
        }
        if (header.vertexCount == 0 || header.indexCount == 0 ||
            header.indexCount % 3 != 0 || header.boneCount == 0) {
            result.error = "SkinnedMeshBin read: invalid vertex/index/bone counts in header.";
            return result;
        }

        result.vertices.resize(header.vertexCount);
        if (!ReadBytes(stream, result.vertices.data(),
                       static_cast<std::size_t>(header.vertexCount) * sizeof(SkinnedVertex))) {
            result.error = "SkinnedMeshBin read: failed reading vertex array.";
            return result;
        }
        result.indices.resize(header.indexCount);
        if (!ReadBytes(stream, result.indices.data(),
                       static_cast<std::size_t>(header.indexCount) * sizeof(uint32_t))) {
            result.error = "SkinnedMeshBin read: failed reading index array.";
            return result;
        }

        result.bones.reserve(header.boneCount);
        for (uint32_t i = 0; i < header.boneCount; ++i) {
            Bone bone;
            int32_t parent = -1;
            if (!ReadBytes(stream, &parent, sizeof(parent))) {
                result.error = "SkinnedMeshBin read: failed reading bone parent.";
                return result;
            }
            bone.parentIndex = static_cast<int>(parent);
            if (!ReadMatrix(stream, bone.inverseBindMatrix)) {
                result.error = "SkinnedMeshBin read: failed reading bone matrix.";
                return result;
            }
            uint32_t nameLength = 0;
            if (!ReadBytes(stream, &nameLength, sizeof(nameLength))) {
                result.error = "SkinnedMeshBin read: failed reading bone name length.";
                return result;
            }
            if (nameLength > 0) {
                bone.name.resize(nameLength);
                if (!ReadBytes(stream, bone.name.data(), nameLength)) {
                    result.error = "SkinnedMeshBin read: failed reading bone name.";
                    return result;
                }
            }
            result.bones.push_back(std::move(bone));
        }

        result.aabbMin = {header.aabbMin[0], header.aabbMin[1], header.aabbMin[2]};
        result.aabbMax = {header.aabbMax[0], header.aabbMax[1], header.aabbMax[2]};
        result.ok = true;
        return result;
    }
} // namespace Horo::SkinnedMeshBin
