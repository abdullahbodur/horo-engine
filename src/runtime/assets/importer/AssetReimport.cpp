/**
 * @copydoc AssetReimport.h
 */

#include "Horo/Assets/AssetReimport.h"

#include "../AssetErrors.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Foundation/PathUtils.h"

#include <atomic>
#include <chrono>
#include <span>

namespace Horo::Assets {
    namespace {
        std::atomic_uint64_t g_reimportTemporarySequence{0};

        [[nodiscard]] std::filesystem::path NormalizeAbsolute(const std::filesystem::path &path) {
            std::error_code error;
            std::filesystem::path absolute = std::filesystem::absolute(path, error);
            if (error)
                return {};
            absolute = absolute.lexically_normal();
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, error);
            return error ? absolute : canonical;
        }

        [[nodiscard]] bool HasPathPrefix(const std::filesystem::path &root, const std::filesystem::path &candidate) {
            return Horo::Foundation::Paths::HasPathPrefix(root, candidate);
        }

        [[nodiscard]] std::filesystem::path TemporarySibling(const std::filesystem::path &destination, const std::string_view role) {
            const std::uint64_t sequence = ++g_reimportTemporarySequence;
            return destination.parent_path() / std::format(".{}.horo-reimport-{}-{}", destination.filename().string(), sequence, role);
        }

        [[nodiscard]] std::span<const std::byte> AsBytes(const std::vector<std::uint8_t> &bytes) {
            return {
                reinterpret_cast<const std::byte *>(bytes.data()),
                bytes.size(),
            };
        }

        [[nodiscard]] std::span<const std::byte> AsBytes(const std::string_view text) {
            return {
                reinterpret_cast<const std::byte *>(text.data()),
                text.size(),
            };
        }

        void BestEffortRemove(DurableFileSystem &files, const std::filesystem::path &path) {
            std::error_code error;
            if (std::filesystem::exists(path, error) && !error)
                static_cast<void>(files.RemoveDurable(path));
        }

        [[nodiscard]] Result<void> RestorePair(DurableFileSystem &files, const std::filesystem::path &payloadBackup,
                                               const std::filesystem::path &payload, const std::filesystem::path &metadataBackup,
                                               const std::filesystem::path &metadata) {
            if (auto restored = files.AtomicReplace(payloadBackup, payload); restored.HasError())
                return restored;
            return files.AtomicReplace(metadataBackup, metadata);
        }

        [[nodiscard]] std::int64_t SourceLastWriteTime(const std::filesystem::path &path) {
            std::error_code error;
            const auto value = std::filesystem::last_write_time(path, error);
            return error ? 0 : static_cast<std::int64_t>(value.time_since_epoch().count());
        }
    }  // namespace

    /** @copydoc ReimportProjectAsset */
    Result<AssetReimportReport> ReimportProjectAsset(const AssetReimportRequest &request, const CancellationToken &cancellation) {
        if (request.importerCatalog == nullptr || request.registry == nullptr || request.files == nullptr)
            return Result<AssetReimportReport>::Failure(MakeError(AssetErrors::IndexIo, "Reimport services are unavailable."));

        const std::filesystem::path projectRoot = NormalizeAbsolute(request.absoluteProjectRoot);
        const std::filesystem::path assetRoot = NormalizeAbsolute(projectRoot / "assets");
        const std::filesystem::path assetPath = NormalizeAbsolute(request.absoluteAssetPath);
        if (projectRoot.empty() || assetRoot.empty() || assetPath.empty() || !request.absoluteAssetPath.is_absolute() ||
            !HasPathPrefix(assetRoot, assetPath)) {
            return Result<AssetReimportReport>::Failure(
                MakeError(AssetErrors::SourceMissing, "Reimport target is outside the project asset root."));
        }

        std::error_code error;
        if (const auto targetStatus = std::filesystem::symlink_status(assetPath, error);
            error || std::filesystem::is_symlink(targetStatus) || !std::filesystem::is_regular_file(targetStatus)) {
            return Result<AssetReimportReport>::Failure(MakeError(AssetErrors::SourceMissing, "Reimport target is missing or unsafe."));
        }

        std::filesystem::path metadataPath = assetPath;
        metadataPath += ".horo";
        auto metadataResult = ReadAssetImportMetadata(metadataPath);
        if (metadataResult.HasError())
            return Result<AssetReimportReport>::Failure(metadataResult.ErrorValue());
        AssetImportMetadata metadata = std::move(metadataResult).Value();
        if (metadata.absoluteSourcePath.empty() || !metadata.absoluteSourcePath.is_absolute() || metadata.importerContributionId.empty()) {
            return Result<AssetReimportReport>::Failure(
                MakeError(AssetErrors::SourceMissing, "This asset predates reproducible import provenance."));
        }

        const AssetImporterContribution *contribution = request.importerCatalog->FindById(metadata.importerContributionId);
        if (contribution == nullptr || contribution->strategy == nullptr || !contribution->HandlesExtension(metadata.sourceExtension)) {
            return Result<AssetReimportReport>::Failure(
                MakeError(ImportErrors::NoImporter, "The exact importer contribution recorded by this asset is unavailable."));
        }

        auto sourceResult = ReadAssetImportSource(metadata.absoluteSourcePath);
        if (sourceResult.HasError())
            return Result<AssetReimportReport>::Failure(sourceResult.ErrorValue());
        std::vector<std::uint8_t> source = std::move(sourceResult).Value();
        std::string sourceHash = HashAssetImportSource(source);

        auto settingsResult = ResolveImportSettings(*contribution, metadata.importSettings);
        if (settingsResult.HasError())
            return Result<AssetReimportReport>::Failure(settingsResult.ErrorValue());
        AssetImportInput input{
            .sourceBytes = source,
            .sourceExtension = metadata.sourceExtension,
            .settings = std::move(settingsResult).Value(),
        };
        auto imported = contribution->strategy->Import(input, cancellation);
        if (imported.HasError())
            return Result<AssetReimportReport>::Failure(imported.ErrorValue());
        if (cancellation.IsCancellationRequested())
            return Result<AssetReimportReport>::Failure(MakeError(ImportErrors::ImportCancelled));

        std::vector<AssetImportReason> reasons;
        if (!metadata.sourceHash.empty() && metadata.sourceHash != sourceHash)
            reasons.push_back(AssetImportReason::SourceChanged);
        if (metadata.importerVersion != contribution->version)
            reasons.push_back(AssetImportReason::ImporterChanged);
        if (metadata.importerModuleId != contribution->moduleId || metadata.importerModuleVersion != contribution->moduleVersion) {
            reasons.push_back(AssetImportReason::ModuleChanged);
        }
        if (reasons.empty())
            reasons.push_back(AssetImportReason::ManualReimport);

        PreparedAssetImport prepared = std::move(imported).Value();
        metadata.assetType = prepared.type;
        metadata.importerVersion = contribution->version;
        metadata.importerPackageId = contribution->packageId;
        metadata.importerModuleId = contribution->moduleId;
        metadata.importerModuleVersion = contribution->moduleVersion;
        metadata.sourceHash = sourceHash;
        metadata.sourceByteSize = source.size();
        metadata.sourceLastWriteTime = SourceLastWriteTime(metadata.absoluteSourcePath);
        metadata.dependencies = prepared.dependencies;
        metadata.lastImportReasons = reasons;
        metadata.importedAtUtc = CurrentImportTimestampUtc();

        auto serialized = SerializeAssetImportMetadata(metadata);
        if (serialized.HasError())
            return Result<AssetReimportReport>::Failure(serialized.ErrorValue());

        const std::filesystem::path payloadStaging = TemporarySibling(assetPath, "payload.new");
        const std::filesystem::path metadataStaging = TemporarySibling(metadataPath, "metadata.new");
        const std::filesystem::path payloadBackup = TemporarySibling(assetPath, "payload.backup");
        const std::filesystem::path metadataBackup = TemporarySibling(metadataPath, "metadata.backup");
        const auto cleanup = [&] {
            BestEffortRemove(*request.files, payloadStaging);
            BestEffortRemove(*request.files, metadataStaging);
            BestEffortRemove(*request.files, payloadBackup);
            BestEffortRemove(*request.files, metadataBackup);
        };

        if (auto result = request.files->WriteDurable(payloadStaging, AsBytes(prepared.editorPayload)); result.HasError()) {
            cleanup();
            return Result<AssetReimportReport>::Failure(result.ErrorValue());
        }
        if (auto result = request.files->WriteDurable(metadataStaging, AsBytes(serialized.Value())); result.HasError()) {
            cleanup();
            return Result<AssetReimportReport>::Failure(result.ErrorValue());
        }
        if (auto result = request.files->CopyDurable(assetPath, payloadBackup); result.HasError()) {
            cleanup();
            return Result<AssetReimportReport>::Failure(result.ErrorValue());
        }
        if (auto result = request.files->CopyDurable(metadataPath, metadataBackup); result.HasError()) {
            cleanup();
            return Result<AssetReimportReport>::Failure(result.ErrorValue());
        }

        if (auto result = request.files->AtomicReplace(payloadStaging, assetPath); result.HasError()) {
            cleanup();
            return Result<AssetReimportReport>::Failure(result.ErrorValue());
        }
        if (auto result = request.files->AtomicReplace(metadataStaging, metadataPath); result.HasError()) {
            static_cast<void>(RestorePair(*request.files, payloadBackup, assetPath, metadataBackup, metadataPath));
            cleanup();
            return Result<AssetReimportReport>::Failure(result.ErrorValue());
        }

        if (const auto rebuilt = RebuildAssetRegistry(*request.registry, projectRoot, AssetRegistryOpenMode::Edit);
            rebuilt.HasError() || rebuilt.Value().status == AssetRegistryBuildStatus::Failed) {
            static_cast<void>(RestorePair(*request.files, payloadBackup, assetPath, metadataBackup, metadataPath));
            static_cast<void>(RebuildAssetRegistry(*request.registry, projectRoot, AssetRegistryOpenMode::Edit));
            cleanup();
            return Result<AssetReimportReport>::Failure(rebuilt.HasError() ? rebuilt.ErrorValue() : MakeError(AssetErrors::IndexMalformed));
        }

        cleanup();
        LOG_INFO("editor.asset_reimport", "Reimported asset id=%s importer=%s@%s module=%s@%s source=%s",
                 metadata.assetId.ToString().c_str(), contribution->contributionId.c_str(), contribution->version.c_str(),
                 contribution->moduleId.c_str(), contribution->moduleVersion.c_str(), metadata.absoluteSourcePath.string().c_str());
        return Result<AssetReimportReport>::Success(AssetReimportReport{
            .assetId = metadata.assetId,
            .reasons = std::move(reasons),
            .sourceHash = std::move(sourceHash),
            .importerVersion = contribution->version,
            .moduleVersion = contribution->moduleVersion,
        });
    }
}  // namespace Horo::Assets
