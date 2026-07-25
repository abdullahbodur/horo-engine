/**
 * @copydoc AssetImportModal.h
 */

#include "Horo/Editor/AssetImportModal.h"

#include "Horo/Assets/AssetImporter.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Foundation/ErrorCode.h"
#include "runtime/assets/importer/ProjectAssetImportCommitter.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Horo::Editor
{
    AssetImportModal::AssetImportModal(const Theme::Fonts& fonts, JobSystem& jobs,
                                       std::shared_ptr<const Assets::AssetImporterCatalogSnapshot> catalog) noexcept
        : m_fonts(fonts), m_jobs(jobs), m_catalog(std::move(catalog))
    {
    }

    ModalId AssetImportModal::Id() const
    {
        return ModalId{kModalId};
    }

    ModalPresentation AssetImportModal::Presentation() const
    {
        return {.size = ModalSizePolicy::Large, .dimWorkspace = true};
    }

    ModalClosePolicy AssetImportModal::ClosePolicy() const
    {
        return {
            .allowCloseButton = true,
            .allowEscape = true,
            .allowOutsideClick = false,
            .allowApplicationShutdown = true,
        };
    }

    Result<void> AssetImportModal::OnOpen(EditorModalContext& context)
    {
        m_events = &context.events;
        m_prepared = false;
        m_queuedFiles.clear();
        m_snapshot = Assets::AssetImportSnapshot{};

        m_logCtx = std::make_unique<Log::LogContext>(
            "modal", "asset_import", "modal_id", std::to_string(kModalId));
        LOG_INFO("editor.asset_import", "AssetImportModal opened.");

        return Result<void>::Success();
    }

    CloseDecision AssetImportModal::CanClose(const ModalCloseReason reason)
    {
        if (reason == ModalCloseReason::ApplicationShutdown)
            return CloseDecision::Allow;

        // Prevent close while import is in progress
        if (m_snapshot.phase == Assets::AssetImportPhase::Preparing ||
            m_snapshot.phase == Assets::AssetImportPhase::Committing)
        {
            return CloseDecision::Deny;
        }

        return CloseDecision::Allow;
    }

    void AssetImportModal::OnClose(const ModalCloseReason reason)
    {
        const char* reasonStr = (reason == ModalCloseReason::Completed)
                                    ? "completed"
                                    : (reason == ModalCloseReason::Cancelled)
                                    ? "cancelled"
                                    : "app_shutdown";
        LOG_INFO("editor.asset_import", "AssetImportModal closed (reason=%s).", reasonStr);

        if (m_operation)
            m_operation->Cancel();

        m_logCtx.reset();
    }

    const Assets::AssetImportSnapshot& AssetImportModal::Snapshot() const noexcept
    {
        return m_snapshot;
    }

    const Assets::AssetImporterCatalogSnapshot& AssetImportModal::Catalog() const noexcept
    {
        return *m_catalog;
    }

    void AssetImportModal::SetProjectRoot(const std::filesystem::path &root) noexcept
    {
        m_projectRoot = root;
    }

    void AssetImportModal::SelectItem(std::size_t index)
    {
        if (index < m_snapshot.items.size())
            m_snapshot.selectedItemIndex = index;
    }

    Result<void> AssetImportModal::BeginImport(
        const std::vector<std::filesystem::path>& sourceFiles,
        const CancellationToken& cancellation)
    {
        // Walk up from the first file to find the project root (the directory containing .horo/).
        std::filesystem::path projectRoot = sourceFiles.front().parent_path();
        while (!projectRoot.empty() && projectRoot != projectRoot.root_path())
        {
            if (std::filesystem::exists(projectRoot / ".horo" / "project.json"))
                break;
            projectRoot = projectRoot.parent_path();
        }
        if (projectRoot.empty() || projectRoot == projectRoot.root_path())
            projectRoot = sourceFiles.front().parent_path();  // Fallback
        return BeginImport(sourceFiles, projectRoot, cancellation);
    }

    Result<void> AssetImportModal::BeginImport(
        const std::vector<std::filesystem::path>& sourceFiles,
        const std::filesystem::path& projectRoot,
        const CancellationToken& cancellation)
    {
        if (!m_catalog)
        {
            Error err;
            err.code = ErrorCode{"editor.asset_import.no_catalog"};
            err.domain = ErrorDomainId{"horo.editor"};
            return Result<void>::Failure(err);
        }

        m_projectRoot = projectRoot;
        m_committer = Assets::MakeProjectCommitter();

        // If operation already exists, append files; otherwise start a new one.
        if (m_operation)
        {
            auto result = m_operation->AddFiles(sourceFiles, projectRoot, cancellation);
            if (result.HasError())
                return Result<void>::Failure(result.ErrorValue());
            m_snapshot = result.Value();
            LOG_INFO("editor.asset_import", "Added %zu files to queue: %zu total.",
                     sourceFiles.size(), m_snapshot.items.size());
            return Result<void>::Success();
        }

        m_operation = std::make_unique<Assets::AssetImportOperation>(m_jobs, m_catalog);

        Assets::AssetImportRequest request{
            .projectRoot = projectRoot,
            .sourceFiles = sourceFiles,
        };

        auto result = m_operation->Start(request, cancellation);
        if (result.HasError())
            return Result<void>::Failure(result.ErrorValue());

        m_snapshot = result.Value();
        LOG_INFO("editor.asset_import", "Import started: %zu files.", m_snapshot.items.size());

        return Result<void>::Success();
    }

    Result<void> AssetImportModal::PrepareImport(const CancellationToken& cancellation)
    {
        // No longer needed — import happens per-file via ImportSingleItem.
        // Kept for API compatibility with headless tests.
        (void)cancellation;
        return Result<void>::Success();
    }

    Result<void> AssetImportModal::ImportSingleItem(std::size_t index, const CancellationToken& cancellation)
    {
        if (!m_operation)
        {
            Error err;
            err.code = ErrorCode{"editor.asset_import.no_operation"};
            err.domain = ErrorDomainId{"horo.editor"};
            return Result<void>::Failure(err);
        }

        auto result = m_operation->ImportSingleItem(index, cancellation);
        if (result.HasError())
            return Result<void>::Failure(result.ErrorValue());

        m_snapshot = result.Value();
        LOG_INFO("editor.asset_import", "Imported item %zu: %s",
                 index, m_snapshot.items[index].displayName.c_str());

        // Check for conflicts before committing
        const auto &item = m_snapshot.items[index];

        // Skip items that failed to import (no result payload)
        if (!item.result.has_value())
        {
            LOG_INFO("editor.asset_import", "Skipping commit for %s — import produced no result.",
                     item.displayName.c_str());
            return Result<void>::Success();
        }

        if (WouldConflict(item))
        {
            ConflictItem conflict;
            conflict.assetName = m_snapshot.items[index].displayName + "." + m_snapshot.items[index].sourceExtension;
            conflict.conflictDescription = "An asset with this name already exists in Assets/";
            conflict.snapshotIndex = index;
            m_conflictQueue.push_back(std::move(conflict));
            LOG_INFO("editor.asset_import", "Conflict detected for %s — queued for resolution",
                     m_snapshot.items[index].displayName.c_str());
        }
        else
        {
            // Commit to project storage directly
            if (m_committer)
            {
                std::string destFolder = m_snapshot.items[index].destinationFolder;
                if (destFolder.empty())
                    destFolder = "Assets";
                Assets::PreparedAssetImportBatch batch{
                    .operationId = m_snapshot.operationId,
                    .projectRoot = m_projectRoot,
                    .destinationFolder = ProjectPath::Parse(destFolder).Value(),
                    .items = {m_snapshot.items[index]},
                };

                struct DummyIdGen final : Assets::IAssetIdGenerator
                {
                    Assets::AssetId Generate() override { return Assets::AssetId{}; }
                } idGen;
                CancellationToken noCancel;
                auto commitResult = m_committer->Commit(batch, idGen, noCancel);
                if (commitResult.HasError())
                    LOG_ERROR("editor.asset_import", "Commit failed for item %zu", index);
                else
                    LOG_INFO("editor.asset_import", "Committed item %zu to project storage.", index);
            }
        }

        return Result<void>::Success();
    }

    // -------------------------------------------------------------------------
    // Conflict resolution
    // -------------------------------------------------------------------------

    bool AssetImportModal::WouldConflict(const Assets::AssetImportItem &item) const
    {
        if (m_projectRoot.empty())
            return false;

        std::string destFolder = item.destinationFolder;
        if (destFolder.empty())
            destFolder = "Assets";
        std::filesystem::path targetPath = m_projectRoot / destFolder / (item.displayName + ".horoasset");
        std::error_code ec;
        return std::filesystem::exists(targetPath, ec);
    }

    void AssetImportModal::CommitCurrentItem(
        const Assets::AssetImportItem &item,
        ConflictChoice choice,
        bool applyAll)
    {
        if (choice == ConflictChoice::Skip)
        {
            LOG_INFO("editor.asset_import", "Skipped import for %s (user chose Skip)", item.displayName.c_str());
            return;
        }

        std::string destFolder = m_snapshot.items[m_conflictCursor].destinationFolder;
        if (destFolder.empty())
            destFolder = "Assets";

        std::string assetName = item.displayName;
        if (choice == ConflictChoice::Rename)
        {
            // Simple rename: append _1, _2, etc.
            int suffix = 1;
            std::filesystem::path base = m_projectRoot / destFolder;
            while (std::filesystem::exists(base / (assetName + ".horoasset")))
                assetName = item.displayName + "_" + std::to_string(++suffix);
        }

        // Build a modified batch with the (possibly renamed) name
        Assets::PreparedAssetImportBatch batch{
            .operationId = m_snapshot.operationId,
            .projectRoot = m_projectRoot,
            .destinationFolder = ProjectPath::Parse(destFolder).Value(),
            .items = {item},
        };

        // Override displayName for rename case
        batch.items[0].displayName = assetName;

        struct DummyIdGen final : Assets::IAssetIdGenerator
        {
            Assets::AssetId Generate() override { return Assets::AssetId{}; }
        } idGen;

        CancellationToken noCancel;
        auto commitResult = m_committer->Commit(batch, idGen, noCancel);
        if (commitResult.HasError())
            LOG_ERROR("editor.asset_import", "Commit failed for %s", assetName.c_str());
        else
            LOG_INFO("editor.asset_import", "Committed %s (choice=%d)", assetName.c_str(), static_cast<int>(choice));
    }

    void AssetImportModal::ResolveCurrentConflict(ConflictChoice choice, bool applyAll)
    {
        if (m_conflictCursor >= m_conflictQueue.size())
            return;

        if (applyAll)
            m_applyAllChoice = choice;

        const auto &conflict = m_conflictQueue[m_conflictCursor];
        const auto &item = m_snapshot.items[conflict.snapshotIndex];
        CommitCurrentItem(item, choice, applyAll);

        ++m_conflictCursor;
        if (m_conflictCursor >= m_conflictQueue.size())
        {
            m_conflictQueue.clear();
            m_conflictCursor = 0;
        }
    }
} // namespace Horo::Editor
