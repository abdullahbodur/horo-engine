/** @file SkinnedMeshBin.cpp
 *  @brief Skinned-mesh + skeleton binary writer / reader. See SkinnedMeshBin.h.
 */
#include "renderer/SkinnedMeshBin.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>

#include "renderer/BinaryStream.h"
#include "renderer/BinaryMeshIoShared.h"

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
        std::ofstream stream =
            BinaryStream::OpenForWrite(path, "SkinnedMeshBin write", &result.error);
        if (!stream.is_open())
            return result;

        Vec3 aabbMin{};
        Vec3 aabbMax{};
        BinaryMeshIoShared::ComputePositionAabb(vertices, &aabbMin, &aabbMax);

        Header header{};
        header.magic = kSkinnedMeshBinMagic;
        header.version = kSkinnedMeshBinVersion;
        header.vertexCount = static_cast<uint32_t>(vertices.size());
        header.indexCount = static_cast<uint32_t>(indices.size());
        header.boneCount = static_cast<uint32_t>(bones.size());
        header.vertexStride = static_cast<uint32_t>(sizeof(SkinnedVertex));
        BinaryStream::StoreAabbToHeader(header, aabbMin, aabbMax);

        if (!BinaryMeshIoShared::WriteHeaderVerticesIndices(
                stream, header, vertices, indices, "SkinnedMeshBin write", &result.error)) {
            return result;
        }
        for (const Bone &bone: bones) {
            if (!WriteBone(stream, bone, &result.error))
                return result;
        }

        if (!BinaryMeshIoShared::FlushStream(stream, "SkinnedMeshBin write", &result.error)) {
            return result;
        }
        result.ok = true;
        return result;
    }

    /** @copydoc Horo::SkinnedMeshBin::ReadSkinnedMesh */
    ReadResult ReadSkinnedMesh(const std::string &sourcePath) {
        ReadResult result;

        std::ifstream stream =
            BinaryStream::OpenForRead(sourcePath, "SkinnedMeshBin read", &result.error);
        if (!stream.is_open())
            return result;

        Header header{};
        if (!BinaryMeshIoShared::ReadHeaderBlob(stream, header,
                                                "SkinnedMeshBin read",
                                                &result.error)) {
            return result;
        }
        if (!ValidateHeader(header, &result.error))
            return result;

        if (!BinaryMeshIoShared::ReadVerticesIndices(
                stream, header, result.vertices, result.indices, "SkinnedMeshBin read",
                &result.error)) {
            return result;
        }

        result.bones.reserve(header.boneCount);
        for (uint32_t i = 0; i < header.boneCount; ++i) {
            Bone bone;
            if (!ReadBone(stream, bone, &result.error))
                return result;
            result.bones.push_back(std::move(bone));
        }

        BinaryStream::LoadAabbFromHeader(header, result.aabbMin, result.aabbMax);
        result.ok = true;
        return result;
    }
} // namespace Horo::SkinnedMeshBin
