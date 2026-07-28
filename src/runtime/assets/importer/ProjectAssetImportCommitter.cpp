/**
 * @copydoc ProjectAssetImportCommitter.h
 */

#include "ProjectAssetImportCommitter.h"

#include "Horo/Assets/AssetImportMetadata.h"
#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Foundation/PathUtils.h"
#include "Horo/Foundation/Result.h"
#include "../AssetErrors.h"

#include <filesystem>
#include <fstream>
#include <format>

namespace Horo::Assets
{
namespace
{
Result<void> WriteBytes(const std::filesystem::path &path, const std::span<const std::uint8_t> bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return Result<void>::Failure(
            MakeError(AssetErrors::IndexIo, std::format("Unable to open {}.", path.string())));
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output)
        return Result<void>::Failure(
            MakeError(AssetErrors::IndexIo, std::format("Unable to write {}.", path.string())));
    return Result<void>::Success();
}

Result<void> WriteIdentitySidecar(
    const std::filesystem::path& path, const AssetImportMetadata& metadata)
{
    auto serialized = SerializeAssetImportMetadata(metadata);
    if (serialized.HasError())
        return Result<void>::Failure(serialized.ErrorValue());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return Result<void>::Failure(
            MakeError(AssetErrors::IndexIo, std::format("Unable to open {}.", path.string())));
    output << serialized.Value();
    output.flush();
    if (!output)
        return Result<void>::Failure(
            MakeError(AssetErrors::IndexIo, std::format("Unable to write {}.", path.string())));
    return Result<void>::Success();
}
} // namespace

Result<void> ProjectAssetImportCommitter::Commit(
    PreparedAssetImportBatch batch,
    IAssetIdGenerator &idGenerator,
    const CancellationToken &cancellation)
{
    for (const auto &item : batch.items)
    {
        if (cancellation.IsCancellationRequested())
            return Result<void>::Failure(MakeError(ImportErrors::ImportCancelled));

        if (!item.result.has_value())
            continue;

        const auto &prepared = item.result.value();
        const std::filesystem::path outputDir =
            Foundation::Paths::Resolve(batch.projectRoot, batch.destinationFolder);

        const auto ensureResult = Foundation::Paths::EnsureDirectory(outputDir);
        if (ensureResult.HasError())
            return Result<void>::Failure(ensureResult.ErrorValue());

        const std::string assetFileName = item.displayName + item.targetExtension;
        const std::filesystem::path outputPath = outputDir / assetFileName;

        if (const Result<void> written = WriteBytes(outputPath, prepared.editorPayload); written.HasError())
            return written;

        LOG_INFO("editor.asset_import",
                 "Committed asset → %s (%zu bytes)",
                 outputPath.string().c_str(),
                 prepared.editorPayload.size());

        if (item.createMetaSidecar && item.supportsMetaSidecar)
        {
            const AssetId id = item.preservedAssetId.value_or(idGenerator.Generate());
            if (!id.IsValid())
                return Result<void>::Failure(MakeError(AssetErrors::IdentityInvalid));
            std::filesystem::path sidecarPath = outputPath;
            sidecarPath += ".horo";
            const AssetImportMetadata metadata{
                .assetId = id,
                .assetType = prepared.type,
                .importerContributionId = item.importerContributionId,
                .importerVersion = item.importerVersion,
                .importerPackageId = item.importerPackageId,
                .importerModuleId = item.importerModuleId,
                .importerModuleVersion = item.importerModuleVersion,
                .absoluteSourcePath = item.absoluteSourcePath,
                .sourceExtension = item.sourceExtension,
                .sourceHash = item.sourceHash,
                .sourceByteSize = item.sourceByteSize,
                .sourceLastWriteTime = item.sourceLastWriteTime,
                .importSettings = item.settings,
                .dependencies = prepared.dependencies,
                .lastImportReasons = item.importReasons.empty()
                    ? std::vector{AssetImportReason::InitialImport}
                    : item.importReasons,
                .importedAtUtc = CurrentImportTimestampUtc(),
            };
            if (const Result<void> written = WriteIdentitySidecar(sidecarPath, metadata);
                written.HasError())
                return written;
            LOG_INFO("editor.asset_import", "Committed identity sidecar → %s",
                     sidecarPath.string().c_str());
        }
    }

    if (registry_ != nullptr)
    {
        auto rebuilt = RebuildAssetRegistry(*registry_, batch.projectRoot, AssetRegistryOpenMode::Edit);
        if (rebuilt.HasError())
            return Result<void>::Failure(rebuilt.ErrorValue());
        if (rebuilt.Value().status == AssetRegistryBuildStatus::Failed)
            return Result<void>::Failure(MakeError(AssetErrors::IndexMalformed));
        LOG_INFO("editor.asset_import", "Published asset registry revision %llu with %zu assets.",
                 static_cast<unsigned long long>(rebuilt.Value().publishedRevision.value),
                 rebuilt.Value().registeredAssets);
    }

    return Result<void>::Success();
}

} // namespace Horo::Assets
