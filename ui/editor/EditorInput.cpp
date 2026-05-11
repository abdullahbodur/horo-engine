/**
 * @file EditorInput.cpp
 * @brief Per-frame keyboard/mouse handling for @ref EditorLayer: shortcuts, fly camera, gizmo, and picking.
 */
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "renderer/DebugDraw.h"
#include "ui/editor/EditorDebugTrace.h"
#include "ui/editor/EditorUiLogic.h"
#include "ui/editor/TransformGizmo.h"

namespace Horo::Editor {

/** @copydoc EditorLayer::HandleEditorKeyboardShortcuts */
void EditorLayer::HandleEditorKeyboardShortcuts( // NOSONAR
    const Camera &cam) {
  if (!m_window)
    return;
  const ImGuiIO &io = ImGui::GetIO();
  const bool accelHeld =
      glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(m_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(m_window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
      glfwGetKey(m_window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
  const bool shiftHeld =
      glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
      glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
  const bool slashHeld = glfwGetKey(m_window, GLFW_KEY_SLASH) == GLFW_PRESS;
  const bool f1Held = glfwGetKey(m_window, GLFW_KEY_F1) == GLFW_PRESS;
  const bool currHelpToggle = f1Held || (slashHeld && shiftHeld);
  if (ShouldToggleHelpPopup(currHelpToggle, m_prevHelpToggle, io.WantTextInput,
                            ImGui::IsAnyItemActive())) {
    m_helpPopup.SetOpen(!m_helpPopup.IsOpen());
    if (!m_helpPopup.IsOpen())
      m_helpPopup.SetSearchQuery("");
  }
  m_prevHelpToggle = currHelpToggle;

  const bool currQuickOpenToggle =
      accelHeld && !shiftHeld && glfwGetKey(m_window, GLFW_KEY_P) == GLFW_PRESS;
  if (ShouldOpenQuickOpen(currQuickOpenToggle, m_prevQuickOpenToggle, m_flyMode,
                          io.WantTextInput, ImGui::IsAnyItemActive())) {
    m_quickOpenOpen = true;
    m_quickOpenQuery.clear();
  }
  m_prevQuickOpenToggle = currQuickOpenToggle;

  const bool currCommandPaletteToggle =
      accelHeld && shiftHeld && glfwGetKey(m_window, GLFW_KEY_P) == GLFW_PRESS;
  if (ShouldOpenCommandPalette(currCommandPaletteToggle,
                               m_prevCommandPaletteToggle, m_flyMode,
                               io.WantTextInput, ImGui::IsAnyItemActive())) {
    m_commandPaletteOpen = true;
    m_commandPaletteQuery.clear();
  }
  m_prevCommandPaletteToggle = currCommandPaletteToggle;

  const bool currEsc = glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
  const bool hasBlockingPopup = m_helpPopup.IsOpen() || m_quickOpenOpen ||
                                m_commandPaletteOpen || m_assetSearchOpen ||
                                m_uiWidgets.IsConfirmDeleteObjectsOpen() ||
                                m_uiWidgets.IsConfirmDeleteAssetOpen() || m_uiWidgets.IsConfirmExitOpen();
  const bool currUndo =
      accelHeld && !shiftHeld && glfwGetKey(m_window, GLFW_KEY_Z) == GLFW_PRESS;
  const bool currRedo =
      accelHeld &&
      ((shiftHeld && glfwGetKey(m_window, GLFW_KEY_Z) == GLFW_PRESS) ||
       glfwGetKey(m_window, GLFW_KEY_Y) == GLFW_PRESS);
  if (!m_flyMode && !io.WantTextInput && !ImGui::IsAnyItemActive() &&
      !hasBlockingPopup) {
    if (currUndo && !m_prevUndo)
      UndoHistory();
    if (currRedo && !m_prevRedo)
      RedoHistory();
  }
  m_prevUndo = currUndo;
  m_prevRedo = currRedo;
  // Escape: dismiss gizmo only (no editor quit on Escape; use File / window
  // close).
  if (currEsc && !m_prevEsc && !io.WantTextInput && !ImGui::IsAnyItemActive() &&
      !hasBlockingPopup && m_gizmo.IsActive()) {
    RequestGizmoMode(GizmoMode::None);
    if (m_gizmoHistoryPending) {
      FinalizeHistoryTransaction();
      m_gizmoHistoryPending = false;
    }
  }
  m_prevEsc = currEsc;

  // Tab toggles fly mode
  bool currTab = glfwGetKey(m_window, GLFW_KEY_TAB) == GLFW_PRESS;
  if (currTab && !m_prevTab && !io.WantTextInput && !ImGui::IsAnyItemActive())
    ToggleFlyMode(cam);
  m_prevTab = currTab;
}

/** @copydoc EditorLayer::UpdateFlyCameraWithGizmoSync */
void EditorLayer::UpdateFlyCameraWithGizmoSync(float dt, Camera &cam) {
  UpdateFlyCamera(dt, cam);
  // Keep gizmo anchored to object even while flying
  if (m_gizmo.IsActive()) {
    const int syncIdx = PrimaryIdx();
    if (syncIdx >= 0 && syncIdx < static_cast<int>(m_document.objects.size())) {
      const auto &syncObj = m_document.objects[syncIdx];
      Quaternion syncRot = Quaternion::FromEuler(ToRadians(syncObj.pitch),
                                                 ToRadians(syncObj.yaw),
                                                 ToRadians(syncObj.roll));
      m_gizmo.SyncTarget(syncObj.position, syncRot, syncObj.scale);
    }
  }
}

/** @copydoc EditorLayer::RequestGizmoMode */
void EditorLayer::RequestGizmoMode(GizmoMode mode) {
  using enum GizmoMode;
  m_currentGizmoMode = mode;
  if (mode == None) {
    m_gizmo.Deactivate();
    return;
  }
  // Activate only when there is a valid single selection and no drag in progress.
  const int gizmoIdx = PrimaryIdx();
  if (gizmoIdx < 0 || gizmoIdx >= static_cast<int>(m_document.objects.size()))
    return;
  if (m_gizmo.GetDragAxis() != GizmoAxis::None)
    return;
  const auto &gizmoObj = m_document.objects[gizmoIdx];
  const Quaternion gizmoRot = Quaternion::FromEuler(ToRadians(gizmoObj.pitch),
                                                    ToRadians(gizmoObj.yaw),
                                                    ToRadians(gizmoObj.roll));
  m_gizmo.Activate(mode, gizmoObj.position, gizmoRot, gizmoObj.scale);
}

/** @copydoc EditorLayer::HandleGizmoModeHotkeys */
void EditorLayer::HandleGizmoModeHotkeys(const ImGuiIO &io) {
  using enum GizmoMode;
  const bool currW = glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS;
  const bool currE = glfwGetKey(m_window, GLFW_KEY_E) == GLFW_PRESS;
  const bool currR = glfwGetKey(m_window, GLFW_KEY_R) == GLFW_PRESS;
  if (!io.WantTextInput && !ImGui::IsAnyItemActive()) {
    if (currW && (!m_prevGizmoW || m_gizmo.GetMode() != Translate))
      RequestGizmoMode(Translate);
    else if (currE && (!m_prevGizmoE || m_gizmo.GetMode() != Rotate))
      RequestGizmoMode(Rotate);
    else if (currR && (!m_prevGizmoR || m_gizmo.GetMode() != Scale))
      RequestGizmoMode(Scale);
  }
  m_prevGizmoW = currW;
  m_prevGizmoE = currE;
  m_prevGizmoR = currR;
}

/** @copydoc EditorLayer::SyncGizmoToSelection */
void EditorLayer::SyncGizmoToSelection() {
  if (!m_gizmo.IsActive())
    return;
  const int syncIdx = PrimaryIdx();
  if (syncIdx < 0 || syncIdx >= static_cast<int>(m_document.objects.size())) {
    m_gizmo.Deactivate();
    if (m_gizmoHistoryPending) {
      FinalizeHistoryTransaction();
      m_gizmoHistoryPending = false;
    }
    return;
  }
  const auto &syncObj = m_document.objects[syncIdx];
  Quaternion syncRot =
      Quaternion::FromEuler(ToRadians(syncObj.pitch), ToRadians(syncObj.yaw),
                            ToRadians(syncObj.roll));
  m_gizmo.SyncTarget(syncObj.position, syncRot, syncObj.scale);
}

/** @copydoc EditorLayer::ApplyGizmoTranslateSnapping */
void EditorLayer::ApplyGizmoTranslateSnapping( // NOSONAR: cpp:S3776 complex
                                               // AABB snapping geometry;
                                               // extraction would obscure snap
                                               // semantics
    GizmoAxis dragAxis, int primIdx, Vec3 &dPos) {
  const bool ctrlHeld =
      glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(m_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(m_window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
      glfwGetKey(m_window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
  const bool shiftHeld =
      glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
      glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

  // Extracts the double face-pair loop to keep nesting within S134 limits.
  auto checkFacePairs = [](float sf0, float sf1, float of0, float of1,
                           float &bestDist, float &bestOffset, bool &didSnap) {
    const std::array<float, 2> selfFaces = {sf0, sf1};
    const std::array<float, 2> otherFaces = {of0, of1};
    for (float selfFace : selfFaces) {
      for (float otherFace : otherFaces) {
        const float gap = std::abs(selfFace - otherFace);
        if (gap < bestDist) {
          bestDist = gap;
          bestOffset = otherFace - selfFace;
          didSnap = true;
        }
      }
    }
  };

  if (ctrlHeld && dragAxis != GizmoAxis::None && primIdx >= 0 &&
      primIdx < static_cast<int>(m_document.objects.size())) {
    const auto &selfObj = m_document.objects[primIdx];
    Vec3 selfCenter = selfObj.position;
    Vec3 selfHalf = selfObj.scale;
    if (m_liveRegistry)
      TryPropWorldAabb(*m_liveRegistry, selfObj, selfCenter, selfHalf);
    Vec3 rawPos = selfCenter + dPos;

    int axisIdx = 2;
    if (dragAxis == GizmoAxis::X)
      axisIdx = 0;
    else if (dragAxis == GizmoAxis::Y)
      axisIdx = 1;

    float bestDist = std::numeric_limits<float>::max();
    float bestOffset = 0.0f;
    bool didSnap = false;

    for (int oi = 0; oi < static_cast<int>(m_document.objects.size()); ++oi) {
      if (IsSelected(oi))
        continue;
      const auto &other = m_document.objects[oi];
      Vec3 otherCenter = other.position;
      Vec3 otherHalfV = other.scale;
      if (m_liveRegistry)
        TryPropWorldAabb(*m_liveRegistry, other, otherCenter, otherHalfV);
      float otherHalf = otherHalfV[axisIdx];
      float selfHalf1 = selfHalf[axisIdx];

      const std::array<float, 2> selfFaces = {rawPos[axisIdx] - selfHalf1,
                                              rawPos[axisIdx] + selfHalf1};
      const std::array<float, 2> otherFaces = {
          otherCenter[axisIdx] - otherHalf, otherCenter[axisIdx] + otherHalf};

      checkFacePairs(selfFaces[0], selfFaces[1], otherFaces[0], otherFaces[1],
                     bestDist, bestOffset, didSnap);
    }

    if (didSnap) {
      Vec3 snappedCenter = rawPos;
      snappedCenter[axisIdx] += bestOffset;
      Vec3 centerToOrigin = selfObj.position - selfCenter;
      dPos = snappedCenter + centerToOrigin - selfObj.position;

      Vec3 axisDir = m_gizmo.AxisDir(dragAxis);
      Vec3 facePoint = selfObj.position + dPos;
      DebugDraw::Line(facePoint - axisDir * 0.3f, facePoint + axisDir * 0.3f,
                      {1.0f, 1.0f, 0.0f, 1.0f});
    }
  }

  if (shiftHeld && dPos.LengthSq() > 1e-12f && primIdx >= 0 &&
      primIdx < static_cast<int>(m_document.objects.size())) {
    constexpr float kGridSize = 0.5f;
    const auto &selfObj = m_document.objects[primIdx];
    Vec3 rawPos = selfObj.position + dPos;
    rawPos.x = std::round(rawPos.x / kGridSize) * kGridSize;
    rawPos.y = std::round(rawPos.y / kGridSize) * kGridSize;
    rawPos.z = std::round(rawPos.z / kGridSize) * kGridSize;
    dPos = rawPos - selfObj.position;
  }
}

/** @copydoc EditorLayer::UpdateNonFlyModeInput */
void EditorLayer::UpdateNonFlyModeInput(const Camera &cam, int screenW,
                                        int screenH) {
  if (!m_window)
    return;
  const ImGuiIO &io = ImGui::GetIO();
  const bool accelHeld =
      glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(m_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(m_window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
      glfwGetKey(m_window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
  const bool shiftHeld =
      glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
      glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

  // Ctrl/Cmd + Shift + C copies selected object reference code to clipboard.
  bool currCopyRef =
      accelHeld && shiftHeld && glfwGetKey(m_window, GLFW_KEY_C) == GLFW_PRESS;
  const int idx = PrimaryIdx();
  if (const bool hasPrimarySelection =
          idx >= 0 && idx < static_cast<int>(m_document.objects.size());
      ShouldCopySelectionRef(currCopyRef, m_prevCopyRef, io.WantTextInput,
                             ImGui::IsAnyItemActive(), hasPrimarySelection)) {
    const std::string code =
        BuildSelectionRefCode(m_document.objects[idx], idx);
    glfwSetClipboardString(m_window, code.c_str());
    m_uiWidgets.OnClipboardAction("Reference copied", 1.6f);
  }
  m_prevCopyRef = currCopyRef;

  HandleGizmoModeHotkeys(io);

  // Sync gizmo to primary selected object each frame
  SyncGizmoToSelection();

  Vec3 dPos = Vec3::Zero();
  Quaternion dRot = Quaternion::Identity();
  Vec3 dScale = Vec3::One();
  bool gizmoConsumed = false;
  if (m_gizmo.IsActive()) {
    using enum GizmoMode;
    gizmoConsumed =
        m_gizmo.Update(m_window, cam, screenW, screenH, dPos, dRot, dScale);

    // --- Surface snap (Ctrl) and Grid snap (Shift) ---
    if (m_gizmo.GetMode() == Translate) {
      ApplyGizmoTranslateSnapping(m_gizmo.GetDragAxis(), PrimaryIdx(), dPos);
    }

    // Detect any non-trivial delta
    if (const float dRotXYZSq =
            dRot.x * dRot.x + dRot.y * dRot.y + dRot.z * dRot.z;
        dPos.LengthSq() > 1e-10f || dRotXYZSq > 1e-8f ||
        std::abs(dScale.x - 1.0f) > 1e-6f ||
        std::abs(dScale.y - 1.0f) > 1e-6f ||
        std::abs(dScale.z - 1.0f) > 1e-6f) {
      ApplyGizmoDeltaToSelection(dPos, dScale, dRot, dRotXYZSq);
    }
    if (m_gizmoHistoryPending && m_gizmo.GetDragAxis() == GizmoAxis::None) {
      FinalizeHistoryTransaction();
      m_gizmoHistoryPending = false;
    }
  }

  if (gizmoConsumed) {
    // Update m_prevMouseL so HandlePicking doesn't see a phantom click
    // on the frame when the gizmo releases.
    m_prevMouseL =
        glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  } else {
    HandlePicking(cam, screenW, screenH);
  }

  // Del key — delete all selected objects immediately
  bool currDel = glfwGetKey(m_window, GLFW_KEY_DELETE) == GLFW_PRESS;
  if (ShouldRequestDeleteSelection(currDel, m_prevDel,
                                   !m_selectedIndices.empty(),
                                   io.WantTextInput,
                                   ImGui::IsAnyItemActive()))
    RequestDeleteSelectedObjects();
  m_prevDel = currDel;
}

/** @copydoc EditorLayer::OnUpdate */
bool EditorLayer::OnUpdate(float dt, Camera &cam, int screenW, int screenH) {

  if (m_active) {
    ProcessMcpCommands();

    if (ShouldFinalizeEditorClose(m_closeRequested, m_wantsReload)) {
      Toggle();
      return false;
    }

    if (m_playMode) {
      const bool currEsc = glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
      if (currEsc && !m_prevEsc) {
        ++m_playModeEscPresses;
        if (m_playModeEscPresses >= 2) {
          m_playMode = false;
          m_playModeEscPresses = 0;
        }
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
      }
      m_prevEsc = currEsc;
      PublishMcpSnapshot();
      return false;
    }

    EditorTrace("OnUpdate frame=%u fly=%d",
                static_cast<unsigned>(ImGui::GetFrameCount()),
                m_flyMode ? 1 : 0);

    HandleEditorKeyboardShortcuts(cam);

    if (m_flyMode)
      UpdateFlyCameraWithGizmoSync(dt, cam);
    else
      UpdateNonFlyModeInput(cam, screenW, screenH);

    ApplyPendingViewSnap(cam);
    PublishMcpSnapshot();
  }

  const ImGuiIO &io = ImGui::GetIO();
  return m_active && !m_flyMode &&
         (io.WantCaptureMouse || io.WantCaptureKeyboard);
}

/** @copydoc EditorLayer::ToggleFlyMode */
void EditorLayer::ToggleFlyMode(const Camera &cam) {
  m_flyMode = !m_flyMode;
  m_flyCamInitialized = false;
  m_prevCursorInit = false;
  glfwSetInputMode(m_window, GLFW_CURSOR,
                   m_flyMode ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
  (void)cam; // camera sync happens lazily in UpdateFlyCamera
}

/** @copydoc EditorLayer::UpdateFlyCamera */
void EditorLayer::UpdateFlyCamera(float dt, Camera &cam) {
  // Fly mode always uses world-up to avoid inverted controls after view snaps.
  cam.up = Vec3::Up();

  // --- Sync yaw/pitch from live camera on first frame ---
  if (!m_flyCamInitialized) {
    Vec3 dir = cam.target - cam.position;
    if (const float len =
            std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        len > 0.001f) {
      dir.x /= len;
      dir.y /= len;
      dir.z /= len;
    }
    m_flyPitch =
        std::asin(std::max(-1.0f, std::min(1.0f, dir.y))) * (180.0f / PI);

    // If the camera is nearly vertical (Top/Bottom snap), yaw is ill-defined.
    // Reset to a deterministic heading so entering fly mode matches the snapped
    // view instead of reusing stale yaw from a prior fly session.
    if (const float horizLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        horizLen > 0.0001f)
      m_flyYaw = -std::atan2(dir.x, -dir.z) * (180.0f / PI);
    else
      m_flyYaw = 0.0f;

    m_flyCamInitialized = true;
  }

  // --- Mouse look ---
  double cx = 0.0;
  double cy = 0.0;
  glfwGetCursorPos(m_window, &cx, &cy);
  if (!m_prevCursorInit) {
    m_prevCursorX = cx;
    m_prevCursorY = cy;
    m_prevCursorInit = true;
  }
  const float MOUSE_SENS = 0.15f;
  m_flyYaw -= static_cast<float>(cx - m_prevCursorX) * MOUSE_SENS;
  m_flyPitch -= static_cast<float>(cy - m_prevCursorY) * MOUSE_SENS;
  m_flyPitch = std::max(-89.0f, std::min(89.0f, m_flyPitch));
  m_prevCursorX = cx;
  m_prevCursorY = cy;

  // --- Compute forward/right from yaw/pitch ---
  const float yawRad = m_flyYaw * (PI / 180.0f);
  const float pitchRad = m_flyPitch * (PI / 180.0f);
  Vec3 forward = {-std::sin(yawRad) * std::cos(pitchRad), std::sin(pitchRad),
                  -std::cos(yawRad) * std::cos(pitchRad)};
  Vec3 right = Vec3::Cross(forward, {0.0f, 1.0f, 0.0f});
  if (const float rLen =
          std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
      rLen > 0.001f) {
    right.x /= rLen;
    right.y /= rLen;
    right.z /= rLen;
  }

  // --- WASD movement ---
  const float FLY_SPEED = 8.0f;
  Vec3 move = {0.0f, 0.0f, 0.0f};
  if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS)
    move = {move.x + forward.x, move.y + forward.y, move.z + forward.z};
  if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS)
    move = {move.x - forward.x, move.y - forward.y, move.z - forward.z};
  if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS)
    move = {move.x - right.x, move.y - right.y, move.z - right.z};
  if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS)
    move = {move.x + right.x, move.y + right.y, move.z + right.z};

  if (const float mLen =
          std::sqrt(move.x * move.x + move.y * move.y + move.z * move.z);
      mLen > 0.001f) {
    cam.position.x += (move.x / mLen) * FLY_SPEED * dt;
    cam.position.y += (move.y / mLen) * FLY_SPEED * dt;
    cam.position.z += (move.z / mLen) * FLY_SPEED * dt;
  }
  cam.target = {cam.position.x + forward.x, cam.position.y + forward.y,
                cam.position.z + forward.z};
}

} // namespace Horo::Editor
