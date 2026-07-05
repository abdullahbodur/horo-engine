/** @file BinaryMeshIoShared.h
 *  @brief Shared helpers for mesh-like binary payloads.
 */
#pragma once

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "math/Vec3.h"
#include "renderer/BinaryStream.h"

namespace Horo::BinaryMeshIoShared {
    /** @brief Computes AABB from vertex.position over @p vertices. */
    template <typename VertexT>
    void ComputePositionAabb(const std::vector<VertexT> &vertices, Vec3 *outMin,
                             Vec3 *outMax) {
        *outMin = {std::numeric_limits<float>::infinity(),
                   std::numeric_limits<float>::infinity(),
                   std::numeric_limits<float>::infinity()};
        *outMax = {-std::numeric_limits<float>::infinity(),
                   -std::numeric_limits<float>::infinity(),
                   -std::numeric_limits<float>::infinity()};
        for (const VertexT &vertex: vertices) {
            outMin->x = std::min(outMin->x, vertex.position.x);
            outMin->y = std::min(outMin->y, vertex.position.y);
            outMin->z = std::min(outMin->z, vertex.position.z);
            outMax->x = std::max(outMax->x, vertex.position.x);
            outMax->y = std::max(outMax->y, vertex.position.y);
            outMax->z = std::max(outMax->z, vertex.position.z);
        }
    }

    /** @brief Writes header + vertex/index arrays with consistent diagnostics. */
    template <typename HeaderT, typename VertexT>
    bool WriteHeaderVerticesIndices(std::ofstream &stream, const HeaderT &header,
                                    const std::vector<VertexT> &vertices,
                                    const std::vector<uint32_t> &indices,
                                    std::string_view context, std::string *errorOut) {
        const std::string prefix(context);
        if (!BinaryStream::WriteValue(stream, header)) {
            *errorOut = prefix + ": failed writing header.";
            return false;
        }
        if (!BinaryStream::WriteArray(stream, vertices.data(), vertices.size())) {
            *errorOut = prefix + ": failed writing vertex array.";
            return false;
        }
        if (!BinaryStream::WriteArray(stream, indices.data(), indices.size())) {
            *errorOut = prefix + ": failed writing index array.";
            return false;
        }
        return true;
    }

    /** @brief Reads vertex/index arrays from a header-declared payload. */
    template <typename HeaderT, typename VertexT>
    bool ReadVerticesIndices(std::ifstream &stream, const HeaderT &header,
                             std::vector<VertexT> &vertices,
                             std::vector<uint32_t> &indices,
                             std::string_view context, std::string *errorOut) {
        const std::string prefix(context);
        vertices.resize(header.vertexCount);
        if (!BinaryStream::ReadArray(stream, vertices.data(),
                                     static_cast<std::size_t>(header.vertexCount))) {
            *errorOut = prefix + ": failed reading vertex array.";
            return false;
        }
        indices.resize(header.indexCount);
        if (!BinaryStream::ReadArray(stream, indices.data(),
                                     static_cast<std::size_t>(header.indexCount))) {
            *errorOut = prefix + ": failed reading index array.";
            return false;
        }
        return true;
    }

    /** @brief Flushes stream and reports a format-scoped diagnostic on failure. */
    inline bool FlushStream(std::ofstream &stream, std::string_view context,
                            std::string *errorOut) {
        stream.flush();
        if (!stream.good()) {
            *errorOut = std::string(context) +
                        ": stream entered fail state during flush.";
            return false;
        }
        return true;
    }

    /** @brief Reads a trivial header POD after OpenForRead; sets a scoped diagnostic if truncated. */
    template <typename HeaderT>
    bool ReadHeaderBlob(std::ifstream &stream, HeaderT &header,
                        std::string_view formatName, std::string *errorOut) {
        if (!BinaryStream::ReadValue(stream, header)) {
            *errorOut = std::string(formatName) + ": file too short for header.";
            return false;
        }
        return true;
    }
} // namespace Horo::BinaryMeshIoShared
