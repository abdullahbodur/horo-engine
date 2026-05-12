/**
 * @file EditorHistory.cpp
 * @brief Undo/redo snapshot stack for @ref Horo::Editor::EditorLayer.
 *
 * Snapshots capture document, saved baseline, object selection by ID, and assets-panel selection.
 * @ref EditorLayer::CommitHistoryChange pushes onto undo when the post-state differs; redo clears on new edits.
 */
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#include <vector>

namespace Horo::Editor {
    /** @copydoc EditorLayer::HistorySnapshotsEqual */
    bool EditorLayer::HistorySnapshotsEqual(const EditorHistorySnapshot &lhs,
                                            const EditorHistorySnapshot &rhs) {
        return lhs.document == rhs.document &&
               lhs.savedDocument == rhs.savedDocument &&
               lhs.selectedObjectIds == rhs.selectedObjectIds &&
               lhs.selectedAssetId == rhs.selectedAssetId;
    }

    /** @copydoc EditorLayer::TrimHistory */
    void EditorLayer::TrimHistory(std::vector<EditorHistorySnapshot> *history) {
        if (!history)
            return;
        if (history->size() <= kMaxEditorHistorySnapshots)
            return;
        history->erase(history->begin(),
                       history->begin() +
                       static_cast<std::ptrdiff_t>(history->size() -
                                                   kMaxEditorHistorySnapshots));
    }

    /** @copydoc EditorLayer::CaptureHistorySnapshot */
    EditorLayer::EditorHistorySnapshot EditorLayer::CaptureHistorySnapshot() const {
        EditorHistorySnapshot snapshot;
        snapshot.document = m_document;
        snapshot.savedDocument = m_lastSavedDocument;
        snapshot.selectedObjectIds = GetSelectedObjectIds();
        snapshot.selectedAssetId = m_selectedAssetId;
        return snapshot;
    }

    /** @copydoc EditorLayer::RestoreHistorySnapshot */
    void EditorLayer::RestoreHistorySnapshot(
        const EditorHistorySnapshot &snapshot) {
        m_document = snapshot.document;
        m_lastSavedDocument = snapshot.savedDocument;
        SetSelectedObjectIds(snapshot.selectedObjectIds);
        if (!snapshot.selectedAssetId.empty() &&
            m_document.assets.contains(snapshot.selectedAssetId)) {
            m_selectedAssetId = snapshot.selectedAssetId;
        } else {
            m_selectedAssetId.clear();
        }
        TriggerReload();
    }

    /** @copydoc EditorLayer::CommitHistoryChange */
    void EditorLayer::CommitHistoryChange(const EditorHistorySnapshot &before) {
        if (EditorHistorySnapshot after = CaptureHistorySnapshot();
            HistorySnapshotsEqual(before, after))
            return;

        m_undoHistory.push_back(before);
        TrimHistory(&m_undoHistory);
        m_redoHistory.clear();
    }

    /** @copydoc EditorLayer::BeginHistoryTransaction */
    void EditorLayer::BeginHistoryTransaction(const EditorHistorySnapshot &before) {
        if (m_historyTransactionOpen)
            return;
        m_historyTransactionBefore = before;
        m_historyTransactionOpen = true;
    }

    /** @copydoc EditorLayer::FinalizeHistoryTransaction */
    void EditorLayer::FinalizeHistoryTransaction() {
        if (!m_historyTransactionOpen)
            return;
        CommitHistoryChange(m_historyTransactionBefore);
        m_historyTransactionOpen = false;
    }

    /** @copydoc EditorLayer::ClearHistory */
    void EditorLayer::ClearHistory() {
        m_undoHistory.clear();
        m_redoHistory.clear();
        m_historyTransactionOpen = false;
    }

    /** @copydoc EditorLayer::RefreshHistorySavedBaseline */
    void EditorLayer::RefreshHistorySavedBaseline() {
        auto refreshSnapshot = [this](EditorHistorySnapshot *snapshot) {
            if (!snapshot)
                return;
            if (snapshot->document.filePath == m_document.filePath) {
                snapshot->savedDocument = m_lastSavedDocument;
                snapshot->document.dirty =
                        !(snapshot->document == snapshot->savedDocument);
            }
        };

        for (EditorHistorySnapshot &snapshot: m_undoHistory)
            refreshSnapshot(&snapshot);
        for (EditorHistorySnapshot &snapshot: m_redoHistory)
            refreshSnapshot(&snapshot);
        if (m_historyTransactionOpen)
            refreshSnapshot(&m_historyTransactionBefore);
    }

    /** @copydoc EditorLayer::CanUndoHistory */
    bool EditorLayer::CanUndoHistory() const { return !m_undoHistory.empty(); }

    /** @copydoc EditorLayer::CanRedoHistory */
    bool EditorLayer::CanRedoHistory() const { return !m_redoHistory.empty(); }

    /** @copydoc EditorLayer::UndoHistory */
    bool EditorLayer::UndoHistory() {
        FinalizeHistoryTransaction();
        if (m_undoHistory.empty())
            return false;

        const EditorHistorySnapshot current = CaptureHistorySnapshot();
        const EditorHistorySnapshot target = m_undoHistory.back();
        m_undoHistory.pop_back();
        m_redoHistory.push_back(current);
        TrimHistory(&m_redoHistory);
        RestoreHistorySnapshot(target);
        return true;
    }

    /** @copydoc EditorLayer::RedoHistory */
    bool EditorLayer::RedoHistory() {
        FinalizeHistoryTransaction();
        if (m_redoHistory.empty())
            return false;

        const EditorHistorySnapshot current = CaptureHistorySnapshot();
        const EditorHistorySnapshot target = m_redoHistory.back();
        m_redoHistory.pop_back();
        m_undoHistory.push_back(current);
        TrimHistory(&m_undoHistory);
        RestoreHistorySnapshot(target);
        return true;
    }
} // namespace Horo::Editor
