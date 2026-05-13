/**
 * @file EditorAssetDrops.cpp
 * @brief OS file-drop integration for texture albedo assignment and OBJ draft import.
 *
 * @ref EditorLayer::OnPathsDropped queues UTF-8 paths and cursor coordinates;
 * @ref EditorLayer::ProcessPendingPathDrops drains them after the frame boundary.
 * Texture paths hit-test draft vs. selected-asset albedo drop zones; OBJ paths drive
 * @ref AssetImportService::ImportAssetFromSource into the new-asset draft state.
 */
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#include <string>

#include "ui/editor/AssetIdentity.h"
#include "core/Logger.h"
#include "ui/editor/EditorAssetImport.h"

namespace Horo::Editor
{
  /** @copydoc EditorLayer::OnPathsDropped */
  void EditorLayer::OnPathsDropped(int pathCount, const char** utf8Paths,
                                   float dropX, float dropY)
  {
    if (!utf8Paths || pathCount <= 0)
      return;
    m_pendingPathDropPaths.clear();
    m_pendingPathDropPaths.reserve(static_cast<size_t>(pathCount));
    for (int i = 0; i < pathCount; ++i)
    {
      if (utf8Paths[i])
        m_pendingPathDropPaths.emplace_back(utf8Paths[i]);
    }
    if (m_pendingPathDropPaths.empty())
      return;
    m_pendingPathDropX = dropX;
    m_pendingPathDropY = dropY;
    m_hasPendingPathDrop = true;
  }

  /** @copydoc EditorLayer::TryApplyDraftAlbedoDrop */
  bool EditorLayer::TryApplyDraftAlbedoDrop(const std::string& path)
  {
    if (m_assetDraftGuid.empty())
      m_assetDraftGuid = GenerateAssetGuid();
    if (m_assetDraftId.empty())
      m_assetDraftId = AssetIdFromImportedPath(path);
    if (m_assetDraftDisplayName.empty())
      m_assetDraftDisplayName = m_assetDraftId;

    AssetDef draftAsset;
    draftAsset.guid = m_assetDraftGuid;
    draftAsset.displayName = m_assetDraftDisplayName;
    draftAsset.mesh = m_assetDraftMesh;
    draftAsset.renderScale = m_assetDraftRenderScale.empty()
                               ? "1.0000,1.0000,1.0000"
                               : m_assetDraftRenderScale;
    draftAsset.albedoMap = m_assetDraftAlbedoMap;

    if (std::string err; !m_assetImportService.ImportTextureForAsset(
      path, m_assetDraftId, &draftAsset, &err))
    {
      if (!err.empty())
        LogWarn("Texture drop: {}", err);
      return false;
    }

    m_assetDraftAlbedoMap = draftAsset.albedoMap;
    m_uiWidgets.OnClipboardAction("Albedo texture set", 2.0f);
    return true;
  }

  /** @copydoc EditorLayer::TryApplySelectedAssetAlbedoDrop */
  bool EditorLayer::TryApplySelectedAssetAlbedoDrop(const std::string& path)
  {
    if (m_selectedAssetId.empty())
      return false;
    auto it = m_document.assets.find(m_selectedAssetId);
    if (it == m_document.assets.end())
      return false;

    if (std::string err; !m_assetImportService.ImportTextureForAsset(
      path, m_selectedAssetId, &it->second, &err))
    {
      if (!err.empty())
        LogWarn("Texture drop: {}", err);
      return false;
    }

    m_document.dirty = true;
    m_uiWidgets.OnClipboardAction("Albedo texture set", 2.0f);
    return true;
  }

  /** @copydoc EditorLayer::ProcessPendingTextureDrops */
  void EditorLayer::ProcessPendingTextureDrops()
  {
    constexpr float kTextureDropHitSlopPx = 6.0f;
    const float px = m_pendingPathDropX;
    const float py = m_pendingPathDropY;
    for (const std::string& path : m_pendingPathDropPaths)
    {
      if (!IsTextureFilePath(path))
        continue;
      if (m_albedoDraftDrop.Contains(px, py, kTextureDropHitSlopPx) &&
        TryApplyDraftAlbedoDrop(path))
      {
        m_pendingPathDropPaths.clear();
        return;
      }
      if (m_albedoSelDrop.Contains(px, py, kTextureDropHitSlopPx) &&
        TryApplySelectedAssetAlbedoDrop(path))
      {
        m_pendingPathDropPaths.clear();
        return;
      }
    }
  }

  /** @copydoc EditorLayer::ProcessPendingObjDrops */
  void EditorLayer::ProcessPendingObjDrops()
  {
    for (const std::string& path : m_pendingPathDropPaths)
    {
      if (!IsObjFilePath(path))
        continue;
      if (m_assetDraftGuid.empty())
        m_assetDraftGuid = GenerateAssetGuid();
      if (m_assetDraftId.empty())
        m_assetDraftId = AssetIdFromImportedPath(path);
      if (m_assetDraftDisplayName.empty())
        m_assetDraftDisplayName = m_assetDraftId;
      AssetImportResult importResult = m_assetImportService.ImportAssetFromSource(
        path, m_assetDraftId, m_assetDraftGuid, m_assetDraftDisplayName);
      if (!importResult.ok)
      {
        const std::string& err = importResult.error;
        if (!err.empty())
          LogWarn("Drop import: {}", err);
        m_assetImportError = err.empty() ? "Drop import failed." : err;
        m_openNewAssetHeader = true;
        m_pendingPathDropPaths.clear();
        return;
      }
      m_assetDraftMesh = importResult.asset.mesh;
      m_assetDraftAlbedoMap = importResult.asset.albedoMap;
      m_assetDraftRenderScale = importResult.asset.renderScale;
      m_assetImportError.clear();
      m_openNewAssetHeader = true;
      m_uiWidgets.OnClipboardAction("OBJ dropped — draft ready", 2.2f);
      m_pendingPathDropPaths.clear();
      return;
    }
  }

  /** @copydoc EditorLayer::ProcessPendingPathDrops */
  void EditorLayer::ProcessPendingPathDrops()
  {
    if (!m_hasPendingPathDrop)
      return;
    m_hasPendingPathDrop = false;
    ProcessPendingTextureDrops();
    ProcessPendingObjDrops();
    m_pendingPathDropPaths.clear();
  }

  /** @copydoc EditorLayer::ProcessImportAssetModalRequest */
  void EditorLayer::ProcessImportAssetModalRequest()
  {
    if (!m_importAssetModal.HasPendingRequest())
      return;
    const ImportAssetRequest req = m_importAssetModal.ConsumePendingRequest();
    ImportAssetOutcome outcome;
    if (req.sourcePath.empty() || req.assetId.empty()) {
      outcome.error = "Source path and asset id are required.";
      m_importAssetModal.SetLastResult(outcome);
      return;
    }

    const std::string assetGuid = GenerateAssetGuid();
    const AssetImportResult result = m_assetImportService.ImportAssetFromSource(
      req.sourcePath, req.assetId, assetGuid, req.displayName.empty()
        ? req.assetId : req.displayName);

    outcome.ok = result.ok;
    outcome.error = result.error;
    outcome.diagnostics = result.diagnostics;
    if (result.ok) {
      outcome.assetMesh = result.asset.mesh;
      outcome.assetAlbedoMap = result.asset.albedoMap;
      m_document.assets[req.assetId] = result.asset;
      m_document.dirty = true;
    }
    m_importAssetModal.SetLastResult(outcome);
  }
} // namespace Horo::Editor
