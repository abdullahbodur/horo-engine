#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserToolbar.h"

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserInteractionSession.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserPaneLayout.h"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <string>
#include <vector>

namespace Horo::Editor {
    namespace {
        constexpr float HeaderHeight = 28.0F;
        constexpr float ToolbarHeight = 28.0F;
        constexpr float NavigationButtonSize = 22.0F;
        constexpr float NavigationWidth = NavigationButtonSize * 3.0F + 2.0F;
        constexpr float BreadcrumbGap = 12.0F;
        constexpr float HeaderFontSize = kGlobalDockMinimumFontSize;

        struct ToolbarLayout {
            float gap{0.0F};
            float typeWidth{0.0F};
            float sortWidth{0.0F};
            float directionWidth{0.0F};

            [[nodiscard]] float FixedWidth() const noexcept {
                return typeWidth + sortWidth + directionWidth + gap * 3.0F;
            }
        };

        [[nodiscard]] constexpr ToolbarLayout ResolveLayout(const bool compact) noexcept {
            return compact ? ToolbarLayout{3.0F, 52.0F, 52.0F, 38.0F} : ToolbarLayout{6.0F, 132.0F, 108.0F, 48.0F};
        }

        [[nodiscard]] ImFont *ResolveFont(ImFont *preferred) {
            return preferred != nullptr ? preferred : ImGui::GetFont();
        }

        [[nodiscard]] float DrawBreadcrumb(const ImVec2 position, const ContentBrowserDirectory &directory,
                                           EditorWorkspaceViewCommandData &command, ImFont *font, const float maximumWidth) {
            if (directory.breadcrumbs.empty() || maximumWidth <= 1.0F)
                return 0.0F;

            const float separatorWidth = font->CalcTextSizeA(HeaderFontSize, std::numeric_limits<float>::max(), 0.0F, "/").x + 10.0F;
            std::vector<float> segmentWidths;
            segmentWidths.reserve(directory.breadcrumbs.size());
            float naturalWidth = 0.0F;
            for (const ContentBrowserBreadcrumb &segment : directory.breadcrumbs) {
                const float width = font->CalcTextSizeA(HeaderFontSize, std::numeric_limits<float>::max(), 0.0F, segment.label.c_str()).x;
                segmentWidths.push_back(width);
                naturalWidth += width;
            }
            naturalWidth += separatorWidth * static_cast<float>(directory.breadcrumbs.size() - 1);

            std::size_t firstVisible = 0;
            const bool clipped = naturalWidth > maximumWidth;
            if (clipped) {
                const float ellipsisWidth =
                    font->CalcTextSizeA(HeaderFontSize, std::numeric_limits<float>::max(), 0.0F, "...").x + separatorWidth;
                float trailingWidth = 0.0F;
                firstVisible = directory.breadcrumbs.size();
                while (firstVisible > 0) {
                    const std::size_t candidate = firstVisible - 1;
                    const float candidateWidth = segmentWidths[candidate] + (trailingWidth > 0.0F ? separatorWidth : 0.0F);
                    if (candidate > 0 && trailingWidth + candidateWidth + ellipsisWidth > maximumWidth)
                        break;
                    trailingWidth += candidateWidth;
                    firstVisible = candidate;
                }
                if (firstVisible == directory.breadcrumbs.size())
                    firstVisible = directory.breadcrumbs.size() - 1;
            }

            ImGui::SetCursorScreenPos(position);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
            ImGui::BeginChild("##ContentBrowserBreadcrumb", {std::min(naturalWidth, maximumWidth), HeaderHeight}, false,
                              ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::SetCursorPosY((HeaderHeight - ImGui::GetTextLineHeight()) * 0.5F);
            if (clipped && firstVisible > 0) {
                ImGui::TextColored(Theme::Dim(), "...");
                ImGui::SameLine(0.0F, 5.0F);
                ImGui::TextColored(Theme::Dim(), "/");
                ImGui::SameLine(0.0F, 5.0F);
            }
            for (std::size_t index = firstVisible; index < directory.breadcrumbs.size(); ++index) {
                const ContentBrowserBreadcrumb &segment = directory.breadcrumbs[index];
                ImGui::PushID(segment.absolutePath.c_str());
                if (Ui::TextLink("##BreadcrumbLink", segment.label.c_str(), font, HeaderFontSize,
                                 index + 1 == directory.breadcrumbs.size())) {
                    command = AssetBrowserInteractionSession::Navigate(segment.absolutePath);
                }
                ImGui::PopID();
                if (index + 1 < directory.breadcrumbs.size()) {
                    ImGui::SameLine(0.0F, 5.0F);
                    ImGui::TextColored(Theme::Dim(), "/");
                    ImGui::SameLine(0.0F, 5.0F);
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            return std::min(naturalWidth, maximumWidth);
        }

        [[nodiscard]] float DrawNavigationAndBreadcrumbs(const ImVec2 position, const EditorWorkspaceViewModel &viewModel,
                                                         EditorWorkspaceViewCommandData &command, ImFont *font,
                                                         const float maximumBreadcrumbWidth) {
            constexpr ImVec2 buttonSize{NavigationButtonSize, NavigationButtonSize};
            const auto drawNavigationButton = [&command, buttonSize](const char *id, const Ui::NavigationIcon icon, const bool enabled,
                                                                     const EditorWorkspaceViewCommand target) {
                if (Ui::NavigationIconButton(id, icon, buttonSize, enabled))
                    command = AssetBrowserInteractionSession::Navigate(target);
            };

            ImGui::SetCursorScreenPos({position.x, position.y + (HeaderHeight - buttonSize.y) * 0.5F});
            drawNavigationButton("ContentBrowserBack", Ui::NavigationIcon::Back, viewModel.contentBrowserCanNavigateBack,
                                 EditorWorkspaceViewCommand::NavigateContentBrowserBack);
            ImGui::SameLine(0.0F, 1.0F);
            drawNavigationButton("ContentBrowserForward", Ui::NavigationIcon::Forward, viewModel.contentBrowserCanNavigateForward,
                                 EditorWorkspaceViewCommand::NavigateContentBrowserForward);
            ImGui::SameLine(0.0F, 1.0F);
            drawNavigationButton("ContentBrowserUp", Ui::NavigationIcon::Up,
                                 viewModel.contentBrowser.absoluteCurrentPath != viewModel.contentBrowser.absoluteRootPath,
                                 EditorWorkspaceViewCommand::NavigateContentBrowserUp);

            const float breadcrumbWidth = DrawBreadcrumb({position.x + NavigationWidth + BreadcrumbGap, position.y},
                                                         viewModel.contentBrowser, command, font, maximumBreadcrumbWidth);
            return NavigationWidth + (breadcrumbWidth > 0.0F ? BreadcrumbGap + breadcrumbWidth : 0.0F);
        }

        void DrawQueryControls(const ImVec2 position, const float availableWidth, const ToolbarLayout &layout,
                               const ContentBrowserDirectory &directory, AssetBrowserInteractionState &state,
                               const EditorGuiContext &context) {
            const float searchWidth = std::max(1.0F, availableWidth - layout.FixedWidth());
            ImGui::SetCursorScreenPos(position);
            static_cast<void>(Ui::InputTextControl("##ContentBrowserSearch", state.search.data(), state.search.size(), context.theme.fonts,
                                                   false, searchWidth));
            const bool searchActive = ImGui::IsItemActive();
            const ImVec2 searchMin = ImGui::GetItemRectMin();
            const ImVec2 searchMax = ImGui::GetItemRectMax();
            if (state.search[0] == '\0' && !searchActive) {
                const std::string &placeholder = context.localization.Get("editor", "workspace.content_browser.search");
                ImFont *font = ResolveFont(context.theme.fonts.sansCompact);
                ImGui::GetWindowDrawList()->AddText(font, HeaderFontSize,
                                                    {searchMin.x + 10.0F,
                                                     searchMin.y + (searchMax.y - searchMin.y - HeaderFontSize) * 0.5F},
                                                    Theme::U32(Theme::Dim()), placeholder.c_str());
            }

            std::vector<std::string> typeLabels{
                context.localization.Get("editor", "workspace.content_browser.filter.all_types"),
            };
            for (const ContentBrowserEntry &entry : directory.entries) {
                if (entry.kind == ContentBrowserEntryKind::Asset && !entry.assetType.empty() &&
                    std::ranges::find(typeLabels, entry.assetType) == typeLabels.end()) {
                    typeLabels.push_back(entry.assetType);
                }
            }
            std::ranges::sort(typeLabels.begin() + 1, typeLabels.end());
            int typeIndex = 0;
            if (!state.assetTypeFilter.empty()) {
                const auto selected = std::ranges::find(typeLabels, state.assetTypeFilter);
                if (selected == typeLabels.end())
                    state.assetTypeFilter.clear();
                else
                    typeIndex = static_cast<int>(std::distance(typeLabels.begin(), selected));
            }
            std::vector<const char *> typeItems;
            typeItems.reserve(typeLabels.size());
            for (const std::string &label : typeLabels)
                typeItems.push_back(label.c_str());

            ImGui::SameLine(0.0F, layout.gap);
            ImGui::SetNextItemWidth(layout.typeWidth);
            if (Ui::ComboControl("ContentBrowserTypeFilter", &typeIndex, typeItems.data(), static_cast<int>(typeItems.size()),
                                 context.theme.fonts, false, ToolbarHeight)) {
                state.assetTypeFilter = typeIndex == 0 ? std::string{} : typeLabels[static_cast<std::size_t>(typeIndex)];
            }

            const std::array sortLabels{
                context.localization.Get("editor", "workspace.content_browser.sort.name"),
                context.localization.Get("editor", "workspace.content_browser.sort.type"),
            };
            const std::array sortItems{sortLabels[0].c_str(), sortLabels[1].c_str()};
            int sortIndex = state.sortField == ContentBrowserSortField::Name ? 0 : 1;
            ImGui::SameLine(0.0F, layout.gap);
            ImGui::SetNextItemWidth(layout.sortWidth);
            if (Ui::ComboControl("ContentBrowserSort", &sortIndex, sortItems.data(), static_cast<int>(sortItems.size()),
                                 context.theme.fonts, false, ToolbarHeight)) {
                state.sortField = sortIndex == 0 ? ContentBrowserSortField::Name : ContentBrowserSortField::Type;
            }

            ImGui::SameLine(0.0F, layout.gap);
            const bool ascending = state.sortDirection == ContentBrowserSortDirection::Ascending;
            if (Ui::Button({
                    .label = ascending ? "A-Z" : "Z-A",
                    .size = {layout.directionWidth, ToolbarHeight},
                    .variant = Ui::ButtonVariant::Secondary,
                    .fontSize = 12.0F,
                    .font = context.theme.fonts.sansCompact,
                    .baseFontSize = Theme::FontPx::SansCompact,
                    .componentSize = Ui::ButtonSize::Small,
                })) {
                state.sortDirection = ascending ? ContentBrowserSortDirection::Descending : ContentBrowserSortDirection::Ascending;
            }
        }
    }  // namespace

    /** @copydoc DrawAssetBrowserToolbar */
    void DrawAssetBrowserToolbar(const ImVec2 &position, const float availableWidth, const EditorWorkspaceViewModel &viewModel,
                                 EditorWorkspaceViewCommandData &command, AssetBrowserInteractionState &state,
                                 const EditorGuiContext &context) {
        const float safeWidth = std::max(1.0F, availableWidth);
        const bool compact = safeWidth < 640.0F;
        const ToolbarLayout layout = ResolveLayout(compact);
        const float groupGap = compact ? 4.0F : 8.0F;
        const float minimumSearchWidth = compact ? 1.0F : 120.0F;
        const float maximumBreadcrumbWidth =
            std::max(0.0F, safeWidth - NavigationWidth - groupGap - minimumSearchWidth - layout.FixedWidth());
        ImFont *font = ResolveFont(context.theme.fonts.sansCompact);
        const float navigationWidth = DrawNavigationAndBreadcrumbs(position, viewModel, command, font, maximumBreadcrumbWidth);
        DrawQueryControls({position.x + navigationWidth + groupGap, position.y}, std::max(1.0F, safeWidth - navigationWidth - groupGap),
                          layout, viewModel.contentBrowser, state, context);
    }
}  // namespace Horo::Editor
