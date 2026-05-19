/** @file EditorAssetsPanel.cpp
 *  @brief Implements asset-grid rendering and spotlight search. */
#include "ui/editor/components/EditorAssetsPanel.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <ranges>
#include <string>

#include <imgui.h>

#include "core/StringUtils.h"
#include "ui/UiComponents.h"
#include "renderer/RenderTargetHandle.h"
#include "ui/editor/AssetImportService.h"
#include "ui/IconsFontAwesome6.h"
#include "ui/editor/AssetMetadata.h"
#include "ui/editor/EditorSearch.h"
#include "ui/editor/EditorUiLogic.h"
#include "ui/editor/SceneDocument.h"
#include "ui/editor/components/EditorAssetThumbnailPreview.h"

namespace Horo::Editor
{
    namespace
    {
        /** @brief ImGui window name used for the standalone assets panel. */
        constexpr const char* kEditorAssetsWindow = "Assets";

        /** @brief Toolbar height used to anchor the assets panel in the left dock layout. */
        constexpr float kEditorToolbarH = 32.0f;
        /** @brief Status-bar height reserved at the bottom of the editor viewport. */
        constexpr float kEditorStatusH = 20.0f;
        /** @brief Fraction of left-dock vertical space allocated to the hierarchy section. */
        constexpr float kHierarchySectionRatio = 0.5f;
        /** @brief Shared ImGui flags used for the docked assets panel container window. */
        constexpr ImGuiWindowFlags kMainPanelWindowFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;
    }

    /** @copydoc EditorAssetsPanel::Draw */
    void EditorAssetsPanel::Draw(const EditorComponentContext& /*ctx*/,
                                 const EditorAssetsPanelCallbacks& callbacks,
                                 const EditorAssetsPanelState& state) const
    {
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

        if (state.albedoSelDrop)
            state.albedoSelDrop->Clear();

        if (ImGui::Button("+", ImVec2(28.0f, 0.0f)))
            callbacks.openImportAssetModal();
        ImGui::SameLine();
        ImGui::SetCursorPosX(
            std::max(0.0f, ImGui::GetWindowContentRegionMax().x - 74.0f));
        if (ImGui::Button("Search", ImVec2(64.0f, 0.0f)))
        {
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
        DrawAssetGrid(state, assetIds, callbacks);

        ImGui::End();
    }

    /** @copydoc EditorAssetsPanel::DrawContent */
    void EditorAssetsPanel::DrawContent(const EditorAssetsPanelCallbacks& callbacks,
                                        const EditorAssetsPanelState& state) const
    {
        if (!state.document || !state.selectedAssetId || !state.assetSearchQuery)
            return;

        if (state.albedoSelDrop)
            state.albedoSelDrop->Clear();

        std::vector<std::string> assetIds;
        assetIds.reserve(state.document->assets.size());
        for (const auto& [assetId, _] : state.document->assets)
            assetIds.push_back(assetId);
        std::ranges::sort(assetIds);

        DrawAssetSpotlightPopup(state, assetIds);
        DrawAssetGrid(state, assetIds, callbacks);
    }

    /** @copydoc EditorAssetsPanel::DrawAssetSpotlightPopup */
    void EditorAssetsPanel::DrawAssetSpotlightPopup(
        const EditorAssetsPanelState& state,
        const std::vector<std::string>& assetIds) const
    {
        std::array<char, 256> queryBuf{};
        state.assetSearchQuery->copy(queryBuf.data(), queryBuf.size() - 1);

        if (!Horo::Ui::BeginEditorPickerModal(
            {"Asset Search", "Jump to asset", 520.0f, "Type asset id..."},
            *state.assetSearchOpen, queryBuf.data(), queryBuf.size()))
            return;

        *state.assetSearchQuery = queryBuf.data();

        bool picked = false;
        int shownCount = 0;
        for (const std::string& assetId : assetIds)
        {
            auto it = state.document->assets.find(assetId);
            if (it == state.document->assets.end())
                continue;
            if (!AssetMatchesQuickOpenQuery(assetId, it->second,
                                            *state.assetSearchQuery))
                continue;
            if (const auto label = std::format("{}##asset_spotlight_{}", assetId, assetId);
                Horo::Ui::EditorPickerModalRow(label.c_str(),
                                               *state.selectedAssetId == assetId))
            {
                *state.selectedAssetId = assetId;
                picked = true;
            }
            ++shownCount;
        }

        if (shownCount == 0)
            ImGui::TextDisabled("No assets match '%s'",
                                state.assetSearchQuery->c_str());

        if (picked)
        {
            *state.assetSearchOpen = false;
            state.assetSearchQuery->clear();
            ImGui::CloseCurrentPopup();
        }
        Horo::Ui::EndEditorPickerModal(*state.assetSearchOpen, state.assetSearchQuery);
    }

    namespace
    {
        /** @brief Renders the asset thumbnail image (or a fallback label) inside a tile. */
        ImVec4 WithAlpha(ImVec4 color, float alpha)
        {
            color.w = alpha;
            return color;
        }

        /** @brief Converts a theme token into a packed ImGui draw colour. */
        ImU32 ThemeColor(ImVec4 color, float alpha)
        {
            return ImGui::ColorConvertFloat4ToU32(WithAlpha(color, alpha));
        }

        /** @brief Renders the asset thumbnail image (or a fallback label) inside a tile. */
        void DrawAssetThumbnail(ImDrawList* dl, const Horo::Ui::EditorTheme& theme,
                                const std::string& assetId, const AssetDef& asset,
                                const ImVec2& thumbMin, const ImVec2& thumbMax)
        {
            dl->AddRectFilled(thumbMin, thumbMax,
                              ThemeColor(theme.palette.input, 0.95f), 6.0f);
            dl->AddRect(thumbMin, thumbMax, ThemeColor(theme.palette.border, 0.85f),
                        6.0f);

            if (RenderTargetHandle previewHandle;
                TryGetAssetPreviewHandle(assetId, asset, &previewHandle) &&
                previewHandle.IsValid())
            {
                const ImVec2 uv0 = previewHandle.needsYFlip ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
                const ImVec2 uv1 = previewHandle.needsYFlip ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
                dl->AddImage(ToImTextureId(previewHandle), thumbMin, thumbMax, uv0, uv1);
                return;
            }

            const ImU32 labelCol = ThemeColor(theme.palette.textMuted, 0.95f);
            const ImU32 meshCol = ThemeColor(theme.palette.textMuted, 0.80f);
            const std::string ext = asset.mesh.empty()
                                        ? std::string("mesh")
                                        : Horo::ToLowerAscii(std::filesystem::path(asset.mesh).extension().string());
            dl->AddText(ImVec2(thumbMin.x + 8.0f, thumbMin.y + 10.0f), labelCol, "No preview texture");
            dl->AddText(ImVec2(thumbMin.x + 8.0f, thumbMin.y + 30.0f), meshCol, ext.c_str());
        }

        /** @brief Renders the context menu for a single asset tile. */
        void DrawAssetTileContextMenu(const EditorAssetsPanelState& state,
                                      const std::string& assetId,
                                      const EditorAssetsPanelCallbacks& callbacks)
        {
            if (!ImGui::BeginPopupContextItem("##asset_tile_ctx"))
                return;

            if (ImGui::MenuItem("Add Prop"))
            {
                SceneObject obj = callbacks.makeObjectFromAsset(assetId);
                state.document->objects.push_back(std::move(obj));
                if (state.selectedIndices)
                    *state.selectedIndices = {
                        static_cast<int>(state.document->objects.size()) - 1
                    };
                callbacks.markDirtyAndReload();
            }
            if (ImGui::MenuItem("Delete Asset"))
                callbacks.requestDeleteAsset(assetId);
            ImGui::EndPopup();
        }
    } // namespace

    /** @copydoc EditorAssetsPanel::DrawAssetTile */
    void EditorAssetsPanel::DrawAssetTile(
        const EditorAssetsPanelState& state,
        const std::string& assetId,
        const AssetDef& asset,
        const AssetTileDimensions& dims,
        const EditorAssetsPanelCallbacks& callbacks) const
    {
        const bool isSelectedAsset = (*state.selectedAssetId == assetId);
        const auto& theme = Horo::Ui::GetEditorTheme();
        Horo::Ui::EditorCardConfig cardCfg{"##asset_tile", dims.tileW, dims.tileH, isSelectedAsset};
        Horo::Ui::BeginEditorCard(theme, cardCfg);

        ImGui::InvisibleButton("##asset_tile_select",
                               ImVec2(dims.tileW - 2.0f, dims.tileH - 2.0f));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            *state.selectedAssetId = isSelectedAsset ? std::string() : assetId;
            if (!isSelectedAsset && state.selectedIndices)
                state.selectedIndices->clear();
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("ASSET_ID", assetId.c_str(), assetId.size() + 1);
            ImGui::Text("+ %s", assetId.c_str());
            ImGui::EndDragDropSource();
        }

        DrawAssetTileContextMenu(state, assetId, callbacks);

        const ImVec2 tileMin = ImGui::GetWindowPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 thumbMin(tileMin.x + dims.thumbPad, tileMin.y + dims.thumbPad);
        const ImVec2 thumbMax(thumbMin.x + dims.thumbSize, thumbMin.y + dims.thumbSize);

        DrawAssetThumbnail(dl, theme, assetId, asset, thumbMin, thumbMax);

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
        if (const float maxLabelW = dims.tileW - 14.0f;
            ImGui::CalcTextSize(nameLabel.c_str()).x > maxLabelW)
        {
            while (!nameLabel.empty() &&
                ImGui::CalcTextSize((nameLabel + "...").c_str()).x > maxLabelW)
                nameLabel.pop_back();
            nameLabel += "...";
        }
        const ImVec2 nameSz = ImGui::CalcTextSize(nameLabel.c_str());
        const float nameX = tileMin.x + std::max(7.0f, (dims.tileW - nameSz.x) * 0.5f);
        const float nameY = thumbMax.y + 6.0f;
        const ImU32 nameColor = hasDiagnostics
                                    ? IM_COL32(255, 120, 120, 255)
                                    : ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddText(ImVec2(nameX, nameY), nameColor, nameLabel.c_str());

        Horo::Ui::EndEditorCard();
    }

    /** @copydoc EditorAssetsPanel::DrawAddAssetTile */
    void EditorAssetsPanel::DrawAddAssetTile(const EditorAssetsPanelCallbacks& callbacks,
                                              const AssetTileDimensions& dims) const
    {
        const float lineH = ImGui::GetTextLineHeight();
        const float labelH = 6.0f + lineH + 4.0f;
        const float addTileH = dims.thumbPad * 2.0f + dims.thumbSize + labelH;
        const auto& theme = Horo::Ui::GetEditorTheme();
        // Use at least tileH so the tile is never shorter than regular tiles
        const float childH = std::max(dims.tileH, addTileH);

        ImGui::BeginChild("##add_asset_tile_child", ImVec2(dims.tileW, childH),
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // Invisible button covers the whole tile for click detection
        ImGui::InvisibleButton("##add_asset_btn", ImVec2(dims.tileW - 2.0f, childH - 2.0f));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            callbacks.openImportAssetModal();

        // Accept internal ASSET_ID drags (e.g. future project-tree drag sources)
        if (ImGui::BeginDragDropTarget())
        {
            ImGui::AcceptDragDropPayload("ASSET_ID");
            ImGui::EndDragDropTarget();
        }

        const ImVec2 tileMin = ImGui::GetWindowPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const ImVec4 bgColor = hovered
                                   ? WithAlpha(theme.palette.cardHover, 0.95f)
                                   : WithAlpha(theme.palette.card, 0.85f);
        const ImVec2 bgMin = tileMin;
        const ImVec2 bgMax(tileMin.x + dims.tileW, tileMin.y + childH);
        dl->AddRectFilled(bgMin, bgMax, ImGui::ColorConvertFloat4ToU32(bgColor),
                          6.0f);

        const ImVec4 borderColor = hovered
                                       ? WithAlpha(theme.palette.accentHover, 0.90f)
                                       : WithAlpha(theme.palette.border, 0.70f);
        dl->AddRect(bgMin, bgMax, ImGui::ColorConvertFloat4ToU32(borderColor),
                    6.0f, 0, 1.5f);

        // Thumbnail area bounds
        const ImVec2 thumbMin(tileMin.x + dims.thumbPad, tileMin.y + dims.thumbPad);
        const ImVec2 thumbMax(thumbMin.x + dims.thumbSize, thumbMin.y + dims.thumbSize);

        dl->AddRectFilled(thumbMin, thumbMax,
                          ThemeColor(hovered ? theme.palette.inputHover
                                             : theme.palette.input,
                                     0.90f),
                          6.0f);
        dl->AddRect(thumbMin, thumbMax, ThemeColor(theme.palette.border, 0.50f),
                    6.0f, 0, 1.0f);

        // Centered "+" icon — 1.4× font size, large enough to be prominent but not overwhelming
        const float iconFontSize = ImGui::GetFontSize() * 1.4f;
        ImFont* font = ImGui::GetFont();
        const ImVec2 iconSz = font->CalcTextSizeA(iconFontSize, FLT_MAX, 0.0f, ICON_FA_PLUS);

        const ImVec2 iconPos(
            thumbMin.x + (dims.thumbSize - iconSz.x) * 0.5f,
            thumbMin.y + (dims.thumbSize - iconSz.y) * 0.5f);
        const ImU32 iconColor = hovered
                                    ? ThemeColor(theme.palette.accentHover, 1.0f)
                                    : ThemeColor(theme.palette.accent, 0.90f);

        dl->AddText(font, iconFontSize, iconPos, iconColor, ICON_FA_PLUS);

        const float labelY = thumbMax.y + 6.0f;
        const char* label = "Add Asset";
        const ImVec2 sz = ImGui::CalcTextSize(label);
        dl->AddText(
            ImVec2(tileMin.x + std::max(7.0f, (dims.tileW - sz.x) * 0.5f), labelY),
            ImGui::GetColorU32(ImGuiCol_Text),
            label);

        ImGui::EndChild();
    }

    /** @copydoc EditorAssetsPanel::DrawAssetGrid */
    void EditorAssetsPanel::DrawAssetGrid(const EditorAssetsPanelState& state,
                                          const std::vector<std::string>& assetIds,
                                          const EditorAssetsPanelCallbacks& callbacks) const
    {
        ImGui::BeginChild("##asset_grid", ImVec2(0, 0), false);

        const float spacing = 8.0f;
        const float minTileW = 130.0f;
        const float availW = std::max(40.0f, ImGui::GetContentRegionAvail().x);
        const int columns =
            std::max(1, static_cast<int>((availW + spacing) / (minTileW + spacing)));
        AssetTileDimensions dims;
        dims.tileW = (availW - spacing * static_cast<float>(columns - 1)) /
            static_cast<float>(columns);
        dims.thumbPad = 8.0f;
        dims.thumbSize = dims.tileW - dims.thumbPad * 2.0f;
        const float lineH = ImGui::GetTextLineHeight();
        const float labelH = 6.0f + lineH + 4.0f;
        dims.tileH = dims.thumbPad * 2.0f + dims.thumbSize + labelH;

        int shownAssetCount = 0;
        for (const auto& assetId : assetIds)
        {
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
            DrawAssetTile(state, assetId, asset, dims, callbacks);
            ImGui::PopID();
        }

        // "No matches" hint when a search filter is active but returns nothing.
        // The add-asset tile still appears below regardless.
        if (const FilteredListState assetState = EvaluateFilteredListState(
                assetIds.size(), shownAssetCount, *state.assetSearchQuery);
            assetState == FilteredListState::NoMatches)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("No assets match '%s'",
                                state.assetSearchQuery->c_str());
            if (ImGui::Button("Clear Asset Search"))
                state.assetSearchQuery->clear();
            ImGui::Spacing();
        }

        // Always render the add-asset tile at the end of the grid
        {
            if (const int addTileCol = shownAssetCount % columns; addTileCol > 0)
                ImGui::SameLine(0.0f, spacing);
            DrawAddAssetTile(callbacks, dims);
        }

        ImGui::EndChild();
    }
}
