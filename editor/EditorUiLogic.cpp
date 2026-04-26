#include "editor/EditorUiLogic.h"

#include <algorithm>
#include <cstdio>

namespace Monolith::Editor {
namespace {
constexpr EditorViewGimbalMetrics kEditorViewGimbalMetrics{};
} // namespace

bool ShouldToggleHelpPopup(bool currToggle, bool prevToggle,
                           bool wantsTextInput, bool anyItemActive) {
  return currToggle && !prevToggle && !wantsTextInput && !anyItemActive;
}

bool ShouldOpenQuickOpen(bool currToggle, bool prevToggle, bool flyMode,
                         bool wantsTextInput, bool anyItemActive) {
  return currToggle && !prevToggle && !flyMode && !wantsTextInput &&
         !anyItemActive;
}

bool ShouldOpenCommandPalette(bool currToggle, bool prevToggle, bool flyMode,
                              bool wantsTextInput, bool anyItemActive) {
  return currToggle && !prevToggle && !flyMode && !wantsTextInput &&
         !anyItemActive;
}

bool ShouldCopySelectionRef(bool currCopyRef, bool prevCopyRef,
                            bool wantsTextInput, bool anyItemActive,
                            bool hasPrimarySelection) {
  return currCopyRef && !prevCopyRef && !wantsTextInput && !anyItemActive &&
         hasPrimarySelection;
}

bool ShouldRequestDeleteSelection(bool currDelete, bool prevDelete,
                                  bool hasSelection) {
  return currDelete && !prevDelete && hasSelection;
}

bool ShouldHandleEditorEscape(bool currEsc, bool prevEsc, bool wantsTextInput,
                              bool anyItemActive, bool hasBlockingPopup) {
  return currEsc && !prevEsc && !wantsTextInput && !anyItemActive &&
         !hasBlockingPopup;
}

bool CanEditSingleSelection(int selectionCount, int primaryIndex,
                            int objectCount) {
  return selectionCount == 1 && primaryIndex >= 0 && primaryIndex < objectCount;
}

EditorExitDecision ResolveEditorExitDecision(bool hasUnsavedChanges) {
  return hasUnsavedChanges ? EditorExitDecision::PromptUnsavedConfirm
                           : EditorExitDecision::ExitImmediately;
}

bool ShouldFinalizeEditorClose(bool closeRequested, bool hasPendingReload) {
  return closeRequested && !hasPendingReload;
}

EditorStatusText BuildEditorStatusText(const EditorStatusSnapshot &snapshot) {
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

EditorViewportAssetDropResult
DrawViewportAssetDropTarget(bool playMode, float targetWidth,
                            float targetHeight,
                            void *userData, // NOSONAR: ImGui API requires void*
                            const EditorViewportAssetDropHandler &onDrop) {
  EditorViewportAssetDropResult result;
  if (const ImGuiPayload *activeDrag = ImGui::GetDragDropPayload();
      playMode || !activeDrag || !activeDrag->IsDataType("ASSET_ID"))
    return result;
  if (targetWidth <= 0.0f || targetHeight <= 0.0f)
    return result;

  result.targetVisible = true;
  // The viewport drag-drop target spans the full panel, so it must explicitly
  // allow later widgets (wireframe toggle, view gimbal) to overlap it.
  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton("##viewport_drop_target",
                         ImVec2(targetWidth, targetHeight));
  if (!ImGui::BeginDragDropTarget())
    return result;

  if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
          "ASSET_ID", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
    result.payloadMatched = true;
    result.delivered = payload->Delivery;
    if (payload->Delivery && onDrop) {
      result.accepted =
          onDrop(userData, static_cast<const char *>(payload->Data));
    }
  }
  ImGui::EndDragDropTarget();
  return result;
}

EditorViewportRect
BuildEditorViewportRect(float displayWidth, float displayHeight,
                        float toolbarHeight, float statusHeight,
                        float bottomDockHeight, float leftDockWidth,
                        float rightPanelWidth) {
  EditorViewportRect rect;
  rect.minX = std::max(0.0f, leftDockWidth);
  rect.minY = std::max(0.0f, toolbarHeight);
  rect.maxX = std::max(rect.minX, displayWidth - rightPanelWidth);
  rect.maxY =
      std::max(rect.minY, displayHeight - statusHeight - bottomDockHeight);
  return rect;
}

const EditorViewGimbalMetrics &GetEditorViewGimbalMetrics() {
  return kEditorViewGimbalMetrics;
}

EditorViewGimbalLayout
BuildEditorViewGimbalLayout(const EditorViewportRect &viewportRect,
                            float displayWidth, float rightPanelWidth,
                            float toolbarHeight) {
  const EditorViewGimbalMetrics &metrics = GetEditorViewGimbalMetrics();

  const float viewportRight = viewportRect.maxX > viewportRect.minX
                                  ? viewportRect.maxX
                                  : displayWidth - rightPanelWidth;
  const float viewportTop =
      viewportRect.maxY > viewportRect.minY ? viewportRect.minY : toolbarHeight;
  const float wx = viewportRight - metrics.windowWidth - metrics.edgeMargin;
  const float wy = viewportTop + metrics.edgeMargin;
  const float btnX = wx - metrics.buttonFrameSize - metrics.buttonGap;
  const float btnY = wy;

  EditorViewGimbalLayout layout;
  layout.wireButtonRect = {btnX, btnY, btnX + metrics.buttonFrameSize,
                           btnY + metrics.buttonFrameSize};
  layout.gimbalRect = {wx, wy, wx + metrics.windowWidth,
                       wy + metrics.windowHeight};
  layout.pickRect = {
      std::min(layout.wireButtonRect.minX, layout.gimbalRect.minX),
      std::min(layout.wireButtonRect.minY, layout.gimbalRect.minY),
      std::max(layout.wireButtonRect.maxX, layout.gimbalRect.maxX),
      std::max(layout.wireButtonRect.maxY, layout.gimbalRect.maxY)};
  return layout;
}

bool TryParseVec3Csv(const std::string &text, Vec3 *outValue) {
  if (!outValue)
    return false;

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  int consumed = 0;
#ifdef _WIN32
  if (sscanf_s(text.c_str(), " %f , %f , %f %n", &x, &y, &z, &consumed) != 3)
#else
  if (std::sscanf(text.c_str(), " %f , %f , %f %n", &x, &y, &z, &consumed) != 3)
#endif
    return false;

  if (consumed != static_cast<int>(text.size()))
    return false;

  *outValue = {x, y, z};
  return true;
}
} // namespace Monolith::Editor
