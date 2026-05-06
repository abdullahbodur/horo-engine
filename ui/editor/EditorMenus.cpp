#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#include <GLFW/glfw3.h>

namespace Horo::Editor {

void EditorLayer::OnMenuNewScene() {
  RequestSceneAction(PendingSceneAction::NewScene);
}

void EditorLayer::OnMenuOpenScene() {
  RequestSceneAction(PendingSceneAction::OpenSceneFile);
}

void EditorLayer::OnMenuAddPanel() {
  using enum SceneObjectType;
  AddObject(Panel);
}

void EditorLayer::OnMenuAddProp() {
  using enum SceneObjectType;
  AddObject(Prop);
}

void EditorLayer::OnMenuUndo() {
  if (CanUndoHistory())
    UndoHistory();
}

void EditorLayer::OnMenuRedo() {
  if (CanRedoHistory())
    RedoHistory();
}

void EditorLayer::OnMenuFlyMode() {
  m_flyMode = !m_flyMode;
  m_flyCamInitialized = false;
  m_prevCursorInit = false;
  glfwSetInputMode(m_window, GLFW_CURSOR,
                   m_flyMode ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void EditorLayer::OnMenuResetLayout() {
  m_resetDockLayoutRequested = true;
}

void EditorLayer::OnMenuSettings() {
  m_settingsModal.SetOpen(true);
  *m_settingsModal.GetDraft() = m_mcpController.GetSettings();
  m_settingsModal.GetError()->clear();
}

void EditorLayer::OnMenuCloseEditor() {
  RequestSceneAction(PendingSceneAction::CloseEditor);
}

void EditorLayer::OnMenuAddLight() {
  using enum SceneObjectType;
  AddObject(Light);
}

void EditorLayer::OnMenuAddCamera() {
  using enum SceneObjectType;
  AddObject(Camera);
}

void EditorLayer::OnMenuAddPropFromAsset() {
  if (!m_selectedAssetId.empty() && m_document.assets.contains(m_selectedAssetId))
    AddObjectFromSelectedAsset();
}

void EditorLayer::OnMenuRename() {
  const int primaryIdx = PrimaryIdx();
  const bool hasSingleSelection = CanEditSingleSelection(
      static_cast<int>(m_selectedIndices.size()), primaryIdx,
      static_cast<int>(m_document.objects.size()));
  if (hasSingleSelection && primaryIdx >= 0)
    OpenRenameObjectModal(primaryIdx);
}

void EditorLayer::OnMenuCreatePrefab() {
}

void EditorLayer::OnMenuDuplicate() {
  if (!m_selectedIndices.empty())
    DuplicateSelectedObjects();
}

void EditorLayer::OnMenuDelete() {
  if (!m_selectedIndices.empty())
    m_uiWidgets.OpenConfirmDeleteObjects(m_selectedIndices);
}

void EditorLayer::OnMenuHelp() {
  m_helpPopup.SetOpen(true);
}

void EditorLayer::OnMenuQuickOpen() {
  m_quickOpenOpen = true;
}

void EditorLayer::OnMenuCommandPalette() {
  m_commandPaletteOpen = true;
  m_commandPaletteQuery.clear();
}

void EditorLayer::OnMenuViewResetLayout() {
  m_resetDockLayoutRequested = true;
}

} // namespace Horo::Editor
