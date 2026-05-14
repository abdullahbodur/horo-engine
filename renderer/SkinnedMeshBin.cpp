/** @file SkinnedMeshBin.cpp
 *  @brief Skinned-mesh + skeleton binary writer / reader. See SkinnedMeshBin.h.
 */
#include "renderer/SkinnedMeshBin.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>

#include "core/Logger.h"
#include "renderer/BinaryStream.h"

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
            std::array<float, 3> aabbMin;
            std::array<float, 3> aabbMax;
            uint32_t reserved2;
            uint32_t reserved3;
        };

        static_assert(sizeof(Header) == 64,
                      "SkinnedMeshBin header layout must remain 64 bytes; bump kSkinnedMeshBinVersion before changing it.");

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

        /** @brief Writes a Mat4 as 16 column-major float32s. */
        bool WriteMatrix(std::ofstream &stream, const Mat4 &m) {
            std::array<float, 16> buf{};
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                    buf[static_cast<std::size_t>(col * 4 + row)] = m(row, col);
            return BinaryStream::WriteValue(stream, buf);
        }

        /** @brief Reads a Mat4 from 16 column-major float32s. */
        bool ReadMatrix(std::ifstream &stream, Mat4 &out) {
            std::array<float, 16> buf{};
            if (!BinaryStream::ReadValue(stream, buf))
                return false;
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                    out(row, col) = buf[static_cast<std::size_t>(col * 4 + row)];
            return true;
        }

        /** @brief Writes the on-disk record for one @p bone. Sets @p errorOut on failure. */
        bool WriteBone(std::ofstream &stream, const Bone &bone, std::string *errorOut) {
            if (const auto parent = static_cast<int32_t>(bone.parentIndex);
                !BinaryStream::WriteValue(stream, parent)) {
                *errorOut = "SkinnedMeshBin write: failed writing bone parent.";
                return false;
            }
            if (!WriteMatrix(stream, bone.inverseBindMatrix)) {
                *errorOut = "SkinnedMeshBin write: failed writing bone matrix.";
                return false;
            }
            const auto nameLength = static_cast<uint32_t>(bone.name.size());
            if (!BinaryStream::WriteValue(stream, nameLength)) {
                *errorOut = "SkinnedMeshBin write: failed writing bone name length.";
                return false;
            }
            if (nameLength > 0 &&
                !BinaryStream::WriteArray(stream, bone.name.data(), nameLength)) {
                *errorOut = "SkinnedMeshBin write: failed writing bone name.";
                return false;
            }
            return true;
        }

        /** @brief Reads one bone record. Sets @p errorOut on failure. */
        bool ReadBone(std::ifstream &stream, Bone &out, std::string *errorOut) {
            int32_t parent = -1;
            if (!BinaryStream::ReadValue(stream, parent)) {
                *errorOut = "SkinnedMeshBin read: failed reading bone parent.";
                return false;
            }
            out.parentIndex = static_cast<int>(parent);
            if (!ReadMatrix(stream, out.inverseBindMatrix)) {
                *errorOut = "SkinnedMeshBin read: failed reading bone matrix.";
                return false;
            }
            uint32_t nameLength = 0;
            if (!BinaryStream::ReadValue(stream, nameLength)) {
                *errorOut = "SkinnedMeshBin read: failed reading bone name length.";
                return false;
            }
            if (nameLength > 0) {
                out.name.resize(nameLength);
                if (!BinaryStream::ReadArray(stream, out.name.data(), nameLength)) {
                    *errorOut = "SkinnedMeshBin read: failed reading bone name.";
                    return false;
                }
            }
            return true;
        }

        /** @brief Validates header counts for a sane payload. */
        bool ValidateHeader(const Header &h, std::string *errorOut) {
            if (h.magic != kSkinnedMeshBinMagic) {
                *errorOut = "SkinnedMeshBin read: bad magic bytes; not a HoroSkinnedBin file.";
                return false;
            }
            if (h.version != kSkinnedMeshBinVersion) {
                *errorOut = std::format("SkinnedMeshBin read: unsupported version {} (expected {}).",
                                        h.version, kSkinnedMeshBinVersion);
                return false;
            }
            if (h.vertexStride != sizeof(SkinnedVertex)) {
                *errorOut = std::format("SkinnedMeshBin read: vertex stride mismatch; expected {}, got {}.",
                                        sizeof(SkinnedVertex), h.vertexStride);
                return false;
            }
            if (h.vertexCount == 0 || h.indexCount == 0 ||
                h.indexCount % 3 != 0 || h.boneCount == 0) {
                *errorOut = "SkinnedMeshBin read: invalid vertex/index/bone counts in header.";
                return false;
            }
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

        const std::filesystem::path path(destPath);
        if (path.has_parent_path()) {
            std::error_code ec;
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

        if (!BinaryStream::WriteValue(stream, header)) {
            result.error = "SkinnedMeshBin write: failed writing header.";
            return result;
        }
        if (!BinaryStream::WriteArray(stream, vertices.data(), vertices.size())) {
            result.error = "SkinnedMeshBin write: failed writing vertex array.";
            return result;
        }
        if (!BinaryStream::WriteArray(stream, indices.data(), indices.size())) {
            result.error = "SkinnedMeshBin write: failed writing index array.";
            return result;
        }
        for (const Bone &bone: bones) {
            if (!WriteBone(stream, bone, &result.error))
                return result;
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

        if (std::error_code ec; !std::filesystem::is_regular_file(sourcePath, ec) || ec) {
            result.error = "SkinnedMeshBin read: source path is not a regular file.";
            return result;
        }

        std::ifstream stream(sourcePath, std::ios::binary);
        if (!stream.is_open()) {
            result.error = "SkinnedMeshBin read: cannot open source file.";
            return result;
        }

        Header header{};
        if (!BinaryStream::ReadValue(stream, header)) {
            result.error = "SkinnedMeshBin read: file too short for header.";
            return result;
        }
        if (!ValidateHeader(header, &result.error))
            return result;

        result.vertices.resize(header.vertexCount);
        if (!BinaryStream::ReadArray(stream, result.vertices.data(),
                                     static_cast<std::size_t>(header.vertexCount))) {
            result.error = "SkinnedMeshBin read: failed reading vertex array.";
            return result;
        }
        result.indices.resize(header.indexCount);
        if (!BinaryStream::ReadArray(stream, result.indices.data(),
                                     static_cast<std::size_t>(header.indexCount))) {
            result.error = "SkinnedMeshBin read: failed reading index array.";
            return result;
        }

        result.bones.reserve(header.boneCount);
        for (uint32_t i = 0; i < header.boneCount; ++i) {
            Bone bone;
            if (!ReadBone(stream, bone, &result.error))
                return result;
            result.bones.push_back(std::move(bone));
        }

        result.aabbMin = {header.aabbMin[0], header.aabbMin[1], header.aabbMin[2]};
        result.aabbMax = {header.aabbMax[0], header.aabbMax[1], header.aabbMax[2]};
        result.ok = true;
        return result;
    }
} // namespace Horo::SkinnedMeshBin
