#include "ui/editor/components/EditorAssetsPanel.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <ranges>
#include <string>

#include <imgui.h>

#include "core/Logger.h"
#include "renderer/RenderTargetHandle.h"
#include "ui/editor/AssetIdentity.h"
#include "ui/editor/AssetImportService.h"
#include "ui/editor/AssetMetadata.h"
#include "ui/editor/EditorImGuiBackend.h"
#include "ui/editor/EditorSearch.h"
#include "ui/editor/EditorUiLogic.h"
#include "ui/editor/SceneDocument.h"
#include "ui/editor/components/EditorAssetThumbnailPreview.h"
#include "ui/editor/components/EditorComponentContext.h"

namespace Horo::Editor {

namespace {
    constexpr const char* kEditorAssetsWindow = "Assets";

    // Layout constants (copied from EditorLayer)
    constexpr float kEditorToolbarH = 32.0f;
    constexpr float kEditorStatusH = 20.0f;
    constexpr float kHierarchySectionRatio = 0.5f;
    constexpr ImGuiWindowFlags kMainPanelWindowFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    std::string ToLowerAscii(const std::string& str) {
        std::string result = str;
        for (auto& c : result)
            c = std::tolower(static_cast<unsigned char>(c));
        return result;
    }
}

void EditorAssetsPanel::Draw(const EditorComponentContext& ctx,
                            const EditorAssetsPanelCallbacks& callbacks,
                            const EditorAssetsPanelState& state) {
    if (!state.document || !state.selectedAssetId || !state.assetSearchQuery)
        return;

    const ImGuiIO& io = ImGui::GetIO();
    const float bottomDockH = ComputeEditorBottomDockHeight(io.DisplaySize.y);
    const float leftDockW = ComputeEditorLeftDockWidth(io.DisplaySize.x);
    const float workBottom = io.DisplaySize.y - kEditorStatusH - bottomDockH;
    const float hierarchyHeight =
        std::max(220.0f, (workBottom - kEditorToolbarH) * kHierarchySectionRatio);
    const float assetsTop = kEditorToolbarH + hierarchyHeight + 4.0f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, assetsTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(leftDockW, std::max(180.0f, workBottom - assetsTop)),
        ImGuiCond_Always);
    ImGui::Begin(kEditorAssetsWindow, nullptr, kMainPanelWindowFlags);

    if (state.albedoDraftDrop)
        state.albedoDraftDrop->Clear();
    if (state.albedoSelDrop)
        state.albedoSelDrop->Clear();

    if (ImGui::Button("+", ImVec2(28.0f, 0.0f)))
        *state.openNewAssetHeader = true;
    ImGui::SameLine();
    ImGui::SetCursorPosX(
        std::max(0.0f, ImGui::GetWindowContentRegionMax().x - 74.0f));
    if (ImGui::Button("Search", ImVec2(64.0f, 0.0f))) {
        *state.assetSearchOpen = true;
        state.assetSearchQuery->clear();
    }
    ImGui::Separator();

    std::vector<std::string> assetIds;
    assetIds.reserve(state.document->assets.size());
    for (const auto& [assetId, _] : state.document->assets)
        assetIds.push_back(assetId);
    std::ranges::sort(assetIds);

    DrawAssetSpotlightPopup(state, assetIds);

    bool openNewAssetModal = *state.openNewAssetHeader;
    if (*state.openNewAssetHeader)
        *state.openNewAssetHeader = false;

    DrawAssetGrid(state, assetIds, openNewAssetModal, callbacks);

    DrawCreateAssetModal(state, openNewAssetModal, callbacks);

    ImGui::End();
}

void EditorAssetsPanel::DrawAssetSpotlightPopup(
    const EditorAssetsPanelState& state,
    const std::vector<std::string>& assetIds) {
    if (*state.assetSearchOpen) {
        ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
        if (!ImGui::IsPopupOpen("Asset Search"))
            ImGui::OpenPopup("Asset Search");
    }

    if (!ImGui::BeginPopupModal("Asset Search", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextDisabled("Jump to asset");
    ImGui::SetNextItemWidth(440.0f);
    std::array<char, 256> queryBuf{};
    state.assetSearchQuery->copy(queryBuf.data(), queryBuf.size() - 1);
    if (ImGui::InputTextWithHint("##asset_spotlight_input", "Type asset id...",
                                 queryBuf.data(), queryBuf.size()))
        *state.assetSearchQuery = queryBuf.data();

    ImGui::Separator();
    bool picked = false;
    int shownCount = 0;
    for (const std::string& assetId : assetIds) {
        auto it = state.document->assets.find(assetId);
        if (it == state.document->assets.end())
            continue;
        if (!AssetMatchesQuickOpenQuery(assetId, it->second,
                                       *state.assetSearchQuery))
            continue;
        if (const auto label =
                std::format("{}##asset_spotlight_{}", assetId, assetId);
            ImGui::Selectable(label.c_str(), *state.selectedAssetId == assetId)) {
            *state.selectedAssetId = assetId;
            picked = true;
        }
        ++shownCount;
    }

    if (shownCount == 0)
        ImGui::TextDisabled("No assets match '%s'",
                           state.assetSearchQuery->c_str());

    if (picked || ImGui::Button("Close") ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        *state.assetSearchOpen = false;
        state.assetSearchQuery->clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorAssetsPanel::DrawAssetTile(
    const EditorAssetsPanelState& state,
    const std::string& assetId,
    const AssetDef& asset,
    float tileW,
    float tileH,
    float thumbPad,
    float thumbSize,
    const EditorAssetsPanelCallbacks& callbacks) {
    const bool isSelectedAsset = (*state.selectedAssetId == assetId);
    if (isSelectedAsset)
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.14f, 0.24f, 0.34f, 0.70f));
    ImGui::BeginChild(
        "##asset_tile", ImVec2(tileW, tileH), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (isSelectedAsset)
        ImGui::PopStyleColor();

    ImGui::InvisibleButton("##asset_tile_select",
                          ImVec2(tileW - 2.0f, tileH - 2.0f));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        *state.selectedAssetId = isSelectedAsset ? std::string() : assetId;

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("ASSET_ID", assetId.c_str(), assetId.size() + 1);
        ImGui::Text("+ %s", assetId.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginPopupContextItem("##asset_tile_ctx")) {
        if (ImGui::MenuItem("Add Prop")) {
            SceneObject obj = callbacks.makeObjectFromAsset(assetId);
            state.document->objects.push_back(std::move(obj));
            if (state.selectedIndices)
                *state.selectedIndices = {
                    static_cast<int>(state.document->objects.size()) - 1};
            callbacks.markDirtyAndReload();
        }
        if (ImGui::MenuItem("Delete Asset"))
            callbacks.requestDeleteAsset(assetId);
        ImGui::EndPopup();
    }

    const ImVec2 tileMin = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 thumbMin(tileMin.x + thumbPad, tileMin.y + thumbPad);
    const ImVec2 thumbMax(thumbMin.x + thumbSize, thumbMin.y + thumbSize);

    dl->AddRectFilled(
        thumbMin, thumbMax,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.10f, 0.13f, 0.18f, 0.95f)), 6.0f);
    dl->AddRect(thumbMin, thumbMax,
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.26f, 0.34f, 0.46f, 1.0f)),
                6.0f);

    if (RenderTargetHandle previewHandle;
        TryGetAssetPreviewHandle(assetId, asset, &previewHandle) &&
        previewHandle.IsValid()) {
        const ImVec2 uv0 =
            previewHandle.needsYFlip ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
        const ImVec2 uv1 =
            previewHandle.needsYFlip ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
        dl->AddImage(ToImTextureId(previewHandle), thumbMin, thumbMax, uv0, uv1);
    } else {
        const ImU32 labelCol =
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.67f, 0.74f, 0.84f, 0.95f));
        const ImU32 meshCol =
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.64f, 0.75f, 0.95f));
        const std::string ext =
            asset.mesh.empty()
                ? std::string("mesh")
                : ToLowerAscii(
                      std::filesystem::path(asset.mesh).extension().string());
        dl->AddText(ImVec2(thumbMin.x + 8.0f, thumbMin.y + 10.0f), labelCol,
                   "No preview texture");
        dl->AddText(ImVec2(thumbMin.x + 8.0f, thumbMin.y + 30.0f), meshCol,
                   ext.c_str());
    }

    AssetMetadata tileMetadata;
    const bool hasTileMetadata =
        LoadAssetMetadata(asset.guid, &tileMetadata, nullptr);
    const bool hasDiagnostics =
        hasTileMetadata &&
        (!tileMetadata.lastImportSucceeded || !tileMetadata.diagnostics.empty());

    std::string nameLabel =
        asset.displayName.empty() ? assetId : asset.displayName;
    if (hasDiagnostics)
        nameLabel += " !";
    if (const float maxLabelW = tileW - 14.0f;
        ImGui::CalcTextSize(nameLabel.c_str()).x > maxLabelW) {
        while (!nameLabel.empty() &&
               ImGui::CalcTextSize((nameLabel + "...").c_str()).x > maxLabelW)
            nameLabel.pop_back();
        nameLabel += "...";
    }
    const ImVec2 nameSz = ImGui::CalcTextSize(nameLabel.c_str());
    const float nameX = tileMin.x + std::max(7.0f, (tileW - nameSz.x) * 0.5f);
    const float nameY = thumbMax.y + 6.0f;
    const ImU32 nameColor = hasDiagnostics ? IM_COL32(255, 120, 120, 255)
                                           : ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddText(ImVec2(nameX, nameY), nameColor, nameLabel.c_str());

    ImGui::EndChild();
}

void EditorAssetsPanel::DrawAssetGrid(const EditorAssetsPanelState& state,
                                      const std::vector<std::string>& assetIds,
                                      bool& openNewAssetModal,
                                      const EditorAssetsPanelCallbacks& callbacks) {
    ImGui::BeginChild("##asset_grid", ImVec2(0, 0), false);

    const float spacing = 8.0f;
    const float minTileW = 130.0f;
    const float availW = std::max(40.0f, ImGui::GetContentRegionAvail().x);
    const int columns =
        std::max(1, static_cast<int>((availW + spacing) / (minTileW + spacing)));
    const float tileW = (availW - spacing * static_cast<float>(columns - 1)) /
                        static_cast<float>(columns);
    const float thumbPad = 8.0f;
    const float thumbSize = tileW - thumbPad * 2.0f;
    const float tileH = thumbSize + thumbPad * 2.0f + 22.0f;

    int shownAssetCount = 0;
    for (const auto& assetId : assetIds) {
        const auto assetIt = state.document->assets.find(assetId);
        if (assetIt == state.document->assets.end())
            continue;
        const auto& asset = assetIt->second;
        if (!AssetMatchesQuickOpenQuery(assetId, asset,
                                       *state.assetSearchQuery))
            continue;
        ++shownAssetCount;
        if (const int col = (shownAssetCount - 1) % columns; col > 0)
            ImGui::SameLine(0.0f, spacing);
        ImGui::PushID(assetId.c_str());
        DrawAssetTile(state, assetId, asset, tileW, tileH, thumbPad, thumbSize,
                     callbacks);
        ImGui::PopID();
    }

    if (const FilteredListState assetState = EvaluateFilteredListState(
            assetIds.size(), shownAssetCount, *state.assetSearchQuery);
        assetState != FilteredListState::None) {
        ImGui::Spacing();
        if (assetState == FilteredListState::EmptyData) {
            ImGui::TextDisabled("Asset registry is empty");
            ImGui::TextDisabled(
                "Create your first asset to enable fast prop placement.");
            if (ImGui::Button("Create First Asset"))
                openNewAssetModal = true;
        } else if (assetState == FilteredListState::NoMatches) {
            ImGui::TextDisabled("No assets match '%s'",
                               state.assetSearchQuery->c_str());
            if (ImGui::Button("Clear Asset Search"))
                state.assetSearchQuery->clear();
        }
    }

    ImGui::EndChild();
}

void EditorAssetsPanel::DrawCreateAssetModal(
    const EditorAssetsPanelState& state,
    bool openModal,
    const EditorAssetsPanelCallbacks& callbacks) {
    if (openModal)
        ImGui::OpenPopup("Create Asset");
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Create Asset", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawCreateAssetModalContent(state, callbacks);
        ImGui::EndPopup();
    }
}

void EditorAssetsPanel::DrawCreateAssetModalContent(
    const EditorAssetsPanelState& state,
    const EditorAssetsPanelCallbacks& callbacks) {
    constexpr float blockW = 470.0f;
    ImGui::PushItemWidth(blockW);

    if (ImGui::Button("Import .obj...", ImVec2(blockW, 0.0f))) {
        state.assetImportError->clear();
#if !defined(_WIN32) && !defined(__APPLE__)
        *state.assetImportError = "Import dialog is not supported on this platform yet.";
#else
        callbacks.setDeferredFilePick(1);  // DeferredFilePick::ImportObjBulk
#endif
    }
    if (!state.assetImportError->empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + blockW);
        ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s",
                          state.assetImportError->c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();

    std::string idBuf(128, '\0');
    state.assetDraftId->copy(idBuf.data(), idBuf.size() - 1);
    ImGui::TextDisabled("Asset ID");
    if (ImGui::InputText("##draft_id", idBuf.data(), idBuf.size())) {
        *state.assetDraftId = idBuf.data();
        if (state.assetDraftDisplayName->empty())
            *state.assetDraftDisplayName = *state.assetDraftId;
    }

    std::string displayNameBuf(128, '\0');
    state.assetDraftDisplayName->copy(displayNameBuf.data(),
                                       displayNameBuf.size() - 1);
    ImGui::TextDisabled("Display name");
    if (ImGui::InputText("##draft_display_name", displayNameBuf.data(),
                         displayNameBuf.size()))
        *state.assetDraftDisplayName = displayNameBuf.data();

    if (!state.assetDraftGuid->empty()) {
        ImGui::TextDisabled("GUID");
        ImGui::TextWrapped("%s", state.assetDraftGuid->c_str());
    }

    std::string meshBuf(256, '\0');
    state.assetDraftMesh->copy(meshBuf.data(), meshBuf.size() - 1);
    ImGui::TextDisabled("Mesh");
    if (ImGui::InputText("##draft_mesh", meshBuf.data(), meshBuf.size()))
        *state.assetDraftMesh = meshBuf.data();

    std::string scaleBuf(128, '\0');
    state.assetDraftRenderScale->copy(scaleBuf.data(), scaleBuf.size() - 1);
    ImGui::TextDisabled("Render scale");
    if (ImGui::InputText("##draft_scale", scaleBuf.data(), scaleBuf.size()))
        *state.assetDraftRenderScale = scaleBuf.data();

    std::string albDraftBuf(512, '\0');
    state.assetDraftAlbedoMap->copy(albDraftBuf.data(), albDraftBuf.size() - 1);
    ImGui::TextDisabled("Albedo map (optional)");
    const ImVec2 draftAlbLabelMin = ImGui::GetItemRectMin();
    const ImVec2 draftAlbLabelMax = ImGui::GetItemRectMax();
    if (ImGui::InputText("##draft_albedo", albDraftBuf.data(),
                         albDraftBuf.size()))
        *state.assetDraftAlbedoMap = albDraftBuf.data();
    {
        const ImVec2 fMin = ImGui::GetItemRectMin();
        const ImVec2 fMax = ImGui::GetItemRectMax();
        if (state.albedoDraftDrop) {
            state.albedoDraftDrop->valid = true;
            state.albedoDraftDrop->minX = std::min(draftAlbLabelMin.x, fMin.x);
            state.albedoDraftDrop->minY = draftAlbLabelMin.y;
            state.albedoDraftDrop->maxX = std::max(draftAlbLabelMax.x, fMax.x);
            state.albedoDraftDrop->maxY = fMax.y;
        }
    }

#if defined(_WIN32) || defined(__APPLE__)
    if (ImGui::Button("Browse texture...##alb_pick_draft", ImVec2(blockW, 0.0f)))
        callbacks.setDeferredFilePick(2);  // DeferredFilePick::NewAssetAlbedo
#else
    ImGui::BeginDisabled();
    ImGui::Button("Browse texture...##alb_pick_draft", ImVec2(blockW, 0.0f));
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Texture file dialog is not available on this platform.");
#endif

    ImGui::Spacing();

    const bool canCreate = !state.assetDraftId->empty() && !state.assetDraftMesh->empty();
    if (!canCreate)
        ImGui::BeginDisabled();
    if (ImGui::Button("Create Asset", ImVec2(150.0f, 0.0f))) {
        AssetDef def;
        def.mesh = *state.assetDraftMesh;
        def.renderScale = state.assetDraftRenderScale->empty() ? "1.0000,1.0000,1.0000"
                                                                 : *state.assetDraftRenderScale;
        def.albedoMap = *state.assetDraftAlbedoMap;
        def.guid =
            state.assetDraftGuid->empty() ? GenerateAssetGuid() : *state.assetDraftGuid;
        def.displayName = state.assetDraftDisplayName->empty()
                              ? *state.assetDraftId
                              : *state.assetDraftDisplayName;
        state.document->assets[*state.assetDraftId] = std::move(def);
        if (std::string metadataError;
            !state.assetImportService->SaveMetadataForAsset(
                *state.assetDraftId,
                state.document->assets[*state.assetDraftId],
                &metadataError) &&
            !metadataError.empty()) {
            LogWarn("Create Asset metadata sync: {}", metadataError);
        }
        *state.selectedAssetId = *state.assetDraftId;
        state.assetDraftId->clear();
        state.assetDraftGuid->clear();
        state.assetDraftDisplayName->clear();
        state.assetDraftMesh->clear();
        *state.assetDraftRenderScale = "1.0000,1.0000,1.0000";
        state.assetDraftAlbedoMap->clear();
        state.assetImportError->clear();
        callbacks.markDirtyAndReload();
        ImGui::CloseCurrentPopup();
    }
    if (!canCreate)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(150.0f, 0.0f))) {
        state.assetImportError->clear();
        state.assetDraftGuid->clear();
        state.assetDraftDisplayName->clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::PopItemWidth();
}

}
