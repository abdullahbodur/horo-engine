#include "editor/EditorUiLogic.h"

#include <algorithm>

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

EditorExitDecision ResolveEditorExitDecision(bool hasUnsavedChanges) {
  return hasUnsavedChanges ? EditorExitDecision::PromptUnsavedConfirm
                           : EditorExitDecision::ExitImmediately;
}

EditorStatusText BuildEditorStatusText(const EditorStatusSnapshot& snapshot) {
  EditorStatusText out;
  out.selectionCount = std::max(0, snapshot.selectionCount);
  out.dirtyText = snapshot.dirty ? "yes" : "no";
  out.flyText = snapshot.flyMode ? "on" : "off";
  out.reloadText = snapshot.reloadPending ? "pending" : "idle";
  return out;
}

}  // namespace Editor
}  // namespace Monolith
