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

struct EditorViewGimbalLayout {
  EditorViewportRect wireButtonRect;
  EditorViewportRect gimbalRect;
  EditorViewportRect pickRect;
};

struct EditorViewGimbalMetrics {
  float windowWidth = 128.0f;
  float windowHeight = 138.0f;
  float buttonSize = 28.0f;
  float buttonFrameSize = 36.0f;
  float buttonGap = 10.0f;
  float edgeMargin = 10.0f;
  float titleOffsetX = 8.0f;
  float titleOffsetY = 8.0f;
  float contentOffsetX = 10.0f;
  float contentOffsetY = 26.0f;
  float hitRegionHeight = 94.0f;
  float pivotTextOffsetY = 18.0f;
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

using EditorViewportAssetDropHandler = bool (*)(void* userData, const char* assetId);

struct EditorViewportAssetDropResult {
  bool targetVisible = false;
  bool payloadMatched = false;
  bool delivered = false;
  bool accepted = false;
};

enum class EditorExitDecision {
  ExitImmediately,
  PromptUnsavedConfirm,
};

EditorExitDecision ResolveEditorExitDecision(bool hasUnsavedChanges);
bool ShouldFinalizeEditorClose(bool closeRequested, bool hasPendingReload);

EditorStatusText BuildEditorStatusText(const EditorStatusSnapshot& snapshot);
float ComputeEditorLeftDockWidth(float displayWidth);
float ComputeEditorRightPanelWidth(float displayWidth);
float ComputeEditorBottomDockHeight(float displayHeight);
EditorViewportAssetDropResult DrawViewportAssetDropTarget(bool playMode,
                                                          float targetWidth,
                                                          float targetHeight,
                                                          void* userData,
                                                          EditorViewportAssetDropHandler onDrop);
EditorViewportRect BuildEditorViewportRect(float displayWidth,
                                           float displayHeight,
                                           float toolbarHeight,
                                           float statusHeight,
                                           float bottomDockHeight,
                                           float leftDockWidth,
                                           float rightPanelWidth);
const EditorViewGimbalMetrics& GetEditorViewGimbalMetrics();
EditorViewGimbalLayout BuildEditorViewGimbalLayout(const EditorViewportRect& viewportRect,
                                                   float displayWidth,
                                                   float rightPanelWidth,
                                                   float toolbarHeight);
bool TryParseVec3Csv(const std::string& text, Vec3* outValue);

}  // namespace Editor
}  // namespace Monolith
