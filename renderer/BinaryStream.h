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
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
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
        static_assert(std::is_trivially_copyable_v<T>,
                      "BinaryStream::WriteValue requires trivially copyable T.");
        return WriteRaw(stream, std::as_bytes(std::span(&value, 1)));
    }

    /**
     *  @brief Reads @c sizeof(T) bytes into @p out.
     *  @return @c true on success, @c false on stream failure.
     */
    template <typename T>
    bool ReadValue(std::ifstream &stream, T &out) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "BinaryStream::ReadValue requires trivially copyable T.");
        return ReadRaw(stream, std::as_writable_bytes(std::span(&out, 1)));
    }

    /**
     *  @brief Writes a contiguous array of @p count trivially-copyable elements.
     *  @return @c true on success or when @p count is 0.
     */
    template <typename T>
    bool WriteArray(std::ofstream &stream, const T *data, std::size_t count) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "BinaryStream::WriteArray requires trivially copyable T.");
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
        static_assert(std::is_trivially_copyable_v<T>,
                      "BinaryStream::ReadArray requires trivially copyable T.");
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
        if (count > 16'777'216) // 64 MiB worth of floats — reject obviously corrupt lengths.
            return false;
        out.resize(count);
        return ReadArray(stream, out.data(), out.size());
    }

    /**
     *  @brief Writes a uint32 length prefix followed by raw string bytes.
     */
    inline bool WriteLengthPrefixedString(std::ofstream &stream, std::string_view value) {
        if (const auto length = static_cast<std::uint32_t>(value.size());
            !WriteValue(stream, length))
            return false;
        return value.empty() || WriteArray(stream, value.data(), value.size());
    }

    /**
     *  @brief Reads a uint32-prefixed string written by @ref WriteLengthPrefixedString.
     */
    inline bool ReadLengthPrefixedString(std::ifstream &stream, std::string &out) {
        std::uint32_t length = 0;
        if (!ReadValue(stream, length))
            return false;
        if (length > 16'777'216) // 16 MiB — reject obviously corrupt lengths.
            return false;
        out.resize(length);
        return length == 0 || ReadArray(stream, out.data(), out.size());
    }

    /**
     *  @brief Opens @p destPath for binary writing, creating parent directories as needed.
     *
     *  On failure, sets @p errorOut to a descriptive message prefixed with
     *  @p errorContext (e.g. @c "MeshBin write") and returns a closed @ref std::ofstream.
     *  The returned stream is convertible to @c bool via @c stream.is_open().
     *
     *  @param destPath      Filesystem path the stream should be opened on.
     *  @param errorContext  Per-format prefix used when filling @p errorOut.
     *  @param errorOut      Output error buffer; must remain alive for the call duration.
     */
    inline std::ofstream OpenForWrite(const std::filesystem::path &destPath,
                                       std::string_view errorContext,
                                       std::string *errorOut) {
        if (destPath.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(destPath.parent_path(), ec);
            if (ec) {
                *errorOut = std::string(errorContext) +
                            ": cannot create destination directory: " + ec.message();
                return std::ofstream{};
            }
        }
        std::ofstream stream(destPath, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
            *errorOut = std::string(errorContext) + ": cannot open destination file.";
        return stream;
    }

    /**
     *  @brief Opens @p sourcePath for binary reading.
     *
     *  Validates that the path is a regular file before opening; on failure
     *  sets @p errorOut to a descriptive message prefixed with @p errorContext
     *  (e.g. @c "MeshBin read") and returns a closed @ref std::ifstream.
     */
    inline std::ifstream OpenForRead(const std::filesystem::path &sourcePath,
                                      std::string_view errorContext,
                                      std::string *errorOut) {
        if (std::error_code ec;
            !std::filesystem::is_regular_file(sourcePath, ec) || ec) {
            *errorOut = std::string(errorContext) + ": source path is not a regular file.";
            return std::ifstream{};
        }
        std::ifstream stream(sourcePath, std::ios::binary);
        if (!stream.is_open())
            *errorOut = std::string(errorContext) + ": cannot open source file.";
        return stream;
    }

    /// Header concept: any aggregate exposing 3-element float arrays @c aabbMin / @c aabbMax.
    template <typename HeaderT, typename Vec3T>
    void StoreAabbToHeader(HeaderT &header, const Vec3T &aabbMin, const Vec3T &aabbMax) {
        header.aabbMin = {aabbMin.x, aabbMin.y, aabbMin.z};
        header.aabbMax = {aabbMax.x, aabbMax.y, aabbMax.z};
    }

    /// Reads back the @c aabbMin / @c aabbMax arrays as a Vec3 pair.
    template <typename Vec3T, typename HeaderT>
    void LoadAabbFromHeader(const HeaderT &header, Vec3T &outMin, Vec3T &outMax) {
        outMin = {header.aabbMin[0], header.aabbMin[1], header.aabbMin[2]};
        outMax = {header.aabbMax[0], header.aabbMax[1], header.aabbMax[2]};
    }
} // namespace Horo::BinaryStream
