/** @file Packager.h
 *  @brief Archive packer/unpacker for the .horo asset container format.
 *
 *  The Packager class provides a self-contained, GUI-free pipeline for
 *  creating and reading .horo archives.  It is designed to be usable from
 *  both the `horopak` CLI tool and automated build/cook pipelines without
 *  pulling in any editor, rendering, or window-system dependencies.
 *
 *  Typical pack workflow:
 *  @code
 *  Horo::Archive::Packager packer;
 *  packer.AddAsset("textures/hero.dds", textureBytes);
 *  packer.AddAsset("meshes/hero.glb", meshBytes);
 *  if (!packer.Write("bundle.horo")) {
 *      // handle error
 *  }
 *  @endcode
 *
 *  Typical unpack workflow:
 *  @code
 *  Horo::Archive::Packager packer;
 *  if (!packer.Open("bundle.horo")) {
 *      // handle error
 *  }
 *  std::vector<uint8_t> data;
 *  packer.Extract("textures/hero.dds", data);
 *  @endcode
 */
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Horo::Archive {

// Forward declarations from HoroFormat.h
struct HoroHeader;
struct TOCEntry;

/** @brief Result codes for Packager operations.
 *
 *  Every operation that can fail returns a PackResult.  Success is
 *  PackResult::Ok; all other values describe a specific failure mode. */
enum class PackResult {
    /** Operation completed successfully. */
    Ok = 0,

    /** The input data is empty or malformed. */
    InvalidInput,

    /** An asset path is empty or contains invalid characters. */
    InvalidPath,

    /** Archive magic bytes did not match 'HORO'. */
    InvalidMagic,

    /** Archive version is unsupported. */
    UnsupportedVersion,

    /** LZ4 compression failed (internal library error). */
    CompressionFailed,

    /** LZ4 decompression failed (corrupt or truncated chunk). */
    DecompressionFailed,

    /** AES encryption/decryption failed (wrong key, corrupt data). */
    EncryptionFailed,

    /** XXH64 hash mismatch — data integrity check failed. */
    HashMismatch,

    /** A filesystem I/O error occurred (file not found, permission denied). */
    IoError,

    /** The archive TOC is malformed (duplicate hash, invalid offsets). */
    InvalidTOC,
};

/** @brief Callback type for providing asset data during packing.
 *
 *  The packager calls this function for each asset path registered via
 *  AddAsset().  The implementation must populate `out_data` with the raw
 *  (uncompressed, unencrypted) asset bytes and return true.  Returning
 *  false aborts the pack operation.
 *
 *  @param asset_path  The path string passed to AddAsset().
 *  @param out_data    Vector to fill with the asset's raw bytes.
 *  @return true if the data was successfully provided. */
using AssetDataProvider =
    std::function<bool(const std::string& asset_path,
                       std::vector<uint8_t>& out_data)>;

/** @brief Archive packer / unpacker for .horo files.
 *
 *  Owns no GUI state and depends on no editor or window-system libraries.
 *  Compression and hashing are handled through abstracted internal
 *  implementations so that the class can be compiled with different backends
 *  (e.g. LZ4 + XXH64 in the reference implementation) or stubbed for
 *  platforms where those libraries are unavailable.
 *
 *  The class is move-only. */
class Packager {
public:
    /** @brief Construct an empty packager with default settings.
     *
     *  Compression: enabled (LZ4, level 1).
     *  Encryption:  disabled. */
    Packager();

    /** @brief Destructor. */
    ~Packager(); // NOSONAR

    /** @name Move semantics */
    ///@{
    Packager(Packager&& other) noexcept;
    Packager& operator=(Packager&& other) noexcept;
    ///@}

    // Non-copyable.
    Packager(const Packager&) = delete;
    Packager& operator=(const Packager&) = delete;

    // ====================================================================
    // Packing (write path)
    // ====================================================================

    /** @brief Register an asset for inclusion in the next Write() call.
     *
     *  The path serves as the lookup key when unpacking.  It is hashed with
     *  XXH64 to produce the TOC index.  The actual data is not read until
     *  Write() calls the registered AssetDataProvider.
     *
     *  @param path  Logical asset path (e.g. "textures/hero.dds").  Must be
     *               non-empty and must not contain null bytes.
     *  @return PackResult::Ok on success. */
    PackResult AddAsset(const std::string& path);

    /** @brief Write all registered assets to a .horo archive.
     *
     *  Calls the AssetDataProvider for each registered path, compresses and
     *  (optionally) encrypts the data, builds the TOC, and writes everything
     *  to disk.  The archive is written atomically: data is staged to a
     *  temporary file and renamed on success.
     *
     *  @param output_path  Filesystem path for the output .horo file.
     *  @param provider     Callback that supplies raw asset data.
     *  @return PackResult::Ok on success. */
    PackResult Write(const std::string& output_path,
                     const AssetDataProvider& provider) const;

    /** @brief Discard all pending assets without writing. */
    void Clear();

    /** @brief Number of assets currently registered. */
    size_t AssetCount() const;

    // ====================================================================
    // Unpacking (read path)
    // ====================================================================

    /** @brief Open and validate a .horo archive for reading.
     *
     *  Reads the header, validates magic/version/offsets, loads the TOC,
     *  and performs a quick integrity pass (no decompression).
     *
     *  @param archive_path  Filesystem path to the .horo file.
     *  @return PackResult::Ok on success. */
    PackResult Open(const std::string& archive_path);

    /** @brief Extract a single asset by its original path.
     *
     *  @param asset_path  The path string used when the asset was packed.
     *  @param out_data    Vector to fill with the decompressed (and
     *                     decrypted, if applicable) asset bytes.
     *  @return PackResult::Ok on success. */
    PackResult Extract(const std::string& asset_path,
                       std::vector<uint8_t>& out_data);

    /** @brief Extract all assets to a directory on disk.
     *
     *  Creates subdirectories as needed.  Existing files are overwritten.
     *
     *  @param output_dir  Directory to write assets into.
     *  @return PackResult::Ok on success. */
    PackResult ExtractAll(const std::string& output_dir);

    /** @brief List all asset paths contained in the currently-open archive.
     *
     *  Must be called after a successful Open().
     *
     *  @param out_paths  Vector to fill with asset path strings.
     *  @return PackResult::Ok on success, or InvalidInput if no archive
     *          is open. */
    PackResult ListAssets(std::vector<std::string>& out_paths) const;

    /** @brief True after a successful Open() call. */
    bool IsOpen() const;

    /** @brief Enable or disable SHA-256 hash verification.
     *
     *  When enabled, a SHA-256 hash block is written after the TOC during
     *  Write() and verified on every Extract() call.  CRC32 is always
     *  computed (zero runtime cost).
     *
     *  @note SHA-256 adds 32 bytes per chunk to the archive size and
     *        moderate CPU cost during both pack and unpack.
     *  @param enabled  Default: false. */
    void SetSHA256Enabled(bool enabled);

    // ====================================================================
    // Configuration (must be set before Write())
    // ====================================================================

    /** @brief Set the LZ4 compression level.
     *
     *  @param level  1 (fastest) to 12 (smallest).  Values outside this
     *                range are clamped.  Default: 1.
     *
     *  Set to 0 to disable compression entirely (chunks stored as-is). */
    void SetCompressionLevel(int level);

    /** @brief Enable or disable AES-256-CTR encryption.
     *
     *  When enabled, every data chunk is encrypted with the key set via
     *  SetEncryptionKey().  The archive header's Encrypted flag is set.
     *
     *  @note Encryption adds ~16 bytes (nonce) per chunk overhead.
     *  @param enabled  Default: false. */
    void SetEncryptionEnabled(bool enabled);

    /** @brief Set the 256-bit AES encryption key.
     *
     *  Must be called before Write() when encryption is enabled.
     *
     *  @param key  32 bytes of key material. */
    void SetEncryptionKey(const std::array<uint8_t, 32>& key);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Horo::Archive
