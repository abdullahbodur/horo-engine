/**
 *  @file BinaryStream.h
 *  @brief Byte-oriented read/write helpers shared by the engine's binary asset formats.
 *
 *  The engine ships several flat, host-endian binary formats (`.mesh.bin`,
 *  `.skinned.bin`, `.anim.bin`). Their writers/readers all need the same
 *  primitive: serialise a typed value or a contiguous typed array as a raw
 *  byte run on top of `std::ofstream` / `std::ifstream`.
 *
 *  The helpers below centralise that primitive in one place. Each
 *  per-format unit (e.g. @ref Horo::MeshBin) layers its own header + payload
 *  logic on top.
 *
 *  The byte buffer is typed as @c std::span<const std::byte> /
 *  @c std::span<std::byte> so the byte-oriented intent is explicit at the
 *  boundary. Pointer interconversion uses @c std::as_bytes /
 *  @c std::as_writable_bytes (which take @c std::span and return a
 *  @c std::byte view) for value-side conversions, and a static-cast chain
 *  through @c void* for the unavoidable iostream `char_type` interface —
 *  no @c reinterpret_cast is needed.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <vector>

namespace Horo::BinaryStream {
    namespace detail {
        /// Static-cast pointer interconversion through @c void* — the standards-blessed
        /// alternative to @c reinterpret_cast for taking a stream's @c char_type view
        /// over a byte buffer.
        inline const char *AsCharPointer(const std::byte *bytes) {
            return static_cast<const char *>(static_cast<const void *>(bytes));
        }

        inline char *AsCharPointer(std::byte *bytes) {
            return static_cast<char *>(static_cast<void *>(bytes));
        }
    } // namespace detail

    /**
     *  @brief Writes the entire byte run @p bytes to @p stream.
     *  @return @c true on success, @c false on stream failure.
     */
    inline bool WriteRaw(std::ofstream &stream, std::span<const std::byte> bytes) {
        stream.write(detail::AsCharPointer(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        return stream.good();
    }

    /**
     *  @brief Reads exactly @p bytes.size() bytes from @p stream into @p bytes.
     *  @return @c true on success, @c false on stream failure / short read.
     */
    inline bool ReadRaw(std::ifstream &stream, std::span<std::byte> bytes) {
        stream.read(detail::AsCharPointer(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        return stream.good();
    }

    /**
     *  @brief Writes one trivially-copyable @p value as @c sizeof(T) bytes.
     *
     *  Use for headers and POD scalars. The value is written host-endian.
     */
    template <typename T>
    bool WriteValue(std::ofstream &stream, const T &value) {
        return WriteRaw(stream, std::as_bytes(std::span(&value, 1)));
    }

    /**
     *  @brief Reads @c sizeof(T) bytes into @p out.
     *  @return @c true on success, @c false on stream failure.
     */
    template <typename T>
    bool ReadValue(std::ifstream &stream, T &out) {
        return ReadRaw(stream, std::as_writable_bytes(std::span(&out, 1)));
    }

    /**
     *  @brief Writes a contiguous array of @p count trivially-copyable elements.
     *  @return @c true on success or when @p count is 0.
     */
    template <typename T>
    bool WriteArray(std::ofstream &stream, const T *data, std::size_t count) {
        if (count == 0)
            return stream.good();
        return WriteRaw(stream, std::as_bytes(std::span(data, count)));
    }

    /**
     *  @brief Reads a contiguous array of @p count trivially-copyable elements into @p data.
     *  @return @c true on success or when @p count is 0.
     */
    template <typename T>
    bool ReadArray(std::ifstream &stream, T *data, std::size_t count) {
        if (count == 0)
            return stream.good();
        return ReadRaw(stream, std::as_writable_bytes(std::span(data, count)));
    }

    /**
     *  @brief Writes a length-prefixed `std::vector<float>` (uint32 count then floats).
     */
    inline bool WriteFloatVector(std::ofstream &stream, const std::vector<float> &v) {
        if (const auto count = static_cast<std::uint32_t>(v.size());
            !WriteValue(stream, count))
            return false;
        return WriteArray(stream, v.data(), v.size());
    }

    /**
     *  @brief Reads a length-prefixed `std::vector<float>` produced by @ref WriteFloatVector.
     */
    inline bool ReadFloatVector(std::ifstream &stream, std::vector<float> &out) {
        std::uint32_t count = 0;
        if (!ReadValue(stream, count))
            return false;
        out.resize(count);
        return ReadArray(stream, out.data(), out.size());
    }
} // namespace Horo::BinaryStream
