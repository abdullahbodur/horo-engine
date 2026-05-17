/**
 * @file EditorViewport.cpp
 * @brief EditorLayer viewport rendering: scene image, view gimbal, object
 *        picking, selection highlight, asset drop handling, and view-snap logic.
 */
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/Logger.h"
#include "renderer/DebugDraw.h"
#include "renderer/Renderer.h"
#include "renderer/RenderViewUtils.h"
#include "scene/Entity.h"
#include "scene/Registry.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"
#include "ui/IconsFontAwesome6.h"
#include "ui/editor/Raycaster.h"
#include "ui/HoroTheme.h"
#include "math/Mat4.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "math/Transform.h"
#include "ui/editor/components/EditorAssetThumbnailPreview.h"

namespace Horo::Editor {

namespace Internal {
bool TryPropWorldAabb(Registry &reg, const SceneObject &obj, Vec3 &outCenter,
                      Vec3 &outHalf) {
  using enum SceneObjectType;
  if (obj.type != Prop)
    return false;
  const auto it = obj.props.find("_eid");
  if (it == obj.props.end())
    return false;
  Entity entity;
  try {
    entity = static_cast<Entity>(std::stoul(it->second));
  } catch (const std::invalid_argument &) {
    return false;
  } catch (const std::out_of_range &) {
    return false;
  }
  if (!reg.IsAlive(entity) || !reg.Has<MeshComponent>(entity) ||
      !reg.Has<TransformComponent>(entity))
    return false;
  const auto &mc = reg.Get<MeshComponent>(entity);
  const auto &tc = reg.Get<TransformComponent>(entity);
  if (!mc.mesh)
    return false;
  const Transform wt(tc.current.position, tc.current.rotation, tc.current.scale);
  WorldAabbFromLocalBox(mc.mesh->GetLocalAabbCenter(), mc.mesh->GetHalfExtents(),
                        wt, outCenter, outHalf);
  return true;
}
}

/** @copydoc EditorLayer::RefreshViewportPanelRect */
void EditorLayer::RefreshViewportPanelRect() {
  const ImVec2 winPos = ImGui::GetWindowPos();
  const ImVec2 innerMin = ImGui::GetWindowContentRegionMin();
  const ImVec2 innerMax = ImGui::GetWindowContentRegionMax();
  m_viewportPanelRect.minX = winPos.x + innerMin.x;
  m_viewportPanelRect.minY = winPos.y + innerMin.y;
  m_viewportPanelRect.maxX = winPos.x + innerMax.x;
  m_viewportPanelRect.maxY = winPos.y + innerMax.y;
}

/** @copydoc EditorLayer::DrawViewportImage */
bool EditorLayer::DrawViewportImage(float targetW, float targetH) const {
  static std::string s_lastViewportRenderError;
  if (targetW <= 0.0f || targetH <= 0.0f)
    return false;

  std::string viewportError;
  RenderTargetHandle viewportHandle;
  const auto viewportWidth =
      static_cast<uint32_t>(std::max(1.0f, std::floor(targetW)));
  if (const auto viewportHeight =
          static_cast<uint32_t>(std::max(1.0f, std::floor(targetH)));
      !Renderer::EnsureEditorViewportRenderTarget(viewportWidth, viewportHeight,
                                                  &viewportError) ||
      !Renderer::TryGetEditorViewportRenderTargetHandle(&viewportHandle, false,
                                                        &viewportError))
    return false;

  const ImTextureID textureId = ToImTextureId(viewportHandle);
  if (textureId == (ImTextureID)0) {
    if (Renderer::GetBackendId() == RenderBackendId::Vulkan &&
        !viewportError.empty() && viewportError != s_lastViewportRenderError) {
      LogWarn("[Editor] Vulkan viewport render target unavailable: {}",
              viewportError);
      s_lastViewportRenderError = viewportError;
    }
    return false;
  }
  ImGui::Image(textureId, ImVec2(targetW, targetH));
  s_lastViewportRenderError.clear();
  return true;
}

/** @copydoc EditorLayer::HandleViewportAssetDrop */
bool EditorLayer::HandleViewportAssetDrop(const Camera &cam, int screenW,
                                          int screenH,
                                          const char *assetIdText) {
  if (!assetIdText) {
    LogWarn("[Editor] Viewport drop rejected: null assetIdText");
    return false;
  }
  const std::string assetId(assetIdText);
  LogInfo("[Editor] Viewport drop started for asset '{}'", assetId);

  if (!m_document.assets.contains(assetId)) {
    LogWarn("[Editor] Viewport drop rejected: missing asset '{}'", assetId);
    return false;
  }

  const auto& asset = m_document.assets.at(assetId);
  LogInfo("[Editor] Asset '{}' mesh path: '{}', albedo: '{}'",
          assetId, asset.mesh, asset.albedoMap);

  Vec3 dropPos = Vec3::Zero();
  if (!TryBuildViewportDropPosition(cam, screenW, screenH, assetId, &dropPos)) {
    LogWarn("[Editor] Viewport drop rejected: camera ray did not hit "
            "a placement surface (ray origin: {}, dir: {})",
            cam.position.ToString(), (cam.target - cam.position).ToString());
    return false;
  }
  LogInfo("[Editor] Viewport drop position calculated: ({}, {}, {})",
          dropPos.x, dropPos.y, dropPos.z);

  if (std::string createError; !CreateObjectFromAsset(
          assetId, "", &dropPos, nullptr, nullptr, &createError)) {
    LogWarn("[Editor] Viewport drop failed: {}", createError);
    return false;
  }
  LogInfo("[Editor] Viewport drop succeeded for asset '{}'", assetId);
  return true;
}

/** @copydoc EditorLayer::DrawViewportPanel */
void EditorLayer::DrawViewportPanel(const Camera &cam, int screenW,
                                    int screenH) {
  struct ViewportAssetDropContext {
    EditorLayer *editor = nullptr;
    const Camera *camera = nullptr;
    int screenW = 0;
    int screenH = 0;
  };

  const ImGuiIO &io = ImGui::GetIO();
  if (m_leftDockWidth <= 0.0f)
    m_leftDockWidth = ComputeEditorLeftDockWidth(io.DisplaySize.x);
  if (m_bottomDockHeight <= 0.0f)
    m_bottomDockHeight = ComputeEditorBottomDockHeight(io.DisplaySize.y);
  const float leftDockW = m_leftDockWidth;
  const float rightDockW = ComputeEditorRightPanelWidth(io.DisplaySize.x);
  const float bottomDockH = m_bottomDockHeight;
  const EditorViewportRect defaultRect = BuildEditorViewportRect(
      io.DisplaySize.x, io.DisplaySize.y, kEditorToolbarH, kEditorStatusH,
      bottomDockH, leftDockW, rightDockW);
  ImGui::SetNextWindowPos(ImVec2(defaultRect.minX, defaultRect.minY),
                          ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(defaultRect.maxX - defaultRect.minX,
                                  defaultRect.maxY - defaultRect.minY),
                           ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.06f);
  m_viewportPanelRect = {};
  if (ImGui::Begin(kEditorViewportWindow, nullptr,
                   kMainPanelWindowFlags | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse)) {
    RefreshViewportPanelRect();
    const auto viewportImageStart = ImGui::GetCursorPos();
    const auto dropTargetSize = ImGui::GetContentRegionAvail();
    const float targetW = std::max(0.0f, dropTargetSize.x);
    const float targetH = std::max(0.0f, dropTargetSize.y);

    if (const bool drewViewportImage = DrawViewportImage(targetW, targetH);
        !drewViewportImage)
      ImGui::Dummy(ImVec2(targetW, targetH));

    ImGui::SetCursorPos(viewportImageStart);
    ViewportAssetDropContext dropContext{this, &cam, screenW, screenH};
    DrawViewportAssetDropTarget(
        m_playMode, dropTargetSize.x, dropTargetSize.y, &dropContext,
        [](void *userData, // NOSONAR
           const char *assetIdText) {
          // NOSONAR: cpp:S5008 required by
          // DrawViewportAssetDropTarget callback API
          auto *ctx = static_cast<ViewportAssetDropContext *>(userData);
          if (!ctx || !ctx->editor || !ctx->camera)
            return false;
          return ctx->editor->HandleViewportAssetDrop(
              *ctx->camera, ctx->screenW, ctx->screenH, assetIdText);
        });

    if (!m_playMode)
      DrawViewGimbal(cam);
  }
  ImGui::End();
  ImGui::PopStyleVar();
}


namespace {
struct AxisData {
  ViewSnap snapPos;
  ViewSnap snapNeg;
  Vec3 worldDir;
  char label;
  ImU32 col;
  const char *tooltipPos;
  const char *tooltipNeg;
};

struct ViewGimbalAxisRenderParams {
  ImVec2 center;
  float lineLength;
  float endpointRadius;
};

struct ViewGimbalAxisDirections {
  const std::array<float, 3>& dirDx;
  const std::array<float, 3>& dirDy;
  const std::array<float, 3>& dirVz;
};

void RenderViewGimbalAxes(ImDrawList* dl,
                          const ViewGimbalAxisRenderParams& params,
                          const std::array<AxisData, 3>& axes,
                          const ViewGimbalAxisDirections& dirs,
                          const std::array<int, 3>& order,
                          ViewSnap hoverSnap) {
  for (int idx : order) {
    const AxisData &axis = axes[idx];
    const bool isFront = dirs.dirVz[idx] >= 0.0f;
    const bool hl = (hoverSnap == axis.snapPos) || (hoverSnap == axis.snapNeg);
    ImU32 col = axis.col;
    float alpha = isFront ? 1.0f : 0.5f;
    if (hl) {
      col = IM_COL32(255, 255, 200, 255);
      alpha = 1.0f;
    }

    const ImU32 lineCol = IM_COL32(
        (col >> 0) & 0xFF,
        (col >> 8) & 0xFF,
        (col >> 16) & 0xFF,
        static_cast<int>(255 * alpha));

    const float endX = params.center.x + dirs.dirDx[idx] * params.lineLength;
    const float endY = params.center.y + dirs.dirDy[idx] * params.lineLength;

    dl->AddLine(params.center, ImVec2(endX, endY), lineCol, hl ? 3.0f : 2.0f);

    const float dotRadius = hl ? params.endpointRadius * 1.3f : params.endpointRadius;
    dl->AddCircleFilled(ImVec2(endX, endY), dotRadius, lineCol, 12);
    dl->AddCircle(ImVec2(endX, endY), dotRadius, IM_COL32(255, 255, 255, static_cast<int>(100 * alpha)), 12, 1.0f);

    const std::array<char, 2> labelStr = {axis.label, '\0'};
    const float labelW = ImGui::CalcTextSize(labelStr.data()).x;
    const float labelH = ImGui::CalcTextSize(labelStr.data()).y;
    dl->AddText(ImVec2(endX - labelW * 0.5f, endY - labelH * 0.5f), IM_COL32(0, 0, 0, static_cast<int>(200 * alpha)), labelStr.data());
    dl->AddText(ImVec2(endX - labelW * 0.5f - 1.0f, endY - labelH * 0.5f - 1.0f), IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), labelStr.data());
  }
}
}  // namespace


/** @copydoc EditorLayer::DrawViewGimbal */
void EditorLayer::DrawViewGimbal(const Camera &cam) {
  using enum ViewSnap;
  const ImGuiIO &io = ImGui::GetIO();
  const EditorViewGimbalMetrics &metrics = GetEditorViewGimbalMetrics();
  const float rightDockW = ComputeEditorRightPanelWidth(io.DisplaySize.x);
  const EditorViewGimbalLayout layout = BuildEditorViewGimbalLayout(
      m_viewportPanelRect, io.DisplaySize.x, rightDockW, kEditorToolbarH);
  const ImVec2 viewportPos = ImGui::GetWindowPos();
  const ImVec2 gimbalLocalPos(layout.gimbalRect.minX - viewportPos.x,
                              layout.gimbalRect.minY - viewportPos.y);
  ImDrawList *dl = ImGui::GetWindowDrawList();

  m_viewGizmoPickRect.valid = true;
  m_viewGizmoPickRect.minX = layout.gimbalRect.minX;
  m_viewGizmoPickRect.minY = layout.gimbalRect.minY;
  m_viewGizmoPickRect.maxX = layout.gimbalRect.maxX;
  m_viewGizmoPickRect.maxY = layout.gimbalRect.maxY;

  const ImVec2 panelMin(layout.gimbalRect.minX, layout.gimbalRect.minY);
  const ImVec2 panelMax(layout.gimbalRect.maxX, layout.gimbalRect.maxY);
  dl->AddRectFilled(panelMin, panelMax, IM_COL32(20, 24, 36, 180), 10.0f);

  const float hitRegionWidth = layout.gimbalRect.maxX - layout.gimbalRect.minX -
                               2.0f * metrics.contentOffsetX;
  ImGui::SetCursorPos(ImVec2(gimbalLocalPos.x + metrics.contentOffsetX,
                             gimbalLocalPos.y + metrics.contentOffsetY));
  ImGui::InvisibleButton("##view_gimbal_hit",
                         ImVec2(hitRegionWidth, metrics.hitRegionHeight));
  const auto hitMin = ImGui::GetItemRectMin();
  const auto hitMax = ImGui::GetItemRectMax();
  const auto center =
      ImVec2((hitMin.x + hitMax.x) * 0.5f, (hitMin.y + hitMax.y) * 0.5f);
  const bool canvasHovered = ImGui::IsItemHovered();
  const bool canvasClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

  constexpr float kLineLength = 32.0f;
  constexpr float kEndpointRadius = 6.0f;
  constexpr float kHitRadius = 12.0f;
  constexpr float kHitRadiusSq = kHitRadius * kHitRadius;

  static const std::array<AxisData, 3> kAxes = {{
      {Right, Left, {1.0f, 0.0f, 0.0f}, 'X', IM_COL32(255, 82, 58, 255), "View Right", "View Left"},
      {Top, Bottom, {0.0f, 1.0f, 0.0f}, 'Y', IM_COL32(80, 230, 104, 255), "View Top", "View Bottom"},
      {Front, Back, {0.0f, 0.0f, 1.0f}, 'Z', IM_COL32(55, 155, 255, 255), "View Front", "View Back"},
  }};

  std::array<float, 3> dirDx{};
  std::array<float, 3> dirDy{};
  std::array<float, 3> dirVz{};
  for (int i = 0; i < 3; ++i) {
    WorldAxisToScreenDir(cam, kAxes[i].worldDir, &dirDx[i], &dirDy[i], &dirVz[i]);
  }

  ViewSnap hoverSnap = None;
  const char *hoverTooltip = nullptr;
  if (canvasHovered) {
    float bestD = kHitRadiusSq;
    for (int i = 0; i < 3; ++i) {
      const float tipX = center.x + dirDx[i] * kLineLength;
      const float tipY = center.y + dirDy[i] * kLineLength;
      const float dx = io.MousePos.x - tipX;
      const float dy = io.MousePos.y - tipY;
      if (const float d = dx * dx + dy * dy; d < bestD) {
        bestD = d;
        hoverSnap = dirVz[i] >= 0.0f ? kAxes[i].snapPos : kAxes[i].snapNeg;
        hoverTooltip = dirVz[i] >= 0.0f ? kAxes[i].tooltipPos : kAxes[i].tooltipNeg;
      }
    }
  }


  dl->AddCircleFilled(center, 4.0f, IM_COL32(120, 120, 120, 255), 16);

  std::array<int, 3> order = {0, 1, 2};
  if (dirVz[0] > dirVz[1]) std::swap(order[0], order[1]);
  if (dirVz[order[1]] > dirVz[order[2]]) std::swap(order[1], order[2]);
  if (dirVz[order[0]] > dirVz[order[1]]) std::swap(order[0], order[1]);

  RenderViewGimbalAxes(
      dl, {center, kLineLength, kEndpointRadius}, kAxes,
      {dirDx, dirDy, dirVz}, order, hoverSnap);

  if (hoverTooltip && canvasHovered) {
    ImGui::SetTooltip("%s", hoverTooltip);
  }

  if (canvasClicked && hoverSnap != ViewSnap::None)
    m_uiWidgets.SetPendingViewSnap(hoverSnap);
}

/** @copydoc EditorLayer::HandlePicking */
void EditorLayer::HandlePicking(const Camera &cam, int screenW, int screenH) {
  bool currL =
      glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  bool clicked = currL && !m_prevMouseL;
  m_prevMouseL = currL;

  if (!clicked)
    return;

  double mx = 0.0;
  double my = 0.0;
  glfwGetCursorPos(m_window, &mx, &my);
  if (!m_viewportPanelRect.Contains(static_cast<float>(mx),
                                    static_cast<float>(my)))
    return;
  if (ImGui::GetIO().WantCaptureMouse &&
      !m_viewportPanelRect.Contains(static_cast<float>(mx),
                                    static_cast<float>(my)))
    return;
  const ImGuiIO &io = ImGui::GetIO();
  const float rightDockW = ComputeEditorRightPanelWidth(io.DisplaySize.x);
  if (const EditorViewGimbalLayout viewGimbalLayout =
          BuildEditorViewGimbalLayout(m_viewportPanelRect, io.DisplaySize.x,
                                      rightDockW, kEditorToolbarH);
      viewGimbalLayout.pickRect.Contains(static_cast<float>(mx),
                                         static_cast<float>(my)))
    return;
  if (m_viewGizmoPickRect.valid &&
      m_viewGizmoPickRect.Contains(static_cast<float>(mx),
                                   static_cast<float>(my), 2.0f))
    return;

  int windowW = 0;
  int windowH = 0;
  glfwGetWindowSize(m_window, &windowW, &windowH);
  const Vec2 mouse = ScaleScreenPointToRenderTarget(
      static_cast<float>(mx), static_cast<float>(my), windowW, windowH,
      screenW, screenH);
  Ray ray = ScreenToRay(mouse.x, mouse.y, screenW, screenH, cam);

  float bestT = std::numeric_limits<float>::max();
  int bestIdx = -1;

  for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
    const auto &obj = m_document.objects[i];
    Vec3 center = obj.position;
    Vec3 half = {std::max(obj.scale.x, 0.25f), std::max(obj.scale.y, 0.25f),
                 std::max(obj.scale.z, 0.25f)};
    if (m_liveRegistry &&
        TryPropWorldAabb(*m_liveRegistry, obj, center, half)) {
      half.x = std::max(half.x, 0.25f);
      half.y = std::max(half.y, 0.25f);
      half.z = std::max(half.z, 0.25f);
    }
    float t = RayVsAABB(ray, center, half);
    if (t >= 0.0f && t < bestT) {
      bestT = t;
      bestIdx = i;
    }
  }

  bool shiftHeld = glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                   glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

  if (bestIdx >= 0) {
    if (shiftHeld)
      ToggleSelect(bestIdx);
    else
      m_selectedIndices = {bestIdx};
  } else if (!shiftHeld) {
    m_selectedIndices.clear();
  }
}

/** @copydoc EditorLayer::DrawSelectionHighlight */
void EditorLayer::DrawSelectionHighlight() {
  using enum SceneObjectType;
  const auto n = static_cast<int>(m_document.objects.size());
  for (int i : m_selectedIndices) {
    if (i < 0 || i >= n)
      continue;
    const auto &obj = m_document.objects[i];

    if (obj.type == Camera) {
      const Vec4 color = {0.2f, 0.7f, 1.0f, 1.0f};
      const float yawRad = ToRadians(obj.yaw);
      const float pitchRad =
          ToRadians(std::max(-89.0f, std::min(89.0f, obj.pitch)));
      const Vec3 forward = {-std::sin(yawRad) * std::cos(pitchRad),
                            std::sin(pitchRad),
                            -std::cos(yawRad) * std::cos(pitchRad)};

      Vec3 right = Vec3::Cross(forward, Vec3::Up());
      if (right.LengthSq() < 1e-5f)
        right = {1.0f, 0.0f, 0.0f};
      else
        right = right.Normalized();
      Vec3 up = Vec3::Cross(right, forward).Normalized();

      const Vec3 tip = obj.position;
      const Vec3 baseCenter = tip + forward * 0.55f;
      const float baseHalfW = 0.24f;
      const float baseHalfH = 0.16f;

      const Vec3 b0 = baseCenter + right * baseHalfW + up * baseHalfH;
      const Vec3 b1 = baseCenter - right * baseHalfW + up * baseHalfH;
      const Vec3 b2 = baseCenter - right * baseHalfW - up * baseHalfH;
      const Vec3 b3 = baseCenter + right * baseHalfW - up * baseHalfH;

      DebugDraw::Line(tip, b0, color);
      DebugDraw::Line(tip, b1, color);
      DebugDraw::Line(tip, b2, color);
      DebugDraw::Line(tip, b3, color);
      DebugDraw::Line(b0, b1, color);
      DebugDraw::Line(b1, b2, color);
      DebugDraw::Line(b2, b3, color);
      DebugDraw::Line(b3, b0, color);

      const Vec3 dirTip = tip + forward * 0.78f;
      DebugDraw::Line(tip, dirTip, {1.0f, 0.55f, 0.1f, 1.0f});
      continue;
    }

    Vec3 center = obj.position;
    Vec3 half = obj.scale;
    bool usingWorldAabb = false;
    if (!m_liveRegistry ||
        !TryPropWorldAabb(*m_liveRegistry, obj, center, half))
      half = {std::max(half.x, 0.25f), std::max(half.y, 0.25f),
              std::max(half.z, 0.25f)};
    else {
      half.x = std::max(half.x, 0.25f);
      half.y = std::max(half.y, 0.25f);
      half.z = std::max(half.z, 0.25f);
      usingWorldAabb = true;
    }

    const Vec4 highlightColor = {0.2f, 0.7f, 1.0f, 1.0f};
    const bool hasRotation =
        obj.pitch != 0.0f || obj.yaw != 0.0f || obj.roll != 0.0f;
    if (hasRotation && !usingWorldAabb) {
      const Quaternion objRotation =
          Quaternion::FromEuler(ToRadians(obj.pitch), ToRadians(obj.yaw),
                                ToRadians(obj.roll));
      DebugDraw::OrientedBox(center, half, objRotation, highlightColor);
    } else {
      DebugDraw::Box(center, half, highlightColor);
    }
  }
}

/** @copydoc EditorLayer::ApplyPendingViewSnap */
void EditorLayer::ApplyPendingViewSnap(Camera &cam) {
  using enum ViewSnap;
  if (m_uiWidgets.GetPendingViewSnap() == None)
    return;

  Vec3 pivot = Vec3::Zero();
  float extent = 1.0f;
  if (const int idx = PrimaryIdx();
      idx >= 0 && idx < static_cast<int>(m_document.objects.size())) {
    const SceneObject &obj = m_document.objects[idx];
    pivot = obj.position;
    extent = std::max(obj.scale.x, std::max(obj.scale.y, obj.scale.z));
  }

  const float distance = std::max(2.0f, extent * 3.0f + 1.0f);
  SnapCameraToAxis(cam, m_uiWidgets.GetPendingViewSnap(), pivot, distance);

  m_flyCamInitialized = false;
  m_uiWidgets.ClearPendingViewSnap();
}

} // namespace Horo::Editor
