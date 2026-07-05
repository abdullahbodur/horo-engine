/** @file ArchiveBuilder.h
 *  @brief Writes .horo asset archives from in-memory asset data.
 *
 *  ArchiveBuilder collects cooked asset buffers and serialises them into
 *  the .horo binary container.  Encryption is optional and delegated to
 *  an ICryptoProvider instance.
 *
 *  Usage:
 *  @code
 *  ArchiveBuilder builder;
 *  builder.SetCryptoProvider(crypto);
 *  builder.AddAsset("scenes/level.bin", sceneData);
 *  builder.AddAsset("models/bad_guy.mesh", meshData);
 *  builder.WriteToFile("data.horo");
 *  @endcode
 *
 *  Thread safety: Not thread-safe.  One builder per archive.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Horo::Build {

class ICryptoProvider;

/**
 * @brief A single asset entry queued for inclusion in an archive.
 */
struct ArchiveAssetEntry {
    std::string path;           ///< Logical asset path (e.g. "scenes/level.bin").
    std::vector<uint8_t> data;  ///< Cooked asset bytes.
};

/**
 * @brief Builds a .horo asset archive from in-memory asset entries.
 *
 * Writes the archive atomically: data is written to a temporary file,
 * then renamed to the target path.  On failure the target is left untouched.
 */
class ArchiveBuilder {
public:
    /** @brief Constructs an empty builder with no assets and no crypto provider. */
    ArchiveBuilder();

    /** @brief Destroys the builder and releases all internal resources. */
    ~ArchiveBuilder();

    /** @name Copy and move
     *  @{ */
    // Non-copyable.
    ArchiveBuilder(const ArchiveBuilder &) = delete;
    ArchiveBuilder &operator=(const ArchiveBuilder &) = delete;

    /** @brief Move-constructs the builder, transferring ownership of the
     *         internal PImpl state. */
    ArchiveBuilder(ArchiveBuilder &&) noexcept;

    /** @brief Move-assigns the builder, transferring ownership of the
     *         internal PImpl state. */
    ArchiveBuilder &operator=(ArchiveBuilder &&) noexcept;
    /** @} */

    /**
     * @brief Sets the crypto provider for encryption.
     *
     * If set and the provider has a valid key, every data chunk is encrypted
     * before writing.  The archive header sets the kEncrypted flag.
     *
     * @param provider Non-null, non-owning pointer.  Must outlive WriteToFile().
     */
    void SetCryptoProvider(ICryptoProvider *provider);

    /**
     * @brief Enables or disables encryption.
     *
     * When disabled, chunks are stored as plaintext even if a crypto provider
     * is set.  Useful for development builds.
     */
    void SetEncryptionEnabled(bool enabled);

    /**
     * @brief Adds an asset to the archive.
     *
     * The asset data is copied into the builder.  The path is hashed with
     * FNV-1a for TOC lookup.  Assets are written in the order they are added.
     *
     * @param path Logical asset path (e.g. "scenes/level.bin").
     * @param data Raw cooked asset bytes.
     */
    void AddAsset(std::string_view path, std::span<const uint8_t> data);

    /**
     * @brief Adds an asset from a string view.
     * @param path Logical asset path.
     * @param data String data (convenience overload).
     */
    void AddAsset(std::string_view path, std::string_view data);

    /**
     * @brief Number of assets currently queued.
     */
    uint32_t AssetCount() const;

    /**
     * @brief Discards all queued assets.
     */
    void Clear();

    /**
     * @brief Writes the archive to the given file path.
     *
     * Atomic: writes to a temp file first, then renames.  On failure the
     * target file is not modified.
     *
     * @param outputPath Destination file path (e.g. "build/release/data.horo").
     * @return True if the archive was written successfully.
     */
    bool WriteToFile(const std::filesystem::path &outputPath);

    /**
     * @brief Progress callback type.
     * @param bytesWritten Number of bytes written so far.
     * @param totalBytes Total bytes to write (0 if unknown).
     */
    using ProgressCallback = std::function<void(uint64_t bytesWritten, uint64_t totalBytes)>;

    /**
     * @brief Sets an optional progress callback invoked during WriteToFile().
     */
    void SetProgressCallback(ProgressCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl; ///< Owns hidden implementation details.
};

} // namespace Horo::Build
