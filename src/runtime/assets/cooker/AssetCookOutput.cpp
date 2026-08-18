/**
 * @copydoc AssetCookOutput.h
 */

#include "Horo/Assets/AssetCookOutput.h"

#include "../AssetErrors.h"
#include "Horo/Foundation/Sha256.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace Horo::Assets {
    namespace {
        /**
         * @brief Formats a SHA-256 digest into a 64-character lowercase hex string.
         */
        [[nodiscard]] std::string HexEncodeSha256(const Sha256Digest &digest) {
            std::string result;
            result.reserve(64);
            for (const auto byte : digest.bytes)
                result += std::format("{:02x}", byte);
            return result;
        }

        /**
         * @brief Produces the canonical manifest JSON text.
         *        Schema: {"schemaVersion":1,"target":"...","artifacts":[...]}
         *        Sorted deterministically by assetId in the entries array.
         */
        std::string BuildManifestJson(std::string_view target, std::span<const AssetCookManifestEntry> entries) {
            // Manual JSON construction to avoid nlohmann dependency.
            // Format: compact JSON with no trailing whitespace, fixed ordering.
            std::ostringstream json;
            json << R"({"schemaVersion":1,"target":")" << target << R"(","artifacts":[)";

            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (i > 0)
                    json << ',';
                const auto &e = entries[i];
                json << R"({"assetId":")" << e.assetId.ToString() << R"(",)"
                     << R"("assetType":")" << e.assetType.Value() << R"(",)"
                     << R"("artifact":")" << e.artifactFile << R"(",)"
                     << R"("artifactHash":"sha256:)" << HexEncodeSha256(e.artifactHash) << R"("})";
            }

            json << "]}";
            return json.str();
        }

        /**
         * @brief Reads the full contents of a file into a byte vector.
         */
        Result<std::vector<std::uint8_t>> ReadFile(const std::filesystem::path &path, std::size_t maxBytes) {
            std::error_code ec;
            const auto fileSize = std::filesystem::file_size(path, ec);
            if (ec || fileSize > maxBytes)
                return Result<std::vector<std::uint8_t>>::Failure(Error{CookErrors::TooLarge.code});

            std::ifstream file(path, std::ios::binary);
            if (!file)
                return Result<std::vector<std::uint8_t>>::Failure(Error{CookErrors::MalformedArtifact.code});

            std::vector<std::uint8_t> bytes(fileSize);
            file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(fileSize));
            if (!file || file.gcount() != static_cast<std::streamsize>(fileSize))
                return Result<std::vector<std::uint8_t>>::Failure(Error{CookErrors::MalformedArtifact.code});

            return Result<std::vector<std::uint8_t>>::Success(std::move(bytes));
        }

        /**
         * @brief Writes bytes atomically: write to temp, then rename.
         */
        Result<void> WriteAtomic(const std::filesystem::path &path, std::span<const std::uint8_t> bytes) {
            auto tempPath = path;
            tempPath += std::format(".tmp.{}", std::chrono::steady_clock::now().time_since_epoch().count());

            {
                std::ofstream temp(tempPath, std::ios::binary | std::ios::trunc);
                if (!temp) {
                    std::filesystem::remove(tempPath);
                    return Result<void>::Failure(Error{CookErrors::MalformedArtifact.code});
                }
                temp.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                if (!temp) {
                    std::filesystem::remove(tempPath);
                    return Result<void>::Failure(Error{CookErrors::MalformedArtifact.code});
                }
            }

            std::error_code ec;
            std::filesystem::rename(tempPath, path, ec);
            if (ec) {
                std::filesystem::remove(tempPath);
                return Result<void>::Failure(Error{CookErrors::MalformedArtifact.code});
            }
            return Result<void>::Success();
        }

        // ---------------------------------------------------------------------------
        // Simple manual JSON parsing for current.json (no nlohmann dependency)
        // ---------------------------------------------------------------------------

        /**
         * @brief Extracts a JSON string value for a given key from flat JSON.
         *        Very limited parser: handles {"key":"value",...} only. No nesting.
         */
        std::string JsonStringValue(std::string_view json, std::string_view key) {
            auto searchKey = std::string("\"") + std::string(key) + "\":\"";
            auto pos = json.find(searchKey);
            if (pos == std::string_view::npos)
                return {};

            pos += searchKey.size();
            auto end = json.find('"', pos);
            if (end == std::string_view::npos)
                return {};

            return std::string(json.substr(pos, end - pos));
        }
    }  // namespace

    // ---------------------------------------------------------------------------
    // ResolveCurrentCookGeneration
    // ---------------------------------------------------------------------------

    Result<AssetCookGeneration> ResolveCurrentCookGeneration(const std::filesystem::path &targetRoot, const AssetCookLimits &limits) {
        const auto currentPath = targetRoot / "current.json";
        if (!std::filesystem::exists(currentPath)) {
            return Result<AssetCookGeneration>::Failure(Error{CookErrors::MalformedArtifact.code});
        }

        auto bytesResult = ReadFile(currentPath, limits.maximumArtifactBytes);
        if (bytesResult.HasError())
            return Result<AssetCookGeneration>::Failure(bytesResult.ErrorValue());

        const auto &bytes = bytesResult.Value();
        std::string_view json(reinterpret_cast<const char *>(bytes.data()), bytes.size());

        auto targetStr = JsonStringValue(json, "target");
        auto manifestHex = JsonStringValue(json, "manifestDigest");
        std::filesystem::path relPath = JsonStringValue(json, "generationPath");
        auto countStr = JsonStringValue(json, "artifactCount");

        if (targetStr.empty() || manifestHex.empty() || relPath.empty()) {
            return Result<AssetCookGeneration>::Failure(Error{CookErrors::MalformedArtifact.code});
        }

        auto target = AssetCookTargetId::Parse(targetStr);
        if (target.HasError())
            return Result<AssetCookGeneration>::Failure(target.ErrorValue());

        std::size_t count = 0;
        if (!countStr.empty())
            count = static_cast<std::size_t>(std::stoull(countStr));

        return Result<AssetCookGeneration>::Success(AssetCookGeneration{
            .target = target.Value(),
            .manifestDigest = Sha256Digest{},
            .generationRoot = targetRoot / relPath,
            .artifactCount = count,
        });
    }

    // ---------------------------------------------------------------------------
    // PublishCookGeneration
    // ---------------------------------------------------------------------------

    Result<AssetCookGeneration> PublishCookGeneration(const std::filesystem::path &targetRoot, const AssetCookTargetId &target,
                                                      std::span<const AssetCookManifestEntry> entries,
                                                      std::span<const std::vector<std::uint8_t>> artifactPayloads,
                                                      const AssetCookLimits &limits) {
        if (entries.empty()) {
            return Result<AssetCookGeneration>::Failure(Error{CookErrors::MalformedArtifact.code});
        }

        if (entries.size() != artifactPayloads.size()) {
            return Result<AssetCookGeneration>::Failure(Error{CookErrors::MalformedArtifact.code});
        }

        // Verify entries are sorted and have no duplicate IDs
        for (std::size_t i = 1; i < entries.size(); ++i) {
            if (entries[i].assetId <= entries[i - 1].assetId) {
                return Result<AssetCookGeneration>::Failure(Error{CookErrors::DuplicateCooker.code});
            }
        }

        // Verify artifact payloads are within bounds
        for (const auto &payload : artifactPayloads) {
            if (payload.size() > limits.maximumArtifactBytes) {
                return Result<AssetCookGeneration>::Failure(Error{CookErrors::TooLarge.code});
            }
        }

        // Build manifest JSON
        auto manifestJson = BuildManifestJson(target.Value(), entries);
        auto manifestBytes =
            std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(manifestJson.data()), manifestJson.size());
        auto manifestDigest = ComputeSha256(std::as_bytes(manifestBytes));

        auto genRelPath = std::string("generations/") + HexEncodeSha256(manifestDigest);
        auto genRoot = targetRoot / genRelPath;

        // Create generations directory
        std::filesystem::create_directories(genRoot);

        // Write each artifact
        for (std::size_t i = 0; i < entries.size(); ++i) {
            auto artifactPath = genRoot / entries[i].artifactFile;
            auto writeResult = WriteAtomic(artifactPath, artifactPayloads[i]);
            if (writeResult.HasError())
                return Result<AssetCookGeneration>::Failure(writeResult.ErrorValue());
        }

        // Write manifest.json
        auto manifestPath = genRoot / "manifest.json";
        {
            auto manifestVec = std::vector<std::uint8_t>(manifestBytes.begin(), manifestBytes.end());
            auto writeResult = WriteAtomic(manifestPath, manifestVec);
            if (writeResult.HasError())
                return Result<AssetCookGeneration>::Failure(writeResult.ErrorValue());
        }

        // Build and write current.json atomically
        const std::string currentStr =
            std::format(R"({{"schemaVersion":1,"target":"{}","manifestDigest":"{}","generationPath":"{}","artifactCount":"{}"}})",
                        target.Value(), HexEncodeSha256(manifestDigest), genRelPath, entries.size());
        auto currentBytes = std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t *>(currentStr.data()),
                                                      reinterpret_cast<const std::uint8_t *>(currentStr.data()) + currentStr.size());

        if (const auto writeResult = WriteAtomic(targetRoot / "current.json", currentBytes); writeResult.HasError())
            return Result<AssetCookGeneration>::Failure(writeResult.ErrorValue());

        return Result<AssetCookGeneration>::Success(AssetCookGeneration{
            .target = target,
            .manifestDigest = manifestDigest,
            .generationRoot = genRoot,
            .artifactCount = entries.size(),
        });
    }
}  // namespace Horo::Assets
