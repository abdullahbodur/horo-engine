#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserGrid.h"

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorIcons.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/screens/workspace/AssetSceneDrop.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"
#include "editor/screens/workspace/panels/global_dock/GlobalDockPaneChrome.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserActions.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserCards.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserDialogs.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserInteractionSession.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserPaneLayout.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserToolbar.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <ranges>
#include <string>

namespace Horo::Editor {
    namespace {
        constexpr float CardRadius = 6.0F;

        [[nodiscard]] float HeaderFontSize() {
            return Theme::TextPx::Label();
        }

        [[nodiscard]] float CardFontSize() {
            return Theme::TextPx::Label();
        }

        constexpr float PreviewRowHeight = 20.0F;

        [[nodiscard]] ImFont *ResolveFont(ImFont *preferred) {
            return preferred != nullptr ? preferred : ImGui::GetFont();
        }

        [[nodiscard]] std::optional<AssetSceneDragPayload> AssetReferenceFromPayload(const ImGuiPayload *payload) {
            if (payload == nullptr || !payload->IsDataType(AssetSceneDragPayloadType) || payload->Data == nullptr ||
                payload->DataSize != sizeof(AssetSceneDragPayload)) {
                return std::nullopt;
            }
            AssetSceneDragPayload reference;
            std::memcpy(&reference, payload->Data, sizeof(reference));
            if (reference.absolutePath.back() != '\0')
                return std::nullopt;
            return reference;
        }

        [[nodiscard]] std::optional<std::string> AbsoluteAssetPathFromPayload(const ImGuiPayload *payload) {
            const std::optional<AssetSceneDragPayload> reference = AssetReferenceFromPayload(payload);
            if (!reference.has_value())
                return std::nullopt;
            const std::filesystem::path path{reference->absolutePath.data()};
            if (!path.is_absolute())
                return std::nullopt;
            return path.lexically_normal().string();
        }

        void HandleDirectoryDragDropTarget(const ContentBrowserEntry &entry, const ImVec2 &cardMin, const float cardWidth,
                                           const float cardHeight, ImDrawList *drawList, EditorWorkspaceViewCommandData &command) {
            if (entry.kind != ContentBrowserEntryKind::Directory || !ImGui::BeginDragDropTarget()) {
                return;
            }
            const ImGuiPayload *acceptedPayload =
                ImGui::AcceptDragDropPayload(AssetSceneDragPayloadType,
                                             ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            if (const std::optional<std::string> source = AbsoluteAssetPathFromPayload(acceptedPayload); source.has_value()) {
                const bool copy = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
                const bool validTarget = copy || std::filesystem::path{*source}.parent_path() != std::filesystem::path{entry.absolutePath};
                drawList->AddRect(cardMin, {cardMin.x + cardWidth, cardMin.y + cardHeight},
                                  Theme::U32(validTarget ? Theme::Accent() : Theme::Err()), CardRadius, 0, 2.0F);
                if (validTarget && acceptedPayload->IsDelivery()) {
                    command = AssetBrowserInteractionSession::Transfer(*source, entry.absolutePath,
                                                                       copy ? ContentBrowserTransferMode::Copy
                                                                            : ContentBrowserTransferMode::Move);
                }
            }
            ImGui::EndDragDropTarget();
        }

        void HandleAssetDragDropSource(const ContentBrowserEntry &entry, const ILocalizationService &localization) {
            if (entry.kind != ContentBrowserEntryKind::Asset || !ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                return;
            }
            const AssetSceneDragPayload payload =
                MakeAssetSceneDragPayload(entry.assetId, entry.assetType, entry.absolutePath, entry.registered);
            ImGui::SetDragDropPayload(AssetSceneDragPayloadType, &payload, sizeof(payload));
            ImGui::TextUnformatted(entry.displayName.c_str());
            const bool copy = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
            ImGui::TextColored(Theme::Dim(), "%s",
                               localization
                                   .Get("editor",
                                        copy ? "workspace.content_browser.action.copy" : "workspace.content_browser.action.move_here")
                                   .c_str());
            ImGui::EndDragDropSource();
        }

        void DrawAssetLocationRail(const ImVec2 minimum, const float height, const EditorGuiContext &context) {
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const float width = AssetBrowserLayout::LocationRailWidth;
            drawList->AddRectFilled(minimum, {minimum.x + width, minimum.y + height}, Theme::U32(Theme::Bg1()));
            drawList->AddLine({minimum.x + width - 1.0F, minimum.y}, {minimum.x + width - 1.0F, minimum.y + height},
                              Theme::U32(Theme::Border()));
            const std::array icons{Ui::UiIcon::Favorite, Ui::UiIcon::History,     Ui::UiIcon::Storage,
                                   Ui::UiIcon::Package,  Ui::UiIcon::AccountTree, Ui::UiIcon::Tag};
            const std::array keys{"workspace.content_browser.location.favorites",    "workspace.content_browser.location.recent",
                                  "workspace.content_browser.location.sources",      "workspace.content_browser.location.packages",
                                  "workspace.content_browser.location.dependencies", "workspace.content_browser.location.tags"};
            for (std::size_t index = 0; index < icons.size(); ++index) {
                const ImVec2 position{minimum.x, minimum.y + 6.0F + static_cast<float>(index) * 32.0F};
                ImGui::SetCursorScreenPos(position);
                ImGui::PushID(static_cast<int>(index));
                static_cast<void>(ImGui::InvisibleButton("##location", {width - 1.0F, 30.0F}));
                const bool hovered = ImGui::IsItemHovered();
                const bool active = index == 0;
                if (active || hovered) {
                    ImVec4 fill = active ? Theme::Accent() : Theme::Text();
                    fill.w = active ? 0.055F : 0.025F;
                    drawList->AddRectFilled(position, {position.x + width - 1.0F, position.y + 30.0F}, Theme::U32(fill));
                }
                if (active)
                    drawList->AddRectFilled({position.x, position.y + 3.0F}, {position.x + 2.0F, position.y + 27.0F},
                                            Theme::U32(Theme::Accent()), 0.0F);
                constexpr float iconSize = 16.0F;
                Ui::DrawEditorIcon(drawList, icons[index], {position.x + (width - iconSize) * 0.5F, position.y + (30.0F - iconSize) * 0.5F},
                                   {iconSize, iconSize}, Theme::U32(active || hovered ? Theme::Text() : Theme::Dim()),
                                   context.theme.fonts.icon);
                if (hovered)
                    Ui::ShowTooltip(context.localization.Get("editor", keys[index]).c_str(), &context.theme.fonts);
                ImGui::PopID();
            }
        }

        [[nodiscard]] std::string EntrySecondaryText(const ContentBrowserEntry &entry, const ILocalizationService &localization) {
            if (entry.kind == ContentBrowserEntryKind::Directory) {
                const std::string &unit =
                    localization.Get("editor", entry.containedItemCount == 1 ? "workspace.content_browser.count.item"
                                                                             : "workspace.content_browser.count.items");
                return std::to_string(entry.containedItemCount) + " " + unit;
            }
            return entry.assetType;
        }

        void DrawAssetFooter(const ImVec2 minimum, const float width, const std::size_t visibleCount,
                             const ContentBrowserDirectory &directory, const ILocalizationService &localization, ImFont *font) {
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const ImVec2 maximum{minimum.x + width, minimum.y + AssetBrowserLayout::FooterHeight};
            drawList->AddRectFilled(minimum, maximum, Theme::U32(Theme::Mix(Theme::Bg0(), Theme::Bg1(), 0.55F)));
            drawList->AddLine(minimum, {maximum.x, minimum.y}, Theme::U32(Theme::Border()));
            const std::size_t directoryCount =
                static_cast<std::size_t>(std::ranges::count_if(directory.entries, [](const ContentBrowserEntry &entry) {
                return entry.kind == ContentBrowserEntryKind::Directory;
            }));
            const bool onlyDirectories = visibleCount == directoryCount && visibleCount == directory.entries.size();
            const std::string &unit =
                localization.Get("editor", onlyDirectories ? (visibleCount == 1 ? "workspace.content_browser.count.folder"
                                                                                : "workspace.content_browser.count.folders")
                                                           : (visibleCount == 1 ? "workspace.content_browser.count.item"
                                                                                : "workspace.content_browser.count.items"));
            const std::string count = std::to_string(visibleCount) + " " + unit;
            drawList->AddText(font, AssetBrowserLayout::SecondaryFontSize(), {minimum.x + 16.0F, minimum.y + 7.0F},
                              Theme::U32(Theme::Dim()), count.c_str());

            const char *statusKey = directory.loadState == ContentBrowserLoadState::Loading ? "workspace.content_browser.loading"
                                    : directory.loadState == ContentBrowserLoadState::Error ? "workspace.content_browser.unavailable"
                                                                                            : "workspace.content_browser.ready";
            const std::string &status = localization.Get("editor", statusKey);
            const ImVec2 statusSize = font->CalcTextSizeA(AssetBrowserLayout::SecondaryFontSize(), FLT_MAX, 0.0F, status.c_str());
            const float dotX = maximum.x - 16.0F;
            drawList->AddText(font, AssetBrowserLayout::SecondaryFontSize(), {dotX - 7.0F - statusSize.x, minimum.y + 7.0F},
                              Theme::U32(Theme::Dim()), status.c_str());
            if (directory.loadState == ContentBrowserLoadState::Ready) {
                drawList->AddCircleFilled({dotX, minimum.y + 14.0F}, 4.0F, Theme::U32(Theme::Ok()), 16);
            }
        }
    }  // namespace

    AssetBrowserGridMetrics ComputeAssetBrowserGridMetrics(const float availableWidth) noexcept {
        const float safeWidth = std::max(1.0F, availableWidth);
        const float track = AssetBrowserLayout::CardMaximumWidth + AssetBrowserLayout::GridGap;
        const auto columns = static_cast<std::size_t>(std::max(1.0F, std::floor((safeWidth + AssetBrowserLayout::GridGap) / track)));
        const float cardWidth = std::min(safeWidth, AssetBrowserLayout::CardMaximumWidth);
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
        const float contentHeight = std::max(1.0F, ImGui::GetContentRegionAvail().y);
        const float toolbarHeight = AssetBrowserLayout::ToolbarHeight;
        const float footerHeight = AssetBrowserLayout::FooterHeight;
        const float bodyHeight = std::max(1.0F, contentHeight - toolbarHeight - footerHeight);
        const ImVec2 contentMaximum{contentOrigin.x + contentWidth, contentOrigin.y + contentHeight};
        const ImVec2 toolbarMaximum{contentMaximum.x, contentOrigin.y + toolbarHeight};
        const ImVec2 bodyOrigin{contentOrigin.x, toolbarMaximum.y};
        const ImVec2 footerOrigin{contentOrigin.x, contentMaximum.y - footerHeight};

        drawList->AddRectFilled(contentOrigin, contentMaximum, Theme::U32(Theme::BottomDockContentSurface()));
        DrawGlobalDockToolbarSurface(contentOrigin, contentWidth, toolbarHeight);
        const ImVec2 toolbarPosition{contentOrigin.x + AssetBrowserLayout::ToolbarPaddingX,
                                     contentOrigin.y + (toolbarHeight - AssetBrowserLayout::ToolbarControlHeight) * 0.5F};
        DrawAssetBrowserToolbar(toolbarPosition, std::max(1.0F, contentWidth - AssetBrowserLayout::ToolbarPaddingX * 2.0F), viewModel,
                                command, state, context);

        const std::vector<std::size_t> visibleEntries = interactionSession.ProjectEntries(directory);
        if (!selectedAssetPath.empty() &&
            std::ranges::none_of(visibleEntries, [&directory, &selectedAssetPath](const std::size_t entryIndex) {
            return directory.entries[entryIndex].absolutePath == selectedAssetPath;
        })) {
            selectedAssetPath.clear();
        }
        cardRenderer.RetainVisible(directory, visibleEntries);

        DrawAssetLocationRail(bodyOrigin, bodyHeight, context);
        const ImVec2 gridViewportOrigin{bodyOrigin.x + AssetBrowserLayout::LocationRailWidth, bodyOrigin.y};
        const float gridViewportWidth = std::max(1.0F, contentWidth - AssetBrowserLayout::LocationRailWidth);
        ImGui::SetCursorScreenPos(gridViewportOrigin);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 5.0F);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BottomDockContentSurface());
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, Theme::BorderStrong());
        ImGui::BeginChild("##AssetGridViewport", {gridViewportWidth, bodyHeight}, false,
                          ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoSavedSettings);
        ImDrawList *gridDrawList = ImGui::GetWindowDrawList();
        const ImVec2 gridOrigin{gridViewportOrigin.x + AssetBrowserLayout::GridPaddingX,
                                gridViewportOrigin.y + AssetBrowserLayout::GridPaddingTop};
        const float gridWidth = std::max(1.0F, gridViewportWidth - AssetBrowserLayout::GridPaddingX * 2.0F - 5.0F);
        float gridY = gridOrigin.y;
        if (!viewModel.contentBrowserOperationError.empty()) {
            const std::string &errorText = localization.Get("editor", viewModel.contentBrowserOperationError);
            gridDrawList->AddText(font, HeaderFontSize(), {gridOrigin.x, gridY}, Theme::U32(Theme::Err()), errorText.c_str());
            gridY += PreviewRowHeight + 4.0F;
        }

        bool drawEntries = directory.loadState == ContentBrowserLoadState::Ready && !visibleEntries.empty();
        if (!drawEntries) {
            const char *messageKey = directory.loadState == ContentBrowserLoadState::Loading ? "workspace.content_browser.loading"
                                     : directory.loadState == ContentBrowserLoadState::Error ? "workspace.content_browser.unavailable"
                                     : directory.entries.empty()                             ? "workspace.content_browser.empty"
                                                                                             : "workspace.content_browser.no_results";
            gridDrawList->AddText(font, HeaderFontSize(), {gridOrigin.x, gridY},
                                  Theme::U32(directory.loadState == ContentBrowserLoadState::Error ? Theme::Err() : Theme::Dim()),
                                  localization.Get("editor", messageKey).c_str());
            ImGui::SetCursorScreenPos({gridOrigin.x, gridY + PreviewRowHeight});
            ImGui::Dummy({gridWidth, 1.0F});
        } else {
            const bool listView = state.viewMode == AssetBrowserViewMode::List;
            const AssetBrowserGridMetrics metrics = ComputeAssetBrowserGridMetrics(gridWidth);
            const std::size_t columns = listView ? 1U : metrics.columns;
            const float cardWidth = listView ? std::min(720.0F, std::max(260.0F, gridWidth)) : metrics.cardWidth;
            const float cardHeight = listView ? 48.0F : AssetBrowserLayout::CardHeight;
            const float previewWidth = listView ? 54.0F : cardWidth;
            const float previewHeight = listView ? 47.0F : AssetBrowserLayout::CardPreviewHeight;
            const float gap = listView ? 5.0F : AssetBrowserLayout::GridGap;
            for (std::size_t index = 0; index < visibleEntries.size(); ++index) {
                const ContentBrowserEntry &entry = directory.entries[visibleEntries[index]];
                const std::size_t row = index / columns;
                const std::size_t column = index % columns;
                const ImVec2 interactionMin{gridOrigin.x + static_cast<float>(column) * (cardWidth + gap),
                                            gridY + static_cast<float>(row) * (cardHeight + gap)};

                ImGui::PushID(static_cast<int>(index));
                ImGui::SetCursorScreenPos(interactionMin);
                if (ImGui::InvisibleButton("##AssetCard", {cardWidth, cardHeight}))
                    interactionSession.Select(entry.absolutePath);
                const bool cardHovered = ImGui::IsItemHovered();
                const bool selected = selectedAssetPath == entry.absolutePath;
                const bool cut = viewModel.contentBrowserClipboard.mode == ContentBrowserClipboardMode::Move &&
                                 viewModel.contentBrowserClipboard.absoluteSourcePath == entry.absolutePath;
                const std::optional<std::string> draggedAssetPath = AbsoluteAssetPathFromPayload(ImGui::GetDragDropPayload());
                const bool dragging = draggedAssetPath.has_value() && *draggedAssetPath == entry.absolutePath;
                const ImVec2 drawMin{interactionMin.x, interactionMin.y - (cardHovered ? 1.0F : 0.0F)};
                const std::string secondaryText = EntrySecondaryText(entry, localization);
                cardRenderer.Draw({.drawList = gridDrawList,
                                   .font = font,
                                   .iconFont = context.theme.fonts.icon,
                                   .fontSize = CardFontSize(),
                                   .cardMin = drawMin,
                                   .cardWidth = cardWidth,
                                   .cardHeight = cardHeight,
                                   .previewWidth = previewWidth,
                                   .previewHeight = previewHeight,
                                   .secondaryText = secondaryText,
                                   .listView = listView,
                                   .hovered = cardHovered,
                                   .selected = selected,
                                   .dimmed = cut || dragging},
                                  entry);
                if (entry.kind == ContentBrowserEntryKind::Directory && cardHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    command = AssetBrowserInteractionSession::Navigate(entry.absolutePath);
                }
                HandleAssetDragDropSource(entry, localization);
                HandleDirectoryDragDropTarget(entry, interactionMin, cardWidth, cardHeight, gridDrawList, command);
                DrawAssetBrowserEntryActions(entry, viewModel, interactionSession, command, context);
                ImGui::PopID();
            }

            const std::size_t rowCount = (visibleEntries.size() + columns - 1U) / columns;
            const float gridHeight = static_cast<float>(rowCount) * cardHeight + static_cast<float>(rowCount - 1U) * gap;
            ImGui::SetCursorScreenPos({gridOrigin.x, gridY + gridHeight + AssetBrowserLayout::GridPaddingBottom});
            ImGui::Dummy({gridWidth, 1.0F});
            HandleAssetBrowserShortcuts(visibleEntries, viewModel, interactionSession, command);
        }
        DrawAssetBrowserBackgroundActions(viewModel, interactionSession, command, context);
        ImGui::EndChild();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        DrawAssetFooter(footerOrigin, contentWidth, visibleEntries.size(), directory, localization, font);
        DrawAssetBrowserDialogs(state, directory, command, context);
    }
}  // namespace Horo::Editor
