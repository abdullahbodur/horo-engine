/**
 * @copydoc AssetCookService.h
 */

#include "Horo/Assets/AssetCookService.h"

#include "Horo/Assets/AssetCook.h"
#include "Horo/Assets/AssetCookCache.h"
#include "Horo/Assets/AssetCookOutput.h"
#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/JobSystem.h"
#include "Horo/Foundation/Sha256.h"
#include "../AssetErrors.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Assets
{
namespace
{

/**
 * @brief Reads source bytes from a project-relative path under sourceRoot.
 */
Result<std::vector<std::uint8_t>> ReadSourceBytes(const std::filesystem::path &sourceRoot,
                                                  std::string_view relativePath,
                                                  std::size_t maxBytes)
{
    auto fullPath = sourceRoot / relativePath;

    // Reject symlinks
    if (std::filesystem::is_symlink(fullPath))
        return Result<std::vector<std::uint8_t>>::Failure(
            Error{CookErrors::MalformedArtifact.code});

    std::error_code ec;
    if (!std::filesystem::exists(fullPath, ec) || ec)
        return Result<std::vector<std::uint8_t>>::Failure(
            Error{CookErrors::MalformedArtifact.code});

    const auto fileSize = std::filesystem::file_size(fullPath, ec);
    if (ec || fileSize > maxBytes)
        return Result<std::vector<std::uint8_t>>::Failure(
            Error{CookErrors::TooLarge.code});

    std::ifstream file(fullPath, std::ios::binary);
    if (!file)
        return Result<std::vector<std::uint8_t>>::Failure(
            Error{CookErrors::MalformedArtifact.code});

    std::vector<std::uint8_t> bytes(fileSize);
    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(fileSize));
    if (!file || file.gcount() != static_cast<std::streamsize>(fileSize))
        return Result<std::vector<std::uint8_t>>::Failure(
            Error{CookErrors::MalformedArtifact.code});

    return Result<std::vector<std::uint8_t>>::Success(std::move(bytes));
}

} // namespace

AssetCookService::AssetCookService(JobSystem &jobs,
                                   std::shared_ptr<const CookerCatalogSnapshot> catalog)
    : jobs_(jobs), catalog_(std::move(catalog))
{
}

Result<AssetCookReport> AssetCookService::Cook(const AssetCookRequest &request,
                                               const CancellationToken &cancellation)
{
    if (cancellation.IsCancellationRequested())
        return Result<AssetCookReport>::Failure(Error{CookErrors::Cancelled.code});

    if (!catalog_)
        return Result<AssetCookReport>::Failure(Error{CookErrors::MalformedArtifact.code});

    // Pin the registry snapshot's records in deterministic order
    auto records = request.registry.Records();
    if (records.empty())
    {
        // Empty snapshot: write an empty current.json directly (no manifest/generation dir needed).
        // Build an empty current.json
        std::ostringstream currentJson;
        currentJson << "{\"schemaVersion\":1,"
                    << "\"target\":\"" << request.target.Value() << "\","
                    << "\"manifestDigest\":\""
                    << "0000000000000000000000000000000000000000000000000000000000000000"
                    << "\","
                    << "\"generationPath\":\"generations/empty\","
                    << "\"artifactCount\":\"0\"}";
        auto currentStr = currentJson.str();
        auto currentBytes = std::vector<std::uint8_t>(
            reinterpret_cast<const std::uint8_t *>(currentStr.data()),
            reinterpret_cast<const std::uint8_t *>(currentStr.data()) + currentStr.size());

        {
            auto currentPath = request.cookedRoot / "current.json";
            auto tempPath = currentPath;
            tempPath += ".tmp";
            {
                std::ofstream temp(tempPath, std::ios::binary | std::ios::trunc);
                temp.write(reinterpret_cast<const char *>(currentBytes.data()),
                           static_cast<std::streamsize>(currentBytes.size()));
            }
            std::filesystem::rename(tempPath, currentPath);
        }

        return Result<AssetCookReport>::Success(AssetCookReport{
            .generation = AssetCookGeneration{
                .target = request.target,
                .generationRoot = request.cookedRoot / "generations" / "empty",
                .artifactCount = 0,
            },
            .totalAssets = 0,
            .cookedAssets = 0,
            .cacheHits = 0,
        });
    }

    if (records.size() > request.limits.maximumAssets)
        return Result<AssetCookReport>::Failure(Error{CookErrors::TooLarge.code});

    // Find the cooker for this target/type
    // All records have the same type? No — each record has its own type.
    // Build a map of type->strategy, fail if any type has no cooker.
    // For simplicity: we require all records to use the same cooker for the target.
    // In production this would be per-type lookup. Let's do the simple path.

    // Collect source bytes and cache keys
    AssetCookCache cache(request.cacheRoot, request.limits);

    struct CookSlot
    {
        AssetRecord record;
        std::vector<std::uint8_t> sourceBytes;
        Sha256Digest sourceDigest;
        AssetCookCacheKey cacheKey;
        bool cacheHit{false};
        std::vector<std::uint8_t> cacheArtifact;
        std::vector<std::uint8_t> cookedArtifact;
    };

    std::vector<CookSlot> slots;
    slots.reserve(records.size());

    for (const auto &record : records)
    {
        // Find cooker strategy
        const auto *strategy = catalog_->Find(record.type, request.target);
        if (!strategy)
            return Result<AssetCookReport>::Failure(Error{CookErrors::CookerMissing.code});

        // Read source bytes
        auto sourceResult = ReadSourceBytes(request.sourceRoot, record.sourcePath.String(),
                                            request.limits.maximumSourceBytes);
        if (sourceResult.HasError())
            return Result<AssetCookReport>::Failure(sourceResult.ErrorValue());

        auto &sourceBytes = sourceResult.Value();
        auto sourceDigest = ComputeSha256(std::as_bytes(std::span{sourceBytes}));

        // Build cache key (simplified: no metadata/settings digests yet)
        auto cacheKey = BuildAssetCookCacheKey(AssetCookCacheKeyInputs{
            .assetId = record.id,
            .assetType = record.type,
            .sourceDigest = sourceDigest,
            .metadataDigest = Sha256Digest{},
            .metadataSchemaVersion = 0,
            .settingsDigest = Sha256Digest{},
            .settingsSchemaVersion = 0,
            .cookerContributionId = "horo.asset-cooker.headless-mesh-validation",
            .cookerVersion = "1.0.0",
            .target = request.target,
            .artifactFormatVersion = AssetCookArtifact::CurrentFormatVersion,
        });

        CookSlot slot{
            .record = record,
            .sourceBytes = std::move(sourceBytes),
            .sourceDigest = sourceDigest,
            .cacheKey = cacheKey,
        };

        // Check cache
        auto cacheResult = cache.Load(cacheKey, cancellation);
        if (cacheResult.HasError())
            return Result<AssetCookReport>::Failure(cacheResult.ErrorValue());

        if (cacheResult.Value().has_value())
        {
            slot.cacheHit = true;
            slot.cookedArtifact = std::move(cacheResult.Value().value());
        }

        slots.push_back(std::move(slot));
    }

    // Count cache hits
    std::size_t cacheHits = 0;
    for (const auto &s : slots)
    {
        if (s.cacheHit)
            ++cacheHits;
    }

    // Submit misses through TaskGroup
    {
        TaskGroup group(jobs_, TaskGroupFailurePolicy::FailFast, cancellation);

        for (auto &slot : slots)
        {
            if (slot.cacheHit)
                continue;

            const auto *strategy = catalog_->Find(slot.record.type, request.target);

            CookSourceView sourceView{
                .id = slot.record.id,
                .type = slot.record.type,
                .target = request.target,
                .sourceDigest = slot.sourceDigest,
                .bytes = slot.sourceBytes,
            };

            // Capture by copy for the job
            auto work = [strategy, sourceView, &slot](
                            const CancellationToken &jobCancellation) -> Result<void> {
                auto cookResult = strategy->Cook(sourceView, jobCancellation);
                if (cookResult.HasError())
                    return Result<void>::Failure(cookResult.ErrorValue());

                auto &sink = cookResult.Value();

                // Build the artifact envelope
                AssetCookArtifact artifact;
                artifact.id = sourceView.id;
                artifact.type = sourceView.type;
                artifact.target = sourceView.target;
                artifact.sourceDigest = sourceView.sourceDigest;
                artifact.payloadDigest = ComputeSha256(std::as_bytes(std::span{sink.payload}));
                artifact.payload = std::move(sink.payload);

                auto encodeResult = EncodeCookedArtifact(artifact);
                if (encodeResult.HasError())
                    return Result<void>::Failure(encodeResult.ErrorValue());

                slot.cookedArtifact = std::move(encodeResult.Value());
                return Result<void>::Success();
            };

            auto spawnResult = group.Spawn(JobDescriptor{}, std::move(work));
            if (spawnResult.HasError())
                return Result<AssetCookReport>::Failure(spawnResult.ErrorValue());
        }

        auto joinResult = group.Join();
        if (joinResult.HasError())
            return Result<AssetCookReport>::Failure(joinResult.ErrorValue());
    }

    // Store cache entries and build manifest
    std::vector<AssetCookManifestEntry> manifestEntries;
    std::vector<std::vector<std::uint8_t>> manifestPayloads;

    for (auto &slot : slots)
    {
        // Store in cache (idempotent)
        auto storeResult = cache.Store(slot.cacheKey, slot.cookedArtifact, cancellation);
        if (storeResult.HasError())
            return Result<AssetCookReport>::Failure(storeResult.ErrorValue());

        auto artifactHash = ComputeSha256(std::as_bytes(std::span{slot.cookedArtifact}));

        manifestEntries.push_back(AssetCookManifestEntry{
            .assetId = slot.record.id,
            .assetType = slot.record.type,
            .artifactFile = slot.record.id.ToString() + ".cooked",
            .artifactHash = artifactHash,
        });

        manifestPayloads.push_back(std::move(slot.cookedArtifact));
    }

    // Re-check cancellation before publishing
    if (cancellation.IsCancellationRequested())
        return Result<AssetCookReport>::Failure(Error{CookErrors::Cancelled.code});

    // Publish generation atomically
    auto pubResult = PublishCookGeneration(request.cookedRoot, request.target,
                                           manifestEntries, manifestPayloads, request.limits);
    if (pubResult.HasError())
        return Result<AssetCookReport>::Failure(pubResult.ErrorValue());

    std::size_t cookedCount = slots.size() - cacheHits;

    return Result<AssetCookReport>::Success(AssetCookReport{
        .generation = pubResult.Value(),
        .totalAssets = slots.size(),
        .cookedAssets = cookedCount,
        .cacheHits = cacheHits,
    });
}

} // namespace Horo::Assets
