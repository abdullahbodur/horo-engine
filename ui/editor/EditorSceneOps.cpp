/**
 * @file EditorSceneOps.cpp
 * @brief Scene/document mutations on @ref EditorLayer: save, prefabs, deletes, duplication, and viewport drops.
 */
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "math/MathUtils.h"
#include "scene/Registry.h"
#include "ui/editor/AssetIdentity.h"
#include "ui/editor/AssetMetadata.h"
#include "ui/editor/EditorImportedAssetPathUtils.h"
#include "ui/editor/EditorPropertyRules.h"
#include "ui/editor/EditorSceneGraph.h"
#include "ui/editor/Raycaster.h"
#include "ui/editor/SceneSerializer.h"

namespace Horo::Editor {
/**
 * @brief Resolves conservative world-space bounds for ray-vs-surface placement (panels/props).
 *
 * Uses object scale as fallback half-extents and optionally expands via TryPropWorldAabb when a live registry exists.
 */
bool TryGetPlacementSurfaceBounds(Registry *liveRegistry,
                                  const SceneObject &obj, Vec3 *outCenter,
                                  Vec3 *outHalf) {
  using enum SceneObjectType;
  if (!outCenter || !outHalf)
    return false;
  if (obj.type != Panel && obj.type != Prop)
    return false;

  Vec3 center = obj.position;
  Vec3 half = {std::max(std::abs(obj.scale.x), 0.25f),
               std::max(std::abs(obj.scale.y), 0.25f),
               std::max(std::abs(obj.scale.z), 0.25f)};
  if (liveRegistry && TryPropWorldAabb(*liveRegistry, obj, center, half)) {
    half.x = std::max(std::abs(half.x), 0.25f);
    half.y = std::max(std::abs(half.y), 0.25f);
    half.z = std::max(std::abs(half.z), 0.25f);
  }

  *outCenter = center;
  *outHalf = half;
  return true;
}

/** @copydoc EditorLayer::ReloadDocumentFromDisk */
bool EditorLayer::ReloadDocumentFromDisk(
    std::string *outError,
    const std::vector<std::string> *preferredSelectionIds,
    const std::string *preferredAssetId) {
  if (outError)
    outError->clear();

  const std::string path = m_document.filePath.empty()
                               ? "assets/scenes/scene.json"
                               : m_document.filePath;
  try {
    SceneDocument reloaded = SceneSerializer::LoadFromFile(path);
    reloaded.dirty = false;
    ApplyLoadedDocument(std::move(reloaded), false);
    m_document.filePath = path;
    m_lastSavedDocument = m_document;
    if (preferredSelectionIds)
      SetSelectedObjectIds(*preferredSelectionIds);
    if (preferredAssetId && m_document.assets.contains(*preferredAssetId))
      m_selectedAssetId = *preferredAssetId;
    TriggerReload();
    return true;
  } catch (const SceneSerializerException &e) {
    if (outError)
      *outError = e.what();
    return false;
  }
}

/** @copydoc EditorLayer::ApplyGizmoDeltaToSelection */
void EditorLayer::ApplyGizmoDeltaToSelection(const Vec3 &dPos,
                                             const Vec3 &dScale,
                                             const Quaternion &dRot,
                                             float dRotXYZSq) {
  if (!m_gizmoHistoryPending) {
    BeginHistoryTransaction(CaptureHistorySnapshot());
    m_gizmoHistoryPending = true;
  }

  for (int si : m_selectedIndices) {
    if (si < 0 || si >= static_cast<int>(m_document.objects.size()))
      continue;

    auto &applyObj = m_document.objects[si];

    const Vec3 oldObjPos = applyObj.position;
    const Quaternion oldObjRot = Quaternion::FromEuler(
        ToRadians(applyObj.pitch), ToRadians(applyObj.yaw),
        ToRadians(applyObj.roll));

    applyObj.position = applyObj.position + dPos;
    applyObj.scale.x *= dScale.x;
    applyObj.scale.y *= dScale.y;
    applyObj.scale.z *= dScale.z;

    Quaternion nextRot = oldObjRot;
    if (dRotXYZSq > 1e-8f) {
      nextRot = (dRot * oldObjRot).Normalized();
      const Vec3 euler = nextRot.ToEuler();
      applyObj.pitch = ToDegrees(euler.x);
      applyObj.yaw = ToDegrees(euler.y);
      applyObj.roll = ToDegrees(euler.z);
    }

    m_document.dirty = true;
    if (m_transformCb)
      m_transformCb(applyObj);

    PropagateHierarchyTransformDelta(
        m_document, si, ParentTransformState{oldObjPos, oldObjRot},
        ParentTransformState{applyObj.position, nextRot}, m_transformCb,
        m_selectedIndices);
  }
}

/** @copydoc EditorLayer::SaveDocument */
bool EditorLayer::SaveDocument(std::string *outError) {
  if (outError)
    outError->clear();

  FinalizeHistoryTransaction();

  std::string path = m_document.filePath.empty() ? "assets/scenes/scene.json"
                                                 : m_document.filePath;
  m_document.filePath = path;
  EnsureAssetIdentity(&m_document);

  LogInfo("[Editor] Saving scene to: {}", path);

  try {
    if (!EnsureAssetMetadataForDocument(&m_document, outError)) {
      LogError("[Editor] Asset metadata save failed: {}",
               outError ? *outError : std::string{});
      return false;
    }
    SceneSerializer::SaveToFile(m_document, path);
    m_document.dirty = false;
    m_lastSavedDocument = m_document;
    RefreshHistorySavedBaseline();
    LogInfo("[Editor] Scene saved OK");
    TriggerReload();
    return true;
  } catch (const SceneSerializerException &e) {
    LogError("[Editor] Save failed: {}", e.what());
    if (outError)
      *outError = e.what();
    return false;
  }
}

/** @copydoc EditorLayer::DeleteAssetDefinition */
EditorLayer::AssetDeleteResult
EditorLayer::DeleteAssetDefinition(const std::string &assetId) {
  AssetDeleteResult result;
  auto it = m_document.assets.find(assetId);
  if (it == m_document.assets.end()) {
    result.error = "Asset not found.";
    return result;
  }

  const EditorHistorySnapshot before = CaptureHistorySnapshot();

  if (const std::filesystem::path managedDirectory =
          GetManagedImportedAssetDirectory(it->second);
      !managedDirectory.empty()) {
    std::error_code ec;
    const bool existedBeforeDelete =
        std::filesystem::exists(managedDirectory, ec) && !ec;
    ec.clear();
    std::filesystem::remove_all(managedDirectory, ec);
    if (ec) {
      result.error = "Failed to delete imported asset files: " + ec.message();
      return result;
    }
    result.deletedManagedFiles = existedBeforeDelete;
    result.deletedAssetDirectory = managedDirectory.generic_string();
  }

  for (SceneObject &object : m_document.objects) {
    if (object.assetId == assetId) {
      object.assetId.clear();
      ++result.clearedReferences;
    }
  }
  if (m_selectedAssetId == assetId)
    m_selectedAssetId.clear();

  m_document.assets.erase(it);
  MarkDirtyAndReload();
  CommitHistoryChange(before);

  result.ok = true;
  return result;
}

/** @copydoc EditorLayer::DiscardUnsavedChanges */
void EditorLayer::DiscardUnsavedChanges() {
  if (!m_document.dirty)
    return;

  m_document = m_lastSavedDocument;
  m_selectedIndices.clear();
  m_selectedAssetId.clear();
  TriggerReload();
}

/** @copydoc EditorLayer::RequestSceneAction */
void EditorLayer::RequestSceneAction(PendingSceneAction action) {
  if (action == PendingSceneAction::None)
    return;

  m_pendingSceneAction = action;
  m_exitConfirmError.clear();
  if (m_document.dirty) {
    m_uiWidgets.OpenConfirmExit();
    return;
  }

  std::string actionError;
  if (!ExecutePendingSceneAction(&actionError))
    LogError("[Editor] Scene action failed: {}", actionError);
}

/** @copydoc EditorLayer::ExecutePendingSceneAction */
bool EditorLayer::ExecutePendingSceneAction(std::string *outError) {
  using enum PendingSceneAction;
  if (outError)
    outError->clear();

  const PendingSceneAction action = m_pendingSceneAction;
  m_pendingSceneAction = None;

  switch (action) {
  case None:
    return true;
  case NewScene: {
    const EditorHistorySnapshot before = CaptureHistorySnapshot();
    AddNewScene();
    m_lastSavedDocument = m_document;
    CommitHistoryChange(before);
    return true;
  }
  case OpenSceneFile: {
    const EditorHistorySnapshot before = CaptureHistorySnapshot();
    OpenAdditionalSceneFile();
    CommitHistoryChange(before);
    return true;
  }
  case LoadSceneFromDisk: {
    const EditorHistorySnapshot before = CaptureHistorySnapshot();
    const std::vector<std::string> selectionIds = GetSelectedObjectIds();
    const std::string selectedAssetId = m_selectedAssetId;
    const bool ok =
        ReloadDocumentFromDisk(outError, &selectionIds, &selectedAssetId);
    if (ok)
      CommitHistoryChange(before);
    return ok;
  }
  case ReloadSceneFromDisk: {
    const EditorHistorySnapshot before = CaptureHistorySnapshot();
    const std::vector<std::string> selectionIds = GetSelectedObjectIds();
    const std::string selectedAssetId = m_selectedAssetId;
    const bool ok =
        ReloadDocumentFromDisk(outError, &selectionIds, &selectedAssetId);
    if (ok)
      CommitHistoryChange(before);
    return ok;
  }
  case CloseEditor:
    m_closeRequested = true;
    return true;
  }

  return true;
}

/** @copydoc EditorLayer::ExecuteCommandPaletteAction */
void EditorLayer::ExecuteCommandPaletteAction(std::string_view commandId) {
  using enum PendingSceneAction;
  if (commandId == "undo")
    UndoHistory();
  else if (commandId == "redo")
    RedoHistory();
  else if (commandId == "new_scene")
    RequestSceneAction(NewScene);
  else if (commandId == "open_scene")
    RequestSceneAction(OpenSceneFile);
  else if (commandId == "load_scene")
    RequestSceneAction(LoadSceneFromDisk);
  else if (commandId == "reload_scene")
    RequestSceneAction(ReloadSceneFromDisk);
  else if (commandId == "save_scene") {
    std::string saveError;
    if (!SaveDocument(&saveError))
      LogError("[Editor] Save failed: {}", saveError);
  } else if (commandId == "reset_layout") {
    m_resetDockLayoutRequested = true;
  } else if (commandId == "quick_open") {
    m_quickOpenOpen = true;
    m_quickOpenQuery.clear();
  } else if (commandId == "close_editor") {
    RequestSceneAction(CloseEditor);
  }
}

/** @copydoc EditorLayer::CreatePrefabFromSelection */
bool EditorLayer::CreatePrefabFromSelection(std::string *outError,
                                            std::string *outPrefabPath) {
  if (outError)
    outError->clear();
  if (outPrefabPath)
    outPrefabPath->clear();

  const int primaryIdx = PrimaryIdx();
  if (m_selectedIndices.size() != 1 || primaryIdx < 0 ||
      primaryIdx >= static_cast<int>(m_document.objects.size())) {
    if (outError)
      *outError = "Select exactly one object to create a prefab.";
    return false;
  }

  const EditorHistorySnapshot before = CaptureHistorySnapshot();
  const SceneObject &sourceObject =
      m_document.objects[static_cast<size_t>(primaryIdx)];
  const std::filesystem::path prefabAbsPath =
      BuildUniquePrefabPath(m_document, sourceObject);

  std::error_code ec;
  std::filesystem::create_directories(prefabAbsPath.parent_path(), ec);
  if (ec) {
    if (outError)
      *outError = "Failed to create prefab directory: " + ec.message();
    return false;
  }

  SceneDocument prefabDoc;
  prefabDoc.version = m_document.version;
  prefabDoc.sceneId = prefabAbsPath.stem().string();
  prefabDoc.sceneName =
      sourceObject.id.empty() ? prefabDoc.sceneId : sourceObject.id;
  prefabDoc.filePath = prefabAbsPath.generic_string();

  SceneObject prefabObject = sourceObject;
  prefabObject.prefabInstance.reset();
  prefabObject.props.erase("parentId");
  prefabDoc.objects.push_back(std::move(prefabObject));
  if (!sourceObject.assetId.empty()) {
    const auto assetIt = m_document.assets.find(sourceObject.assetId);
    if (assetIt != m_document.assets.end())
      prefabDoc.assets[sourceObject.assetId] = assetIt->second;
  }

  try {
    SceneSerializer::SaveToFile(prefabDoc, prefabAbsPath.generic_string());
  } catch (const SceneSerializerException &e) {
    if (outError)
      *outError = e.what();
    return false;
  }

  const std::filesystem::path relativePath =
      prefabAbsPath.lexically_relative(ProjectPath::Root()).lexically_normal();
  SceneObject &object = m_document.objects[static_cast<size_t>(primaryIdx)];
  object.prefabInstance =
      ScenePrefabInstance{prefabDoc.sceneId, relativePath.generic_string()};
  MarkDirtyAndReload();
  CommitHistoryChange(before);

  if (outPrefabPath)
    *outPrefabPath = relativePath.generic_string();
  return true;
}

/** @copydoc EditorLayer::RequestDeleteSelectedObjects */
void EditorLayer::RequestDeleteSelectedObjects() {
  if (m_selectedIndices.empty())
    return;

  m_uiWidgets.OpenConfirmDeleteObjects(m_selectedIndices);
}

/** @copydoc EditorLayer::RequestDeleteAsset */
void EditorLayer::RequestDeleteAsset(std::string_view assetId) {
  if (assetId.empty())
    return;
  if (!m_document.assets.contains(assetId))
    return;

  m_uiWidgets.OpenConfirmDeleteAsset(assetId);
}

/** @copydoc EditorLayer::OpenRenameObjectModal */
void EditorLayer::OpenRenameObjectModal(int index) {
  if (index < 0 || index >= static_cast<int>(m_document.objects.size()))
    return;
  m_uiWidgets.OpenRenameObject(index);
}

/** @copydoc EditorLayer::AddObject */
void EditorLayer::AddObject(SceneObjectType type, std::string_view parentId) {
  using enum SceneObjectType;
  const EditorHistorySnapshot before = CaptureHistorySnapshot();
  SceneObject obj;
  obj.id =
      (type == Camera) ? GenerateCameraId(m_document) : GenerateId(m_document);
  obj.type = type;
  ApplySchemaDefaults(obj);
  if (type == Camera)
    ApplyCameraBuiltinDefaults(obj);
  if (!parentId.empty())
    obj.props["parentId"] = parentId;

  m_document.objects.push_back(std::move(obj));
  m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
  MarkDirtyAndReload();
  CommitHistoryChange(before);
}

/** @copydoc EditorLayer::AddObjectFromSelectedAsset */
void EditorLayer::AddObjectFromSelectedAsset(std::string_view parentId) {
  CreateObjectFromAsset(m_selectedAssetId, parentId);
}

/** @copydoc EditorLayer::CreateObjectFromAsset */
bool EditorLayer::CreateObjectFromAsset(std::string_view assetId,
                                        std::string_view parentId,
                                        const Vec3 *worldPosition,
                                        const std::string *preferredId,
                                        SceneObject *outCreated,
                                        std::string *outError) {
  if (outError)
    outError->clear();

  if (assetId.empty() || !m_document.assets.contains(assetId)) {
    if (outError)
      *outError = "Asset not found.";
    return false;
  }
  if (!parentId.empty() && FindObjectIndexById(m_document, parentId) < 0) {
    if (outError)
      *outError = "Parent object not found.";
    return false;
  }

  const EditorHistorySnapshot before = CaptureHistorySnapshot();

  SceneObject object =
      MakeObjectFromAsset(m_document, assetId, m_schema);
  if (preferredId && !preferredId->empty()) {
    if (IsReservedObjectId(m_document, *preferredId)) {
      if (outError)
        *outError = "Object id already exists.";
      return false;
    }
    object.id = *preferredId;
  }
  if (!parentId.empty())
    object.props["parentId"] = parentId;
  if (worldPosition)
    object.position = *worldPosition;

  m_document.objects.push_back(object);
  m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
  m_selectedAssetId = std::string(assetId);
  MarkDirtyAndReload();
  CommitHistoryChange(before);

  if (outCreated)
    *outCreated = m_document.objects.back();
  return true;
}

/** @copydoc EditorLayer::TryBuildViewportDropPosition */
bool EditorLayer::TryBuildViewportDropPosition(const Camera &cam, int screenW,
                                               int screenH,
                                               std::string_view assetId,
                                               Vec3 *outPosition) const {
  if (!outPosition)
    return false;

  double mx = 0.0;
  double my = 0.0;
  glfwGetCursorPos(m_window, &mx, &my);
  int windowW = 0;
  int windowH = 0;
  glfwGetWindowSize(m_window, &windowW, &windowH);
  const Vec2 mouse = ScaleScreenPointToRenderTarget(
      static_cast<float>(mx), static_cast<float>(my), windowW, windowH,
      screenW, screenH);
  const Ray ray = ScreenToRay(mouse.x, mouse.y, screenW, screenH, cam);
  LogInfo("[Editor] Drop ray: origin=({}, {}, {}), dir=({}, {}, {})",
          ray.origin.x, ray.origin.y, ray.origin.z,
          ray.direction.x, ray.direction.y, ray.direction.z);

  const SceneObject droppedObject =
      MakeObjectFromAsset(m_document, assetId, m_schema);
  const Vec3 droppedHalf = ResolveObjectPlacementHalfExtents(droppedObject);
  LogInfo("[Editor] Dropped object half-extents: ({}, {}, {})",
          droppedHalf.x, droppedHalf.y, droppedHalf.z);

  RayAabbHit bestHit;
  bool hasSurfaceHit = false;
  int surfaceCandidates = 0;
  for (const SceneObject &object : m_document.objects) {
    Vec3 center = Vec3::Zero();
    Vec3 half = Vec3::Zero();
    if (!TryGetPlacementSurfaceBounds(m_liveRegistry, object, &center, &half))
      continue;
    ++surfaceCandidates;
    
    RayAabbHit hit;
    if (!RayVsAABBHit(ray, center, half, &hit))
      continue;
    if (!hasSurfaceHit || hit.distance < bestHit.distance) {
      bestHit = hit;
      hasSurfaceHit = true;
    }
  }
  LogInfo("[Editor] Checked {} surface candidates, hasSurfaceHit={}",
          surfaceCandidates, hasSurfaceHit);

  if (hasSurfaceHit) {
    *outPosition = bestHit.point +
                   bestHit.normal *
                       ProjectHalfExtentOntoNormal(droppedHalf, bestHit.normal);
    LogInfo("[Editor] Surface hit at distance {}, point=({}, {}, {})",
            bestHit.distance, bestHit.point.x, bestHit.point.y, bestHit.point.z);
    return true;
  }

  Vec3 groundHit = Vec3::Zero();
  if (!TryIntersectGroundPlane(ray, &groundHit)) {
    LogInfo("[Editor] Ground plane intersection failed: ray.direction.y={}",
            ray.direction.y);
    return false;
  }

  *outPosition = groundHit + Vec3::Up() * ProjectHalfExtentOntoNormal(
                                              droppedHalf, Vec3::Up());
  LogInfo("[Editor] Ground plane hit at ({}, {}, {})",
          groundHit.x, groundHit.y, groundHit.z);
  return true;
}

/** @copydoc EditorLayer::DuplicatePrimarySelection */
void EditorLayer::DuplicatePrimarySelection() {
  const int primaryIdx = PrimaryIdx();
  if (primaryIdx < 0 ||
      primaryIdx >= static_cast<int>(m_document.objects.size()))
    return;
  const EditorHistorySnapshot before = CaptureHistorySnapshot();
  SceneObject clone = DuplicateObject(
      m_document, m_document.objects[static_cast<size_t>(primaryIdx)]);
  clone.position.x += 1.0f;
  clone.position.z += 1.0f;
  m_document.objects.push_back(std::move(clone));
  m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
  MarkDirtyAndReload();
  CommitHistoryChange(before);
}

/** @copydoc EditorLayer::DuplicateSelectedObjects */
void EditorLayer::DuplicateSelectedObjects() {
  if (m_selectedIndices.empty())
    return;
  if (m_selectedIndices.size() == 1) {
    DuplicatePrimarySelection();
    return;
  }

  const EditorHistorySnapshot before = CaptureHistorySnapshot();

  std::vector<int> sourceIndices = m_selectedIndices;
  std::ranges::sort(sourceIndices);
  const auto sourceIndicesUniqueTail = std::ranges::unique(sourceIndices);
  sourceIndices.erase(sourceIndicesUniqueTail.begin(),
                      sourceIndicesUniqueTail.end());

  std::vector<int> duplicatedIndices;
  duplicatedIndices.reserve(sourceIndices.size());
  for (int idx : sourceIndices) {
    if (idx < 0 || idx >= static_cast<int>(m_document.objects.size()))
      continue;
    SceneObject clone = DuplicateObject(m_document, ObjectAt(m_document, idx));
    clone.position.x += 1.0f;
    clone.position.z += 1.0f;
    m_document.objects.push_back(std::move(clone));
    duplicatedIndices.push_back(static_cast<int>(m_document.objects.size()) - 1);
  }

  if (!duplicatedIndices.empty())
    m_selectedIndices = std::move(duplicatedIndices);
  MarkDirtyAndReload();
  CommitHistoryChange(before);
}
} // namespace Horo::Editor
