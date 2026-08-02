#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserGrid.h"

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserActions.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserCards.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserDialogs.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserInteractionSession.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserPaneLayout.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserToolbar.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <ranges>
#include <string>

namespace Horo::Editor {
    namespace {
        constexpr float OuterPaddingX = 10.0F;
        constexpr float OuterPaddingY = 8.0F;
        constexpr float ToolbarPadX = 12.0F;
        constexpr float ToolbarPadY = 5.0F;
        constexpr float HeaderHeight = 28.0F;
        constexpr float HeaderToGridGap = 8.0F;
        constexpr float CardGap = 6.0F;
        constexpr float MinimumCardWidth = 66.0F;
        constexpr float CardFooterHeight = 20.0F;
        constexpr float CardRadius = 3.0F;
        constexpr float HeaderFontSize = kGlobalDockMinimumFontSize;
        constexpr float CardFontSize = kGlobalDockMinimumFontSize;
        constexpr float PreviewRowHeight = 20.0F;
        constexpr char ContentBrowserAssetPayload[] = "HORO_CONTENT_BROWSER_ASSET";

        [[nodiscard]] ImFont *ResolveFont(ImFont *preferred) {
            return preferred != nullptr ? preferred : ImGui::GetFont();
        }

        [[nodiscard]] std::optional<std::string> AbsoluteAssetPathFromPayload(const ImGuiPayload *payload) {
            if (payload == nullptr || !payload->IsDataType(ContentBrowserAssetPayload) || payload->Data == nullptr ||
                payload->DataSize <= 1) {
                return std::nullopt;
            }
            const auto *bytes = static_cast<const char *>(payload->Data);
            const std::size_t size = static_cast<std::size_t>(payload->DataSize);
            if (bytes[size - 1] != '\0' || std::strlen(bytes) != size - 1)
                return std::nullopt;
            const std::filesystem::path path{bytes};
            if (!path.is_absolute())
                return std::nullopt;
            return path.lexically_normal().string();
        }
    }  // namespace

    AssetBrowserGridMetrics ComputeAssetBrowserGridMetrics(const float availableWidth) noexcept {
        const float safeWidth = std::max(1.0F, availableWidth);
        const auto columns = static_cast<std::size_t>(std::max(1.0F, std::floor((safeWidth + CardGap) / (MinimumCardWidth + CardGap))));
        const float cardWidth = std::max(1.0F, (safeWidth - CardGap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
        return {.columns = columns, .cardWidth = cardWidth};
    }

    /** @copydoc DrawAssetBrowserGrid */
    void DrawAssetBrowserGrid(const ImVec2 &contentOrigin, const float contentWidth, const EditorWorkspaceViewModel &viewModel,
                              EditorWorkspaceViewCommandData &command, const EditorGuiContext &context,
                              AssetBrowserInteractionSession &interactionSession, AssetBrowserCardRenderer &cardRenderer) {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        ImFont *font = ResolveFont(context.theme.fonts.sansCompact);
        AssetBrowserInteractionState &state = interactionSession.State();
        std::string &selectedAssetPath = state.selectedAbsolutePath;
        const ILocalizationService &localization = context.localization;
        const ContentBrowserDirectory &directory = viewModel.contentBrowser;

        // ── App bar background ──────────────────────────────────
        const float barFullWidth = contentWidth + OuterPaddingX * 2.0F;
        const float barHeight = ToolbarPadY * 2.0F + HeaderHeight;
        const ImVec2 barMin{contentOrigin.x, contentOrigin.y};
        const ImVec2 barMax{barMin.x + barFullWidth, barMin.y + barHeight};

        drawList->AddRectFilled(barMin, barMax, Theme::U32(Theme::Bg2()));
        drawList->AddLine({barMin.x, barMax.y}, {barMax.x, barMax.y}, Theme::U32(Theme::Border()), 1.0F);

        const ImVec2 headerPos{barMin.x + ToolbarPadX, barMin.y + ToolbarPadY};
        DrawAssetBrowserToolbar(headerPos, std::max(1.0F, contentWidth - OuterPaddingX * 2.0F), viewModel, command, state, context);
        const std::vector<std::size_t> visibleEntries = interactionSession.ProjectEntries(directory);
        if (!selectedAssetPath.empty() &&
            std::ranges::none_of(visibleEntries, [&directory, &selectedAssetPath](const std::size_t entryIndex) {
            return directory.entries[entryIndex].absolutePath == selectedAssetPath;
        })) {
            selectedAssetPath.clear();
        }

        DrawAssetBrowserBackgroundActions(viewModel, interactionSession, command, context);
        cardRenderer.RetainVisible(directory, visibleEntries);

        float gridY = barMax.y + 4.0F;
        if (!viewModel.contentBrowserOperationError.empty()) {
            const std::string &errorText = localization.Get("editor", viewModel.contentBrowserOperationError);
            drawList->AddText(font, HeaderFontSize, {headerPos.x, gridY}, Theme::U32(Theme::Err()), errorText.c_str());
            gridY += PreviewRowHeight + 4.0F;
        }
        if (directory.loadState == ContentBrowserLoadState::Loading) {
            const std::string &loadingText = localization.Get("editor", "workspace.content_browser.loading");
            drawList->AddText(font, HeaderFontSize, {headerPos.x, gridY}, Theme::U32(Theme::Dim()), loadingText.c_str());
            ImGui::SetCursorScreenPos({contentOrigin.x + OuterPaddingX, gridY + PreviewRowHeight});
            ImGui::Dummy({contentWidth, 1.0F});
            return;
        }
        if (directory.loadState == ContentBrowserLoadState::Error) {
            const std::string &unavailableText = localization.Get("editor", "workspace.content_browser.unavailable");
            drawList->AddText(font, HeaderFontSize, {headerPos.x, gridY}, Theme::U32(Theme::Err()), unavailableText.c_str());
            ImGui::SetCursorScreenPos({contentOrigin.x + OuterPaddingX, gridY + PreviewRowHeight});
            ImGui::Dummy({contentWidth, 1.0F});
            return;
        }
        if (visibleEntries.empty()) {
            const std::string &emptyText = localization.Get("editor", directory.entries.empty() ? "workspace.content_browser.empty"
                                                                                                : "workspace.content_browser.no_results");
            drawList->AddText(font, HeaderFontSize, {headerPos.x, gridY}, Theme::U32(Theme::Dim()), emptyText.c_str());
            ImGui::SetCursorScreenPos({contentOrigin.x + OuterPaddingX, gridY + PreviewRowHeight});
            ImGui::Dummy({contentWidth, 1.0F});
            DrawAssetBrowserDialogs(state, directory, command, context);
            return;
        }

        const AssetBrowserGridMetrics metrics = ComputeAssetBrowserGridMetrics(contentWidth);
        for (std::size_t index = 0; index < visibleEntries.size(); ++index) {
            const ContentBrowserEntry &entry = directory.entries[visibleEntries[index]];
            const std::size_t row = index / metrics.columns;
            const std::size_t column = index % metrics.columns;
            const ImVec2 cardMin{
                contentOrigin.x + OuterPaddingX + static_cast<float>(column) * (metrics.cardWidth + CardGap),
                gridY + static_cast<float>(row) * (metrics.cardWidth + CardFooterHeight + CardGap),
            };

            ImGui::PushID(static_cast<int>(index));
            ImGui::SetCursorScreenPos(cardMin);
            if (ImGui::InvisibleButton("##AssetCard", ImVec2{metrics.cardWidth, metrics.cardWidth + CardFooterHeight}))
                interactionSession.Select(entry.absolutePath);
            const bool cardHovered = ImGui::IsItemHovered();
            const bool selected = selectedAssetPath == entry.absolutePath;
            const bool cut = viewModel.contentBrowserClipboard.mode == ContentBrowserClipboardMode::Move &&
                             viewModel.contentBrowserClipboard.absoluteSourcePath == entry.absolutePath;
            const std::optional<std::string> draggedAssetPath = AbsoluteAssetPathFromPayload(ImGui::GetDragDropPayload());
            const bool dragging = draggedAssetPath.has_value() && *draggedAssetPath == entry.absolutePath;
            cardRenderer.Draw(drawList, font, CardFontSize, entry, cardMin, metrics.cardWidth, cardHovered, selected, cut || dragging);
            if (entry.kind == ContentBrowserEntryKind::Directory && cardHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                command = AssetBrowserInteractionSession::Navigate(entry.absolutePath);
            }
            if (entry.kind == ContentBrowserEntryKind::Asset && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload(ContentBrowserAssetPayload, entry.absolutePath.c_str(), entry.absolutePath.size() + 1);
                ImGui::TextUnformatted(entry.displayName.c_str());
                const bool copy = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
                ImGui::TextColored(Theme::Dim(), "%s",
                                   localization
                                       .Get("editor",
                                            copy ? "workspace.content_browser.action.copy" : "workspace.content_browser.action.move_here")
                                       .c_str());
                ImGui::EndDragDropSource();
            }
            if (entry.kind == ContentBrowserEntryKind::Directory && ImGui::BeginDragDropTarget()) {
                const ImGuiPayload *acceptedPayload =
                    ImGui::AcceptDragDropPayload(ContentBrowserAssetPayload,
                                                 ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                if (const std::optional<std::string> source = AbsoluteAssetPathFromPayload(acceptedPayload); source.has_value()) {
                    const bool copy = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
                    const bool validTarget =
                        copy || std::filesystem::path{*source}.parent_path() != std::filesystem::path{entry.absolutePath};
                    drawList->AddRect(cardMin, {cardMin.x + metrics.cardWidth, cardMin.y + metrics.cardWidth + CardFooterHeight},
                                      Theme::U32(validTarget ? Theme::Accent() : Theme::Err()), CardRadius, 0, 2.0F);
                    if (validTarget && acceptedPayload->IsDelivery()) {
                        command = AssetBrowserInteractionSession::Transfer(*source, entry.absolutePath,
                                                                           copy ? ContentBrowserTransferMode::Copy
                                                                                : ContentBrowserTransferMode::Move);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            DrawAssetBrowserEntryActions(entry, viewModel, interactionSession, command, context);
            ImGui::PopID();
        }

        const std::size_t rowCount = (visibleEntries.size() + metrics.columns - 1) / metrics.columns;
        const float gridHeight =
            static_cast<float>(rowCount) * (metrics.cardWidth + CardFooterHeight) + static_cast<float>(rowCount - 1) * CardGap;
        ImGui::SetCursorScreenPos({contentOrigin.x + OuterPaddingX, gridY + gridHeight});
        ImGui::Dummy({contentWidth, 1.0F});
        HandleAssetBrowserShortcuts(visibleEntries, viewModel, interactionSession, command);
        DrawAssetBrowserDialogs(state, directory, command, context);
    }
}  // namespace Horo::Editor
