/** @file MeshBin.cpp
 *  @brief Static-mesh binary writer / reader implementation. See MeshBin.h for the format.
 */
#include "renderer/MeshBin.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>

#include "core/Logger.h"
#include "renderer/BinaryStream.h"

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

        /** @brief Computes the per-component AABB over @p vertices. */
        void ComputeAabb(const std::vector<Vertex> &vertices, Vec3 *outMin,
                         Vec3 *outMax) {
            *outMin = {std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::infinity()};
            *outMax = {-std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity()};
            for (const Vertex &vertex: vertices) {
                outMin->x = std::min(outMin->x, vertex.position.x);
                outMin->y = std::min(outMin->y, vertex.position.y);
                outMin->z = std::min(outMin->z, vertex.position.z);
                outMax->x = std::max(outMax->x, vertex.position.x);
                outMax->y = std::max(outMax->y, vertex.position.y);
                outMax->z = std::max(outMax->z, vertex.position.z);
            }
        }
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
        if (path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                result.error = "MeshBin write: cannot create destination directory: " + ec.message();
                return result;
            }
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            result.error = "MeshBin write: cannot open destination file for writing.";
            return result;
        }

        Vec3 aabbMin{};
        Vec3 aabbMax{};
        ComputeAabb(vertices, &aabbMin, &aabbMax);

        Header header{};
        header.magic = kMeshBinMagic;
        header.version = kMeshBinVersion;
        header.vertexCount = static_cast<uint32_t>(vertices.size());
        header.indexCount = static_cast<uint32_t>(indices.size());
        header.vertexStride = static_cast<uint32_t>(sizeof(Vertex));
        header.reserved0 = 0;
        header.aabbMin[0] = aabbMin.x;
        header.aabbMin[1] = aabbMin.y;
        header.aabbMin[2] = aabbMin.z;
        header.aabbMax[0] = aabbMax.x;
        header.aabbMax[1] = aabbMax.y;
        header.aabbMax[2] = aabbMax.z;

        if (!BinaryStream::WriteValue(stream, header)) {
            result.error = "MeshBin write: failed writing header.";
            return result;
        }
        if (!BinaryStream::WriteArray(stream, vertices.data(), vertices.size())) {
            result.error = "MeshBin write: failed writing vertex array.";
            return result;
        }
        if (!BinaryStream::WriteArray(stream, indices.data(), indices.size())) {
            result.error = "MeshBin write: failed writing index array.";
            return result;
        }

        stream.flush();
        if (!stream.good()) {
            result.error = "MeshBin write: stream entered fail state during flush.";
            return result;
        }

        result.ok = true;
        return result;
    }

    /** @copydoc Horo::MeshBin::ReadStaticMesh */
    ReadResult ReadStaticMesh(const std::string &sourcePath) {
        ReadResult result;

        if (std::error_code ec; !std::filesystem::is_regular_file(sourcePath, ec) || ec) {
            result.error = "MeshBin read: source path is not a regular file.";
            return result;
        }

        std::ifstream stream(sourcePath, std::ios::binary);
        if (!stream.is_open()) {
            result.error = "MeshBin read: cannot open source file.";
            return result;
        }

        Header header{};
        if (!BinaryStream::ReadValue(stream, header)) {
            result.error = "MeshBin read: file too short for header.";
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

        result.vertices.resize(header.vertexCount);
        if (!BinaryStream::ReadArray(stream, result.vertices.data(),
                                     static_cast<std::size_t>(header.vertexCount))) {
            result.error = "MeshBin read: failed reading vertex array.";
            return result;
        }

        result.indices.resize(header.indexCount);
        if (!BinaryStream::ReadArray(stream, result.indices.data(),
                                     static_cast<std::size_t>(header.indexCount))) {
            result.error = "MeshBin read: failed reading index array.";
            return result;
        }

        result.aabbMin = {header.aabbMin[0], header.aabbMin[1], header.aabbMin[2]};
        result.aabbMax = {header.aabbMax[0], header.aabbMax[1], header.aabbMax[2]};
        result.ok = true;
        return result;
    }
} // namespace Horo::MeshBin
