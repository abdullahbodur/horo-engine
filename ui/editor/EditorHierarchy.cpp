#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/Logger.h"
#include "core/StringHash.h"
#include "ui/editor/EditorSceneGraph.h"
#include "ui/editor/EditorSearch.h"
#include "ui/editor/EditorUiLogic.h"
#include "ui/IconsFontAwesome6.h"
#include "ui/HoroTheme.h"
#include "scene/Entity.h"
#include "scene/Registry.h"
#include "scene/components/TransformComponent.h"

namespace Horo::Editor {

void EditorLayer::DrawObjectList() {
  using enum SceneObjectType;
  const ImGuiIO &io = ImGui::GetIO();
  const float bottomDockH = ComputeEditorBottomDockHeight(io.DisplaySize.y);
  const float leftDockW = ComputeEditorLeftDockWidth(io.DisplaySize.x);
  const float workBottom = io.DisplaySize.y - kEditorStatusH - bottomDockH;
  const float hierarchyHeight =
      std::max(220.0f, (workBottom - kEditorToolbarH) * kHierarchySectionRatio);
  ImGui::SetNextWindowPos(ImVec2(0.0f, kEditorToolbarH), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(leftDockW, hierarchyHeight),
                           ImGuiCond_Always);
  ImGui::Begin(kEditorHierarchyWindow, nullptr, kMainPanelWindowFlags);

  const ImGuiStyle &style = ImGui::GetStyle();
  const float panelWidth = ImGui::GetContentRegionAvail().x;
  const ImVec4 panel = Ui::GetEditorTheme().palette.panelSoft;
  const ImVec4 border = Ui::GetEditorTheme().palette.border;

  ImGui::PushStyleColor(ImGuiCol_Button, panel);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, panel);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, panel);
  ImGui::PushStyleColor(ImGuiCol_Border, border);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::Button("Scene", ImVec2(92.0f, 30.0f));
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(4);

  ImGui::SameLine();
  ImGui::SetCursorPosX(panelWidth - 56.0f);
  if (ImGui::SmallButton(ICON_FA_PLUS))
    ImGui::OpenPopup("hierarchy_add_popup");
  ImGui::SameLine();
  ImGui::SetCursorPosX(panelWidth - 12.0f);
  ImGui::TextDisabled("%s", ICON_FA_ELLIPSIS_VERTICAL);
  if (ImGui::BeginPopup("hierarchy_add_popup")) {
    if (ImGui::MenuItem("Panel"))
      AddObject(Panel);
    if (ImGui::MenuItem("Prop"))
      AddObject(Prop);
    if (ImGui::MenuItem("Light"))
      AddObject(Light);
    if (ImGui::MenuItem("Camera"))
      AddObject(Camera);
    ImGui::EndPopup();
  }

  std::string searchBuf(256, '\0');
  m_objectSearchQuery.copy(searchBuf.data(), searchBuf.size() - 1);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, Ui::GetEditorTheme().palette.input);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
  ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
  ImGui::SetNextItemWidth(std::max(80.0f, panelWidth - 34.0f));
  if (ImGui::InputTextWithHint("##object_search", "Search objects...",
                               searchBuf.data(), searchBuf.size()))
    m_objectSearchQuery = searchBuf.data();
  ImGui::PopItemFlag();
  ImGui::SameLine();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
  ImGui::TextDisabled("%s", ICON_FA_FILTER);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::Spacing();
  ImGui::Spacing();

  const auto &palette = Ui::GetEditorTheme().palette;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 2.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 6.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));

  // Primary scene
  DrawSceneHeader(m_document, true, -1);

  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(3);

  // Right-click empty space → add object to primary scene
  if (ImGui::BeginPopupContextWindow("obj_ctx_empty",
                                     ImGuiPopupFlags_MouseButtonRight |
                                         ImGuiPopupFlags_NoOpenOverItems)) {
    if (ImGui::BeginMenu("Add")) {
      if (ImGui::MenuItem("Panel"))
        AddObject(Panel);
      if (ImGui::MenuItem("Prop"))
        AddObject(Prop);
      if (ImGui::MenuItem("Light"))
        AddObject(Light);
      if (ImGui::MenuItem("Camera"))
        AddObject(Camera);
      ImGui::EndMenu();
    }
    ImGui::EndPopup();
  }

  m_uiWidgets.DrawRenameObjectModal();

  ImGui::End();
}

// Renders a Unity-style collapsible scene header, then the objects tree under
// it.
void EditorLayer::DrawSceneHeaderContextMenu(SceneDocument & /*doc*/,
                                             bool isPrimary,
                                             int additionalIndex) {
  using enum SceneObjectType;
  if (const std::string ctxId =
          isPrimary ? std::string("scene_hdr_ctx_p")
                    : std::format("scene_hdr_ctx_{}", additionalIndex);
      !ImGui::BeginPopupContextItem(ctxId.c_str()))
    return;
  if (isPrimary) {
    if (ImGui::BeginMenu("Add")) {
      if (ImGui::MenuItem("Panel"))
        AddObject(Panel);
      if (ImGui::MenuItem("Prop"))
        AddObject(Prop);
      if (ImGui::MenuItem("Light"))
        AddObject(Light);
      if (ImGui::MenuItem("Camera"))
        AddObject(Camera);
      ImGui::EndMenu();
    }
    ImGui::Separator();
    std::string saveErr;
    if (ImGui::MenuItem("Save Scene"))
      SaveDocument(&saveErr);
  } else {
    std::string saveErr;
    if (ImGui::MenuItem("Save Scene"))
      SaveAdditionalScene(additionalIndex, &saveErr);
    ImGui::Separator();
    if (ImGui::MenuItem("Close Scene"))
      CloseAdditionalScene(additionalIndex);
  }
  ImGui::EndPopup();
}

void EditorLayer::DrawSceneHeaderDragDrop(SceneDocument &doc) {
  if (!ImGui::BeginDragDropTarget())
    return;
  if (const ImGuiPayload *payload =
          ImGui::AcceptDragDropPayload("SCENE_OBJECT_INDEX")) {
    if (payload->DataSize == sizeof(int)) {
      const int src = *static_cast<const int *>(payload->Data);
      if (src >= 0 && src < static_cast<int>(doc.objects.size())) {
        doc.objects[static_cast<size_t>(src)].props.erase("parentId");
        doc.dirty = true;
      }
    }
  }
  if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
    const std::string assetId(static_cast<const char *>(payload->Data));
    std::string createError;
    if (!CreateObjectFromAsset(assetId, "", nullptr, nullptr, nullptr,
                               &createError))
      LogWarn("[Editor] Hierarchy root asset drop failed: {}", createError);
  }
  ImGui::EndDragDropTarget();
}

void EditorLayer::DrawSceneHeader(SceneDocument &doc, bool isPrimary,
                                  int additionalIndex) {
  std::string label = doc.sceneName.empty() ? "Level" : doc.sceneName;
  if (isPrimary && doc.dirty)
    label += " *";
  label = std::format("{}  {}", ICON_FA_FOLDER_OPEN, label);

  if (isPrimary)
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.33f, 0.52f, 1.0f));

  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  const std::string nodeId = isPrimary
                                 ? std::string("##scene_primary")
                                 : std::format("##scene_{}", additionalIndex);
  const bool open = ImGui::TreeNodeEx(nodeId.c_str(),
                                      ImGuiTreeNodeFlags_OpenOnArrow |
                                          ImGuiTreeNodeFlags_SpanAvailWidth |
                                          ImGuiTreeNodeFlags_DefaultOpen,
                                      "%s", label.c_str());

  if (isPrimary)
    ImGui::PopStyleColor();

  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 6.0f);
  ImGui::TextDisabled("%s", ICON_FA_EYE);

  DrawSceneHeaderContextMenu(doc, isPrimary, additionalIndex);
  if (isPrimary)
    DrawSceneHeaderDragDrop(doc);

  if (open) {
    DrawObjectsTree(doc, isPrimary);
    ImGui::TreePop();
  }
}

// ---- DrawTreeNode — extracted from drawNode lambda in DrawObjectsTree
// --------

void EditorLayer::HandleTreeNodeClickSelection(int idx) {
  const auto &treeIo = ImGui::GetIO();
  if (treeIo.KeyShift && m_lastClickedHierarchyIdx >= 0) {
    const int lo = std::min(m_lastClickedHierarchyIdx, idx);
    const int hi = std::max(m_lastClickedHierarchyIdx, idx);
    m_selectedIndices.clear();
    for (int ri = lo; ri <= hi; ++ri)
      m_selectedIndices.push_back(ri);
  } else if (treeIo.KeyCtrl || treeIo.KeySuper) {
    ToggleSelect(idx);
    m_lastClickedHierarchyIdx = idx;
  } else {
    m_selectedIndices = {idx};
    m_lastClickedHierarchyIdx = idx;
  }
}

void EditorLayer::HandleTreeNodeDragDrop(int idx, SceneDocument &doc,
                                         SceneObject &obj) {
  // Guard: TreeNodeEx may be clipped (LastItemData.ID == 0), which would
  // trigger an IM_ASSERT inside BeginDragDropSource.
  if (ImGui::GetItemID() != 0 && ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload("SCENE_OBJECT_INDEX", &idx, sizeof(int));
    ImGui::TextUnformatted(obj.id.c_str());
    ImGui::EndDragDropSource();
  }
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload("SCENE_OBJECT_INDEX")) {
      // Guard: wrong size or invalid parent → skip reparent
      const bool sizeOk = payload->DataSize == sizeof(int);
      const int src = sizeOk ? *static_cast<const int *>(payload->Data) : -1;
      const bool valid = sizeOk && src >= 0 &&
                         src < static_cast<int>(doc.objects.size()) &&
                         src != idx && !IsDescendantOf(doc, idx, src);
      if (valid) {
        doc.objects[static_cast<size_t>(src)].props["parentId"] = obj.id;
        doc.dirty = true;
      }
    }
    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload("ASSET_ID")) {
      const std::string assetId(static_cast<const char *>(payload->Data));
      std::string createError;
      if (!CreateObjectFromAsset(assetId, obj.id, nullptr, nullptr, nullptr,
                                 &createError))
        LogWarn("[Editor] Hierarchy child asset drop failed: {}", createError);
    }
    ImGui::EndDragDropTarget();
  }
  if (ImGui::BeginPopupContextItem("obj_ctx")) {
    ImGui::Separator();
    if (ImGui::MenuItem("Rename..."))
      OpenRenameObjectModal(idx);
    if (ImGui::MenuItem("Duplicate")) {
      m_selectedIndices = {idx};
      DuplicatePrimarySelection();
    }
    if (ImGui::MenuItem("Delete")) {
      m_selectedIndices = {idx};
      RequestDeleteSelectedObjects();
    }
    ImGui::Separator();
    if (const bool hasParent = !GetParentId(obj).empty();
        ImGui::MenuItem("Unparent", nullptr, false, hasParent)) {
      obj.props.erase("parentId");
      doc.dirty = true;
    }
    ImGui::EndPopup();
  }
}

void EditorLayer::DrawTreeNode(int idx, SceneDocument &doc, bool isPrimary,
                               int &shownObjectCount,
                               std::vector<std::vector<int>> &children) {
  if (idx < 0 || idx >= static_cast<int>(doc.objects.size()))
    return;
  SceneObject &obj = ObjectAt(doc, idx);
  ++shownObjectCount;

  ImGui::PushID(idx);
  const bool hasChildren = !children[static_cast<size_t>(idx)].empty();
  ImGuiTreeNodeFlags flags =
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
      ImGuiTreeNodeFlags_FramePadding;
  if (!hasChildren)
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  if (isPrimary && IsSelected(idx))
    flags |= ImGuiTreeNodeFlags_Selected;

  auto nodeLabel = std::format("{}  {}", ObjectTypeIcon(obj.type), obj.id);
  if (!obj.assetId.empty())
    nodeLabel += std::format("  [{}]", obj.assetId);

  const ImVec2 rowMin = ImGui::GetCursorScreenPos();
  const float rowRight = ImGui::GetWindowPos().x +
                         ImGui::GetWindowContentRegionMax().x - 2.0f;
  const ImVec2 rowMax(rowRight, rowMin.y + ImGui::GetFrameHeight());
  const bool rowHovered = ImGui::IsMouseHoveringRect(rowMin, rowMax);
  if (isPrimary && (IsSelected(idx) || rowHovered)) {
    const auto &palette = Ui::GetEditorTheme().palette;
    const ImVec4 fill = IsSelected(idx) ? palette.selection : palette.selectionHover;
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(rowMin.x, rowMin.y + 1.0f),
        ImVec2(rowMax.x, rowMax.y - 1.0f),
        ImGui::ColorConvertFloat4ToU32(fill), 6.0f);
  }

  const bool open =
      ImGui::TreeNodeEx("##obj_tree", flags, "%s", nodeLabel.c_str());
  const bool clickedTreeNode = ImGui::IsItemClicked();

  if (isPrimary && clickedTreeNode)
    HandleTreeNodeClickSelection(idx);
  if (isPrimary)
    HandleTreeNodeDragDrop(idx, doc, obj);

  const float eyeIconWidth = ImGui::CalcTextSize(ICON_FA_EYE).x;
  const float eyeX = ImGui::GetWindowContentRegionMax().x - eyeIconWidth - 16.0f;
  ImGui::SameLine(eyeX);
  ImGui::TextDisabled("%s", ICON_FA_EYE);

  if (open && hasChildren) {
    for (int childIdx : children[static_cast<size_t>(idx)])
      DrawTreeNode(childIdx, doc, isPrimary, shownObjectCount, children);
    ImGui::TreePop();
  }

  ImGui::PopID();
}

void EditorLayer::DrawObjectsTreeSearchMode( // NOSONAR: cpp:S3776 search-mode
                                             // tree draw; complexity from
                                             // per-object context menus
    SceneDocument &doc, bool isPrimary, const std::string &query) {
  using enum SceneObjectType;
  int shownObjectCount = 0;

  // Lambda keeps the multi-select range expansion within S134 nesting limits.
  auto applySearchSelection = [this](int i) {
    const auto &selIo = ImGui::GetIO();
    if (selIo.KeyShift && m_lastClickedHierarchyIdx >= 0) {
      const int lo = std::min(m_lastClickedHierarchyIdx, i);
      const int hi = std::max(m_lastClickedHierarchyIdx, i);
      m_selectedIndices.clear();
      for (int ri = lo; ri <= hi; ++ri)
        m_selectedIndices.push_back(ri);
    } else if (selIo.KeyCtrl || selIo.KeySuper) {
      ToggleSelect(i);
      m_lastClickedHierarchyIdx = i;
    } else {
      m_selectedIndices = {i};
      m_lastClickedHierarchyIdx = i;
    }
  };

  for (int i = 0; i < static_cast<int>(doc.objects.size()); ++i) {
    ImGui::PushID(i);
    const auto &obj = doc.objects[static_cast<size_t>(i)];
    if (!ObjectMatchesQuickOpenQuery(obj, query)) {
      ImGui::PopID();
      continue;
    }
    const char *icon = ObjectTypeIcon(obj.type);

    const std::string selectableId = std::format("##obj_{}", i);

    const float lineH = ImGui::GetTextLineHeight();

    if (const float rowH = obj.assetId.empty() ? lineH : lineH * 2.0f + 2.0f;
        ImGui::Selectable(selectableId.c_str(), IsSelected(i), 0,
                          ImVec2(0, rowH)))
      applySearchSelection(i);
    ImGui::SameLine();
    {
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      ImGui::BeginGroup();
      ImGui::TextDisabled("%s", icon);
      ImGui::SameLine(0.0f, 4.0f);
      ImGui::Text("%s", obj.id.c_str());
      if (!obj.assetId.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + lineH + 2.0f));
        ImGui::TextDisabled("  > %s", obj.assetId.c_str());
      }
      ImGui::EndGroup();
    }

    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 20.0f);
    ImGui::TextDisabled("%s", ICON_FA_EYE);

    if (isPrimary && ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *payload =
              ImGui::AcceptDragDropPayload("ASSET_ID")) {
        const std::string assetId(static_cast<const char *>(payload->Data));
        std::string createError;
        if (!CreateObjectFromAsset( // NOSONAR
                assetId, obj.id, nullptr, nullptr, nullptr, &createError))
          LogWarn("[Editor] Hierarchy child asset drop failed: {}",
                  createError);
      }
      ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem("obj_ctx")) {
      if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Panel")) // NOSONAR: cpp:S134 add-menu items within
          // context popup in search mode
          AddObject(Panel, obj.id);
        if (ImGui::MenuItem("Prop")) // NOSONAR: cpp:S134
          AddObject(Prop, obj.id);
        if (ImGui::MenuItem("Light")) // NOSONAR: cpp:S134
          AddObject(Light, obj.id);
        if (ImGui::MenuItem("Camera")) // NOSONAR: cpp:S134
          AddObject(Camera, obj.id);
        ImGui::EndMenu();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Rename..."))
        OpenRenameObjectModal(i);
      if (ImGui::MenuItem("Duplicate")) {
        m_selectedIndices = {i};
        DuplicatePrimarySelection();
      }
      if (ImGui::MenuItem("Delete")) {
        m_selectedIndices = {i};
        RequestDeleteSelectedObjects();
      }
      ImGui::EndPopup();
    }

    ++shownObjectCount;
    ImGui::PopID();
  }

  if (shownObjectCount == 0 && !doc.objects.empty()) {
    ImGui::TextDisabled("No objects match '%s'", query.c_str());
    if (ImGui::Button("Clear Search"))
      m_objectSearchQuery.clear();
  }
}

void EditorLayer::DrawObjectsTreeRootDropZone() {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  ImGui::InvisibleButton("##hierarchy_root_asset_drop",
                         ImVec2(std::max(avail.x, 1.0f), 24.0f));
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload("ASSET_ID")) {
      const std::string assetId(static_cast<const char *>(payload->Data));
      std::string createError;
      if (!CreateObjectFromAsset(assetId, "", nullptr, nullptr, nullptr,
                                 &createError))
        LogWarn("[Editor] Hierarchy root asset drop failed: {}", createError);
    }
    ImGui::EndDragDropTarget();
  }
}

void EditorLayer::DrawObjectsTreeRuntimeEntities(
    const SceneDocument &doc) const {
  if (!m_liveRegistry)
    return;

  std::unordered_set<Entity> docEntities;
  for (const auto &obj : doc.objects) {
    const auto it = obj.props.find("_eid");
    if (it == obj.props.end())
      continue;
    try {
      docEntities.insert(static_cast<Entity>(std::stoul(it->second)));
    } catch (const std::invalid_argument &) {
      // NOSONAR: cpp:S2486 stoul on untrusted input; skip
      // malformed entity ids
      continue;
    } catch (const std::out_of_range &) {
      // NOSONAR: cpp:S2486 stoul out-of-range; skip gracefully
      continue;
    }
  }

  std::vector<Entity> runtimeEntities;
  for (Entity e : m_liveRegistry->GetEntities<TransformComponent>()) {
    if (!docEntities.contains(e))
      runtimeEntities.push_back(e);
  }

  if (runtimeEntities.empty())
    return;

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
  if (ImGui::TreeNodeEx("##runtime_entities",
                        ImGuiTreeNodeFlags_DefaultOpen |
                            ImGuiTreeNodeFlags_SpanAvailWidth,
                        "[rt] Runtime (%zu)", runtimeEntities.size())) {
    for (Entity e : runtimeEntities) {
      ImGui::PushID(static_cast<int>(e));
      ImGui::TreeNodeEx("##rt_ent",
                        ImGuiTreeNodeFlags_Leaf |
                            ImGuiTreeNodeFlags_NoTreePushOnOpen |
                            ImGuiTreeNodeFlags_SpanAvailWidth,
                        "[>] Entity %u", static_cast<unsigned>(e));
      ImGui::PopID();
    }
    ImGui::TreePop();
  }
  ImGui::PopStyleColor();
}

// Renders the tree of objects inside a scene, plus runtime entities for the
// primary scene.
void EditorLayer::DrawObjectsTree(SceneDocument &doc, bool isPrimary) {
  using enum SceneObjectType;
  // Search filter applies only to the primary scene
  if (const std::string &query = isPrimary ? m_objectSearchQuery : "";
      !query.empty()) {
    DrawObjectsTreeSearchMode(doc, isPrimary, query);
    return;
  }

  // Tree view
  int shownObjectCount = 0;
  std::unordered_map<std::string, int, StringHash, std::equal_to<>> idToIndex;
  idToIndex.reserve(doc.objects.size());
  for (int i = 0; i < static_cast<int>(doc.objects.size()); ++i)
    idToIndex[doc.objects[static_cast<size_t>(i)].id] = i;

  std::vector<std::vector<int>> children(doc.objects.size());
  std::vector<int> roots;
  roots.reserve(doc.objects.size());
  for (int i = 0; i < static_cast<int>(doc.objects.size()); ++i) {
    const SceneObject &obj = doc.objects[static_cast<size_t>(i)];
    int p = -1;
    if (const auto pIt = idToIndex.find(GetParentId(obj));
        pIt != idToIndex.end())
      p = pIt->second;
    if (p >= 0 && p != i)
      children[static_cast<size_t>(p)].push_back(i);
    else
      roots.push_back(i);
  }

  for (int rootIdx : roots)
    DrawTreeNode(rootIdx, doc, isPrimary, shownObjectCount, children);

  if (doc.objects.empty()) {
    ImGui::TextDisabled("No objects in scene");
    if (isPrimary)
      ImGui::TextDisabled("Tip: right-click or use Assets panel.");
  }

  if (isPrimary) {
    DrawObjectsTreeRootDropZone();
    DrawObjectsTreeRuntimeEntities(doc);
  }
}

} // namespace Horo::Editor
