/**
 * @file EditorSelection.cpp
 * @brief Implementation for EditorSelection editor functionality.
 */
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#include <ranges>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "ui/editor/EditorPropertyRules.h"
#include "ui/editor/EditorSelectionRules.h"

namespace Horo::Editor {

bool EditorLayer::IsSelected(int i) const {
  return std::ranges::find(m_selectedIndices, i) != m_selectedIndices.end();
}

int EditorLayer::PrimaryIdx() const {
  return m_selectedIndices.empty() ? -1 : m_selectedIndices.back();
}

void EditorLayer::ToggleSelect(int i) {
  if (auto it = std::ranges::find(m_selectedIndices, i);
      it != m_selectedIndices.end())
    m_selectedIndices.erase(it);
  else
    m_selectedIndices.push_back(i);
}

void EditorLayer::TriggerReload() {
  m_pendingDoc = m_document;
  m_wantsReload = true;
}

void EditorLayer::MarkDirtyAndReload() {
  m_document.dirty = true;
  TriggerReload();
}

std::vector<std::string> EditorLayer::GetSelectedObjectIds() const {
  std::vector<std::string> ids;
  ids.reserve(m_selectedIndices.size());
  for (int idx : m_selectedIndices) {
    if (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
      ids.push_back(ObjectAt(m_document, idx).id);
  }
  return ids;
}

void EditorLayer::SetSelectedObjectIds(const std::vector<std::string> &ids) {
  m_selectedIndices.clear();
  std::unordered_set<std::string, StringHash, std::equal_to<>> seen;
  seen.reserve(ids.size());
  for (const std::string &id : ids) {
    if (id.empty() || !seen.insert(id).second)
      continue;
    for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
      if (m_document.objects[static_cast<size_t>(i)].id == id) {
        m_selectedIndices.push_back(i);
        break;
      }
    }
  }
}

SceneObject EditorLayer::MakeObjectFromAsset(const SceneDocument &doc,
                                             const std::string &assetId,
                                             const EditorSchema &schema) {
  return Horo::Editor::MakeObjectFromAsset(doc, assetId, schema);
}

SceneObject EditorLayer::DuplicateObject(const SceneDocument &doc,
                                         const SceneObject &src) {
  SceneObject clone = src;
  clone.id = GenerateId(doc);
  clone.props.erase("_eid");
  return clone;
}

std::string EditorLayer::BuildSelectionRefCode(const SceneObject &obj,
                                               int idx) const {
  auto getProp = [&](const char *key) {
    auto it = obj.props.find(key);
    return (it != obj.props.end()) ? it->second : "";
  };

  std::ostringstream ss;
  ss.setf(std::ios::fixed);
  ss.precision(4);
  const std::string scenePath = m_document.filePath.empty()
                                    ? "assets/scenes/scene.json"
                                    : m_document.filePath;
  ss << "EDITOR_REF"
     << " scene=\"" << scenePath << "\""
     << " id=" << obj.id << " idx=" << idx
     << " type=" << SceneObjectTypeToString(obj.type) << " pos=("
     << obj.position.x << "," << obj.position.y << "," << obj.position.z << ")"
     << " scale=(" << obj.scale.x << "," << obj.scale.y << "," << obj.scale.z
     << ")"
     << " yaw=" << obj.yaw;

  if (const std::string mesh = getProp("mesh"); !mesh.empty())
    ss << " mesh=\"" << mesh << "\"";

  if (const std::string eid = getProp("_eid"); !eid.empty())
    ss << " _eid=" << eid;

  return ss.str();
}

std::string EditorLayer::GenerateId(const SceneDocument &doc) {
  return GenerateUniqueId(doc, "obj");
}

std::string EditorLayer::GenerateCameraId(const SceneDocument &doc) {
  return GenerateUniqueId(doc, "cam");
}

} // namespace Horo::Editor
