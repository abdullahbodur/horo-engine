/**
 * @file EditorHierarchy.cpp
 * @brief Hierarchy panel: scene headers, object tree, search mode, drag/drop, and runtime entity listing.
 *
 * Implements @ref EditorLayer drawing helpers declared in @ref EditorLayer.h for the left-dock hierarchy window.
 */
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#include <imgui.h>

#include <algorithm>
#include <array>
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
#include "ui/UiComponents.h"
#include "scene/Entity.h"
#include "scene/Registry.h"
#include "scene/components/TransformComponent.h"

namespace Horo::Editor {

/** @copydoc EditorLayer::DrawObjectList */
void EditorLayer::DrawObjectList() {
  using enum SceneObjectType;
  const ImGuiIO &io = ImGui::GetIO();
  const float availableH = io.DisplaySize.y - kEditorStatusH - kEditorToolbarH;

  // Lazy-initialise split values from defaults on first frame.
  if (m_leftDockWidth <= 0.0f)
    m_leftDockWidth = ComputeEditorLeftDockWidth(io.DisplaySize.x);
  if (m_hierarchyHeightRatio <= 0.0f)
    m_hierarchyHeightRatio = kHierarchySectionRatio;

  const float leftDockW = m_leftDockWidth;
  const float hierarchyHeight = std::max(180.0f, availableH * m_hierarchyHeightRatio);
  ImGui::SetNextWindowPos(ImVec2(0.0f, kEditorToolbarH), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(leftDockW, hierarchyHeight),
                           ImGuiCond_Always);
  ImGui::Begin(kEditorHierarchyWindow, nullptr, kMainPanelWindowFlags);

  const float panelWidth = ImGui::GetContentRegionAvail().x;
  const Ui::EditorTheme &theme = Ui::GetEditorTheme();

  // Top bar
  {
    const std::array addItems = {
        Ui::EditorPanelDropdownItem{ICON_FA_EXPAND,    "Panel",  [this] { AddObject(Panel);  }},
        Ui::EditorPanelDropdownItem{ICON_FA_CUBE,      "Prop",   [this] { AddObject(Prop);   }},
        Ui::EditorPanelDropdownItem{ICON_FA_LIGHTBULB, "Light",  [this] { AddObject(Light);  }},
        Ui::EditorPanelDropdownItem{ICON_FA_VIDEO,     "Camera", [this] { AddObject(Camera); }},
    };
    const std::array tabs = {
        Ui::EditorPanelTabItem{Ui::EditorPanelTab::Scene, true},
    };
    const std::array actions = {
        Ui::EditorPanelActionItem{ICON_FA_PLUS, nullptr, addItems},
        Ui::EditorPanelActionItem{ICON_FA_ELLIPSIS_VERTICAL},
    };
    Ui::RenderEditorPanelTopBar(theme, "hierarchy_topbar", tabs, actions);
  }

  // Search bar
  {
    std::array<char, 256> searchBuf{};
    m_objectSearchQuery.copy(searchBuf.data(), searchBuf.size() - 1);
    Ui::EditorTreeSearchSlotConfig searchCfg;
    searchCfg.enabled = true;
    searchCfg.id = "##object_search";
    searchCfg.placeholder = "Search objects...";
    searchCfg.buffer = searchBuf.data();
    searchCfg.bufferSize = searchBuf.size();
    searchCfg.width = std::max(80.0f, panelWidth - 24.0f);
    if (Ui::RenderEditorTreeSearchSlot(theme, searchCfg))
      m_objectSearchQuery = searchBuf.data();
  }

  ImGui::Spacing();

  // Primary scene tree
  {
    const Ui::ScopedEditorTreeRowStyle treeRows(theme);
    DrawSceneHeader(m_document, true, -1);
  }

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

/** @copydoc EditorLayer::DrawSceneHeaderContextMenu */
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

/** @copydoc EditorLayer::DrawSceneHeaderDragDrop */
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

/** @copydoc EditorLayer::DrawSceneHeader */
void EditorLayer::DrawSceneHeader(SceneDocument &doc, bool isPrimary,
                                  int additionalIndex) {
  std::string label = doc.sceneName.empty() ? "Level" : doc.sceneName;
  if (isPrimary && doc.dirty)
    label += " *";

  const Ui::EditorTheme &theme = Ui::GetEditorTheme();
  const std::string nodeId = isPrimary
                                 ? std::string("##scene_primary")
                                 : std::format("##scene_{}", additionalIndex);

  Ui::EditorTreeItemSpec spec;
  spec.id            = nodeId.c_str();
  spec.label         = label.c_str();
  spec.prefixIcon    = ICON_FA_FOLDER_OPEN;
  spec.kind          = Ui::EditorTreeItemKind::Node;
  spec.treeFlags     = ImGuiTreeNodeFlags_OpenOnArrow
                     | ImGuiTreeNodeFlags_SpanAvailWidth
                     | ImGuiTreeNodeFlags_DefaultOpen
                     | ImGuiTreeNodeFlags_FramePadding;
  spec.selected      = isPrimary;
  spec.normalTextColor = &theme.palette.text;

  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  const auto res = Ui::DrawEditorTreeItem(theme, spec);

  // Eye suffix drawn as text so drag/drop and context menu still target the
  // tree node (last ImGui item after DrawEditorTreeItem with no suffixIcon).
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 6.0f);
  ImGui::TextDisabled("%s", ICON_FA_EYE);

  DrawSceneHeaderContextMenu(doc, isPrimary, additionalIndex);
  if (isPrimary)
    DrawSceneHeaderDragDrop(doc);

  if (res.open) {
    DrawObjectsTree(doc, isPrimary);
    ImGui::TreePop();
  }
}


/** @copydoc EditorLayer::HandleTreeNodeClickSelection */
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

namespace {
/** @brief Validates and applies a reparent drag-drop payload. */
void AcceptReparentDrop(int idx, SceneDocument &doc, const SceneObject &obj,
                        const ImGuiPayload *payload) {
  if (payload->DataSize != sizeof(int))
    return;
  const int src = *static_cast<const int *>(payload->Data);
  const bool srcValid = src >= 0 && src < static_cast<int>(doc.objects.size());
  if (srcValid && src != idx && !IsDescendantOf(doc, idx, src)) {
    doc.objects[static_cast<size_t>(src)].props["parentId"] = obj.id;
    doc.dirty = true;
  }
}
} // namespace

/** @copydoc EditorLayer::HandleTreeNodeDragDrop */
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
            ImGui::AcceptDragDropPayload("SCENE_OBJECT_INDEX"))
      AcceptReparentDrop(idx, doc, obj, payload);
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
  if (!ImGui::BeginPopupContextItem("obj_ctx"))
    return;

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

/** @copydoc EditorLayer::DrawTreeNode */
void EditorLayer::DrawTreeNode(int idx, SceneDocument &doc, bool isPrimary,
                               int &shownObjectCount,
                               std::vector<std::vector<int>> &children) {
  if (idx < 0 || idx >= static_cast<int>(doc.objects.size()))
    return;
  SceneObject &obj = ObjectAt(doc, idx);
  ++shownObjectCount;

  ImGui::PushID(idx);
  const bool hasChildren = !children[static_cast<size_t>(idx)].empty();

  auto nodeLabel = std::format("{}  {}", ObjectTypeIcon(obj.type), obj.id);
  if (!obj.assetId.empty())
    nodeLabel += std::format("  [{}]", obj.assetId);

  const Ui::EditorTheme &theme = Ui::GetEditorTheme();
  Ui::EditorTreeItemSpec spec;
  spec.id    = "##obj_tree";
  spec.label = nodeLabel.c_str();
  spec.kind  = Ui::EditorTreeItemKind::Node;
  spec.treeFlags = hasChildren
      ? (ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
         ImGuiTreeNodeFlags_FramePadding)
      : (ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
         ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding);
  spec.selected        = isPrimary && IsSelected(idx);
  spec.normalTextColor = &theme.palette.text;

  const auto res = Ui::DrawEditorTreeItem(theme, spec);

  // After DrawEditorTreeItem (no suffixIcon), the last ImGui item is the tree
  // node — drag/drop and context menu correctly target it.
  if (isPrimary && ImGui::IsItemClicked())
    HandleTreeNodeClickSelection(idx);
  if (isPrimary)
    HandleTreeNodeDragDrop(idx, doc, obj);

  const float eyeIconWidth = ImGui::CalcTextSize(ICON_FA_EYE).x;
  const float eyeX = ImGui::GetWindowContentRegionMax().x - eyeIconWidth - 16.0f;
  ImGui::SameLine(eyeX);
  ImGui::TextDisabled("%s", ICON_FA_EYE);

  if (res.open && hasChildren) {
    for (int childIdx : children[static_cast<size_t>(idx)])
      DrawTreeNode(childIdx, doc, isPrimary, shownObjectCount, children);
    ImGui::TreePop();
  }

  ImGui::PopID();
}

/** @copydoc EditorLayer::DrawObjectsTreeSearchMode */
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
  }
}

/** @copydoc EditorLayer::DrawObjectsTreeRootDropZone */
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

/** @copydoc EditorLayer::DrawObjectsTreeRuntimeEntities */
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

/** @copydoc EditorLayer::DrawObjectsTree */
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
