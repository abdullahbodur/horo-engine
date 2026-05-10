/**
 * @file EditorPropertiesPanel.cpp
 * @brief Properties panel draw methods for EditorLayer.
 *
 * Method definitions are in this file; declarations remain in EditorLayer.h.
 */

// Windows headers must come before GLFW to avoid type redefinition conflicts
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <commdlg.h>
// clang-format on
#endif

// clang-format off
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ui/UiComponents.h"
#include "ui/editor/AssetMetadata.h"
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"
#include "ui/editor/EditorSceneGraph.h"
#include "ui/editor/EditorUiLogic.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"

namespace {
#ifdef HORO_STANDALONE_UI_AUTOMATION
void DrawUiAutomationMarker(const char *label) {
  if (!label || !*label)
    return;
  ImGui::InvisibleButton(label, ImVec2(1.0f, 1.0f));
}
#else
void DrawUiAutomationMarker(const char *) {}
#endif
} // namespace

namespace Horo::Editor {
/** @copydoc EditorLayer::DrawPropertiesPanel */
void EditorLayer::DrawPropertiesPanel() {
  // NOSONAR: cpp:S3776 properties panel
  // integrates multiple section editors
  // with mode-specific branching
  using enum SceneObjectType;
  const ImGuiIO &io = ImGui::GetIO();
  if (m_bottomDockHeight <= 0.0f)
    m_bottomDockHeight = ComputeEditorBottomDockHeight(io.DisplaySize.y);
  const float bottomDockH = m_bottomDockHeight;
  const float rightDockW = ComputeEditorRightPanelWidth(io.DisplaySize.x);
  const float workBottom = io.DisplaySize.y - kEditorStatusH - bottomDockH;
  ImGui::SetNextWindowPos(
      ImVec2(io.DisplaySize.x - rightDockW, kEditorToolbarH), ImGuiCond_Always);
  ImGui::SetNextWindowSize(
      ImVec2(rightDockW, std::max(260.0f, workBottom - kEditorToolbarH)),
      ImGuiCond_Always);
  ImGui::Begin(kEditorPropertiesWindow, nullptr, kMainPanelWindowFlags);

  if (m_selectedIndices.size() > 1) {
    DrawPropertiesMultiSelect();
    ImGui::End();
    return;
  }

  int primaryIdx = PrimaryIdx();
  if (primaryIdx < 0 ||
      primaryIdx >= static_cast<int>(m_document.objects.size())) {
    if (!m_selectedAssetId.empty()) {
      auto assetIt = m_document.assets.find(m_selectedAssetId);
      if (assetIt == m_document.assets.end()) {
        m_selectedAssetId.clear();
      } else {
        DrawPropertiesSelectedAsset(assetIt);
        ImGui::End();
        return;
      }
    }
    ImGui::TextDisabled("No selection");
    DrawUiAutomationMarker("##properties_test/no_selection");
    ImGui::TextDisabled("Pick an object or asset to edit properties.");
    ImGui::End();
    return;
  }

  SceneObject &obj = m_document.objects[primaryIdx];

  DrawPropertiesIdentitySection(obj, primaryIdx);

  if (obj.type == Camera) {
    DrawPropertiesCameraSection(obj, primaryIdx);
    ImGui::End();
    return;
  }

  DrawPropertiesTransformSection(obj, primaryIdx);
  DrawPropertiesAssetSection(obj);
  DrawPropertiesSchemaFields(obj);
  DrawPropertiesComponentsList(obj);
  DrawPropertiesAddComponentMenu(obj);

  ImGui::Separator();
  if (ImGui::Button("Delete")) {
    RequestDeleteSelectedObjects();
  }

  ImGui::End();
}

/** @copydoc EditorLayer::ApplyBatchAssetChange */
void EditorLayer::ApplyBatchAssetChange(
    const std::vector<std::string> &sortedAssetIds) {
  if (m_batchAssetChoice == 1) {
    for (int idx : m_selectedIndices) {
      if (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
        ObjectAt(m_document, idx).assetId.clear();
    }
  } else if (m_batchAssetChoice > 1) {
    const std::string selectedAssetId =
        sortedAssetIds[static_cast<size_t>(m_batchAssetChoice - 2)];
    for (int idx : m_selectedIndices) {
      if (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
        ObjectAt(m_document, idx).assetId = selectedAssetId;
    }
  }
  if (m_batchAssetChoice > 0) {
    SyncAssetScaleMetadata(&m_document);
    MarkDirtyAndReload();
  }
}

/** @copydoc EditorLayer::ApplyBatchTransform */
void EditorLayer::ApplyBatchTransform() {
  for (int idx : m_selectedIndices) {
    if (idx < 0 || idx >= static_cast<int>(m_document.objects.size()))
      continue;
    SceneObject &object = ObjectAt(m_document, idx);
    object.position = object.position + m_batchTranslateDraft;
    object.pitch += m_batchRotateDraft.x;
    object.yaw += m_batchRotateDraft.y;
    object.roll += m_batchRotateDraft.z;
    object.scale.x *= m_batchScaleDraft.x;
    object.scale.y *= m_batchScaleDraft.y;
    object.scale.z *= m_batchScaleDraft.z;
    if (m_transformCb)
      m_transformCb(object);
  }
  MarkDirtyAndReload();
}

/** @copydoc EditorLayer::DrawPropertiesMultiSelect */
void EditorLayer::DrawPropertiesMultiSelect() {
  ImGui::Text("%d objects selected",
              static_cast<int>(m_selectedIndices.size()));
  DrawUiAutomationMarker("##properties_test/multi_select");
  ImGui::Separator();

  std::string sharedAssetId;
  bool hasSharedAssetId = true;
  bool firstObject = true;
  for (int idx : m_selectedIndices) {
    if (idx < 0 || idx >= static_cast<int>(m_document.objects.size()))
      continue;
    const SceneObject &selected = ObjectAt(m_document, idx);
    if (firstObject) {
      sharedAssetId = selected.assetId;
      firstObject = false;
    } else if (sharedAssetId != selected.assetId) {
      hasSharedAssetId = false;
      break;
    }
  }

  ImGui::TextDisabled("Asset");
  if (firstObject)
    ImGui::TextDisabled("No valid selection");
  else if (hasSharedAssetId && sharedAssetId.empty())
    ImGui::TextUnformatted("<none>");
  else if (hasSharedAssetId)
    ImGui::TextUnformatted(sharedAssetId.c_str());
  else
    ImGui::TextDisabled("Mixed");

  std::vector<const char *> assetItems;
  assetItems.reserve(m_document.assets.size() + 2);
  assetItems.push_back("<keep current>");
  assetItems.push_back("<clear>");
  std::vector<std::string> sortedAssetIds;
  sortedAssetIds.reserve(m_document.assets.size());
  for (const auto &[assetId, _] : m_document.assets)
    sortedAssetIds.push_back(assetId);
  std::ranges::sort(sortedAssetIds);
  for (const std::string &assetId : sortedAssetIds)
    assetItems.push_back(assetId.c_str());

  Horo::Ui::Combo(Horo::Ui::GetEditorTheme(), "Batch Asset", &m_batchAssetChoice,
                 assetItems.data(), static_cast<int>(assetItems.size()));
  if (ImGui::Button("Apply Asset"))
    ApplyBatchAssetChange(sortedAssetIds);

  ImGui::Separator();
  if (std::array<float, 3> moveBy = {m_batchTranslateDraft.x,
                                     m_batchTranslateDraft.y,
                                     m_batchTranslateDraft.z};
      ImGui::DragFloat3("Move By", moveBy.data(), 0.05f))
    m_batchTranslateDraft = {moveBy[0], moveBy[1], moveBy[2]};

  if (std::array<float, 3> rotateBy = {m_batchRotateDraft.x,
                                       m_batchRotateDraft.y,
                                       m_batchRotateDraft.z};
      ImGui::DragFloat3("Rotate By (P/Y/R)", rotateBy.data(), 1.0f))
    m_batchRotateDraft = {rotateBy[0], rotateBy[1], rotateBy[2]};

  if (std::array<float, 3> scaleBy = {m_batchScaleDraft.x, m_batchScaleDraft.y,
                                      m_batchScaleDraft.z};
      ImGui::DragFloat3("Scale By", scaleBy.data(), 0.02f, 0.01f, 20.0f))
    m_batchScaleDraft = {scaleBy[0], scaleBy[1], scaleBy[2]};

  if (ImGui::Button("Apply Batch Transform"))
    ApplyBatchTransform();

  ImGui::Separator();
  if (ImGui::Button("Duplicate Selected"))
    DuplicateSelectedObjects();
  ImGui::SameLine();
  if (ImGui::Button("Delete Selected"))
    RequestDeleteSelectedObjects();
}

/** @copydoc EditorLayer::DrawAssetDiagnosticsSection */
void EditorLayer::DrawAssetDiagnosticsSection(
    const AssetMetadata &metadata) const {
  if (!metadata.importerId.empty()) {
    ImGui::TextDisabled("Importer");
    ImGui::TextWrapped("%s", metadata.importerId.c_str());
  }
  if (!metadata.sourcePath.empty()) {
    ImGui::TextDisabled("Source");
    ImGui::TextWrapped("%s", metadata.sourcePath.c_str());
  }
  if (!metadata.lastImportReason.empty()) {
    ImGui::TextDisabled("Last rebuild reason");
    ImGui::TextWrapped("%s", metadata.lastImportReason.c_str());
  }
  if (metadata.diagnostics.empty())
    return;
  ImGui::TextDisabled("Diagnostics");
  for (const AssetImportDiagnostic &diag : metadata.diagnostics) {
    const bool isError = (diag.severity == AssetDiagnosticSeverity::Error);
    auto color = ImVec4(0.6f, 0.85f, 1.f, 1.f);
    if (isError)
      color = ImVec4(1.f, 0.4f, 0.4f, 1.f);
    else if (diag.severity == AssetDiagnosticSeverity::Warning)
      color = ImVec4(1.f, 0.8f, 0.35f, 1.f);
    const std::string line = "[" + diag.code + "] " + diag.message;
    ImGui::TextWrapped("%s", line.c_str());
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(min.x - 6.0f, min.y + 3.0f), ImVec2(min.x - 2.0f, max.y - 3.0f),
        ImGui::ColorConvertFloat4ToU32(color), 1.0f);
  }
}

/** @copydoc EditorLayer::DrawPropertiesSelectedAsset */
void EditorLayer::DrawPropertiesSelectedAsset(
    std::unordered_map<std::string, AssetDef, StringHash,
                       std::equal_to<>>::iterator assetIt) {
  ImGui::TextDisabled("Asset");
  ImGui::TextUnformatted(m_selectedAssetId.c_str());
  ImGui::TextDisabled("Display name");
  ImGui::TextUnformatted(assetIt->second.displayName.c_str());
  ImGui::TextDisabled("GUID");
  ImGui::TextWrapped("%s", assetIt->second.guid.c_str());

  AssetMetadata assetMetadata;
  std::string metadataError;
  const bool hasAssetMetadata =
      LoadAssetMetadata(assetIt->second.guid, &assetMetadata, &metadataError);
  if (hasAssetMetadata)
    DrawAssetDiagnosticsSection(assetMetadata);

  if (ImGui::Button("Deselect Asset", ImVec2(-1.0f, 0.0f))) {
    m_selectedAssetId.clear();
    return;
  }

  if (hasAssetMetadata) {
    const auto &theme = Horo::Ui::GetEditorTheme();
    if (ImGui::Button("Reimport Asset", ImVec2(-1.0f, 0.0f))) {
      const AssetReimportResult reimportResult =
          m_assetImportService.ReimportAssetWithDependents(
              &m_document, assetIt->second.guid, "Manual reimport from editor");
      if (reimportResult.ok) {
        m_document.dirty = true;
        m_assetImportError.clear();
        TriggerReload();
      } else {
        m_assetImportError = reimportResult.error;
      }
    }
    if (!m_assetImportError.empty())
      Horo::Ui::RenderEditorStatusText(theme, Horo::Ui::EditorStatusLevel::Error,
                                       "%s", m_assetImportError.c_str());
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  std::string meshEditBuf(512, '\0');
  assetIt->second.mesh.copy(meshEditBuf.data(), meshEditBuf.size() - 1);
  if (ImGui::InputText("Mesh", meshEditBuf.data(), meshEditBuf.size())) {
    assetIt->second.mesh = meshEditBuf.data();
    m_document.dirty = true;
  }

  std::string displayEditBuf(256, '\0');
  assetIt->second.displayName.copy(displayEditBuf.data(),
                                   displayEditBuf.size() - 1);
  if (ImGui::InputText("Display Name", displayEditBuf.data(),
                       displayEditBuf.size())) {
    assetIt->second.displayName = displayEditBuf.data();
    m_document.dirty = true;
  }

  std::string scaleEditBuf(128, '\0');
  assetIt->second.renderScale.copy(scaleEditBuf.data(),
                                   scaleEditBuf.size() - 1);
  if (ImGui::InputText("Render Scale", scaleEditBuf.data(),
                       scaleEditBuf.size())) {
    assetIt->second.renderScale = scaleEditBuf.data();
    SyncAssetScaleMetadata(&m_document);
    MarkDirtyAndReload();
  }

  ImGui::TextDisabled("Albedo map (optional)");
  const ImVec2 selAlbLabelMin = ImGui::GetItemRectMin();
  const ImVec2 selAlbLabelMax = ImGui::GetItemRectMax();
  std::string albBuf(512, '\0');
  assetIt->second.albedoMap.copy(albBuf.data(), albBuf.size() - 1);
  if (ImGui::InputText("##sel_alb", albBuf.data(), albBuf.size())) {
    assetIt->second.albedoMap = albBuf.data();
    m_document.dirty = true;
  }
  {
    const ImVec2 fMin = ImGui::GetItemRectMin();
    const ImVec2 fMax = ImGui::GetItemRectMax();
    m_albedoSelDrop.valid = true;
    m_albedoSelDrop.minX = std::min(selAlbLabelMin.x, fMin.x);
    m_albedoSelDrop.minY = selAlbLabelMin.y;
    m_albedoSelDrop.maxX = std::max(selAlbLabelMax.x, fMax.x);
    m_albedoSelDrop.maxY = fMax.y;
  }

#if defined(_WIN32) || defined(__APPLE__)
  if (ImGui::Button("Browse texture...##alb_pick_asset", ImVec2(-1.0f, 0.0f)))
    m_deferredFilePick = DeferredFilePick::SelectedAssetAlbedo;
#else
  DrawUnavailableTextureDialogButton("Browse texture...##alb_pick_asset");
#endif

  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float fullW = ImGui::GetContentRegionAvail().x;
  const float btnW = std::max(90.0f, (fullW - gap) * 0.5f);
  if (ImGui::Button("Add Prop##sel_add", ImVec2(btnW, 0.0f))) {
    SceneObject obj =
        MakeObjectFromAsset(m_document, m_selectedAssetId, m_schema);
    m_document.objects.push_back(std::move(obj));
    m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
    MarkDirtyAndReload();
  }
  ImGui::SameLine(0.0f, gap);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.18f, 0.18f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.55f, 0.22f, 0.22f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
  if (ImGui::Button("Delete Asset##sel_del", ImVec2(btnW, 0.0f)))
    RequestDeleteAsset(m_selectedAssetId);
  ImGui::PopStyleColor(3);
}

/** @copydoc EditorLayer::DrawPropertiesIdentitySection */
void EditorLayer::DrawPropertiesIdentitySection(SceneObject &obj,
                                                int primaryIdx) {
  using enum SceneObjectType;
  DrawUiAutomationMarker("##properties_test/identity_section");
  ImGui::LabelText("ID##identity_id", "%s", obj.id.c_str());
  const char *typeName = "Panel";
  if (obj.type == Prop)
    typeName = "Prop";
  else if (obj.type == Light)
    typeName = "Light";
  else if (obj.type == Camera)
    typeName = "Camera";
  ImGui::LabelText("Type##identity_type", "%s", typeName);
  if (obj.prefabInstance.has_value()) {
    ImGui::LabelText("Prefab", "%s", obj.prefabInstance->prefabId.c_str());
    ImGui::TextDisabled("%s", obj.prefabInstance->sourcePath.c_str());
  }

  {
    std::string currentParent{GetParentId(obj)};
    std::vector<std::string> parentIds;
    std::vector<const char *> parentItems;
    parentItems.push_back("<root>");
    int currentIdx = 0;
    for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
      if (i == primaryIdx || IsDescendantOf(m_document, i, primaryIdx))
        continue;
      const std::string &id = m_document.objects[static_cast<size_t>(i)].id;
      parentIds.push_back(id);
      parentItems.push_back(parentIds.back().c_str());
      if (id == currentParent)
        currentIdx = static_cast<int>(parentItems.size()) - 1;
    }
    if (Horo::Ui::Combo(Horo::Ui::GetEditorTheme(), "Parent##identity_parent",
                       &currentIdx, parentItems.data(),
                       static_cast<int>(parentItems.size()))) {
      if (currentIdx == 0)
        obj.props.erase("parentId");
      else
        obj.props["parentId"] = parentIds[static_cast<size_t>(currentIdx - 1)];
      m_document.dirty = true;
    }
  }
  ImGui::Separator();
}

/** @copydoc EditorLayer::DrawPropertiesCameraSection */
void EditorLayer::DrawPropertiesCameraSection(SceneObject &obj,
                                              int primaryIdx) {
  const Vec3 oldPos = obj.position;
  const Quaternion oldRot = Quaternion::FromEuler(
      ToRadians(obj.pitch), ToRadians(obj.yaw), ToRadians(obj.roll));
  bool changedTransform = false;

  if (std::array<float, 3> pos = {obj.position.x, obj.position.y,
                                  obj.position.z};
      Horo::Ui::RenderEditorDragFloat3("Position", "##Position", pos.data(), 0.05f)) {
    obj.position = {pos[0], pos[1], pos[2]};
    changedTransform = true;
    m_document.dirty = true;
    if (m_transformCb)
      m_transformCb(obj);
  }

  if (Horo::Ui::RenderEditorDragFloat("Yaw", "##Yaw", obj.yaw, 1.0f, -360.0f, 360.0f)) {
    changedTransform = true;
    m_document.dirty = true;
    if (m_transformCb)
      m_transformCb(obj);
  }

  if (float pitch = obj.pitch;
      Horo::Ui::RenderEditorDragFloat("Pitch", "##Pitch", pitch, 1.0f, -89.0f, 89.0f)) {
    obj.pitch = std::max(-89.0f, std::min(89.0f, pitch));
    changedTransform = true;
    m_document.dirty = true;
    if (m_transformCb)
      m_transformCb(obj);
  }

  if (changedTransform) {
    const Quaternion newRot = Quaternion::FromEuler(
        ToRadians(obj.pitch), ToRadians(obj.yaw), ToRadians(obj.roll));
    PropagateHierarchyTransformDelta(
        m_document, primaryIdx, ParentTransformState{oldPos, oldRot},
        ParentTransformState{obj.position, newRot}, m_transformCb);
  }

  ImGui::Separator();

  auto drawCamFloatProp = [&](const char *key, auto drawWidget,
                              const char *defaultVal) {
    auto &str = obj.props[key];
    if (str.empty())
      str = defaultVal;
    float v = std::stof(str);
    if (drawWidget(v)) {
      str = std::format("{:.4f}", v);
      m_document.dirty = true;
    }
  };

  drawCamFloatProp(
      "fov",
      [](float &v) { return ImGui::SliderFloat("FOV", &v, 1.0f, 179.0f); },
      "60");
  drawCamFloatProp(
      "nearClip",
      [](float &v) {
        return ImGui::DragFloat("Near Clip", &v, 0.01f, 0.001f, 100.0f);
      },
      "0.1");
  drawCamFloatProp(
      "farClip",
      [](float &v) {
        return ImGui::DragFloat("Far Clip", &v, 1.0f, 1.0f, 100000.0f);
      },
      "500");

  ImGui::Separator();

  // Follow Target dropdown
  {
    auto &followId = obj.props["followTargetId"];
    std::vector<const char *> targetItems;
    targetItems.push_back("<none>");
    int curTarget = 0;
    int ti = 1;
    for (const auto &other : m_document.objects) {
      if (other.id == obj.id)
        continue;
      targetItems.push_back(other.id.c_str());
      if (other.id == followId)
        curTarget = ti;
      ++ti;
    }
    if (Horo::Ui::Combo(Horo::Ui::GetEditorTheme(), "Follow Target", &curTarget,
                       targetItems.data(),
                       static_cast<int>(targetItems.size()))) {
      followId =
          (curTarget == 0) ? "" : targetItems[static_cast<size_t>(curTarget)];
      m_document.dirty = true;
    }
  }

  ImGui::Separator();
  if (ImGui::Button("Delete"))
    RequestDeleteSelectedObjects();
}

/** @copydoc EditorLayer::DrawPropertiesTransformSection */
void EditorLayer::DrawPropertiesTransformSection(SceneObject &obj,
                                                 int primaryIdx) {
  DrawUiAutomationMarker("##properties_test/transform_section");
  const Vec3 oldPos = obj.position;
  const Quaternion oldRot = Quaternion::FromEuler(
      ToRadians(obj.pitch), ToRadians(obj.yaw), ToRadians(obj.roll));
  bool changedTransform = false;

  if (std::array<float, 3> pos = {obj.position.x, obj.position.y,
                                  obj.position.z};
      Horo::Ui::RenderEditorDragFloat3("Position", "##Position", pos.data(), 0.05f)) {
    obj.position = {pos[0], pos[1], pos[2]};
    changedTransform = true;
    m_document.dirty = true;
    if (m_transformCb)
      m_transformCb(obj);
  }

  if (std::array<float, 3> scl = {obj.scale.x, obj.scale.y, obj.scale.z};
      Horo::Ui::RenderEditorDragFloat3("Scale", "##Scale", scl.data(), 0.02f, 0.01f, 200.0f)) {
    obj.scale = {scl[0], scl[1], scl[2]};
    m_document.dirty = true;
    if (m_transformCb)
      m_transformCb(obj);
  }

  if (std::array<float, 3> rot = {obj.pitch, obj.yaw, obj.roll};
      Horo::Ui::RenderEditorDragFloat3("Rotation (P/Y/R)", "##Rotation (P/Y/R)",
                                       rot.data(), 1.0f, -360.0f, 360.0f)) {
    obj.pitch = rot[0];
    obj.yaw = rot[1];
    obj.roll = rot[2];
    changedTransform = true;
    m_document.dirty = true;
    if (m_transformCb)
      m_transformCb(obj);
  }

  if (changedTransform) {
    const Quaternion newRot = Quaternion::FromEuler(
        ToRadians(obj.pitch), ToRadians(obj.yaw), ToRadians(obj.roll));
    PropagateHierarchyTransformDelta(
        m_document, primaryIdx, ParentTransformState{oldPos, oldRot},
        ParentTransformState{obj.position, newRot}, m_transformCb);
  }
}

/** @copydoc EditorLayer::DrawPropertiesAssetSection */
void EditorLayer::DrawPropertiesAssetSection(SceneObject &obj) {
  ImGui::Separator();
  ImGui::Text("Asset");
  DrawUiAutomationMarker("##properties_test/asset_section");

  std::vector<const char *> assetItems;
  assetItems.reserve(m_document.assets.size() + 1);
  assetItems.push_back("<none>");
  int currentAssetIndex = 0;
  int assetIndex = 1;
  for (const auto &[assetId, _] : m_document.assets) {
    assetItems.push_back(assetId.c_str());
    if (obj.assetId == assetId)
      currentAssetIndex = assetIndex;
    ++assetIndex;
  }

  if (Horo::Ui::Combo(Horo::Ui::GetEditorTheme(), "Asset ID", &currentAssetIndex,
                     assetItems.data(), static_cast<int>(assetItems.size()))) {
    if (currentAssetIndex == 0)
      obj.assetId.clear();
    else
      obj.assetId = assetItems[static_cast<size_t>(currentAssetIndex)];
    SyncAssetScaleMetadata(&m_document);
    MarkDirtyAndReload();
  }

  if (!obj.assetId.empty()) {
    auto assetIt = m_document.assets.find(obj.assetId);
    if (assetIt != m_document.assets.end()) {
      ImGui::TextDisabled("mesh: %s", assetIt->second.mesh.c_str());
      ImGui::TextDisabled("renderScale: %s",
                          assetIt->second.renderScale.c_str());
    } else {
      const auto &theme = Horo::Ui::GetEditorTheme();
      Horo::Ui::RenderEditorStatusText(theme, Horo::Ui::EditorStatusLevel::Warning,
                                       "Missing asset: %s", obj.assetId.c_str());
    }
  }
}

/** @copydoc EditorLayer::DrawSchemaFieldWidget */
void EditorLayer::DrawSchemaFieldWidget(const SceneObject &obj,
                                        const FieldDef &fd, std::string &val) {
  using enum SceneObjectType;
  const auto &theme = Horo::Ui::GetEditorTheme();
  const std::string widgetLabel = fd.label + "##schema_" + fd.key;
  // Lambda keeps the repeated Light-transform notification out of the switch
  // body so the per-case nesting depth stays within S3776 limits.
  auto notifyLightTransform = [&]() {
    if (obj.type == Light && m_transformCb)
      m_transformCb(obj);
  };
  switch (fd.widget) {
  case FieldDef::Widget::String: {
    std::string buf(256, '\0');
    val.copy(buf.data(), buf.size() - 1);
    if (ImGui::InputText(widgetLabel.c_str(), buf.data(), buf.size())) {
      val = buf.data();
      m_document.dirty = true;
      notifyLightTransform();
    }
    break;
  }
  case FieldDef::Widget::Float: {
    if (float f = val.empty() ? fd.minVal : std::stof(val);
        Horo::Ui::RenderEditorSliderFloat(fd.label.c_str(), widgetLabel.c_str(),
                                          f, fd.minVal, fd.maxVal)) {
      val = std::format("{:.4f}", f);
      m_document.dirty = true;
      notifyLightTransform();
    }
    break;
  }
  case FieldDef::Widget::Bool: {
    if (bool b = (val == "true" || val == "1");
        Horo::Ui::RenderEditorCheckbox(theme, widgetLabel.c_str(), b)) {
      val = b ? "true" : "false";
      m_document.dirty = true;
      notifyLightTransform();
    }
    break;
  }
  case FieldDef::Widget::Enum: {
    int cur = FindEnumOptionIndex(fd.options, val);
    if (const std::string items = BuildImGuiComboItems(fd.options);
        ImGui::Combo(widgetLabel.c_str(), &cur, items.c_str())) {
      val = fd.options[static_cast<size_t>(cur)];
      m_document.dirty = true;
      notifyLightTransform();
    }
    break;
  }
  case FieldDef::Widget::Color3: {
    std::array<float, 3> col = {1.0f, 1.0f, 1.0f};
    ParseRGBString(val, col.data());
    if (Horo::Ui::RenderEditorColorEdit3(fd.label.c_str(), widgetLabel.c_str(),
                                         col.data())) {
      val = std::format("{:.4f},{:.4f},{:.4f}", col[0], col[1], col[2]);
      m_document.dirty = true;
      notifyLightTransform();
    }
    break;
  }
  }
}

/** @copydoc EditorLayer::DrawPropertiesSchemaFields */
void EditorLayer::DrawPropertiesSchemaFields(SceneObject &obj) {
  using enum SceneObjectType;
  ImGui::Separator();
  ImGui::Text("Props");

  const TypeSchema *schema = m_schema.GetSchema(obj.type);
  if (!schema)
    return;
  for (const auto &fd : schema->fields) {
    std::string &val = obj.props[fd.key];
    if (val.empty())
      val = fd.defaultValue;
    DrawSchemaFieldWidget(obj, fd, val);
  }
}

/** @copydoc EditorLayer::DrawLightComponentFields */
void EditorLayer::DrawLightComponentFields(ComponentDesc &comp) {
  // Intensity
  if (float intensity =
          comp.props.contains("intensity")
              ? std::strtof(comp.props["intensity"].c_str(), nullptr)
              : 1.0f;
      Horo::Ui::RenderEditorSliderFloat("Intensity", "##Intensity", intensity, 0.0f, 10.0f)) {
    comp.props["intensity"] = std::format("{:.4f}", intensity);
    m_document.dirty = true;
  }
  // Color
  std::array<float, 3> col = {1.0f, 1.0f, 1.0f};
  if (comp.props.contains("color"))
    ParseRGBString(comp.props["color"], col.data());
  if (Horo::Ui::RenderEditorColorEdit3("Color", "##Color", col.data())) {
    comp.props["color"] =
        std::format("{:.4f},{:.4f},{:.4f}", col[0], col[1], col[2]);
    m_document.dirty = true;
  }
  // Radius
  float radius = comp.props.contains("radius")
                     ? std::strtof(comp.props["radius"].c_str(), nullptr)
                     : 5.0f;
  if (Horo::Ui::RenderEditorDragFloat("Radius", "##Radius", radius, 0.1f, 0.0f, 100.0f)) {
    comp.props["radius"] = std::format("{:.4f}", radius);
    m_document.dirty = true;
  }
}

/** @copydoc EditorLayer::DrawRigidBodyComponentFields */
void EditorLayer::DrawRigidBodyComponentFields(ComponentDesc &comp) {
  const auto &theme = Horo::Ui::GetEditorTheme();
  // Mass
  if (float mass = comp.props.contains("mass")
                       ? std::strtof(comp.props["mass"].c_str(), nullptr)
                       : 1.0f;
      Horo::Ui::RenderEditorDragFloat("Mass", "##Mass", mass, 0.1f, 0.0f, 10000.0f)) {
    comp.props["mass"] = std::format("{:.4f}", mass);
    m_document.dirty = true;
  }
  // Is Kinematic
  if (bool isKinematic = comp.props.contains("isKinematic") &&
                         comp.props["isKinematic"] == "true";
      Horo::Ui::RenderEditorCheckbox(theme, "Is Kinematic", isKinematic)) {
    comp.props["isKinematic"] = isKinematic ? "true" : "false";
    m_document.dirty = true;
  }
  // Use Gravity
  bool useGravity =
      !comp.props.contains("useGravity") || comp.props["useGravity"] == "true";
  if (Horo::Ui::RenderEditorCheckbox(theme, "Use Gravity", useGravity)) {
    comp.props["useGravity"] = useGravity ? "true" : "false";
    m_document.dirty = true;
  }
}

/** @copydoc EditorLayer::DrawScriptComponentField */
void EditorLayer::DrawScriptComponentField(ComponentDesc &comp) {
  std::vector<std::string> options;
  if (m_scriptBehaviorOptionsCb)
    options = m_scriptBehaviorOptionsCb();
  std::erase_if(options, [](std::string_view s) { return s.empty(); });
  std::ranges::sort(options);
  const auto optionsUniqueTail = std::ranges::unique(options);
  options.erase(optionsUniqueTail.begin(), optionsUniqueTail.end());

  std::string current = comp.props["behaviorTag"];
  if (!current.empty() && std::ranges::find(options, current) == options.end())
    options.push_back(current);

  std::vector<const char *> labels;
  labels.reserve(options.size() + 1);
  labels.push_back("<none>");
  int currentIdx = 0;
  for (int i = 0; i < static_cast<int>(options.size()); ++i) {
    labels.push_back(options[static_cast<size_t>(i)].c_str());
    if (options[static_cast<size_t>(i)] == current)
      currentIdx = i + 1;
  }

  if (Horo::Ui::Combo(Horo::Ui::GetEditorTheme(), "Behavior", &currentIdx,
                     labels.data(), static_cast<int>(labels.size()))) {
    comp.props["behaviorTag"] =
        (currentIdx == 0) ? "" : options[static_cast<size_t>(currentIdx - 1)];
    m_document.dirty = true;
  }
}

/** @copydoc EditorLayer::DrawPropertiesComponentsList */
void EditorLayer::DrawPropertiesComponentsList(SceneObject &obj) {
  // NOSONAR: cpp:S3776 component list with per-type edit panels; complexity
  // from component type dispatch
  ImGui::Separator();
  ImGui::Text("Components");

  int removeIdx = -1;
  for (int ci = 0; ci < static_cast<int>(obj.components.size()); ++ci) {
    ComponentDesc &comp = obj.components[ci];

    // Collapsing header label + inline [x] button via ID stack trick
    std::string headerLabel;
    if (comp.type == "light")
      headerLabel = "Light";
    else if (comp.type == "rigidbody")
      headerLabel = "RigidBody";
    else if (comp.type == "script")
      headerLabel = "Script";
    else
      headerLabel = comp.type;
    ImGui::PushID(ci);
    bool open = ImGui::CollapsingHeader(headerLabel.c_str(),
                                        ImGuiTreeNodeFlags_DefaultOpen);
    bool removeThisComponent = false;

    if (ImGui::BeginPopupContextItem("comp_ctx")) {
      if (ImGui::MenuItem("Remove Component"))
        removeThisComponent = true;
      ImGui::EndPopup();
    }

    // Remove button on the same line, right-aligned
    float btnW =
        ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - btnW);
    if (ImGui::SmallButton("x"))
      removeThisComponent = true;

    if (removeThisComponent)
      removeIdx = ci;

    if (open) {
      if (comp.type == "light")
        DrawLightComponentFields(comp);
      else if (comp.type == "rigidbody")
        DrawRigidBodyComponentFields(comp);
      else if (comp.type == "script")
        DrawScriptComponentField(comp);
    }
    ImGui::PopID();
  }

  if (removeIdx >= 0) {
    obj.components.erase(obj.components.begin() + removeIdx);
    m_document.dirty = true;
  }
}

/** @copydoc EditorLayer::DrawAddComponentMenuItems */
void EditorLayer::DrawAddComponentMenuItems(SceneObject &obj) {
  std::vector<const ComponentSchema *> componentSchemas;
  componentSchemas.reserve(m_schema.ComponentSchemas().size());
  for (const auto &[key, schema] : m_schema.ComponentSchemas()) {
    if (SchemaAppliesToObjectType(schema.appliesTo, obj.type))
      componentSchemas.push_back(&schema);
  }
  std::ranges::sort(componentSchemas, [](const ComponentSchema *lhs,
                                         const ComponentSchema *rhs) {
    const std::string lhsLabel = lhs ? lhs->label : std::string();
    if (const std::string rhsLabel = rhs ? rhs->label : std::string();
        lhsLabel != rhsLabel)
      return lhsLabel < rhsLabel;
    return lhs && rhs ? lhs->name < rhs->name : lhs < rhs;
  });

  if (!componentSchemas.empty()) {
    for (const ComponentSchema *componentSchema : componentSchemas) {
      if (!componentSchema)
        continue;
      const std::string menuLabel = componentSchema->label.empty()
                                        ? componentSchema->name
                                        : componentSchema->label;
      if (ImGui::MenuItem(menuLabel.c_str())) {
        ComponentDesc component;
        component.type = componentSchema->name;
        ApplyComponentSchemaDefaults(component);
        obj.components.push_back(std::move(component));
        m_document.dirty = true;
      }
    }
    return;
  }

  DrawFallbackAddComponentMenuItems(obj);
}

/** @copydoc EditorLayer::DrawFallbackAddComponentMenuItems */
void EditorLayer::DrawFallbackAddComponentMenuItems(SceneObject &obj) {
  // Fallback built-in components when no schema is registered
  if (ImGui::MenuItem("Light")) {
    ComponentDesc component;
    component.type = "light";
    ApplyComponentSchemaDefaults(component);
    if (component.props.empty()) {
      component.props["intensity"] = "1.0000";
      component.props["color"] = "1.0000,1.0000,1.0000";
      component.props["radius"] = "5.0000";
    }
    obj.components.push_back(std::move(component));
    m_document.dirty = true;
  }
  if (ImGui::MenuItem("RigidBody")) {
    ComponentDesc component;
    component.type = "rigidbody";
    ApplyComponentSchemaDefaults(component);
    if (component.props.empty()) {
      component.props["mass"] = "1.0000";
      component.props["isKinematic"] = "false";
      component.props["useGravity"] = "true";
    }
    obj.components.push_back(std::move(component));
    m_document.dirty = true;
  }
  if (ImGui::MenuItem("Script")) {
    ComponentDesc component;
    component.type = "script";
    ApplyComponentSchemaDefaults(component);
    if (component.props.empty())
      component.props["behaviorTag"] = "";
    obj.components.push_back(std::move(component));
    m_document.dirty = true;
  }
}

/** @copydoc EditorLayer::DrawPropertiesAddComponentMenu */
void EditorLayer::DrawPropertiesAddComponentMenu(SceneObject &obj) {
  ImGui::Spacing();
  if (ImGui::Button("+ Add Component"))
    ImGui::OpenPopup("##add_component_popup");
  if (ImGui::BeginPopup("##add_component_popup")) {
    DrawAddComponentMenuItems(obj);
    ImGui::EndPopup();
  }
}

} // namespace Horo::Editor
