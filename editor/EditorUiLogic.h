#pragma once

namespace Monolith {
namespace Editor {

bool ShouldToggleHelpPopup(bool currToggle,
                           bool prevToggle,
                           bool wantsTextInput,
                           bool anyItemActive);

bool ShouldOpenQuickOpen(bool currToggle,
                         bool prevToggle,
                         bool flyMode,
                         bool wantsTextInput,
                         bool anyItemActive);

bool ShouldCopySelectionRef(bool currCopyRef,
                            bool prevCopyRef,
                            bool wantsTextInput,
                            bool anyItemActive,
                            bool hasPrimarySelection);

bool ShouldRequestDeleteSelection(bool currDelete, bool prevDelete, bool hasSelection);

bool ShouldHandleEditorEscape(bool currEsc,
                              bool prevEsc,
                              bool wantsTextInput,
                              bool anyItemActive,
                              bool hasBlockingPopup);

bool CanEditSingleSelection(int selectionCount, int primaryIndex, int objectCount);

struct EditorStatusSnapshot {
  int selectionCount = 0;
  bool dirty = false;
  bool flyMode = false;
  bool reloadPending = false;
};

struct EditorStatusText {
  int selectionCount = 0;
  const char* dirtyText = "no";
  const char* flyText = "off";
  const char* reloadText = "idle";
};

enum class EditorExitDecision {
  ExitImmediately,
  PromptUnsavedConfirm,
};

EditorExitDecision ResolveEditorExitDecision(bool hasUnsavedChanges);
bool ShouldFinalizeEditorClose(bool closeRequested, bool hasPendingReload);

EditorStatusText BuildEditorStatusText(const EditorStatusSnapshot& snapshot);

}  // namespace Editor
}  // namespace Monolith
