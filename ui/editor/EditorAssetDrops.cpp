/**
 * @file EditorAssetDrops.cpp
 * @brief OS file-drop integration for texture albedo assignment and mesh-source import.
 *
 * @ref EditorLayer::OnPathsDropped queues UTF-8 paths and cursor coordinates;
 * @ref EditorLayer::ProcessPendingPathDrops drains them after the frame boundary.
 * Texture drops hit the selected-asset albedo zone or the import-asset modal when open.
 * Mesh-source drops (.obj, .fbx) auto-commit to the document if the import modal is closed,
 * or route to the import modal when it is open.
 */
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#include <string>

#include "ui/editor/AssetIdentity.h"
#include "core/Logger.h"
#include "ui/editor/EditorAssetImport.h"
#include "ui/editor/components/EditorImportAssetModal.h"

namespace Horo::Editor {
    /** @copydoc EditorLayer::OnPathsDropped */
    void EditorLayer::OnPathsDropped(int pathCount, const char **utf8Paths,
                                     float dropX, float dropY) {
        if (!utf8Paths || pathCount <= 0)
            return;
        m_pendingPathDropPaths.clear();
        m_pendingPathDropPaths.reserve(static_cast<size_t>(pathCount));
        for (int i = 0; i < pathCount; ++i) {
            if (utf8Paths[i])
                m_pendingPathDropPaths.emplace_back(utf8Paths[i]);
        }
        if (m_pendingPathDropPaths.empty())
            return;
        m_pendingPathDropX = dropX;
        m_pendingPathDropY = dropY;
        m_hasPendingPathDrop = true;
    }

    /** @copydoc EditorLayer::TryApplySelectedAssetAlbedoDrop */
    bool EditorLayer::TryApplySelectedAssetAlbedoDrop(const std::string &path) {
        if (m_selectedAssetId.empty())
            return false;
        auto it = m_document.assets.find(m_selectedAssetId);
        if (it == m_document.assets.end())
            return false;

        if (std::string err; !m_assetImportService.ImportTextureForAsset(
            path, m_selectedAssetId, &it->second, &err)) {
            if (!err.empty())
                LogWarn("Texture drop: {}", err);
            return false;
        }

        m_document.dirty = true;
        m_uiWidgets.OnClipboardAction("Albedo texture set", 2.0f);
        return true;
    }

    /** @copydoc EditorLayer::ProcessPendingTextureDrops */
    void EditorLayer::ProcessPendingTextureDrops() {
        constexpr float kTextureDropHitSlopPx = 6.0f;
        const float px = m_pendingPathDropX;
        const float py = m_pendingPathDropY;

        // First check if modal is open and accepts the drop
        if (m_importAssetModal.IsOpen()) {
            for (const std::string &path: m_pendingPathDropPaths) {
                if ((IsTextureFilePath(path) || IsObjFilePath(path) || IsFbxFilePath(path)) &&
                    m_importAssetModal.HandleFileDrop(px, py, path)) {
                    m_pendingPathDropPaths.clear();
                    return;
                }
            }
        }

        auto it = std::ranges::find_if(m_pendingPathDropPaths, [](const std::string& p) {
            return IsTextureFilePath(p);
        });
        if (it != m_pendingPathDropPaths.end()) {
            if (m_albedoSelDrop.Contains(px, py, kTextureDropHitSlopPx) &&
                TryApplySelectedAssetAlbedoDrop(*it)) {
                m_pendingPathDropPaths.clear();
                return;
            }
        }
    }

    /** @copydoc EditorLayer::ProcessPendingMeshDrops */
    void EditorLayer::ProcessPendingMeshDrops() {
        // Modal takes priority for mesh drops
        if (m_importAssetModal.IsOpen()) {
            for (const std::string &path: m_pendingPathDropPaths) {
                if ((IsObjFilePath(path) || IsFbxFilePath(path)) &&
                    m_importAssetModal.HandleFileDrop(m_pendingPathDropX, m_pendingPathDropY, path)) {
                    m_pendingPathDropPaths.clear();
                    return;
                }
            }
        }

        auto it = std::ranges::find_if(m_pendingPathDropPaths, [](const std::string& path) {
            return IsObjFilePath(path) || IsFbxFilePath(path);
        });
        if (it == m_pendingPathDropPaths.end())
            return;

        const std::string &path = *it;
        const bool isFbx = IsFbxFilePath(path);
        const std::string assetGuid = GenerateAssetGuid();
        const std::string assetId = AssetIdFromImportedPath(path);

        AssetImportResult importResult = m_assetImportService.ImportAssetFromSource(
            path, assetId, assetGuid, assetId);
        if (!importResult.ok) {
            const std::string &err = importResult.error;
            if (!err.empty())
                LogWarn("Drop import: {}", err);
            m_uiWidgets.OnClipboardAction(
                err.empty() ? "Drop import failed." : "Drop import failed — see log", 4.0f);
            m_pendingPathDropPaths.clear();
            return;
        }

        // Auto-commit on successful drop import
        m_document.assets[assetId] = importResult.asset;
        m_document.dirty = true;
        m_selectedAssetId = assetId;
        if (std::string metadataError;
            !m_assetImportService.SaveMetadataForAsset(
                assetId, importResult.asset, &metadataError) &&
            !metadataError.empty()) {
            LogWarn("Drop metadata sync: {}", metadataError);
        }
        m_uiWidgets.OnClipboardAction(
            isFbx ? "FBX imported" : "OBJ imported", 2.0f);
        m_pendingPathDropPaths.clear();
    }

    /** @copydoc EditorLayer::ProcessPendingPathDrops */
    void EditorLayer::ProcessPendingPathDrops() {
        if (!m_hasPendingPathDrop)
            return;
        m_hasPendingPathDrop = false;
        ProcessPendingTextureDrops();
        ProcessPendingMeshDrops();
        m_pendingPathDropPaths.clear();
    }

    /** @copydoc EditorLayer::ProcessImportAssetModalRequest */
    void EditorLayer::ProcessImportAssetModalRequest() {
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
                                                        ? req.assetId
                                                        : req.displayName, {}, req.textureOverrides);

        outcome.ok = result.ok;
        outcome.error = result.error;
        outcome.diagnostics = result.diagnostics;
        if (result.ok) {
            outcome.assetMesh = result.asset.mesh;
            outcome.assetAlbedoMap = result.asset.albedoMap;
            m_document.assets[req.assetId] = result.asset;
            m_document.dirty = true;
            SaveDocument(nullptr);
        }
        m_importAssetModal.SetLastResult(outcome);
    }
} // namespace Horo::Editor
