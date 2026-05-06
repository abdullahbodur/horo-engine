#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
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

void EditorLayer::RefreshViewportPanelRect() {
  const ImVec2 winPos = ImGui::GetWindowPos();
  const ImVec2 innerMin = ImGui::GetWindowContentRegionMin();
  const ImVec2 innerMax = ImGui::GetWindowContentRegionMax();
  m_viewportPanelRect.minX = winPos.x + innerMin.x;
  m_viewportPanelRect.minY = winPos.y + innerMin.y;
  m_viewportPanelRect.maxX = winPos.x + innerMax.x;
  m_viewportPanelRect.maxY = winPos.y + innerMax.y;
}

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

bool EditorLayer::HandleViewportAssetDrop(const Camera &cam, int screenW,
                                          int screenH,
                                          const char *assetIdText) {
  if (!assetIdText)
    return false;
  const std::string assetId(assetIdText);
  if (!m_document.assets.contains(assetId)) {
    LogWarn("[Editor] Viewport drop rejected: missing asset '{}'", assetId);
    return false;
  }
  Vec3 dropPos = Vec3::Zero();
  if (!TryBuildViewportDropPosition(cam, screenW, screenH, assetId, &dropPos)) {
    LogWarn("[Editor] Viewport drop rejected: camera ray did not hit "
            "a placement surface");
    return false;
  }
  if (std::string createError; !CreateObjectFromAsset(
          assetId, "", &dropPos, nullptr, nullptr, &createError)) {
    LogWarn("[Editor] Viewport drop failed: {}", createError);
    return false;
  }
  return true;
}

void EditorLayer::DrawViewportPanel(const Camera &cam, int screenW,
                                    int screenH) {
  struct ViewportAssetDropContext {
    EditorLayer *editor = nullptr;
    const Camera *camera = nullptr;
    int screenW = 0;
    int screenH = 0;
  };

  const ImGuiIO &io = ImGui::GetIO();
  const float leftDockW = ComputeEditorLeftDockWidth(io.DisplaySize.x);
  const float rightDockW = ComputeEditorRightPanelWidth(io.DisplaySize.x);
  const float bottomDockH = ComputeEditorBottomDockHeight(io.DisplaySize.y);
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


void EditorLayer::DrawViewGimbal( // NOSONAR
    const Camera &cam) {
  using enum ViewSnap;
  const ImGuiIO &io = ImGui::GetIO();
  const EditorViewGimbalMetrics &metrics = GetEditorViewGimbalMetrics();
  const bool supportsWireframeOverlay =
      Renderer::GetBackendCapabilities().supportsWireframeOverlay;
  if (!supportsWireframeOverlay)
    m_wireframeMode = false;
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
  dl->AddRectFilled(panelMin, panelMax, IM_COL32(9, 14, 24, 55), 10.0f);

  const int idx = PrimaryIdx();

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

  constexpr float kShaftPx = 42.0f;
  constexpr float kHeadLength = 13.0f;
  constexpr float kHeadHalfWidth = 6.0f;
  constexpr float kHitPx = 9.0f;
  constexpr float kHitPxSq = kHitPx * kHitPx;

  static const std::array<ViewGimbalAxisDraw, 3> kAxes = {{
      {Right, Left, {1.0f, 0.0f, 0.0f}, IM_COL32(255, 82, 58, 255),
       IM_COL32(145, 48, 42, 210), "X"},
      {Top, Bottom, {0.0f, 1.0f, 0.0f}, IM_COL32(80, 230, 104, 255),
       IM_COL32(42, 135, 60, 210), "Y"},
      {Front, Back, {0.0f, 0.0f, 1.0f}, IM_COL32(55, 155, 255, 255),
       IM_COL32(38, 92, 170, 210), "Z"},
  }};

  // Pre-compute screen directions and view-space depth once per frame.
  // Shared by hover and draw loops — no redundant WorldAxisToScreenDir calls.
  std::array<ViewGimbalAxisCache, 3> cache{};
  for (int i = 0; i < 3; i++) {
    float vz = 0.f;
    WorldAxisToScreenDir(cam, kAxes[i].worldPlus, &cache[i].dx, &cache[i].dy,
                         &vz);
    cache[i].viewZ = vz;
    cache[i].origIdx = i;
  }
  ArrangeViewGimbalAxesAsTriad(cache);

  // Sort ascending by view-space Z so background axes draw first (painter's
  // algorithm).
  std::ranges::sort(
      cache, [](const ViewGimbalAxisCache &a, const ViewGimbalAxisCache &b) {
        return a.viewZ < b.viewZ;
      });

  auto hitCache = cache;
  std::ranges::reverse(hitCache);
  const ViewSnap hoverSnap =
      canvasHovered ? FindViewGimbalHoverSnap(io.MousePos, center, hitCache,
                                               kAxes, kShaftPx, kHeadLength,
                                               kHeadHalfWidth, kHitPxSq)
                    : None;

  const float fs = ImGui::GetFontSize();
  const float cx = center.x;
  const float cy = center.y;
  dl->AddCircleFilled(center, 4.0f, IM_COL32(80, 210, 230, 245), 16);
  for (const ViewGimbalAxisCache &ac : cache) {
    const ViewGimbalAxisDraw &ad = kAxes[ac.origIdx];
    // Highlight whichever direction (pos or neg) is currently hovered.
    const bool hlPos = (hoverSnap == ad.posSnap) || (hoverSnap == ad.negSnap);
    ImU32 cPos = ac.viewZ < 0.0f ? ad.colDim : ad.col;
    if (hlPos)
      cPos = IM_COL32(255, 255, 200, 255);

    const ViewGimbalArrowGeometry arrow = BuildViewGimbalArrow(
        ImVec2(cx, cy), ac.dx, ac.dy, kShaftPx, kHeadLength, kHeadHalfWidth);

    dl->AddLine(center, arrow.shaftEnd, cPos, hlPos ? 4.0f : 3.0f);
    dl->AddTriangleFilled(arrow.tip, arrow.headLeft, arrow.headRight, cPos);

    // Offset label along the axis direction so it doesn't overlap the circle
    // regardless of which way the axis is pointing on screen.
    const float tDist = 8.0f;
    dl->AddText(ImVec2(arrow.tip.x + ac.dx * tDist - fs * 0.3f,
                       arrow.tip.y + ac.dy * tDist - fs * 0.5f),
                cPos, ad.label);
  }

  if (canvasClicked && hoverSnap != ViewSnap::None)
    m_uiWidgets.SetPendingViewSnap(hoverSnap);

  (void)idx;
}

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

  Ray ray = ScreenToRay(static_cast<float>(mx), static_cast<float>(my), screenW,
                        screenH, cam);

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
    if (!m_liveRegistry ||
        !TryPropWorldAabb(*m_liveRegistry, obj, center, half))
      half = {std::max(half.x, 0.25f), std::max(half.y, 0.25f),
              std::max(half.z, 0.25f)};
    else {
      half.x = std::max(half.x, 0.25f);
      half.y = std::max(half.y, 0.25f);
      half.z = std::max(half.z, 0.25f);
    }
    DebugDraw::Box(center, half, {0.2f, 0.7f, 1.0f, 1.0f});
  }
}

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
