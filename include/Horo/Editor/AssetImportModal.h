#pragma once

/**
 * @file AssetImportModal.h
 * @brief Host-owned Asset Import workflow modal and its transient operation state.
 */

#include "Horo/Assets/AssetImportOperation.h"
#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Foundation/JobSystem.h"
#include "runtime/assets/importer/ProjectAssetImportCommitter.h"

#include <filesystem>
#include "Horo/Foundation/Logging/LogContext.h"

#include <memory>
#include <vector>

namespace Horo::Editor::Theme
{
    struct Fonts;
}

namespace Horo::Assets
{
    class AssetImporterCatalogSnapshot;
}

namespace Horo::Editor
{
    /**
     * @brief Host-owned asset import workflow modal.
     * @details Owns an AssetImportOperation and exposes its snapshots to the GUI
     *          presentation layer. The modal lifecycle is:
     *          OnOpen → Draw (repeated) → CanClose → OnClose.
     *
     *          The modal does not own importer strategies, project mutation,
     *          or ImGui state. Those are injected through context.
     */
    class AssetImportModal : public EditorModal
    {
    public:
        static constexpr std::uint64_t kModalId = 0x41534D504F525449ULL;

        /**
         * @brief Constructs the import workflow modal.
         * @param fonts Theme fonts reference (valid for modal lifetime).
         * @param jobs Job system for background import work.
         * @param catalog Published immutable importer catalog snapshot.
         */
        AssetImportModal(const Theme::Fonts& fonts, JobSystem& jobs,
                         std::shared_ptr<const Assets::AssetImporterCatalogSnapshot> catalog) noexcept;

        [[nodiscard]] ModalId Id() const override;
        [[nodiscard]] ModalPresentation Presentation() const override;
        [[nodiscard]] ModalClosePolicy ClosePolicy() const override;
        [[nodiscard]] Result<void> OnOpen(EditorModalContext& context) override;
        [[nodiscard]] ModalFrameResult Draw() override;
        [[nodiscard]] CloseDecision CanClose(ModalCloseReason reason) override;
        void OnClose(ModalCloseReason reason) override;

        /** @brief Returns the current operation snapshot for presentation rendering. */
        [[nodiscard]] const Assets::AssetImportSnapshot& Snapshot() const noexcept;

        /** @brief Returns the pinned importer catalog snapshot. */
        [[nodiscard]] const Assets::AssetImporterCatalogSnapshot& Catalog() const noexcept;

        /** @brief Sets the project root for asset destination paths. Call before BeginImport when known. */
        void SetProjectRoot(const std::filesystem::path &root) noexcept;

        /** @brief Returns the stored project root (empty if not set). */
        [[nodiscard]] const std::filesystem::path &ProjectRoot() const noexcept { return m_projectRoot; }

        /** @brief Initiates an import operation with the given source files. */
        [[nodiscard]] Result<void> BeginImport(const std::vector<std::filesystem::path> &sourceFiles,
                                                 const CancellationToken &cancellation);
        [[nodiscard]] Result<void> BeginImport(const std::vector<std::filesystem::path> &sourceFiles,
                                                 const std::filesystem::path &projectRoot,
                                                 const CancellationToken &cancellation);

        /** @brief Imports a single item by queue index. */
        [[nodiscard]] Result<void> ImportSingleItem(std::size_t index, const CancellationToken& cancellation);

        /** @brief Runs the import preparation phase. */
        [[nodiscard]] Result<void> PrepareImport(const CancellationToken& cancellation);

        /** @brief Selects an item by index for the settings panel. */
        void SelectItem(std::size_t index);

        /** @brief Conflict resolution choice for the popup. */
        enum class ConflictChoice : std::uint8_t
        {
            Overwrite,
            Rename,
            Skip,
        };

        /** @brief Per-file conflict pending resolution. */
        struct ConflictItem
        {
            std::string assetName;
            std::string conflictDescription;
            std::size_t snapshotIndex{0};
        };

        /** @brief Returns true when a conflict popup should be shown. */
        [[nodiscard]] bool HasPendingConflicts() const noexcept { return !m_conflictQueue.empty(); }

        /** @brief Returns the current conflict (front of queue). */
        [[nodiscard]] const ConflictItem &CurrentConflict() const { return m_conflictQueue[m_conflictCursor]; }

        /** @brief True when there are remaining conflicts after the current one. */
        [[nodiscard]] bool HasMoreConflicts() const noexcept { return m_conflictCursor + 1 < m_conflictQueue.size(); }

        /** @brief Apply the chosen resolution and advance to next conflict. */
        void ResolveCurrentConflict(ConflictChoice choice, bool applyAll);

        private:
        /** @brief Checks whether importing @p item would overwrite an existing asset. */
        bool WouldConflict(const Assets::AssetImportItem &item) const;

        /** @brief Commits one item and advances the conflict cursor. */
        void CommitCurrentItem(const Assets::AssetImportItem &item, ConflictChoice choice, bool applyAll);

        const Theme::Fonts &m_fonts;
        JobSystem &m_jobs;
        std::shared_ptr<const Assets::AssetImporterCatalogSnapshot> m_catalog;
        EditorDataBus *m_events = nullptr;
        std::filesystem::path m_projectRoot; /**< Stored for committer. */

        std::unique_ptr<Assets::AssetImportOperation> m_operation;
        std::unique_ptr<Assets::ProjectAssetImportCommitter> m_committer;
        Assets::AssetImportSnapshot m_snapshot;
        bool m_prepared{false};

        // Conflict resolution popup state
        std::vector<ConflictItem> m_conflictQueue;
        std::size_t m_conflictCursor{0};
        ConflictChoice m_applyAllChoice{ConflictChoice::Skip};

        // Source files queued for import
        std::vector<std::filesystem::path> m_queuedFiles;

        /// @brief RAII MDC frame active for the lifetime of this modal.
        std::unique_ptr<Log::LogContext> m_logCtx;
    };
} // namespace Horo::Editor
