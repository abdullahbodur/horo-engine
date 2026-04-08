#pragma once

#include <string>

#include "math/Vec3.h"

namespace Monolith {
namespace Editor {

struct EditorViewportRect {
  float minX = 0.0f;
  float minY = 0.0f;
  float maxX = 0.0f;
  float maxY = 0.0f;

  bool Contains(float x, float y) const {
    return x >= minX && x <= maxX && y >= minY && y <= maxY;
  }
};

struct EditorWindowRect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  bool valid = false;
};

bool ShouldToggleHelpPopup(bool currToggle,
                           bool prevToggle,
                           bool wantsTextInput,
                           bool anyItemActive);

bool ShouldOpenQuickOpen(bool currToggle,
                         bool prevToggle,
                         bool flyMode,
                         bool wantsTextInput,
                         bool anyItemActive);
bool ShouldOpenCommandPalette(bool currToggle,
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
EditorViewportRect BuildEditorViewportRect(float displayWidth,
                                           float displayHeight,
                                           float toolbarHeight,
                                           float statusHeight,
                                           float bottomDockHeight,
                                           float leftDockWidth,
                                           float rightPanelWidth);
EditorWindowRect ScaleEditorWindowRect(const EditorWindowRect& rect,
                                       float oldDisplayWidth,
                                       float oldDisplayHeight,
                                       float newDisplayWidth,
                                       float newDisplayHeight,
                                       float minWidth,
                                       float minHeight);
bool TryParseVec3Csv(const std::string& text, Vec3* outValue);

}  // namespace Editor
}  // namespace Monolith
