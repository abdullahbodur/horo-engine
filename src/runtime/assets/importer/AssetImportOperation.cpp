/**
 * @copydoc AssetImportOperation.h
 */

#include "Horo/Assets/AssetImportOperation.h"

#include "Horo/Foundation/JobSystem.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "../AssetErrors.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Horo::Assets
{
namespace
{
std::string LowerExtension(const std::filesystem::path &path)
{
    auto ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.')
        ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}
} // namespace

AssetImportOperation::AssetImportOperation(
    JobSystem &jobs, std::shared_ptr<const AssetImporterCatalogSnapshot> catalog)
    : jobs_(jobs), catalog_(std::move(catalog))
{
}

Result<AssetImportSnapshot> AssetImportOperation::Start(
    const AssetImportRequest &request, const CancellationToken &cancellation)
{
    if (cancellation.IsCancellationRequested())
        return Result<AssetImportSnapshot>::Failure(Error{CookErrors::Cancelled.code});

    if (!catalog_)
        return Result<AssetImportSnapshot>::Failure(Error{CookErrors::MalformedArtifact.code});

    snapshot_ = AssetImportSnapshot{
        .operationId = "import-" + std::to_string(++revision_),
        .revision = revision_,
        .phase = AssetImportPhase::Selecting,
        .canCancel = true,
    };

    for (const auto &sourceFile : request.sourceFiles)
    {
        auto ext = LowerExtension(sourceFile);
        const auto *strategy = catalog_->FindByExtension(ext);

        // Find contribution ID from the strategy — workaround until
        // FindContributionByExtension is added to the snapshot.
        std::string contribId;
        const auto *contrib = catalog_->FindContributionByExtension(ext);
        if (contrib)
            contribId = contrib->contributionId;

        auto parsed = ProjectPath::Parse(
            std::filesystem::relative(sourceFile, request.projectRoot).string());
        if (!parsed.HasValue())
        {
            // File is outside the project root — use just the filename
            // as the project-relative path.
            parsed = ProjectPath::Parse(sourceFile.filename().string());
            if (!parsed.HasValue())
                continue;
        }
        AssetImportItem item{
            .sourceFile = std::move(parsed).Value(),
            .absoluteSourcePath = sourceFile,
            .importerContributionId = contribId,
            .sourceExtension = ext,
            .displayName = sourceFile.filename().string(),
            .destinationFolder = request.destinationFolder,
        };

        if (!strategy)
        {
            item.diagnostics.push_back(ImportDiagnostic{
                .severity = ImportDiagnostic::Severity::Error,
                .code = ImportErrors::NoImporter.code.Value(),
                .message = "No importer registered for extension ." + ext,
            });
            LOG_ERROR("editor.asset_import",
                      "No importer for .%s — %s",
                      ext.c_str(),
                      sourceFile.filename().string().c_str());
        }

        snapshot_.items.push_back(std::move(item));
    }

    return Result<AssetImportSnapshot>::Success(snapshot_);
}

Result<AssetImportSnapshot> AssetImportOperation::AddFiles(
    const std::vector<std::filesystem::path> &sourceFiles,
    const std::filesystem::path &projectRoot,
    const CancellationToken &cancellation)
{
    if (cancellation.IsCancellationRequested())
        return Result<AssetImportSnapshot>::Failure(Error{CookErrors::Cancelled.code});

    if (!catalog_)
        return Result<AssetImportSnapshot>::Failure(Error{CookErrors::MalformedArtifact.code});

    for (const auto &sourceFile : sourceFiles)
    {
        auto ext = LowerExtension(sourceFile);
        const auto *strategy = catalog_->FindByExtension(ext);

        std::string contribId;
        const auto *contrib = catalog_->FindContributionByExtension(ext);
        if (contrib)
            contribId = contrib->contributionId;

        auto parsed = ProjectPath::Parse(
            std::filesystem::relative(sourceFile, projectRoot).string());
        if (!parsed.HasValue())
        {
            // File is outside the project root — use just the filename
            // as the project-relative path. The importer reads from the
            // original absolute path; sourceFile is for display only.
            parsed = ProjectPath::Parse(sourceFile.filename().string());
            if (!parsed.HasValue())
                continue;  // Can't even parse the filename — skip
        }

        AssetImportItem item{
            .sourceFile = std::move(parsed).Value(),
            .absoluteSourcePath = sourceFile,
            .importerContributionId = contribId,
            .sourceExtension = ext,
            .displayName = sourceFile.filename().string(),
            .destinationFolder = "Assets",
        };

        if (!strategy)
        {
            item.diagnostics.push_back(ImportDiagnostic{
                .severity = ImportDiagnostic::Severity::Error,
                .code = ImportErrors::NoImporter.code.Value(),
                .message = "No importer registered for extension ." + ext,
            });
            LOG_ERROR("editor.asset_import",
                      "No importer for .%s — %s",
                      ext.c_str(),
                      sourceFile.filename().string().c_str());
        }

        snapshot_.items.push_back(std::move(item));
    }

    snapshot_.revision = ++revision_;
    return Result<AssetImportSnapshot>::Success(snapshot_);
}

Result<AssetImportSnapshot> AssetImportOperation::ImportSingleItem(
    std::size_t index, const CancellationToken &cancellation)
{
    if (cancelled_)
        return Result<AssetImportSnapshot>::Failure(Error{CookErrors::Cancelled.code});

    if (index >= snapshot_.items.size())
        return Result<AssetImportSnapshot>::Failure(Error{CookErrors::MalformedArtifact.code});

    auto &item = snapshot_.items[index];
    if (item.result.has_value())
        return Result<AssetImportSnapshot>::Success(snapshot_);

    const auto *strategy = catalog_->FindByExtension(item.sourceExtension);
    if (!strategy)
    {
        item.diagnostics.push_back(ImportDiagnostic{
            .severity = ImportDiagnostic::Severity::Error,
            .code = ImportErrors::NoImporter.code.Value(),
            .message = "No importer for ." + item.sourceExtension,
        });
        LOG_ERROR("editor.asset_import",
                  "No importer for .%s — %s",
                  item.sourceExtension.c_str(),
                  item.displayName.c_str());
        snapshot_.revision = ++revision_;
        return Result<AssetImportSnapshot>::Success(snapshot_);
    }

    // Read source file bytes
    std::vector<std::uint8_t> fileBytes;
    {
        std::ifstream file(item.absoluteSourcePath, std::ios::binary | std::ios::ate);
        if (!file)
        {
            item.diagnostics.push_back(ImportDiagnostic{
                .severity = ImportDiagnostic::Severity::Error,
                .code = ImportErrors::NoImporter.code.Value(),
                .message = "Cannot open source file: " + item.absoluteSourcePath.string(),
            });
            LOG_ERROR("editor.asset_import",
                      "Cannot open source file: %s",
                      item.absoluteSourcePath.string().c_str());
            snapshot_.revision = ++revision_;
            return Result<AssetImportSnapshot>::Success(snapshot_);
        }
        auto size = file.tellg();
        file.seekg(0, std::ios::beg);
        fileBytes.resize(static_cast<std::size_t>(size));
        file.read(reinterpret_cast<char *>(fileBytes.data()), size);
    }

    AssetImportInput input{
        .sourceBytes = fileBytes,
        .sourceExtension = item.sourceExtension,
        .settings = {},
    };

    auto result = strategy->Import(input, cancellation);
    if (result.HasValue())
    {
        item.resolvedType = result.Value().type;
        item.result = std::move(result.Value());
    }
    else
    {
        const auto &err = result.ErrorValue();
        item.diagnostics.push_back(ImportDiagnostic{
            .severity = ImportDiagnostic::Severity::Error,
            .code = err.code.Value(),
            .message = err.message,
        });
        LOG_ERROR("editor.asset_import",
                  "Import failed for %s: %s",
                  item.displayName.c_str(),
                  err.message.c_str());
    }

    snapshot_.revision = ++revision_;
    return Result<AssetImportSnapshot>::Success(snapshot_);
}

AssetImportSnapshot AssetImportOperation::Snapshot() const noexcept
{
    return snapshot_;
}

void AssetImportOperation::Cancel()
{
    cancelled_ = true;
    snapshot_.phase = AssetImportPhase::Cancelled;
    snapshot_.revision = ++revision_;
}

} // namespace Horo::Assets
