#include "editor/EditorUiLogic.h"

#include <algorithm>
#include <cstdio>

namespace Monolith {
namespace Editor {

bool ShouldToggleHelpPopup(bool currToggle,
                           bool prevToggle,
                           bool wantsTextInput,
                           bool anyItemActive) {
  return currToggle && !prevToggle && !wantsTextInput && !anyItemActive;
}

bool ShouldOpenQuickOpen(bool currToggle,
                         bool prevToggle,
                         bool flyMode,
                         bool wantsTextInput,
                         bool anyItemActive) {
  return currToggle && !prevToggle && !flyMode && !wantsTextInput && !anyItemActive;
}

bool ShouldOpenCommandPalette(bool currToggle,
                              bool prevToggle,
                              bool flyMode,
                              bool wantsTextInput,
                              bool anyItemActive) {
  return currToggle && !prevToggle && !flyMode && !wantsTextInput && !anyItemActive;
}

bool ShouldCopySelectionRef(bool currCopyRef,
                            bool prevCopyRef,
                            bool wantsTextInput,
                            bool anyItemActive,
                            bool hasPrimarySelection) {
  return currCopyRef && !prevCopyRef && !wantsTextInput && !anyItemActive && hasPrimarySelection;
}

bool ShouldRequestDeleteSelection(bool currDelete, bool prevDelete, bool hasSelection) {
  return currDelete && !prevDelete && hasSelection;
}

bool ShouldHandleEditorEscape(bool currEsc,
                              bool prevEsc,
                              bool wantsTextInput,
                              bool anyItemActive,
                              bool hasBlockingPopup) {
  return currEsc && !prevEsc && !wantsTextInput && !anyItemActive && !hasBlockingPopup;
}

bool CanEditSingleSelection(int selectionCount, int primaryIndex, int objectCount) {
  return selectionCount == 1 && primaryIndex >= 0 && primaryIndex < objectCount;
}

EditorExitDecision ResolveEditorExitDecision(bool hasUnsavedChanges) {
  return hasUnsavedChanges ? EditorExitDecision::PromptUnsavedConfirm
                           : EditorExitDecision::ExitImmediately;
}

bool ShouldFinalizeEditorClose(bool closeRequested, bool hasPendingReload) {
  return closeRequested && !hasPendingReload;
}

EditorStatusText BuildEditorStatusText(const EditorStatusSnapshot& snapshot) {
  EditorStatusText out;
  out.selectionCount = std::max(0, snapshot.selectionCount);
  out.dirtyText = snapshot.dirty ? "yes" : "no";
  out.flyText = snapshot.flyMode ? "on" : "off";
  out.reloadText = snapshot.reloadPending ? "pending" : "idle";
  return out;
}

float ComputeEditorLeftDockWidth(float displayWidth) {
  return std::clamp(displayWidth * 0.16f, 220.0f, 320.0f);
}

float ComputeEditorRightPanelWidth(float displayWidth) {
  return std::clamp(displayWidth * 0.18f, 280.0f, 380.0f);
}

float ComputeEditorBottomDockHeight(float displayHeight) {
  return std::clamp(displayHeight * 0.18f, 180.0f, 260.0f);
}

EditorViewportRect BuildEditorViewportRect(float displayWidth,
                                           float displayHeight,
                                           float toolbarHeight,
                                           float statusHeight,
                                           float bottomDockHeight,
                                           float leftDockWidth,
                                           float rightPanelWidth) {
  EditorViewportRect rect;
  rect.minX = std::max(0.0f, leftDockWidth);
  rect.minY = std::max(0.0f, toolbarHeight);
  rect.maxX = std::max(rect.minX, displayWidth - rightPanelWidth);
  rect.maxY = std::max(rect.minY, displayHeight - statusHeight - bottomDockHeight);
  return rect;
}

bool TryParseVec3Csv(const std::string& text, Vec3* outValue) {
  if (!outValue)
    return false;

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
#ifdef _WIN32
  if (sscanf_s(text.c_str(), " %f , %f , %f ", &x, &y, &z) != 3)
#else
  if (std::sscanf(text.c_str(), " %f , %f , %f ", &x, &y, &z) != 3)
#endif
    return false;

  *outValue = {x, y, z};
  return true;
}

}  // namespace Editor
}  // namespace Monolith
