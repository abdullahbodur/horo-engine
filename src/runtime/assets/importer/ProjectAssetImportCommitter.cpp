/**
 * @copydoc ProjectAssetImportCommitter.h
 */

#include "ProjectAssetImportCommitter.h"

#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Foundation/PathUtils.h"
#include "Horo/Foundation/Result.h"
#include "../AssetErrors.h"

#include <filesystem>
#include <fstream>

namespace Horo::Assets
{

Result<void> ProjectAssetImportCommitter::Commit(
    PreparedAssetImportBatch batch,
    IAssetIdGenerator & /*idGenerator*/,
    const CancellationToken &cancellation)
{
    for (const auto &item : batch.items)
    {
        if (cancellation.IsCancellationRequested())
            return Result<void>::Failure(MakeError(ImportErrors::ImportCancelled));

        if (!item.result.has_value())
            continue;

        const auto &prepared = item.result.value();

        // Write editor payload to the configured destination directory
        std::filesystem::path outputDir =
            Foundation::Paths::Resolve(batch.projectRoot, batch.destinationFolder);

        auto ensureResult = Foundation::Paths::EnsureDirectory(outputDir);
        if (ensureResult.HasError())
        {
            LOG_ERROR("editor.asset_import",
                      "Failed to create output directory: %s — %s",
                      outputDir.string().c_str(),
                      ensureResult.ErrorValue().message.c_str());
            continue;
        }

        std::string assetFileName = item.displayName + ".horoasset";
        std::filesystem::path outputPath = outputDir / assetFileName;

        std::ofstream out(outputPath, std::ios::binary);
        if (!out)
        {
            LOG_ERROR("editor.asset_import",
                      "Failed to open output file: %s",
                      outputPath.string().c_str());
            continue;
        }

        out.write(reinterpret_cast<const char *>(prepared.editorPayload.data()),
                  static_cast<std::streamsize>(prepared.editorPayload.size()));
        out.close();

        LOG_INFO("editor.asset_import",
                 "Committed asset → %s (%zu bytes)",
                 outputPath.string().c_str(),
                 prepared.editorPayload.size());
    }

    return Result<void>::Success();
}

} // namespace Horo::Assets
