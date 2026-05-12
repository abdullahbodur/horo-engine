/**
 * @file EditorMenus.cpp
 * @brief Menu bar and palette command handlers bound to @ref EditorLayer scene/UI actions.
 *
 * Each handler forwards to scene actions, history, layout reset, or modal open helpers declared on EditorLayer.
 */
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#include <GLFW/glfw3.h>

namespace Horo::Editor {

/** @copydoc EditorLayer::OnMenuNewScene */
void EditorLayer::OnMenuNewScene() {
  RequestSceneAction(PendingSceneAction::NewScene);
}

/** @copydoc EditorLayer::OnMenuOpenScene */
void EditorLayer::OnMenuOpenScene() {
  RequestSceneAction(PendingSceneAction::OpenSceneFile);
}

/** @copydoc EditorLayer::OnMenuAddPanel */
void EditorLayer::OnMenuAddPanel() {
  using enum SceneObjectType;
  AddObject(Panel);
}

/** @copydoc EditorLayer::OnMenuAddProp */
void EditorLayer::OnMenuAddProp() {
  using enum SceneObjectType;
  AddObject(Prop);
}

/** @copydoc EditorLayer::OnMenuUndo */
void EditorLayer::OnMenuUndo() {
  if (CanUndoHistory())
    UndoHistory();
}

/** @copydoc EditorLayer::OnMenuRedo */
void EditorLayer::OnMenuRedo() {
  if (CanRedoHistory())
    RedoHistory();
}

/** @copydoc EditorLayer::OnMenuFlyMode */
void EditorLayer::OnMenuFlyMode() {
  m_flyMode = !m_flyMode;
  m_flyCamInitialized = false;
  m_prevCursorInit = false;
  glfwSetInputMode(m_window, GLFW_CURSOR,
                   m_flyMode ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

/** @copydoc EditorLayer::OnMenuResetLayout */
void EditorLayer::OnMenuResetLayout() {
  m_resetDockLayoutRequested = true;
}

/** @copydoc EditorLayer::OnMenuSettings */
void EditorLayer::OnMenuSettings() {
  m_settingsModal.Open(m_mcpController.GetSettings(),
                       m_userSettingsDocument.settings);
}

/** @copydoc EditorLayer::OnMenuCloseEditor */
void EditorLayer::OnMenuCloseEditor() {
  RequestSceneAction(PendingSceneAction::CloseEditor);
}

/** @copydoc EditorLayer::OnMenuAddLight */
void EditorLayer::OnMenuAddLight() {
  using enum SceneObjectType;
  AddObject(Light);
}

/** @copydoc EditorLayer::OnMenuAddCamera */
void EditorLayer::OnMenuAddCamera() {
  using enum SceneObjectType;
  AddObject(Camera);
}

/** @copydoc EditorLayer::OnMenuAddPropFromAsset */
void EditorLayer::OnMenuAddPropFromAsset() {
  if (!m_selectedAssetId.empty() && m_document.assets.contains(m_selectedAssetId))
    AddObjectFromSelectedAsset();
}

/** @copydoc EditorLayer::OnMenuRename */
void EditorLayer::OnMenuRename() {
  const int primaryIdx = PrimaryIdx();
  const bool hasSingleSelection = CanEditSingleSelection(
      static_cast<int>(m_selectedIndices.size()), primaryIdx,
      static_cast<int>(m_document.objects.size()));
  if (hasSingleSelection && primaryIdx >= 0)
    OpenRenameObjectModal(primaryIdx);
}

/** @copydoc EditorLayer::OnMenuCreatePrefab */
void EditorLayer::OnMenuCreatePrefab() {
  std::string prefabError;
  if (!CreatePrefabFromSelection(&prefabError))
    LogError("[Editor] Create prefab failed: {}", prefabError);
}

/** @copydoc EditorLayer::OnMenuDuplicate */
void EditorLayer::OnMenuDuplicate() {
  if (!m_selectedIndices.empty())
    DuplicateSelectedObjects();
}

/** @copydoc EditorLayer::OnMenuDelete */
void EditorLayer::OnMenuDelete() {
  if (!m_selectedIndices.empty())
    m_uiWidgets.OpenConfirmDeleteObjects(m_selectedIndices);
}

/** @copydoc EditorLayer::OnMenuHelp */
void EditorLayer::OnMenuHelp() {
  m_helpPopup.SetOpen(true);
}

/** @copydoc EditorLayer::OnMenuQuickOpen */
void EditorLayer::OnMenuQuickOpen() {
  m_quickOpenOpen = true;
}

/** @copydoc EditorLayer::OnMenuCommandPalette */
void EditorLayer::OnMenuCommandPalette() {
  m_commandPaletteOpen = true;
  m_commandPaletteQuery.clear();
}

/** @copydoc EditorLayer::OnMenuViewResetLayout */
void EditorLayer::OnMenuViewResetLayout() {
  m_resetDockLayoutRequested = true;
}

} // namespace Horo::Editor
