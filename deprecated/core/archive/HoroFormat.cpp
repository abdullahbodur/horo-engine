/** @file HoroFormat.cpp
 *  @brief Validation and serialization implementations for the .horo format. */
#include "core/archive/HoroFormat.h"

#include <cstring>
#include <istream>
#include <ostream>

namespace Horo::Archive {
namespace {

/** @brief Check whether a header's magic bytes match the expected signature. */
inline bool ValidateMagic(const HoroHeader& header) noexcept {
    return std::memcmp(header.magic, kHoroMagic.data(), kHoroMagic.size()) == 0;
}

} // namespace

// ============================================================================
// Header I/O
// ============================================================================

/** @copydoc ReadHeader */
bool ReadHeader(std::istream& stream, HoroHeader& out_header) {
    if (!stream.read(reinterpret_cast<char*>(&out_header),
                     sizeof(HoroHeader))) {
        return false;
    }
    return stream.good();
}

/** @copydoc WriteHeader */
bool WriteHeader(std::ostream& stream, const HoroHeader& header) {
    stream.write(reinterpret_cast<const char*>(&header), sizeof(HoroHeader));
    return stream.good();
}

/** @copydoc ValidateHeader */
bool ValidateHeader(const HoroHeader& header) {
    if (!ValidateMagic(header)) {
        return false;
    }
    if (header.version != kHoroVersion) {
        return false;
    }
    if (header.toc_count > kMaxTocEntries) {
        return false;
    }
    if (header.toc_offset < sizeof(HoroHeader)) {
        return false;
    }
    if (header.data_offset < sizeof(HoroHeader) ||
        header.data_offset > header.toc_offset) {
        return false;
    }
    // Upper 4 bits of flags are reserved.
    if ((header.flags & 0xF0000000u) != 0) {
        return false;
    }
    return true;
}

// ============================================================================
// TOC I/O
// ============================================================================

/** @copydoc ReadTOC */
bool ReadTOC(std::istream& stream, uint32_t count,
             std::vector<TOCEntry>& out_entries) {
    if (count == 0) {
        out_entries.clear();
        return true;
    }
    if (count > kMaxTocEntries) {
        return false;
    }

    out_entries.resize(count);
    const auto bytes = static_cast<std::streamsize>(count * sizeof(TOCEntry));
    if (!stream.read(reinterpret_cast<char*>(out_entries.data()), bytes)) {
        return false;
    }

    // Per-entry validation.
    for (const auto& entry : out_entries) {
        // Non-zero hash requires non-zero sizes.
        if (entry.hash != 0) {
            if (entry.compressed_size == 0 || entry.uncompressed_size == 0) {
                return false;
            }
        }
        // crc32 field may be non-zero when HashCRC32 flag is set; we accept
        // any value here — verification is deferred to the caller. 
        // Upper 4 bits of per-entry flags are reserved.
        if ((entry.flags & 0xF0000000u) != 0) {
            return false;
        }
    }

    return stream.good();
}

/** @copydoc WriteTOC */
bool WriteTOC(std::ostream& stream, const std::vector<TOCEntry>& entries) {
    if (entries.empty()) {
        return true;
    }
    if (entries.size() > kMaxTocEntries) {
        return false;
    }

    const auto bytes = static_cast<std::streamsize>(entries.size() *
                                                    sizeof(TOCEntry));
    stream.write(reinterpret_cast<const char*>(entries.data()), bytes);
    return stream.good();
}

} // namespace Horo::Archive
