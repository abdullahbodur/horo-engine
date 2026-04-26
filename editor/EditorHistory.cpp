// Undo/redo history management for EditorLayer.
// Method definitions are in this file; declarations remain in EditorLayer.h.
#include "editor/EditorLayer.h"
#include "editor/EditorLayerInternal.h"

#include <vector>

namespace Horo::Editor {
    bool EditorLayer::HistorySnapshotsEqual(const EditorHistorySnapshot &lhs,
                                            const EditorHistorySnapshot &rhs) {
        return lhs.document == rhs.document &&
               lhs.savedDocument == rhs.savedDocument &&
               lhs.selectedObjectIds == rhs.selectedObjectIds &&
               lhs.selectedAssetId == rhs.selectedAssetId;
    }

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

    EditorLayer::EditorHistorySnapshot EditorLayer::CaptureHistorySnapshot() const {
        EditorHistorySnapshot snapshot;
        snapshot.document = m_document;
        snapshot.savedDocument = m_lastSavedDocument;
        snapshot.selectedObjectIds = GetSelectedObjectIds();
        snapshot.selectedAssetId = m_selectedAssetId;
        return snapshot;
    }

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

    void EditorLayer::CommitHistoryChange(const EditorHistorySnapshot &before) {
        if (EditorHistorySnapshot after = CaptureHistorySnapshot();
            HistorySnapshotsEqual(before, after))
            return;

        m_undoHistory.push_back(before);
        TrimHistory(&m_undoHistory);
        m_redoHistory.clear();
    }

    void EditorLayer::BeginHistoryTransaction(const EditorHistorySnapshot &before) {
        if (m_historyTransactionOpen)
            return;
        m_historyTransactionBefore = before;
        m_historyTransactionOpen = true;
    }

    void EditorLayer::FinalizeHistoryTransaction() {
        if (!m_historyTransactionOpen)
            return;
        CommitHistoryChange(m_historyTransactionBefore);
        m_historyTransactionOpen = false;
    }

    void EditorLayer::ClearHistory() {
        m_undoHistory.clear();
        m_redoHistory.clear();
        m_historyTransactionOpen = false;
    }

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

    bool EditorLayer::CanUndoHistory() const { return !m_undoHistory.empty(); }

    bool EditorLayer::CanRedoHistory() const { return !m_redoHistory.empty(); }

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
