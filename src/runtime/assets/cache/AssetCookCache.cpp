/**
 * @copydoc AssetCookCache.h
 */

#include "Horo/Assets/AssetCookCache.h"

#include "Horo/Foundation/Sha256.h"
#include "../AssetErrors.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Assets
{
namespace
{

// ---------------------------------------------------------------------------
// Little-endian write helpers for building the canonical cache key pre-image.
// ---------------------------------------------------------------------------

/**
 * @brief Appends a little-endian u32 to the output buffer.
 */
void AppendLE32(std::vector<std::uint8_t> &out, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
}

/**
 * @brief Appends a length-delimited string to the output buffer:
 *        u32 LE byte count, then UTF-8 bytes.
 */
void AppendDelimited(std::vector<std::uint8_t> &out, std::string_view text)
{
    AppendLE32(out, static_cast<std::uint32_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}

/**
 * @brief Appends raw bytes (fixed-size fields like digests).
 */
void AppendBytes(std::vector<std::uint8_t> &out, std::span<const std::uint8_t> bytes)
{
    out.insert(out.end(), bytes.begin(), bytes.end());
}

/**
 * @brief Appends a 16-byte AssetId.
 */
void AppendId(std::vector<std::uint8_t> &out, const AssetId &id)
{
    const auto &b = id.Bytes();
    AppendBytes(out, std::span{b.data(), b.size()});
}

/**
 * @brief Appends a 32-byte Sha256Digest.
 */
void AppendDigest(std::vector<std::uint8_t> &out, const Sha256Digest &digest)
{
    AppendBytes(out, std::span{digest.bytes.data(), digest.bytes.size()});
}

/**
 * @brief Formats two hex digits from a byte.
 */
constexpr char HexNibble(std::uint8_t nibble) noexcept
{
    return static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
}

/**
 * @brief Formats a digest as lowercase hex for filesystem paths.
 */
std::string FormatHex(const Sha256Digest &digest)
{
    std::string result(64, '\0');
    for (std::size_t i = 0; i < 32; ++i)
    {
        result[i * 2] = HexNibble(digest.bytes[i] >> 4);
        result[i * 2 + 1] = HexNibble(digest.bytes[i] & 0x0F);
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// BuildAssetCookCacheKey
// ---------------------------------------------------------------------------

AssetCookCacheKey BuildAssetCookCacheKey(const AssetCookCacheKeyInputs &inputs)
{
    // Canonical cache key pre-image format (length-delimited, LE):
    //
    //   "horo.asset.cook-cache.v1\0"  (domain tag + NUL)
    //   AssetId bytes                  (16 bytes)
    //   u32 asset type length + bytes  (length-delimited)
    //   source digest                  (32 bytes)
    //   u32 metadata schema version    (4 bytes LE)
    //   metadata digest                (32 bytes)
    //   u32 cooker ID length + bytes   (length-delimited)
    //   u32 cooker version len + bytes (length-delimited)
    //   u32 target length + bytes      (length-delimited)
    //   u32 target profile length + 0  (absent profile: 4 zero bytes)
    //   u32 settings schema version    (4 bytes LE)
    //   settings digest                (32 bytes)
    //   u32 artifact format version    (4 bytes LE)

    std::vector<std::uint8_t> preimage;
    constexpr std::string_view kDomainTag = "horo.asset.cook-cache.v1";
    preimage.insert(preimage.end(), kDomainTag.begin(), kDomainTag.end());
    preimage.push_back(0x00); // NUL terminator

    AppendId(preimage, inputs.assetId);
    AppendDelimited(preimage, inputs.assetType.Value());
    AppendDigest(preimage, inputs.sourceDigest);
    AppendLE32(preimage, inputs.metadataSchemaVersion);
    AppendDigest(preimage, inputs.metadataDigest);
    AppendDelimited(preimage, inputs.cookerContributionId);
    AppendDelimited(preimage, inputs.cookerVersion);
    AppendDelimited(preimage, inputs.target.Value());
    // absent profile: zero-length
    AppendLE32(preimage, 0);
    AppendLE32(preimage, inputs.settingsSchemaVersion);
    AppendDigest(preimage, inputs.settingsDigest);
    AppendLE32(preimage, inputs.artifactFormatVersion);

    return AssetCookCacheKey{ComputeSha256(std::as_bytes(std::span{preimage}))};
}

// ---------------------------------------------------------------------------
// AssetCookCache
// ---------------------------------------------------------------------------

AssetCookCache::AssetCookCache(std::filesystem::path root, AssetCookLimits limits)
    : root_(std::move(root)), limits_(limits)
{
    std::filesystem::create_directories(root_);
}

std::filesystem::path AssetCookCache::PathForKey(const Sha256Digest &digest) const
{
    const auto hex = FormatHex(digest);
    // <root>/<first-two-hex>/<remaining-hex>.cooked
    return root_ / hex.substr(0, 2) / (hex.substr(2) + ".cooked");
}

Result<std::optional<std::vector<std::uint8_t>>> AssetCookCache::Load(
    const AssetCookCacheKey &key, const CancellationToken &cancellation) const
{
    if (cancellation.IsCancellationRequested())
        return Result<std::optional<std::vector<std::uint8_t>>>::Failure(
            Error{CookErrors::Cancelled.code});

    const auto path = PathForKey(key.digest);

    if (!std::filesystem::exists(path))
        return Result<std::optional<std::vector<std::uint8_t>>>::Success(std::nullopt);

    if (std::filesystem::is_symlink(path))
        return Result<std::optional<std::vector<std::uint8_t>>>::Failure(
            Error{CookErrors::MalformedArtifact.code});

    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec)
        return Result<std::optional<std::vector<std::uint8_t>>>::Failure(
            Error{CookErrors::MalformedArtifact.code});

    if (fileSize > limits_.maximumArtifactBytes)
        return Result<std::optional<std::vector<std::uint8_t>>>::Failure(
            Error{CookErrors::TooLarge.code});

    std::ifstream file(path, std::ios::binary);
    if (!file)
        return Result<std::optional<std::vector<std::uint8_t>>>::Failure(
            Error{CookErrors::MalformedArtifact.code});

    std::vector<std::uint8_t> bytes(fileSize);
    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(fileSize));

    if (!file || file.gcount() != static_cast<std::streamsize>(fileSize))
    {
        // Truncated or partial read
        return Result<std::optional<std::vector<std::uint8_t>>>::Failure(
            Error{CookErrors::MalformedArtifact.code});
    }

    return Result<std::optional<std::vector<std::uint8_t>>>::Success(std::move(bytes));
}

Result<void> AssetCookCache::Store(const AssetCookCacheKey &key,
                                   std::span<const std::uint8_t> artifact,
                                   const CancellationToken &cancellation)
{
    if (cancellation.IsCancellationRequested())
        return Result<void>::Failure(Error{CookErrors::Cancelled.code});

    if (artifact.size() > limits_.maximumArtifactBytes)
        return Result<void>::Failure(Error{CookErrors::TooLarge.code});

    const auto targetPath = PathForKey(key.digest);

    // If the key path already exists and is not a symlink, verify content.
    if (std::filesystem::exists(targetPath))
    {
        if (std::filesystem::is_symlink(targetPath))
            return Result<void>::Failure(Error{CookErrors::MalformedArtifact.code});

        // Read existing bytes and compare
        std::error_code ec;
        const auto existingSize = std::filesystem::file_size(targetPath, ec);
        if (ec || existingSize != static_cast<std::uint64_t>(artifact.size()))
        {
            // Size mismatch — existing entry is stale/corrupt; this is not a benign collision.
            // In a full implementation this would be a typed cache-corruption error.
            // For V1: treat as existing content conflict, return success (entry already there).
            return Result<void>::Failure(Error{CookErrors::DuplicateCooker.code});
        }

        std::ifstream existing(targetPath, std::ios::binary);
        if (!existing)
            return Result<void>::Failure(Error{CookErrors::MalformedArtifact.code});

        std::vector<std::uint8_t> existingBytes(existingSize);
        existing.read(reinterpret_cast<char *>(existingBytes.data()),
                      static_cast<std::streamsize>(existingSize));

        if (!existing || existing.gcount() != static_cast<std::streamsize>(existingSize))
            return Result<void>::Failure(Error{CookErrors::MalformedArtifact.code});

        // Constant-time-ish byte comparison
        if (existingBytes.size() == artifact.size() &&
            std::memcmp(existingBytes.data(), artifact.data(), artifact.size()) == 0)
        {
            return Result<void>::Success(); // Identical content already stored
        }
        // Content differs — collision under same key. V1: treat as existing entry wins.
        return Result<void>::Failure(Error{CookErrors::DuplicateCooker.code});
    }

    // Write to a unique temporary file in the parent directory.
    std::filesystem::create_directories(targetPath.parent_path());

    auto tempPath = targetPath;
    tempPath += ".tmp." + std::to_string(
                              std::chrono::steady_clock::now().time_since_epoch().count());

    {
        std::ofstream temp(tempPath, std::ios::binary | std::ios::trunc);
        if (!temp)
            return Result<void>::Failure(Error{CookErrors::MalformedArtifact.code});

        temp.write(reinterpret_cast<const char *>(artifact.data()),
                   static_cast<std::streamsize>(artifact.size()));
        if (!temp)
        {
            std::filesystem::remove(tempPath);
            return Result<void>::Failure(Error{CookErrors::MalformedArtifact.code});
        }
    }

    // Atomic rename: if another writer won, targetPath will already exist.
    std::error_code renameEc;
    std::filesystem::rename(tempPath, targetPath, renameEc);

    if (renameEc)
    {
        // Another writer won the race. Verify existing bytes match.
        std::filesystem::remove(tempPath);

        if (std::filesystem::exists(targetPath) && !std::filesystem::is_symlink(targetPath))
        {
            std::error_code sizeEc;
            const auto existingSize = std::filesystem::file_size(targetPath, sizeEc);
            if (!sizeEc)
            {
                std::ifstream existing(targetPath, std::ios::binary);
                std::vector<std::uint8_t> existingBytes(existingSize);
                existing.read(reinterpret_cast<char *>(existingBytes.data()),
                              static_cast<std::streamsize>(existingSize));

                if (existing && existing.gcount() == static_cast<std::streamsize>(existingSize) &&
                    existingBytes.size() == artifact.size() &&
                    std::memcmp(existingBytes.data(), artifact.data(), artifact.size()) == 0)
                {
                    return Result<void>::Success();
                }
            }
        }
        return Result<void>::Failure(Error{CookErrors::MalformedArtifact.code});
    }

    return Result<void>::Success();
}

} // namespace Horo::Assets
