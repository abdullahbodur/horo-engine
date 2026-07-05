/** @file ArchiveBuilder.cpp
 *  @brief ArchiveBuilder implementation — writes .horo binary archives.
 */
#include "core/pipeline/ArchiveBuilder.h"
#include "core/pipeline/ArchiveFormat.h"
#include "core/pipeline/CryptoProvider.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <span>
#include <system_error>

namespace Horo::Build {
namespace {

/** @brief Writes a POD value to a stream in little-endian byte order. */
template <typename T>
void WriteLe(std::ostream &out, T value) {
    out.write(reinterpret_cast<const char *>(&value), sizeof(T));
}

/** @brief Allocates and zeroes a buffer with 16 extra bytes for AES-CTR IV. */
std::vector<uint8_t> MakeEncryptBuffer(uint32_t dataSize) {
    std::vector<uint8_t> buf(dataSize + kAesBlockSize, 0);
    return buf;
}

/** @brief Generates a temporary path next to the target. */
std::filesystem::path TempPath(const std::filesystem::path &target) {
    return std::filesystem::path(target.string() + ".tmp");
}

} // namespace

// ── PImpl ────────────────────────────────────────────────────────────────

struct ArchiveBuilder::Impl {
    std::vector<ArchiveAssetEntry> assets;
    ICryptoProvider *crypto = nullptr;
    bool encryptionEnabled = false;
    ProgressCallback progress;
};

// ── ArchiveBuilder ───────────────────────────────────────────────────────

/** @copydoc ArchiveBuilder::ArchiveBuilder */
ArchiveBuilder::ArchiveBuilder() : m_impl(std::make_unique<Impl>()) {}

/** @copydoc ArchiveBuilder::~ArchiveBuilder */
ArchiveBuilder::~ArchiveBuilder() = default;

/** @copydoc ArchiveBuilder::ArchiveBuilder */
ArchiveBuilder::ArchiveBuilder(ArchiveBuilder &&other) noexcept = default;

/** @copydoc ArchiveBuilder::operator= */
ArchiveBuilder &ArchiveBuilder::operator=(ArchiveBuilder &&other) noexcept = default;

/** @copydoc ArchiveBuilder::SetCryptoProvider */
void ArchiveBuilder::SetCryptoProvider(ICryptoProvider *provider) {
    m_impl->crypto = provider;
}

/** @copydoc ArchiveBuilder::SetEncryptionEnabled */
void ArchiveBuilder::SetEncryptionEnabled(bool enabled) {
    m_impl->encryptionEnabled = enabled;
}

/** @copydoc ArchiveBuilder::AddAsset */
void ArchiveBuilder::AddAsset(std::string_view path,
                               std::span<const uint8_t> data) {
    ArchiveAssetEntry entry;
    entry.path = std::string(path);
    entry.data.assign(data.begin(), data.end());
    m_impl->assets.push_back(std::move(entry));
}

/** @copydoc ArchiveBuilder::AddAsset */
void ArchiveBuilder::AddAsset(std::string_view path, std::string_view data) {
    const auto *bytes = reinterpret_cast<const uint8_t *>(data.data());
    AddAsset(path, std::span<const uint8_t>(bytes, data.size()));
}

/** @copydoc ArchiveBuilder::AssetCount */
uint32_t ArchiveBuilder::AssetCount() const {
    return static_cast<uint32_t>(m_impl->assets.size());
}

/** @copydoc ArchiveBuilder::Clear */
void ArchiveBuilder::Clear() {
    m_impl->assets.clear();
}

/** @copydoc ArchiveBuilder::SetProgressCallback */
void ArchiveBuilder::SetProgressCallback(ProgressCallback callback) {
    m_impl->progress = std::move(callback);
}

/** @copydoc ArchiveBuilder::WriteToFile */
bool ArchiveBuilder::WriteToFile(const std::filesystem::path &outputPath) {
    if (m_impl->assets.empty())
        return false;

    const bool useEncryption = m_impl->encryptionEnabled &&
                               m_impl->crypto &&
                               m_impl->crypto->HasKey();

    const auto tempPath = TempPath(outputPath);

    // Build TOC entries and compute data section layout
    struct PreparedEntry {
        TocEntry toc{};
        uint64_t rawDataOffset = 0; // Position in serialised data for this chunk
    };
    std::vector<PreparedEntry> prepared;
    prepared.reserve(m_impl->assets.size());

    uint64_t dataOffset = 0;
    for (const auto &asset : m_impl->assets) {
        PreparedEntry pe;
        pe.toc.pathHash = Fnv1a64(asset.path);
        pe.toc.originalSize = static_cast<uint32_t>(asset.data.size());
        pe.toc.contentHash = Fnv1a64(asset.data.data(),
                                      static_cast<uint64_t>(asset.data.size()));
        pe.toc.storedSize = pe.toc.originalSize;
        if (useEncryption)
            pe.toc.storedSize += kAesBlockSize; // IV overhead
        pe.toc.offset = dataOffset;
        pe.rawDataOffset = dataOffset;
        dataOffset += pe.toc.storedSize;
        prepared.push_back(pe);
    }

    const uint64_t tocEntryCount = static_cast<uint64_t>(prepared.size());
    const uint64_t tocSize = sizeof(uint32_t) + tocEntryCount * sizeof(TocEntry);
    const uint64_t tocOffset = kArchiveHeaderSize;
    const uint64_t actualDataOffset = tocOffset + tocSize;

    // Adjust TOC entry offsets relative to data section start
    for (auto &pe : prepared)
        pe.toc.offset += actualDataOffset;

    // Build header. Value-initialization is required: flags/reserved/KCV must
    // start as deterministic zeroes before optional encryption metadata is set.
    ArchiveHeader header{};
    header.magic[0] = 'H';
    header.magic[1] = 'O';
    header.magic[2] = 'R';
    header.magic[3] = 'O';
    header.version = 1;
    header.tocOffset = tocOffset;
    header.tocSize = tocSize;
    header.dataOffset = actualDataOffset;
    if (useEncryption) {
        header.flags |= static_cast<uint32_t>(ArchiveFlag::kEncrypted);
        m_impl->crypto->GenerateKcv(header.kcv);
    }

    // Write to temp file
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::out |
                                         std::ios::trunc);
        if (!out.good())
            return false;

        // ── Header ──
        out.write(reinterpret_cast<const char *>(&header), sizeof(header));
        if (!out.good())
            return false;

        // ── TOC ──
        WriteLe(out, static_cast<uint32_t>(tocEntryCount));
        if (!out.good())
            return false;

        for (const auto &pe : prepared) {
            WriteLe(out, pe.toc.pathHash);
            WriteLe(out, pe.toc.offset);
            WriteLe(out, pe.toc.storedSize);
            WriteLe(out, pe.toc.originalSize);
            WriteLe(out, pe.toc.contentHash);
        }
        if (!out.good())
            return false;

        // ── Data chunks ──
        uint64_t totalBytes = 0;
        for (size_t i = 0; i < m_impl->assets.size(); ++i) {
            const auto &asset = m_impl->assets[i];
            const uint32_t plainSize = static_cast<uint32_t>(asset.data.size());

            if (useEncryption) {
                // Encrypt: copy data into buffer with 16 extra bytes for IV
                std::vector<uint8_t> encBuf = MakeEncryptBuffer(plainSize);
                std::memcpy(encBuf.data() + kAesBlockSize, asset.data.data(),
                            plainSize);

                std::span<uint8_t> encSpan(encBuf);
                uint32_t encSize = 0;
                if (!m_impl->crypto->Encrypt(encSpan, encSize))
                    return false;

                // encSpan now has [IV(16) | ciphertext]
                out.write(reinterpret_cast<const char *>(encBuf.data()),
                          static_cast<std::streamsize>(encSize));
            } else {
                out.write(reinterpret_cast<const char *>(asset.data.data()),
                          static_cast<std::streamsize>(plainSize));
            }

            if (!out.good())
                return false;

            totalBytes += prepared[i].toc.storedSize;
            if (m_impl->progress)
                m_impl->progress(totalBytes, dataOffset);
        }

        out.close();
        if (!out.good())
            return false;
    }

    // Atomic rename: temp → target
    std::error_code ec;
    std::filesystem::rename(tempPath, outputPath, ec);
    if (ec) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    return true;
}

} // namespace Horo::Build
