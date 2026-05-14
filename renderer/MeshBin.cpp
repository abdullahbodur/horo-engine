/** @file MeshBin.cpp
 *  @brief Static-mesh binary writer / reader implementation. See MeshBin.h for the format.
 */
#include "renderer/MeshBin.h"

#include <array>
#include <filesystem>
#include <format>
#include <fstream>

#include "core/Logger.h"
#include "renderer/BinaryStream.h"
#include "renderer/BinaryMeshIoShared.h"

namespace Horo::MeshBin {
    namespace {
        /**
         * @brief On-disk fixed-size header. Field layout matches @ref MeshBin.h documentation.
         *
         * Stored host-endian on the wire; all currently supported targets are
         * little-endian, and a magic-mismatch read on a future big-endian platform
         * would produce a clear "bad magic" diagnostic rather than silent
         * mis-interpretation.
         */
        struct Header {
            uint32_t magic;
            uint32_t version;
            uint32_t vertexCount;
            uint32_t indexCount;
            uint32_t vertexStride;
            uint32_t reserved0;
            std::array<float, 3> aabbMin;
            std::array<float, 3> aabbMax;
        };

        static_assert(sizeof(Header) == 48,
                      "MeshBin header layout must remain 48 bytes; bump kMeshBinVersion before changing it.");

    } // namespace

    /** @copydoc Horo::MeshBin::WriteStaticMesh */
    WriteResult WriteStaticMesh(const std::string &destPath,
                                const std::vector<Vertex> &vertices,
                                const std::vector<uint32_t> &indices) {
        WriteResult result;
        if (vertices.empty()) {
            result.error = "MeshBin write: empty vertex array.";
            return result;
        }
        if (indices.empty() || indices.size() % 3 != 0) {
            result.error = "MeshBin write: index count must be a non-zero multiple of 3.";
            return result;
        }

        const std::filesystem::path path(destPath);
        std::ofstream stream = BinaryStream::OpenForWrite(path, "MeshBin write", &result.error);
        if (!stream.is_open())
            return result;

        Vec3 aabbMin{};
        Vec3 aabbMax{};
        BinaryMeshIoShared::ComputePositionAabb(vertices, &aabbMin, &aabbMax);

        Header header{};
        header.magic = kMeshBinMagic;
        header.version = kMeshBinVersion;
        header.vertexCount = static_cast<uint32_t>(vertices.size());
        header.indexCount = static_cast<uint32_t>(indices.size());
        header.vertexStride = static_cast<uint32_t>(sizeof(Vertex));
        header.reserved0 = 0;
        BinaryStream::StoreAabbToHeader(header, aabbMin, aabbMax);

        if (!BinaryMeshIoShared::WriteHeaderVerticesIndices(
                stream, header, vertices, indices, "MeshBin write", &result.error)) {
            return result;
        }

        if (!BinaryMeshIoShared::FlushStream(stream, "MeshBin write", &result.error)) {
            return result;
        }

        result.ok = true;
        return result;
    }

    /** @copydoc Horo::MeshBin::ReadStaticMesh */
    ReadResult ReadStaticMesh(const std::string &sourcePath) {
        ReadResult result;

        std::ifstream stream = BinaryStream::OpenForRead(sourcePath, "MeshBin read", &result.error);
        if (!stream.is_open())
            return result;

        Header header{};
        if (!BinaryMeshIoShared::ReadHeaderBlob(stream, header, "MeshBin read",
                                                &result.error)) {
            return result;
        }
        if (header.magic != kMeshBinMagic) {
            result.error = "MeshBin read: bad magic bytes; not a HoroMeshBin file.";
            return result;
        }
        if (header.version != kMeshBinVersion) {
            result.error = std::format("MeshBin read: unsupported version {} (expected {}).",
                                       header.version, kMeshBinVersion);
            return result;
        }
        if (header.vertexStride != sizeof(Vertex)) {
            result.error = std::format("MeshBin read: vertex stride mismatch; expected {}, got {}.",
                                       sizeof(Vertex), header.vertexStride);
            return result;
        }
        if (header.vertexCount == 0 || header.indexCount == 0 ||
            header.indexCount % 3 != 0) {
            result.error = "MeshBin read: invalid vertex/index counts in header.";
            return result;
        }

        if (!BinaryMeshIoShared::ReadVerticesIndices(
                stream, header, result.vertices, result.indices, "MeshBin read",
                &result.error)) {
            return result;
        }

<<<<<<< HEAD
=======
        result.indices.resize(header.indexCount);
        if (!BinaryStream::ReadArray(stream, result.indices.data(),
                                     static_cast<std::size_t>(header.indexCount))) {
            result.error = "MeshBin read: failed reading index array.";
            return result;
        }

>>>>>>> 488af99 (chore: refactor)
        BinaryStream::LoadAabbFromHeader(header, result.aabbMin, result.aabbMax);
        result.ok = true;
        return result;
    }
} // namespace Horo::MeshBin
