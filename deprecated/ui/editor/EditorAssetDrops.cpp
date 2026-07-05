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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <string>
#include <utility>

#include <imgui.h>

#include "core/Logger.h"

#include "ui/editor/AssetIdentity.h"
#include "ui/editor/EditorAssetImport.h"
#include "ui/editor/components/EditorImportAssetModal.h"
#include "ui/IconsFontAwesome6.h"

namespace Horo::Editor {
    namespace {
        /** @brief Returns a compact display name for a path-like snackbar detail. */
        std::string SnackbarPathDetail(const std::string &path) {
            const std::filesystem::path fsPath(path);
            if (const std::string filename = fsPath.filename().generic_string();
                !filename.empty()) {
                return filename;
            }
            return path;
        }

        /** @brief Returns the accent colour for the snackbar level. */
        ImVec4 SnackbarAccentColor(const EditorLayer::SnackbarLevel level) {
            using enum EditorLayer::SnackbarLevel;
            switch (level) {
                case Success: return ImVec4(0.27f, 0.74f, 0.34f, 1.0f);
                case Warning: return ImVec4(1.0f, 0.78f, 0.28f, 1.0f);
                case Error:   return ImVec4(0.95f, 0.35f, 0.32f, 1.0f);
                case Info:    return ImVec4(0.23f, 0.54f, 0.93f, 1.0f);
            }
            return ImVec4(0.23f, 0.54f, 0.93f, 1.0f);
        }

        /** @brief Returns the soft icon tile fill for the snackbar level. */
        ImVec4 SnackbarIconFillColor(const EditorLayer::SnackbarLevel level) {
            ImVec4 color = SnackbarAccentColor(level);
            color.x *= 0.35f;
            color.y *= 0.35f;
            color.z *= 0.35f;
            color.w = 0.42f;
            return color;
        }

        /** @brief Draws the round status mark used by the snackbar icon tile. */
        void DrawSnackbarStatusGlyph(ImDrawList *dl, const ImVec2 center,
                                     const float radius,
                                     const EditorLayer::SnackbarLevel level,
                                     const float alpha) {
            ImVec4 accent = SnackbarAccentColor(level);
            accent.w *= alpha;
            const ImU32 accentU32 = ImGui::ColorConvertFloat4ToU32(accent);
            dl->AddCircleFilled(center, radius, accentU32, 24);

            const ImU32 markU32 = ImGui::ColorConvertFloat4ToU32(
                ImVec4(0.02f, 0.10f, 0.06f, alpha));
            using enum EditorLayer::SnackbarLevel;
            if (level == Success) {
                dl->AddLine(ImVec2(center.x - radius * 0.45f, center.y - radius * 0.02f),
                            ImVec2(center.x - radius * 0.14f, center.y + radius * 0.30f),
                            markU32, 2.2f);
                dl->AddLine(ImVec2(center.x - radius * 0.14f, center.y + radius * 0.30f),
                            ImVec2(center.x + radius * 0.48f, center.y - radius * 0.38f),
                            markU32, 2.2f);
                return;
            }

            if (level == Warning) {
                dl->AddLine(ImVec2(center.x, center.y - radius * 0.48f),
                            ImVec2(center.x, center.y + radius * 0.12f),
                            markU32, 2.2f);
                dl->AddCircleFilled(ImVec2(center.x, center.y + radius * 0.46f),
                                    1.8f, markU32, 10);
                return;
            }

            if (level == Error) {
                dl->AddLine(ImVec2(center.x - radius * 0.35f, center.y - radius * 0.35f),
                            ImVec2(center.x + radius * 0.35f, center.y + radius * 0.35f),
                            markU32, 2.2f);
                dl->AddLine(ImVec2(center.x + radius * 0.35f, center.y - radius * 0.35f),
                            ImVec2(center.x - radius * 0.35f, center.y + radius * 0.35f),
                            markU32, 2.2f);
                return;
            }

            dl->AddCircleFilled(center, 2.2f, markU32, 10);
        }
    } // namespace

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
            auto handledIt = std::ranges::find_if(m_pendingPathDropPaths, [&](const std::string &path) {
                return (IsTextureFilePath(path) || IsObjFilePath(path) || IsFbxFilePath(path)) &&
                       m_importAssetModal.HandleFileDrop(px, py, path);
            });
            if (handledIt != m_pendingPathDropPaths.end()) {
                m_pendingPathDropPaths.clear();
                return;
            }
        }

        auto it = std::ranges::find_if(m_pendingPathDropPaths, [](const std::string& p) {
            return IsTextureFilePath(p);
        });
        if (it != m_pendingPathDropPaths.end() && m_albedoSelDrop.Contains(px, py, kTextureDropHitSlopPx) &&
            TryApplySelectedAssetAlbedoDrop(*it)) {
            m_pendingPathDropPaths.clear();
        }
    }

    /** @copydoc EditorLayer::ProcessPendingMeshDrops */
    void EditorLayer::ProcessPendingMeshDrops() {
        // Modal takes priority for mesh drops
        if (m_importAssetModal.IsOpen()) {
            auto handledIt = std::ranges::find_if(m_pendingPathDropPaths, [&](const std::string &path) {
                return (IsObjFilePath(path) || IsFbxFilePath(path)) &&
                       m_importAssetModal.HandleFileDrop(m_pendingPathDropX, m_pendingPathDropY, path);
            });
            if (handledIt != m_pendingPathDropPaths.end()) {
                m_pendingPathDropPaths.clear();
                return;
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

        // Use async import — don't block the editor
        m_asyncImportRequest = ImportAssetRequest{
            assetId, assetGuid, assetId, path, {}};
        m_asyncImportAssetGuid = assetGuid;
        m_asyncImportFuture = m_assetImportService.ImportAssetFromSourceAsync(
            path, assetId, assetGuid, assetId);
        m_asyncImportActive = true;
        m_pendingPathDropPaths.clear();

        ShowEditorOperation(EditorOperation{.id = "asset_import",
                                            .label = "Import",
                                            .detail = SnackbarPathDetail(path),
                                            .progress = -1.0f});
        ShowSnackbar(SnackbarNotification{.level = SnackbarLevel::Info,
                                          .title = "Importing asset",
                                          .detail = SnackbarPathDetail(path),
                                          .dismissible = false,
                                          .pinWhileAsyncImport = true});
        LogInfo("[Drop] Async import started for '{}'", path);
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
        if (req.sourcePath.empty() || req.assetId.empty()) {
            ImportAssetOutcome outcome;
            outcome.error = "Source path and asset id are required.";
            m_importAssetModal.SetLastResult(outcome);
            return;
        }

        const std::string assetGuid = GenerateAssetGuid();

        // Kick off async import — runs on worker thread, UI stays responsive
        m_asyncImportRequest = req;
        m_asyncImportAssetGuid = assetGuid;
        m_asyncImportFuture = m_assetImportService.ImportAssetFromSourceAsync(
            req.sourcePath, req.assetId, assetGuid,
            req.displayName.empty() ? req.assetId : req.displayName,
            {}, req.textureOverrides);
        m_asyncImportActive = true;

        ShowEditorOperation(EditorOperation{.id = "asset_import",
                                            .label = "Import",
                                            .detail = req.displayName.empty()
                                                          ? req.assetId
                                                          : req.displayName,
                                            .progress = -1.0f});
        ShowSnackbar(SnackbarNotification{.level = SnackbarLevel::Info,
                                          .title = "Importing asset",
                                          .detail = req.displayName.empty()
                                                        ? req.assetId
                                                        : req.displayName,
                                          .dismissible = false,
                                          .pinWhileAsyncImport = true});
        LogInfo("[Import] Async import started for '{}'", req.sourcePath);
    }

    void EditorLayer::PollAsyncImport() {
        if (!m_asyncImportActive)
            return;
        if (!m_asyncImportFuture.valid())
            return;

        // Non-blocking poll — check if worker thread finished
        if (m_asyncImportFuture.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready)
            return;

        m_asyncImportActive = false;
        const AssetImportResult result = m_asyncImportFuture.get();
        ClearEditorOperation("asset_import");

        ImportAssetOutcome outcome;
        outcome.ok = result.ok;
        outcome.error = result.error;
        outcome.diagnostics = result.diagnostics;
        if (result.ok) {
            outcome.assetMesh = result.asset.mesh;
            outcome.assetAlbedoMap = result.asset.albedoMap;
            m_document.assets[m_asyncImportRequest.assetId] = result.asset;
            m_document.dirty = true;
            SaveDocument(nullptr);
        }
        m_importAssetModal.SetLastResult(outcome);

        if (result.ok) {
            const std::string importedAssetId = m_asyncImportRequest.assetId;
            ShowSnackbar(SnackbarNotification{
                .level = SnackbarLevel::Success,
                .title = "Asset imported successfully",
                .detail = SnackbarPathDetail(m_asyncImportRequest.sourcePath),
                .action = SnackbarAction{
                    .label = "Undo",
                    .handler = [this, importedAssetId]() {
                        const AssetDeleteResult deleteResult =
                            DeleteAssetDefinition(importedAssetId);
                        if (!deleteResult.ok) {
                            ShowSnackbar(SnackbarNotification{
                                .level = SnackbarLevel::Error,
                                .title = "Undo failed",
                                .detail = deleteResult.error.empty()
                                              ? "Asset could not be removed."
                                              : deleteResult.error,
                                .duration = 10.0f});
                        }
                    }},
                .duration = 5.0f});
        } else {
            ShowSnackbar(SnackbarNotification{.level = SnackbarLevel::Error,
                                              .title = "Import failed",
                                              .detail = result.error,
                                              .duration = 10.0f});
        }

        LogInfo("[Import] Async import completed for '{}' — ok={}",
                m_asyncImportRequest.sourcePath, result.ok);
    }

    /** @copydoc EditorLayer::ShowSnackbar(SnackbarNotification) */
    void EditorLayer::ShowSnackbar(SnackbarNotification notification) {
        if (notification.duration <= 0.0f)
            notification.duration = kSnackbarDuration;
        m_snackbar = std::move(notification);
        m_snackbarTimer = m_snackbar.duration;
        m_snackbarDismissTimer = 0.0f;
        m_snackbarDismissing = false;
    }

    /** @copydoc EditorLayer::ShowSnackbar(const std::string&) */
    void EditorLayer::ShowSnackbar(const std::string& msg) {
        ShowSnackbar(SnackbarNotification{.title = msg});
    }

    /** @copydoc EditorLayer::ShowSnackbar(const std::string&, float) */
    void EditorLayer::ShowSnackbar(const std::string& msg, float duration) {
        ShowSnackbar(SnackbarNotification{.title = msg, .duration = duration});
    }

    /** @copydoc EditorLayer::DismissSnackbar */
    void EditorLayer::DismissSnackbar() {
        if (m_snackbar.title.empty() && m_snackbar.detail.empty())
            return;
        m_snackbarTimer = 0.0f;
        m_snackbarDismissTimer = 0.0f;
        m_snackbarDismissing = true;
    }

    /** @copydoc EditorLayer::ShowEditorOperation */
    void EditorLayer::ShowEditorOperation(EditorOperation operation) {
        if (operation.id.empty())
            operation.id = operation.label.empty() ? "operation" : operation.label;
        if (operation.label.empty())
            operation.label = "Operation";
        m_editorOperation = std::move(operation);
        m_editorOperationActive = true;
    }

    /** @copydoc EditorLayer::ClearEditorOperation */
    void EditorLayer::ClearEditorOperation(std::string_view id) {
        if (!m_editorOperationActive)
            return;
        if (!id.empty() && m_editorOperation.id != id)
            return;
        m_editorOperation = {};
        m_editorOperationActive = false;
    }

    /** @copydoc EditorLayer::DrawSnackbar */
    void EditorLayer::DrawSnackbar() {
        if (!m_snackbarDismissing && m_snackbarTimer <= 0.0f)
            return;

        const bool pinned = m_snackbar.pinWhileAsyncImport && m_asyncImportActive;
        const float dt = ImGui::GetIO().DeltaTime;
        if (!pinned) {
            if (m_snackbarDismissing) {
                m_snackbarDismissTimer += dt;
            } else {
                m_snackbarTimer -= dt;
                if (m_snackbarTimer <= 0.0f) {
                    DismissSnackbar();
                }
            }
        }

        const float dismissProgress = m_snackbarDismissing
                                          ? std::clamp(m_snackbarDismissTimer /
                                                           kSnackbarDismissDuration,
                                                       0.0f, 1.0f)
                                          : 0.0f;
        if (dismissProgress >= 1.0f) {
            m_snackbar = {};
            m_snackbarTimer = 0.0f;
            m_snackbarDismissTimer = 0.0f;
            m_snackbarDismissing = false;
            return;
        }
        const float easedDismiss = 1.0f - std::pow(1.0f - dismissProgress, 3.0f);
        const float alpha = 1.0f - easedDismiss;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float pad = 16.0f;
        const float maxWidth = std::min(720.0f, vp->WorkSize.x - pad * 2.0f);
        const float minWidth = std::min(360.0f, maxWidth);
        const float titleWidth = ImGui::CalcTextSize(m_snackbar.title.c_str()).x;
        const float detailWidth = ImGui::CalcTextSize(m_snackbar.detail.c_str()).x;
        const bool hasAction = !m_snackbar.action.label.empty() &&
                               static_cast<bool>(m_snackbar.action.handler);
        const float actionWidth = hasAction
                                      ? ImGui::CalcTextSize(m_snackbar.action.label.c_str()).x + 18.0f
                                      : 0.0f;
        const float closeWidth = m_snackbar.dismissible ? 34.0f : 0.0f;
        const float contentWidth = 86.0f + std::max(titleWidth, detailWidth) +
                                   actionWidth + closeWidth;
        const float boxWidth = std::clamp(contentWidth, minWidth, maxWidth);
        const ImVec2 boxSize(boxWidth, 72.0f);
        const float reservedBottom = Internal::kEditorStatusH +
                                     (m_active ? std::max(0.0f, m_bottomDockHeight) : 0.0f);
        const float yAboveEditorChrome =
            vp->WorkPos.y + vp->WorkSize.y - reservedBottom - boxSize.y - pad;
        const float slideOffset = easedDismiss * (boxSize.x + pad);
        const ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - boxSize.x - pad +
                             slideOffset,
                         std::max(vp->WorkPos.y + pad, yAboveEditorChrome));

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(boxSize, ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.075f, 0.125f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.34f, 0.50f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.28f, 0.46f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.36f, 0.58f, 0.70f));

        ImGui::Begin("##Snackbar", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);

        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        const ImVec2 winMax(winPos.x + boxSize.x, winPos.y + boxSize.y);
        ImVec4 accent = SnackbarAccentColor(m_snackbar.level);
        accent.w *= alpha;
        ImVec4 iconFill = SnackbarIconFillColor(m_snackbar.level);
        iconFill.w *= alpha;
        dl->AddRectFilled(winPos, ImVec2(winPos.x + 4.0f, winMax.y),
                          ImGui::ColorConvertFloat4ToU32(accent), 8.0f,
                          ImDrawFlags_RoundCornersLeft);
        const ImVec2 iconMin(winPos.x + 16.0f, winPos.y + 12.0f);
        const ImVec2 iconMax(iconMin.x + 46.0f, iconMin.y + 46.0f);
        dl->AddRectFilled(iconMin, iconMax,
                          ImGui::ColorConvertFloat4ToU32(iconFill), 7.0f);
        DrawSnackbarStatusGlyph(dl,
                                ImVec2((iconMin.x + iconMax.x) * 0.5f,
                                       (iconMin.y + iconMax.y) * 0.5f),
                                12.5f, m_snackbar.level, alpha);

        const float rightControlsWidth = actionWidth + closeWidth + 16.0f;
        const float textStartX = winPos.x + 74.0f;
        const float textEndX = std::max(textStartX + 80.0f,
                                        winMax.x - rightControlsWidth);
        ImGui::SetCursorScreenPos(ImVec2(textStartX, winPos.y + 15.0f));
        ImGui::PushTextWrapPos(textEndX);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.98f, 1.0f, alpha));
        ImGui::TextUnformatted(m_snackbar.title.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();

        if (!m_snackbar.detail.empty()) {
            ImGui::SetCursorScreenPos(ImVec2(textStartX, winPos.y + 39.0f));
            ImGui::PushTextWrapPos(textEndX);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.78f, 0.88f, alpha));
            ImGui::TextUnformatted(m_snackbar.detail.c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }

        float controlX = winMax.x - closeWidth - actionWidth - 10.0f;
        if (hasAction) {
            ImGui::SetCursorScreenPos(ImVec2(controlX, winPos.y + 23.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.62f, 1.0f, alpha));
            if (ImGui::Button(std::format("{}##snackbar_action", m_snackbar.action.label).c_str(),
                              ImVec2(actionWidth, 26.0f))) {
                auto handler = std::move(m_snackbar.action.handler);
                m_snackbar = {};
                m_snackbarTimer = 0.0f;
                m_snackbarDismissTimer = 0.0f;
                m_snackbarDismissing = false;
                handler();
            }
            ImGui::PopStyleColor();
            controlX += actionWidth;
        }

        if (m_snackbar.dismissible) {
            ImGui::SetCursorScreenPos(ImVec2(controlX + 6.0f, winPos.y + 21.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.68f, 0.74f, 0.84f, alpha));
            const std::string closeLabel = std::string(ICON_FA_XMARK) + "##snackbar_close";
            if (ImGui::Button(closeLabel.c_str(), ImVec2(28.0f, 28.0f))) {
                DismissSnackbar();
            }
            ImGui::PopStyleColor();
        }
        ImGui::End();

        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(5);
    }
} // namespace Horo::Editor
