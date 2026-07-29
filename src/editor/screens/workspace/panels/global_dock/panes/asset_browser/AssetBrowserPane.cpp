#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserPane.h"

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/renderer/EditorGuiRenderer.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserPaneLayout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Horo::Editor {
    namespace {
        constexpr float kOuterPaddingX = 10.0F;
        constexpr float kOuterPaddingY = 8.0F;
        constexpr float kHeaderHeight = 28.0F;
        constexpr float kHeaderToGridGap = 8.0F;
        constexpr float kAssetToolbarHeight = 28.0F;
        constexpr float kAssetToolbarGap = 6.0F;
        constexpr float kAssetTypeFilterWidth = 132.0F;
        constexpr float kAssetSortWidth = 108.0F;
        constexpr float kAssetSortDirectionWidth = 48.0F;
        constexpr float kAssetNavigationButtonSize = 22.0F;
        constexpr float kAssetNavigationWidth = kAssetNavigationButtonSize * 3.0F + 2.0F;
        constexpr float kAssetBreadcrumbGap = 12.0F;
        constexpr float kCardGap = 6.0F;
        constexpr float kMinimumCardWidth = 66.0F;
        constexpr float kCardFooterHeight = 20.0F;
        constexpr float kCardRadius = 3.0F;
        constexpr float kHeaderFontSize = kGlobalDockMinimumFontSize;
        constexpr float kCardFontSize = kGlobalDockMinimumFontSize;
        constexpr float kPreviewRowHeight = 20.0F;
        constexpr char kContentBrowserAssetPayload[] = "HORO_CONTENT_BROWSER_ASSET";

        struct AssetToolbarLayout {
            float gap{0.0F};
            float typeWidth{0.0F};
            float sortWidth{0.0F};
            float directionWidth{0.0F};

            [[nodiscard]] float FixedWidth() const {
                return typeWidth + sortWidth + directionWidth + gap * 3.0F;
            }
        };

        [[nodiscard]] constexpr AssetToolbarLayout ResolveAssetToolbarLayout(const bool compact) {
            if (compact)
                return {3.0F, 52.0F, 52.0F, 38.0F};
            return {kAssetToolbarGap, kAssetTypeFilterWidth, kAssetSortWidth, kAssetSortDirectionWidth};
        }

        enum class AssetGlyph {
            None,
            Folder,
            Mesh,
            Image,
            Audio,
            Generic,
            Prefab,
        };

        struct AssetCardPresentation {
            std::string_view name;
            ImU32 gradientStart;
            ImU32 gradientMid;
            ImU32 gradientEnd;
            AssetGlyph glyph;
        };

        [[nodiscard]] ImFont *ResolveFont(ImFont *preferred) {
            return preferred != nullptr ? preferred : ImGui::GetFont();
        }

        [[nodiscard]] AssetCardPresentation PresentEntry(const ContentBrowserEntry &entry) {
            if (entry.kind == ContentBrowserEntryKind::Directory) {
                return {entry.displayName, IM_COL32(24, 31, 39, 255), IM_COL32(30, 39, 50, 255), IM_COL32(35, 46, 60, 255),
                        AssetGlyph::Folder};
            }
            if (entry.previewFallback == Assets::AssetPreviewFallback::Image) {
                return {entry.displayName, IM_COL32(26, 21, 32, 255), IM_COL32(30, 23, 38, 255), IM_COL32(34, 25, 44, 255),
                        AssetGlyph::Image};
            }
            if (entry.previewFallback == Assets::AssetPreviewFallback::Audio) {
                return {entry.displayName, IM_COL32(28, 26, 18, 255), IM_COL32(33, 31, 22, 255), IM_COL32(38, 35, 26, 255),
                        AssetGlyph::Audio};
            }
            if (entry.assetType.find("prefab") != std::string::npos) {
                return {entry.displayName, IM_COL32(14, 24, 32, 255), IM_COL32(18, 28, 40, 255), IM_COL32(21, 32, 48, 255),
                        AssetGlyph::Prefab};
            }
            if (entry.previewFallback == Assets::AssetPreviewFallback::Mesh) {
                return {entry.displayName, IM_COL32(19, 32, 26, 255), IM_COL32(22, 37, 29, 255), IM_COL32(25, 42, 32, 255),
                        AssetGlyph::Mesh};
            }
            return {entry.displayName, IM_COL32(24, 32, 42, 255), IM_COL32(27, 36, 48, 255), IM_COL32(29, 40, 54, 255),
                    AssetGlyph::Generic};
        }

        void DrawAssetGlyph(ImDrawList *drawList, const AssetGlyph glyph, const ImVec2 center) {
            const ImU32 color = Theme::U32(Theme::Text());
            if (glyph == AssetGlyph::Folder) {
                constexpr float width = 17.0F;
                constexpr float height = 12.0F;
                const ImVec2 folderMin{center.x - width * 0.5F, center.y - height * 0.35F};
                const ImVec2 folderMax{folderMin.x + width, folderMin.y + height};
                drawList->AddRect(folderMin, folderMax, color, 2.0F, ImDrawFlags_RoundCornersAll, 1.5F);
                drawList->AddLine({folderMin.x + 1.5F, folderMin.y}, {folderMin.x + 6.0F, folderMin.y - 3.5F}, color, 1.5F);
                drawList->AddLine({folderMin.x + 6.0F, folderMin.y - 3.5F}, {folderMin.x + 10.0F, folderMin.y - 3.5F}, color, 1.5F);
                drawList->AddLine({folderMin.x + 10.0F, folderMin.y - 3.5F}, {folderMin.x + 12.0F, folderMin.y}, color, 1.5F);
            } else if (glyph == AssetGlyph::Mesh) {
                const std::array front{
                    ImVec2{center.x - 6.0F, center.y - 4.0F},
                    ImVec2{center.x + 2.0F, center.y - 4.0F},
                    ImVec2{center.x + 2.0F, center.y + 4.0F},
                    ImVec2{center.x - 6.0F, center.y + 4.0F},
                };
                const std::array back{
                    ImVec2{center.x - 2.0F, center.y - 7.0F},
                    ImVec2{center.x + 6.0F, center.y - 7.0F},
                    ImVec2{center.x + 6.0F, center.y + 1.0F},
                    ImVec2{center.x - 2.0F, center.y + 1.0F},
                };
                drawList->AddPolyline(front.data(), front.size(), color, ImDrawFlags_Closed, 1.25F);
                drawList->AddPolyline(back.data(), back.size(), color, ImDrawFlags_Closed, 1.25F);
                for (std::size_t index = 0; index < front.size(); ++index)
                    drawList->AddLine(front[index], back[index], color, 1.25F);
            } else if (glyph == AssetGlyph::Image) {
                const ImVec2 minimum{center.x - 8.0F, center.y - 7.0F};
                const ImVec2 maximum{center.x + 8.0F, center.y + 7.0F};
                drawList->AddRect(minimum, maximum, color, 2.0F, ImDrawFlags_RoundCornersAll, 1.3F);
                drawList->AddCircleFilled({center.x + 3.5F, center.y - 2.5F}, 1.7F, color, 10);
                drawList->AddPolyline(
                    std::array{
                        ImVec2{minimum.x + 2.0F, maximum.y - 2.0F},
                        ImVec2{center.x - 2.0F, center.y},
                        ImVec2{center.x + 1.0F, center.y + 3.0F},
                        ImVec2{center.x + 5.0F, center.y - 0.5F},
                        ImVec2{maximum.x - 2.0F, maximum.y - 2.0F},
                    }
                        .data(),
                    5, color, 0, 1.2F);
            } else if (glyph == AssetGlyph::Audio) {
                constexpr std::array heights{4.0F, 8.0F, 12.0F, 7.0F, 10.0F, 5.0F};
                for (std::size_t index = 0; index < heights.size(); ++index) {
                    const float x = center.x - 7.5F + static_cast<float>(index) * 3.0F;
                    drawList->AddLine({x, center.y - heights[index] * 0.5F}, {x, center.y + heights[index] * 0.5F}, color, 1.5F);
                }
            } else if (glyph == AssetGlyph::Generic) {
                const ImVec2 minimum{center.x - 6.0F, center.y - 8.0F};
                const ImVec2 maximum{center.x + 6.0F, center.y + 8.0F};
                drawList->AddRect(minimum, maximum, color, 1.5F, ImDrawFlags_RoundCornersAll, 1.3F);
                drawList->AddLine({center.x + 1.0F, minimum.y}, {maximum.x, center.y - 3.0F}, color, 1.2F);
                drawList->AddLine({maximum.x, center.y - 3.0F}, {center.x + 1.0F, center.y - 3.0F}, color, 1.2F);
            } else if (glyph == AssetGlyph::Prefab) {
                constexpr float halfWidth = 5.2F;
                constexpr float radius = 6.0F;
                const std::array points{
                    ImVec2{center.x, center.y - radius},
                    ImVec2{center.x + halfWidth, center.y - radius * 0.5F},
                    ImVec2{center.x + halfWidth, center.y + radius * 0.5F},
                    ImVec2{center.x, center.y + radius},
                    ImVec2{center.x - halfWidth, center.y + radius * 0.5F},
                    ImVec2{center.x - halfWidth, center.y - radius * 0.5F},
                };
                drawList->AddPolyline(points.data(), points.size(), color, ImDrawFlags_Closed, 1.5F);
            }
        }

        void DrawMeshPreview(ImDrawList *drawList, const ContentBrowserEntry &entry, const ImVec2 previewMin, const ImVec2 previewMax) {
            if (entry.meshPreviewPoints.empty())
                return;

            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float maxY = std::numeric_limits<float>::lowest();
            std::vector<ImVec2> projected;
            projected.reserve(entry.meshPreviewPoints.size());
            for (const ContentBrowserMeshPreviewPoint &point : entry.meshPreviewPoints) {
                const ImVec2 value{
                    point.x * 0.82F + point.z * 0.58F,
                    -point.y + point.x * 0.24F + point.z * 0.18F,
                };
                minX = std::min(minX, value.x);
                minY = std::min(minY, value.y);
                maxX = std::max(maxX, value.x);
                maxY = std::max(maxY, value.y);
                projected.push_back(value);
            }

            const float width = std::max(maxX - minX, 0.0001F);
            const float height = std::max(maxY - minY, 0.0001F);
            const float availableWidth = std::max(1.0F, previewMax.x - previewMin.x - 16.0F);
            const float availableHeight = std::max(1.0F, previewMax.y - previewMin.y - 16.0F);
            const float scale = std::min(availableWidth / width, availableHeight / height);
            const ImVec2 center{(previewMin.x + previewMax.x) * 0.5F, (previewMin.y + previewMax.y) * 0.5F};
            const ImU32 shadow = IM_COL32(0, 0, 0, 70);
            const ImU32 pointColor = Theme::U32(Theme::Text());
            drawList->PushClipRect(previewMin, previewMax, true);
            for (const ImVec2 point : projected) {
                const ImVec2 screen{
                    center.x + (point.x - (minX + maxX) * 0.5F) * scale,
                    center.y + (point.y - (minY + maxY) * 0.5F) * scale,
                };
                drawList->AddCircleFilled({screen.x + 1.0F, screen.y + 1.0F}, 1.35F, shadow, 6);
                drawList->AddCircleFilled(screen, 1.05F, pointColor, 6);
            }
            drawList->PopClipRect();
        }

        void DrawAssetCard(ImDrawList *drawList, ImFont *font, const float fontSize, const ContentBrowserEntry &entry,
                           const AssetCardPresentation &asset, const ImVec2 cardMin, const float cardWidth, const bool hovered,
                           const bool selected, const bool cut, const std::uintptr_t previewTexture) {
            const ImVec2 cardMax{cardMin.x + cardWidth, cardMin.y + cardWidth + kCardFooterHeight};
            const ImVec2 thumbMax{cardMax.x, cardMin.y + cardWidth};

            drawList->AddRectFilled(cardMin, cardMax, Theme::U32(Theme::Bg3()), kCardRadius);
            drawList->AddRectFilled(cardMin, thumbMax, asset.gradientStart, kCardRadius, ImDrawFlags_RoundCornersTop);
            drawList->AddRectFilledMultiColor(ImVec2{cardMin.x + 1.0F, cardMin.y + 1.0F}, ImVec2{thumbMax.x - 1.0F, thumbMax.y},
                                              asset.gradientStart, asset.gradientMid, asset.gradientMid, asset.gradientEnd);
            if (previewTexture != 0) {
                drawList->AddImage(previewTexture, {cardMin.x + 3.0F, cardMin.y + 3.0F}, {thumbMax.x - 3.0F, thumbMax.y - 3.0F});
            } else if (entry.assetType == "core.mesh" && !entry.meshPreviewPoints.empty())
                DrawMeshPreview(drawList, entry, cardMin, thumbMax);
            else
                DrawAssetGlyph(drawList, asset.glyph, ImVec2{cardMin.x + cardWidth * 0.5F, cardMin.y + cardWidth * 0.5F});
            if (hovered) {
                drawList->AddRectFilled(cardMin, thumbMax, IM_COL32(255, 255, 255, 10), kCardRadius, ImDrawFlags_RoundCornersTop);
            }
            if (cut) {
                drawList->AddRectFilled(cardMin, cardMax, IM_COL32(6, 10, 14, 118), kCardRadius);
                drawList->AddRect({cardMin.x + 2.0F, cardMin.y + 2.0F}, {cardMax.x - 2.0F, cardMax.y - 2.0F}, Theme::U32(Theme::Accent()),
                                  kCardRadius, ImDrawFlags_RoundCornersAll, 1.0F);
            }
            drawList->AddLine(ImVec2{cardMin.x, thumbMax.y}, thumbMax, Theme::U32(Theme::Border()), 1.0F);
            drawList->AddRect(cardMin, cardMax,
                              Theme::U32(selected  ? Theme::Accent()
                                         : hovered ? Theme::BorderStrong()
                                                   : Theme::Border()),
                              kCardRadius, ImDrawFlags_RoundCornersAll, selected ? 1.5F : 1.0F);

            const std::string name{asset.name};
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, cardWidth - 8.0F, 0.0F, name.c_str());
            const ImVec2 textPos{cardMin.x + (cardWidth - textSize.x) * 0.5F, thumbMax.y + (kCardFooterHeight - fontSize) * 0.5F - 1.0F};
            const ImVec4 clipRect{cardMin.x + 4.0F, thumbMax.y, cardMax.x - 4.0F, cardMax.y};
            drawList->AddText(font, fontSize, textPos, Theme::U32(Theme::Muted()), name.c_str(), nullptr, 0.0F, &clipRect);
        }

        [[nodiscard]] std::uint64_t PreviewFingerprint(const Assets::AssetPreviewImage &image) {
            std::uint64_t hash = 1469598103934665603ULL;
            const auto mix = [&hash](const std::uint8_t value) {
                hash ^= value;
                hash *= 1099511628211ULL;
            };
            for (unsigned shift = 0; shift < 32; shift += 8) {
                mix(static_cast<std::uint8_t>(image.width >> shift & 0xffU));
                mix(static_cast<std::uint8_t>(image.height >> shift & 0xffU));
            }
            for (const std::uint8_t value : image.pixels)
                mix(value);
            return hash;
        }

        [[nodiscard]] std::uintptr_t ResolvePreviewTexture(
            const ContentBrowserEntry &entry, IEditorGuiRenderer *renderer,
            std::unordered_map<std::string, std::pair<std::uint64_t, std::uintptr_t>> &cache) {
            if (renderer == nullptr || !entry.previewImage.IsValid())
                return 0;
            const std::uint64_t fingerprint = PreviewFingerprint(entry.previewImage);
            if (const auto found = cache.find(entry.absolutePath); found != cache.end()) {
                if (found->second.first == fingerprint)
                    return found->second.second;
                renderer->DestroyTexture(found->second.second);
                cache.erase(found);
            }
            auto uploaded = renderer->CreateTexture(EditorRgba8ImageView{
                .width = entry.previewImage.width,
                .height = entry.previewImage.height,
                .pixels = entry.previewImage.pixels,
            });
            if (uploaded.HasError())
                return 0;
            const std::uintptr_t textureId = std::move(uploaded).Value();
            cache.emplace(entry.absolutePath, std::pair{fingerprint, textureId});
            return textureId;
        }

        [[nodiscard]] float DrawBreadcrumb(const ImVec2 position, const ContentBrowserDirectory &directory,
                                           EditorWorkspaceViewCommandData &command, ImFont *font, const float maximumWidth) {
            if (directory.breadcrumbs.empty() || maximumWidth <= 1.0F)
                return 0.0F;

            const float separatorWidth = font->CalcTextSizeA(kHeaderFontSize, std::numeric_limits<float>::max(), 0.0F, "/").x + 10.0F;
            std::vector<float> segmentWidths;
            segmentWidths.reserve(directory.breadcrumbs.size());
            float naturalWidth = 0.0F;
            for (const ContentBrowserBreadcrumb &segment : directory.breadcrumbs) {
                const float width = font->CalcTextSizeA(kHeaderFontSize, std::numeric_limits<float>::max(), 0.0F, segment.label.c_str()).x;
                segmentWidths.push_back(width);
                naturalWidth += width;
            }
            naturalWidth += separatorWidth * static_cast<float>(directory.breadcrumbs.size() - 1);

            std::size_t firstVisible = 0;
            const bool clipped = naturalWidth > maximumWidth;
            if (clipped) {
                const float ellipsisWidth =
                    font->CalcTextSizeA(kHeaderFontSize, std::numeric_limits<float>::max(), 0.0F, "...").x + separatorWidth;
                float trailingWidth = 0.0F;
                firstVisible = directory.breadcrumbs.size();
                while (firstVisible > 0) {
                    const std::size_t candidate = firstVisible - 1;
                    const float candidateWidth = segmentWidths[candidate] + (trailingWidth > 0.0F ? separatorWidth : 0.0F);
                    if (candidate > 0 && trailingWidth + candidateWidth + ellipsisWidth > maximumWidth) {
                        break;
                    }
                    trailingWidth += candidateWidth;
                    firstVisible = candidate;
                }
                if (firstVisible == directory.breadcrumbs.size())
                    firstVisible = directory.breadcrumbs.size() - 1;
            }

            ImGui::SetCursorScreenPos(position);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
            ImGui::BeginChild("##ContentBrowserBreadcrumb", {std::min(naturalWidth, maximumWidth), kHeaderHeight}, false,
                              ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::SetCursorPosY((kHeaderHeight - ImGui::GetTextLineHeight()) * 0.5F);
            if (clipped && firstVisible > 0) {
                ImGui::TextColored(Theme::Dim(), "...");
                ImGui::SameLine(0.0F, 5.0F);
                ImGui::TextColored(Theme::Dim(), "/");
                ImGui::SameLine(0.0F, 5.0F);
            }
            for (std::size_t index = firstVisible; index < directory.breadcrumbs.size(); ++index) {
                const ContentBrowserBreadcrumb &segment = directory.breadcrumbs[index];
                ImGui::PushID(segment.absolutePath.c_str());
                if (Ui::TextLink("##BreadcrumbLink", segment.label.c_str(), font, kHeaderFontSize,
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

        [[nodiscard]] float DrawAssetNavigationHeader(const ImVec2 position, const EditorWorkspaceViewModel &viewModel,
                                                      EditorWorkspaceViewCommandData &command, ImFont *font, const EditorGuiContext &,
                                                      const float maximumBreadcrumbWidth) {
            constexpr ImVec2 buttonSize{kAssetNavigationButtonSize, kAssetNavigationButtonSize};
            const auto drawNavigationButton = [&command, buttonSize](const char *id, const Ui::NavigationIcon icon, const bool enabled,
                                                                     const EditorWorkspaceViewCommand target) {
                if (Ui::NavigationIconButton(id, icon, buttonSize, enabled))
                    command = AssetBrowserInteractionSession::Navigate(target);
            };

            ImGui::SetCursorScreenPos({position.x, position.y + (kHeaderHeight - buttonSize.y) * 0.5F});
            drawNavigationButton("ContentBrowserBack", Ui::NavigationIcon::Back, viewModel.contentBrowserCanNavigateBack,
                                 EditorWorkspaceViewCommand::NavigateContentBrowserBack);
            ImGui::SameLine(0.0F, 1.0F);
            drawNavigationButton("ContentBrowserForward", Ui::NavigationIcon::Forward, viewModel.contentBrowserCanNavigateForward,
                                 EditorWorkspaceViewCommand::NavigateContentBrowserForward);
            ImGui::SameLine(0.0F, 1.0F);
            drawNavigationButton("ContentBrowserUp", Ui::NavigationIcon::Up,
                                 viewModel.contentBrowser.absoluteCurrentPath != viewModel.contentBrowser.absoluteRootPath,
                                 EditorWorkspaceViewCommand::NavigateContentBrowserUp);

            const float breadcrumbWidth = DrawBreadcrumb({position.x + kAssetNavigationWidth + kAssetBreadcrumbGap, position.y},
                                                         viewModel.contentBrowser, command, font, maximumBreadcrumbWidth);
            return kAssetNavigationWidth + (breadcrumbWidth > 0.0F ? kAssetBreadcrumbGap + breadcrumbWidth : 0.0F);
        }

        void DrawAssetToolbar(const ImVec2 position, const float availableWidth, const AssetToolbarLayout &layout,
                              const ContentBrowserDirectory &directory, std::array<char, 160> &search, std::string &assetTypeFilter,
                              ContentBrowserSortField &sortField, ContentBrowserSortDirection &sortDirection,
                              const EditorGuiContext &context) {
            const float searchWidth = std::max(1.0F, availableWidth - layout.FixedWidth());

            ImGui::SetCursorScreenPos(position);
            static_cast<void>(
                Ui::InputTextControl("##ContentBrowserSearch", search.data(), search.size(), context.theme.fonts, false, searchWidth));
            const bool searchActive = ImGui::IsItemActive();
            const ImVec2 searchMin = ImGui::GetItemRectMin();
            const ImVec2 searchMax = ImGui::GetItemRectMax();
            if (search[0] == '\0' && !searchActive) {
                const std::string &placeholder = context.localization.Get("editor", "workspace.content_browser.search");
                ImFont *font = ResolveFont(context.theme.fonts.sansCompact);
                ImGui::GetWindowDrawList()->AddText(font, kHeaderFontSize,
                                                    {searchMin.x + 10.0F,
                                                     searchMin.y + (searchMax.y - searchMin.y - kHeaderFontSize) * 0.5F},
                                                    Theme::U32(Theme::Dim()), placeholder.c_str());
            }

            std::vector<std::string> typeLabels;
            typeLabels.push_back(context.localization.Get("editor", "workspace.content_browser.filter.all_types"));
            for (const ContentBrowserEntry &entry : directory.entries) {
                if (entry.kind == ContentBrowserEntryKind::Asset && !entry.assetType.empty() &&
                    std::ranges::find(typeLabels, entry.assetType) == typeLabels.end()) {
                    typeLabels.push_back(entry.assetType);
                }
            }
            std::ranges::sort(typeLabels.begin() + 1, typeLabels.end());
            int typeIndex = 0;
            if (!assetTypeFilter.empty()) {
                const auto selected = std::ranges::find(typeLabels, assetTypeFilter);
                if (selected == typeLabels.end())
                    assetTypeFilter.clear();
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
                                 context.theme.fonts, false, kAssetToolbarHeight)) {
                assetTypeFilter = typeIndex == 0 ? std::string{} : typeLabels[static_cast<std::size_t>(typeIndex)];
            }

            const std::array sortLabels{
                context.localization.Get("editor", "workspace.content_browser.sort.name"),
                context.localization.Get("editor", "workspace.content_browser.sort.type"),
            };
            const std::array sortItems{sortLabels[0].c_str(), sortLabels[1].c_str()};
            int sortIndex = sortField == ContentBrowserSortField::Name ? 0 : 1;
            ImGui::SameLine(0.0F, layout.gap);
            ImGui::SetNextItemWidth(layout.sortWidth);
            if (Ui::ComboControl("ContentBrowserSort", &sortIndex, sortItems.data(), sortItems.size(), context.theme.fonts, false,
                                 kAssetToolbarHeight)) {
                sortField = sortIndex == 0 ? ContentBrowserSortField::Name : ContentBrowserSortField::Type;
            }

            ImGui::SameLine(0.0F, layout.gap);
            if (const bool ascending = sortDirection == ContentBrowserSortDirection::Ascending; Ui::Button({
                    .label = ascending ? "A-Z" : "Z-A",
                    .size = {layout.directionWidth, kAssetToolbarHeight},
                    .variant = Ui::ButtonVariant::Secondary,
                    .fontSize = 12.0F,
                    .font = context.theme.fonts.sansCompact,
                    .baseFontSize = Theme::FontPx::SansCompact,
                    .componentSize = Ui::ButtonSize::Small,
                })) {
                sortDirection = ascending ? ContentBrowserSortDirection::Descending : ContentBrowserSortDirection::Ascending;
            }
        }

        [[nodiscard]] std::string FormatByteSize(const std::uintmax_t bytes) {
            if (bytes < 1024U)
                return std::format("{} B", bytes);
            if (bytes < 1024U * 1024U)
                return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
            return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        }

        void PushContentBrowserModalStyle() {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20.0F, 18.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8.0F, 8.0F});
            ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::Bg1());
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::BorderStrong());
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
            ImGui::PushStyleColor(ImGuiCol_Separator, Theme::Border());
            ImVec4 dim = Theme::Bg0();
            dim.w = 0.72F;
            ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, dim);
        }

        void PopContentBrowserModalStyle() {
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(4);
        }

        void DrawModalHeading(const std::string &title, const EditorGuiContext &context) {
            Theme::ScopedTextStyle textStyle(context.theme.fonts.sansEmphasis, 19.0F, Theme::FontPx::SansEmphasis);
            ImGui::TextColored(Theme::Text(), "%s", title.c_str());
            ImGui::Dummy({0.0F, 2.0F});
            ImGui::Separator();
            ImGui::Dummy({0.0F, 4.0F});
        }

        [[nodiscard]] std::optional<std::string> AbsoluteAssetPathFromPayload(const ImGuiPayload *payload) {
            if (payload == nullptr || !payload->IsDataType(kContentBrowserAssetPayload) || payload->Data == nullptr ||
                payload->DataSize <= 1) {
                return std::nullopt;
            }
            const auto *bytes = static_cast<const char *>(payload->Data);
            const std::size_t size = static_cast<std::size_t>(payload->DataSize);
            if (bytes[size - 1] != '\0' || std::strlen(bytes) != size - 1) {
                return std::nullopt;
            }
            const std::filesystem::path path{bytes};
            if (!path.is_absolute())
                return std::nullopt;
            return path.lexically_normal().string();
        }

        [[nodiscard]] bool DrawClosableModalHeading(const std::string &title, const EditorGuiContext &context) {
            constexpr float closeSize = 22.0F;
            const float headerY = ImGui::GetCursorPosY();
            {
                Theme::ScopedTextStyle textStyle(context.theme.fonts.sansEmphasis, 19.0F, Theme::FontPx::SansEmphasis);
                ImGui::TextColored(Theme::Text(), "%s", title.c_str());
            }
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeSize);
            ImGui::SetCursorPosY(headerY);
            const bool close = Ui::IconCloseButton("##CloseAssetInfo", {closeSize, closeSize});
            ImGui::SetCursorPosY(headerY + closeSize + 5.0F);
            ImGui::Separator();
            ImGui::Dummy({0.0F, 2.0F});
            return close;
        }

        void DrawAssetInfoRow(const char *label, const char *value, const EditorGuiContext &context) {
            ImGui::PushID(label);
            if (!ImGui::BeginTable("##AssetInfoRow", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::PopID();
                return;
            }
            const float labelWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.30F, 72.0F, 132.0F);
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            {
                Theme::ScopedTextStyle textStyle(context.theme.fonts.sans, 14.0F, Theme::FontPx::Sans);
                ImGui::TextColored(Theme::Muted(), "%s", label);
            }
            ImGui::TableSetColumnIndex(1);
            {
                Theme::ScopedTextStyle textStyle(context.theme.fonts.sansCompact, 14.0F, Theme::FontPx::SansCompact);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                ImGui::PushTextWrapPos(0.0F);
                ImGui::TextWrapped("%s", value);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
            ImGui::Separator();
            ImGui::PopID();
        }

        void DrawAssetInfoPopup(std::optional<ContentBrowserEntry> &popupEntry, bool &openAssetInfo, const EditorGuiContext &context) {
            constexpr const char *popupId = "##ContentBrowserAssetInfo";
            if (openAssetInfo) {
                ImGui::OpenPopup(popupId);
                openAssetInfo = false;
            }
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            const float popupWidth = std::max(160.0F, std::min(560.0F, viewport->WorkSize.x - 32.0F));
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
            ImGui::SetNextWindowSizeConstraints({popupWidth, 0.0F}, {popupWidth, std::max(240.0F, viewport->WorkSize.y - 48.0F)});
            PushContentBrowserModalStyle();
            if (ImGui::BeginPopupModal(popupId, nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                           ImGuiWindowFlags_NoSavedSettings)) {
                if (DrawClosableModalHeading(context.localization.Get("editor", "workspace.content_browser.info.title"), context)) {
                    ImGui::CloseCurrentPopup();
                }
                if (popupEntry.has_value()) {
                    const ContentBrowserEntry &entry = *popupEntry;
                    const auto &fonts = context.theme.fonts;
                    {
                        Theme::ScopedTextStyle textStyle(fonts.sansEmphasis, 17.0F, Theme::FontPx::SansEmphasis);
                        ImGui::PushTextWrapPos(0.0F);
                        ImGui::TextWrapped("%s", entry.displayName.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::Dummy({0.0F, 2.0F});
                    DrawAssetInfoRow(context.localization.Get("editor", "workspace.content_browser.info.path").c_str(),
                                     entry.absolutePath.c_str(), context);
                    if (entry.kind == ContentBrowserEntryKind::Asset) {
                        const std::string &none = context.localization.Get("editor", "workspace.content_browser.info.none");
                        const std::string size = FormatByteSize(entry.byteSize);
                        DrawAssetInfoRow(context.localization.Get("editor", "workspace.content_browser.info.type").c_str(),
                                         entry.assetType.empty() ? none.c_str() : entry.assetType.c_str(), context);
                        DrawAssetInfoRow(context.localization.Get("editor", "workspace.content_browser.info.asset_id").c_str(),
                                         entry.assetId.empty() ? none.c_str() : entry.assetId.c_str(), context);
                        DrawAssetInfoRow(context.localization.Get("editor", "workspace.content_browser.info.registration").c_str(),
                                         context.localization
                                             .Get("editor", entry.registered ? "workspace.content_browser.info.registered"
                                                                             : "workspace.content_browser.info.unregistered")
                                             .c_str(),
                                         context);
                        DrawAssetInfoRow(context.localization.Get("editor", "workspace.content_browser.info.size").c_str(), size.c_str(),
                                         context);
                        const std::string importer =
                            entry.importerContributionId.empty()
                                ? none
                                : entry.importerContributionId +
                                      (entry.importerVersion.empty() ? std::string{} : " @ " + entry.importerVersion) +
                                      (entry.importerChanged ? " \xE2\x86\x92 " + entry.activeImporterVersion : std::string{});
                        DrawAssetInfoRow(context.localization.Get("editor", "workspace.content_browser.info.importer").c_str(),
                                         importer.c_str(), context);
                        const std::string module =
                            entry.importerModuleId.empty()
                                ? none
                                : entry.importerModuleId +
                                      (entry.importerModuleVersion.empty() ? std::string{} : " @ " + entry.importerModuleVersion) +
                                      (entry.moduleChanged
                                           ? " \xE2\x86\x92 " + entry.activeImporterModuleId + " @ " + entry.activeImporterModuleVersion
                                           : std::string{});
                        DrawAssetInfoRow(context.localization.Get("editor", "workspace.content_browser.info.module").c_str(),
                                         module.c_str(), context);
                        DrawAssetInfoRow(context.localization.Get("editor", "workspace.content_browser.info.source").c_str(),
                                         entry.absoluteImportSourcePath.empty() ? none.c_str() : entry.absoluteImportSourcePath.c_str(),
                                         context);
                        std::string reimportState = context.localization.Get("editor", "workspace.content_browser.info.reimport_manual");
                        if (entry.sourceChanged)
                            reimportState = context.localization.Get("editor", "workspace.content_browser.info.reimport_source_stale");
                        if (entry.importerChanged || entry.moduleChanged) {
                            if (entry.sourceChanged)
                                reimportState += ", ";
                            else
                                reimportState.clear();
                            reimportState +=
                                context.localization.Get("editor", entry.moduleChanged
                                                                       ? "workspace.content_browser.info.reimport_module_changed"
                                                                       : "workspace.content_browser.info.reimport_importer_changed");
                        }
                        DrawAssetInfoRow(context.localization.Get("editor", "workspace.content_browser.info.reimport_status").c_str(),
                                         entry.canReimport ? reimportState.c_str() : none.c_str(), context);
                        std::string lastImport;
                        for (const Assets::AssetImportReason reason : entry.lastImportReasons) {
                            const char *key = nullptr;
                            switch (reason) {
                                case Assets::AssetImportReason::InitialImport:
                                    key = "workspace.content_browser.info.import_reason_initial";
                                    break;
                                case Assets::AssetImportReason::ManualReimport:
                                    key = "workspace.content_browser.info.import_reason_manual";
                                    break;
                                case Assets::AssetImportReason::SourceChanged:
                                    key = "workspace.content_browser.info.reimport_source_changed";
                                    break;
                                case Assets::AssetImportReason::ImporterChanged:
                                    key = "workspace.content_browser.info.reimport_importer_changed";
                                    break;
                                case Assets::AssetImportReason::ModuleChanged:
                                    key = "workspace.content_browser.info.reimport_module_changed";
                                    break;
                            }
                            if (!lastImport.empty())
                                lastImport += ", ";
                            lastImport += context.localization.Get("editor", key);
                        }
                        DrawAssetInfoRow(context.localization.Get("editor", "workspace.content_browser.info.last_import_reason").c_str(),
                                         lastImport.empty() ? none.c_str() : lastImport.c_str(), context);
                        DrawAssetInfoRow(context.localization.Get("editor", "workspace.content_browser.info.metadata").c_str(),
                                         entry.absoluteMetadataPath.empty() ? none.c_str() : entry.absoluteMetadataPath.c_str(), context);
                    }
                }
                ImGui::EndPopup();
            }
            PopContentBrowserModalStyle();
        }

        void DrawRenamePopup(std::optional<ContentBrowserEntry> &popupEntry, std::array<char, 256> &renameBuffer, bool &openRename,
                             EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
            constexpr const char *popupId = "##ContentBrowserRename";
            if (openRename) {
                ImGui::OpenPopup(popupId);
                openRename = false;
            }
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            const float popupWidth = std::max(160.0F, std::min(460.0F, viewport->WorkSize.x - 32.0F));
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
            ImGui::SetNextWindowSizeConstraints({popupWidth, 0.0F}, {popupWidth, viewport->WorkSize.y - 48.0F});
            PushContentBrowserModalStyle();
            if (ImGui::BeginPopupModal(popupId, nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                           ImGuiWindowFlags_NoSavedSettings)) {
                DrawModalHeading(context.localization.Get("editor", "workspace.content_browser.rename.title"), context);
                Ui::FieldLabel(context.localization.Get("editor", "workspace.content_browser.rename.name").c_str(), context.theme.fonts);
                static_cast<void>(
                    Ui::InputTextControl("##ContentBrowserRenameInput", renameBuffer.data(), renameBuffer.size(), context.theme.fonts));
                ImGui::Dummy({0.0F, 12.0F});
                const Ui::ButtonProps cancel{
                    .label = context.localization.Get("editor", "workspace.content_browser.action.cancel").c_str(),
                    .size = {92.0F, 32.0F},
                    .variant = Ui::ButtonVariant::Secondary,
                    .font = context.theme.fonts.sans,
                };
                if (Ui::Button(cancel))
                    ImGui::CloseCurrentPopup();
                ImGui::SameLine();
                const Ui::ButtonProps apply{
                    .label = context.localization.Get("editor", "workspace.content_browser.action.rename").c_str(),
                    .size = {92.0F, 32.0F},
                    .variant = Ui::ButtonVariant::Primary,
                    .enabled = renameBuffer[0] != '\0',
                    .font = context.theme.fonts.sans,
                };
                if (Ui::Button(apply) && popupEntry.has_value()) {
                    command = AssetBrowserInteractionSession::Rename(popupEntry->absolutePath, renameBuffer.data());
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            PopContentBrowserModalStyle();
        }

        void DrawDeletePopup(std::optional<ContentBrowserEntry> &popupEntry, bool &openDeleteConfirmation,
                             EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
            constexpr const char *popupId = "##ContentBrowserDelete";
            if (openDeleteConfirmation) {
                ImGui::OpenPopup(popupId);
                openDeleteConfirmation = false;
            }
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            const float popupWidth = std::max(160.0F, std::min(480.0F, viewport->WorkSize.x - 32.0F));
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
            ImGui::SetNextWindowSizeConstraints({popupWidth, 0.0F}, {popupWidth, viewport->WorkSize.y - 48.0F});
            PushContentBrowserModalStyle();
            if (ImGui::BeginPopupModal(popupId, nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                           ImGuiWindowFlags_NoSavedSettings)) {
                DrawModalHeading(context.localization.Get("editor", "workspace.content_browser.delete.title"), context);
                const std::string &message = context.localization.Get("editor", "workspace.content_browser.delete.message");
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400.0F);
                ImGui::TextColored(Theme::Muted(), "%s", message.c_str());
                ImGui::PopTextWrapPos();
                if (popupEntry.has_value()) {
                    ImGui::Dummy({0.0F, 6.0F});
                    ImGui::TextColored(Theme::Text(), "%s", popupEntry->displayName.c_str());
                    if (popupEntry->kind == ContentBrowserEntryKind::Asset) {
                        const std::string dependencyCount = std::to_string(popupEntry->dependencyCount);
                        DrawAssetInfoRow(context.localization.Get("editor", "workspace.content_browser.delete.dependencies").c_str(),
                                         dependencyCount.c_str(), context);
                        if (popupEntry->dependencyCount > 0) {
                            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400.0F);
                            ImGui::TextColored(Theme::Warn(), "%s",
                                               context.localization.Get("editor", "workspace.content_browser.delete.dependency_warning")
                                                   .c_str());
                            ImGui::PopTextWrapPos();
                        }
                    }
                }
                ImGui::Dummy({0.0F, 12.0F});
                const Ui::ButtonProps cancel{
                    .label = context.localization.Get("editor", "workspace.content_browser.action.cancel").c_str(),
                    .size = {92.0F, 32.0F},
                    .variant = Ui::ButtonVariant::Secondary,
                    .font = context.theme.fonts.sans,
                };
                if (Ui::Button(cancel))
                    ImGui::CloseCurrentPopup();
                ImGui::SameLine();
                const Ui::ButtonProps remove{
                    .label = context.localization.Get("editor", "workspace.content_browser.action.delete").c_str(),
                    .size = {92.0F, 32.0F},
                    .variant = Ui::ButtonVariant::Primary,
                    .font = context.theme.fonts.sans,
                };
                if (Ui::Button(remove) && popupEntry.has_value()) {
                    command = AssetBrowserInteractionSession::Delete(popupEntry->absolutePath);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            PopContentBrowserModalStyle();
        }

        void DrawCreateFolderPopup(std::array<char, 256> &folderBuffer, bool &openCreateFolder, const ContentBrowserDirectory &directory,
                                   EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
            constexpr const char *popupId = "##ContentBrowserCreateFolder";
            if (openCreateFolder) {
                ImGui::OpenPopup(popupId);
                openCreateFolder = false;
            }
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            const float popupWidth = std::max(160.0F, std::min(460.0F, viewport->WorkSize.x - 32.0F));
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
            ImGui::SetNextWindowSizeConstraints({popupWidth, 0.0F}, {popupWidth, viewport->WorkSize.y - 48.0F});
            PushContentBrowserModalStyle();
            if (ImGui::BeginPopupModal(popupId, nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                           ImGuiWindowFlags_NoSavedSettings)) {
                DrawModalHeading(context.localization.Get("editor", "workspace.content_browser.create_folder.title"), context);
                Ui::FieldLabel(context.localization.Get("editor", "workspace.content_browser.create_folder.name").c_str(),
                               context.theme.fonts);
                static_cast<void>(Ui::InputTextControl("##ContentBrowserCreateFolderInput", folderBuffer.data(), folderBuffer.size(),
                                                       context.theme.fonts));
                ImGui::Dummy({0.0F, 12.0F});
                const Ui::ButtonProps cancel{
                    .label = context.localization.Get("editor", "workspace.content_browser.action.cancel").c_str(),
                    .size = {92.0F, 32.0F},
                    .variant = Ui::ButtonVariant::Secondary,
                    .font = context.theme.fonts.sans,
                };
                if (Ui::Button(cancel))
                    ImGui::CloseCurrentPopup();
                ImGui::SameLine();
                const Ui::ButtonProps create{
                    .label = context.localization.Get("editor", "workspace.content_browser.action.create_folder").c_str(),
                    .size = {112.0F, 32.0F},
                    .variant = Ui::ButtonVariant::Primary,
                    .enabled = folderBuffer[0] != '\0',
                    .font = context.theme.fonts.sans,
                };
                if (Ui::Button(create)) {
                    command = AssetBrowserInteractionSession::CreateFolder(directory.absoluteCurrentPath, folderBuffer.data());
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            PopContentBrowserModalStyle();
        }

        void DrawAssetPane(const ImVec2 contentOrigin, const float contentWidth, ImDrawList *drawList, ImFont *font,
                           const EditorWorkspaceViewModel &viewModel, EditorWorkspaceViewCommandData &command,
                           const EditorGuiContext &context, AssetBrowserInteractionSession &interactionSession,
                           IEditorGuiRenderer *guiRenderer,
                           std::unordered_map<std::string, std::pair<std::uint64_t, std::uintptr_t>> &previewTextures) {
            AssetBrowserInteractionState &state = interactionSession.State();
            auto &popupEntry = state.popupEntry;
            auto &renameBuffer = state.renameBuffer;
            auto &openAssetInfo = state.openAssetInfo;
            auto &openRename = state.openRename;
            auto &openDeleteConfirmation = state.openDeleteConfirmation;
            auto &createFolderBuffer = state.createFolderBuffer;
            auto &openCreateFolder = state.openCreateFolder;
            auto &selectedAssetPath = state.selectedAbsolutePath;
            auto &search = state.search;
            auto &assetTypeFilter = state.assetTypeFilter;
            auto &sortField = state.sortField;
            auto &sortDirection = state.sortDirection;
            const ILocalizationService &localization = context.localization;
            const ContentBrowserDirectory &directory = viewModel.contentBrowser;
            const ImVec2 headerPos{contentOrigin.x + kOuterPaddingX, contentOrigin.y + kOuterPaddingY};
            const float toolbarWidth = std::max(1.0F, contentWidth - kOuterPaddingX * 2.0F);
            const bool compactToolbar = toolbarWidth < 640.0F;
            const AssetToolbarLayout toolbarLayout = ResolveAssetToolbarLayout(compactToolbar);
            const float toolbarGap = compactToolbar ? 4.0F : 8.0F;
            const float minimumSearchWidth = compactToolbar ? 1.0F : 120.0F;
            const float maximumBreadcrumbWidth =
                std::max(0.0F, toolbarWidth - kAssetNavigationWidth - toolbarGap - minimumSearchWidth - toolbarLayout.FixedWidth());
            const float navigationAndBreadcrumbWidth =
                DrawAssetNavigationHeader(headerPos, viewModel, command, font, context, maximumBreadcrumbWidth);
            const float toolbarX = headerPos.x + navigationAndBreadcrumbWidth + toolbarGap;
            DrawAssetToolbar({toolbarX, headerPos.y}, std::max(1.0F, toolbarWidth - navigationAndBreadcrumbWidth - toolbarGap),
                             toolbarLayout, directory, search, assetTypeFilter, sortField, sortDirection, context);
            const std::vector<std::size_t> visibleEntries = interactionSession.ProjectEntries(directory);
            if (!selectedAssetPath.empty() &&
                std::ranges::none_of(visibleEntries, [&directory, &selectedAssetPath](const std::size_t entryIndex) {
                return directory.entries[entryIndex].absolutePath == selectedAssetPath;
            })) {
                selectedAssetPath.clear();
            }

            if (Ui::BeginContextWindowMenu("##ContentBrowserBackgroundMenu")) {
                const bool clipboardAvailable = viewModel.contentBrowserClipboard.mode != ContentBrowserClipboardMode::None;
                ImGui::BeginDisabled(!clipboardAvailable);
                if (Ui::ContextMenuItem(localization
                                            .Get("editor", viewModel.contentBrowserClipboard.mode == ContentBrowserClipboardMode::Move
                                                               ? "workspace.content_browser.action.move_here"
                                                               : "workspace.content_browser.action.paste_here")
                                            .c_str(),
                                        "Ctrl+V", context.theme.fonts, Ui::ContextMenuItemTone::Normal, "action.paste")) {
                    command = AssetBrowserInteractionSession::Paste(directory.absoluteCurrentPath);
                }
                ImGui::EndDisabled();
                Ui::ContextMenuSeparator();
                if (Ui::ContextMenuItem(localization.Get("editor", "workspace.content_browser.action.create_folder").c_str(), nullptr,
                                        context.theme.fonts, Ui::ContextMenuItemTone::Normal, "action.create")) {
                    interactionSession.OpenCreateFolder();
                }
                if (Ui::ContextMenuItem(localization.Get("editor", "workspace.content_browser.action.import_here").c_str(), nullptr,
                                        context.theme.fonts, Ui::ContextMenuItemTone::Normal, "action.import")) {
                    command = AssetBrowserInteractionSession::ImportHere(directory.absoluteCurrentPath);
                }
                if (Ui::ContextMenuItem(localization.Get("editor", "workspace.content_browser.action.refresh").c_str(), nullptr,
                                        context.theme.fonts, Ui::ContextMenuItemTone::Normal, "action.refresh")) {
                    command = AssetBrowserInteractionSession::Refresh();
                }
                Ui::EndContextMenu();
            }

            if (guiRenderer != nullptr && !previewTextures.empty()) {
                std::unordered_set<std::string_view> visiblePaths;
                visiblePaths.reserve(visibleEntries.size());
                for (const std::size_t entryIndex : visibleEntries) {
                    visiblePaths.emplace(directory.entries[entryIndex].absolutePath);
                }
                for (auto cached = previewTextures.begin(); cached != previewTextures.end();) {
                    if (visiblePaths.contains(cached->first)) {
                        ++cached;
                        continue;
                    }
                    guiRenderer->DestroyTexture(cached->second.second);
                    cached = previewTextures.erase(cached);
                }
            }

            float gridY = contentOrigin.y + kOuterPaddingY + kHeaderHeight + kHeaderToGridGap;
            if (!viewModel.contentBrowserOperationError.empty()) {
                const std::string &errorText = localization.Get("editor", viewModel.contentBrowserOperationError);
                drawList->AddText(font, kHeaderFontSize, {headerPos.x, gridY}, Theme::U32(Theme::Err()), errorText.c_str());
                gridY += kPreviewRowHeight + 4.0F;
            }
            if (directory.loadState == ContentBrowserLoadState::Loading) {
                const std::string &loadingText = localization.Get("editor", "workspace.content_browser.loading");
                drawList->AddText(font, kHeaderFontSize, {headerPos.x, gridY}, Theme::U32(Theme::Dim()), loadingText.c_str());
                ImGui::SetCursorScreenPos({contentOrigin.x + kOuterPaddingX, gridY + kPreviewRowHeight});
                ImGui::Dummy({contentWidth, 1.0F});
                return;
            }
            if (directory.loadState == ContentBrowserLoadState::Error) {
                const std::string &unavailableText = localization.Get("editor", "workspace.content_browser.unavailable");
                drawList->AddText(font, kHeaderFontSize, {headerPos.x, gridY}, Theme::U32(Theme::Err()), unavailableText.c_str());
                ImGui::SetCursorScreenPos({contentOrigin.x + kOuterPaddingX, gridY + kPreviewRowHeight});
                ImGui::Dummy({contentWidth, 1.0F});
                return;
            }
            if (visibleEntries.empty()) {
                const std::string &emptyText =
                    localization.Get("editor", directory.entries.empty() ? "workspace.content_browser.empty"
                                                                         : "workspace.content_browser.no_results");
                drawList->AddText(font, kHeaderFontSize, {headerPos.x, gridY}, Theme::U32(Theme::Dim()), emptyText.c_str());
                ImGui::SetCursorScreenPos({contentOrigin.x + kOuterPaddingX, gridY + kPreviewRowHeight});
                ImGui::Dummy({contentWidth, 1.0F});
                DrawCreateFolderPopup(createFolderBuffer, openCreateFolder, directory, command, context);
                return;
            }

            const AssetBrowserGridMetrics metrics = ComputeAssetBrowserGridMetrics(contentWidth);
            for (std::size_t index = 0; index < visibleEntries.size(); ++index) {
                const ContentBrowserEntry &entry = directory.entries[visibleEntries[index]];
                const AssetCardPresentation presentation = PresentEntry(entry);
                const std::size_t row = index / metrics.columns;
                const std::size_t column = index % metrics.columns;
                const ImVec2 cardMin{
                    contentOrigin.x + kOuterPaddingX + static_cast<float>(column) * (metrics.cardWidth + kCardGap),
                    gridY + static_cast<float>(row) * (metrics.cardWidth + kCardFooterHeight + kCardGap),
                };

                ImGui::PushID(static_cast<int>(index));
                ImGui::SetCursorScreenPos(cardMin);
                const bool clicked =
                    ImGui::InvisibleButton("##AssetCard", ImVec2{metrics.cardWidth, metrics.cardWidth + kCardFooterHeight});
                if (clicked)
                    interactionSession.Select(entry.absolutePath);
                const bool cardHovered = ImGui::IsItemHovered();
                const std::uintptr_t previewTexture = ResolvePreviewTexture(entry, guiRenderer, previewTextures);
                const bool selected = selectedAssetPath == entry.absolutePath;
                const bool cut = viewModel.contentBrowserClipboard.mode == ContentBrowserClipboardMode::Move &&
                                 viewModel.contentBrowserClipboard.absoluteSourcePath == entry.absolutePath;
                const std::optional<std::string> draggedAssetPath = AbsoluteAssetPathFromPayload(ImGui::GetDragDropPayload());
                const bool dragging = draggedAssetPath.has_value() && *draggedAssetPath == entry.absolutePath;
                DrawAssetCard(drawList, font, kCardFontSize, entry, presentation, cardMin, metrics.cardWidth, cardHovered, selected,
                              cut || dragging, previewTexture);
                if (entry.kind == ContentBrowserEntryKind::Directory && cardHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    command = AssetBrowserInteractionSession::Navigate(entry.absolutePath);
                }
                if (entry.kind == ContentBrowserEntryKind::Asset && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload(kContentBrowserAssetPayload, entry.absolutePath.c_str(), entry.absolutePath.size() + 1);
                    ImGui::TextUnformatted(entry.displayName.c_str());
                    const bool copy = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
                    ImGui::TextColored(Theme::Dim(), "%s",
                                       localization
                                           .Get("editor", copy ? "workspace.content_browser.action.copy"
                                                               : "workspace.content_browser.action.move_here")
                                           .c_str());
                    ImGui::EndDragDropSource();
                }
                if (entry.kind == ContentBrowserEntryKind::Directory && ImGui::BeginDragDropTarget()) {
                    const ImGuiPayload *acceptedPayload =
                        ImGui::AcceptDragDropPayload(kContentBrowserAssetPayload,
                                                     ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                    if (const std::optional<std::string> source = AbsoluteAssetPathFromPayload(acceptedPayload); source.has_value()) {
                        const bool copy = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
                        const bool validTarget =
                            copy || std::filesystem::path{*source}.parent_path() != std::filesystem::path{entry.absolutePath};
                        drawList->AddRect(cardMin, {cardMin.x + metrics.cardWidth, cardMin.y + metrics.cardWidth + kCardFooterHeight},
                                          Theme::U32(validTarget ? Theme::Accent() : Theme::Err()), kCardRadius, 0, 2.0F);
                        if (validTarget && acceptedPayload->IsDelivery()) {
                            command = AssetBrowserInteractionSession::Transfer(*source, entry.absolutePath,
                                                                               copy ? ContentBrowserTransferMode::Copy
                                                                                    : ContentBrowserTransferMode::Move);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (Ui::BeginContextMenu("##ContentBrowserCardMenu")) {
                    if (entry.kind == ContentBrowserEntryKind::Asset) {
                        if (Ui::ContextMenuItem(localization.Get("editor", "workspace.content_browser.action.duplicate").c_str(), "Ctrl+D",
                                                context.theme.fonts, Ui::ContextMenuItemTone::Normal, "action.duplicate")) {
                            command = AssetBrowserInteractionSession::Duplicate(entry.absolutePath);
                        }
                        if (Ui::ContextMenuItem(localization.Get("editor", "workspace.content_browser.action.copy").c_str(), "Ctrl+C",
                                                context.theme.fonts, Ui::ContextMenuItemTone::Normal, "action.copy")) {
                            command = AssetBrowserInteractionSession::Copy(entry.absolutePath);
                        }
                        if (Ui::ContextMenuItem(localization.Get("editor", "workspace.content_browser.action.cut").c_str(), "Ctrl+X",
                                                context.theme.fonts, Ui::ContextMenuItemTone::Normal, "action.cut")) {
                            command = AssetBrowserInteractionSession::Cut(entry.absolutePath);
                        }
                        Ui::ContextMenuSeparator();
                    } else {
                        const bool clipboardAvailable = viewModel.contentBrowserClipboard.mode != ContentBrowserClipboardMode::None;
                        ImGui::BeginDisabled(!clipboardAvailable);
                        if (Ui::ContextMenuItem(localization
                                                    .Get("editor",
                                                         viewModel.contentBrowserClipboard.mode == ContentBrowserClipboardMode::Move
                                                             ? "workspace.content_browser.action.move_here"
                                                             : "workspace.content_browser.action.paste_here")
                                                    .c_str(),
                                                nullptr, context.theme.fonts, Ui::ContextMenuItemTone::Normal, "action.paste")) {
                            command = AssetBrowserInteractionSession::Paste(entry.absolutePath);
                        }
                        ImGui::EndDisabled();
                        if (Ui::ContextMenuItem(localization.Get("editor", "workspace.content_browser.action.import_here").c_str(), nullptr,
                                                context.theme.fonts, Ui::ContextMenuItemTone::Normal, "action.import")) {
                            command = AssetBrowserInteractionSession::ImportHere(entry.absolutePath);
                        }
                        Ui::ContextMenuSeparator();
                    }
                    if (Ui::ContextMenuItem(localization.Get("editor", "workspace.content_browser.action.rename").c_str(), "F2",
                                            context.theme.fonts, Ui::ContextMenuItemTone::Normal, "action.rename")) {
                        interactionSession.OpenRename(entry);
                    }
                    if (Ui::ContextMenuItem(localization.Get("editor", "workspace.content_browser.action.asset_info").c_str(), nullptr,
                                            context.theme.fonts)) {
                        interactionSession.OpenInfo(entry);
                    }
                    if (entry.kind == ContentBrowserEntryKind::Asset) {
                        ImGui::BeginDisabled(!entry.canReimport);
                        if (Ui::ContextMenuItem(localization.Get("editor", "workspace.content_browser.action.reimport").c_str(), nullptr,
                                                context.theme.fonts, Ui::ContextMenuItemTone::Normal, "action.refresh")) {
                            command = AssetBrowserInteractionSession::Reimport(entry.absolutePath);
                        }
                        ImGui::EndDisabled();
                    }
                    if (Ui::ContextMenuItem(localization.Get("editor", "workspace.content_browser.action.reveal").c_str(), nullptr,
                                            context.theme.fonts)) {
                        command = AssetBrowserInteractionSession::Reveal(entry.absolutePath);
                    }
                    if (Ui::ContextMenuItem(localization.Get("editor", "workspace.content_browser.action.copy_path").c_str(), nullptr,
                                            context.theme.fonts)) {
                        ImGui::SetClipboardText(entry.absolutePath.c_str());
                    }
                    Ui::ContextMenuSeparator();
                    if (Ui::ContextMenuItem(localization.Get("editor", "workspace.content_browser.action.delete").c_str(), "Delete",
                                            context.theme.fonts, Ui::ContextMenuItemTone::Danger, "action.delete")) {
                        interactionSession.OpenDelete(entry);
                    }
                    Ui::EndContextMenu();
                }
                ImGui::PopID();
            }

            const std::size_t rowCount = (visibleEntries.size() + metrics.columns - 1) / metrics.columns;
            const float gridHeight =
                static_cast<float>(rowCount) * (metrics.cardWidth + kCardFooterHeight) + static_cast<float>(rowCount - 1) * kCardGap;
            ImGui::SetCursorScreenPos(ImVec2{contentOrigin.x + kOuterPaddingX, gridY + gridHeight});
            ImGui::Dummy(ImVec2{contentWidth, 1.0F});

            const ImGuiIO &io = ImGui::GetIO();
            const bool commandModifier = io.KeyCtrl || io.KeySuper;
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !io.WantTextInput && !ImGui::IsAnyItemActive()) {
                const auto selected = std::ranges::find(visibleEntries, selectedAssetPath, [&directory](const std::size_t entryIndex) {
                    return directory.entries[entryIndex].absolutePath;
                });
                const bool selectedAsset =
                    selected != visibleEntries.end() && directory.entries[*selected].kind == ContentBrowserEntryKind::Asset;
                if (selected != visibleEntries.end() && ImGui::IsKeyPressed(ImGuiKey_F2)) {
                    interactionSession.OpenRename(directory.entries[*selected]);
                } else if (selected != visibleEntries.end() && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                    interactionSession.OpenDelete(directory.entries[*selected]);
                } else if (commandModifier && selectedAsset && ImGui::IsKeyPressed(ImGuiKey_D)) {
                    command = AssetBrowserInteractionSession::Duplicate(selectedAssetPath);
                } else if (commandModifier && selectedAsset && ImGui::IsKeyPressed(ImGuiKey_C)) {
                    command = AssetBrowserInteractionSession::Copy(selectedAssetPath);
                } else if (commandModifier && selectedAsset && ImGui::IsKeyPressed(ImGuiKey_X)) {
                    command = AssetBrowserInteractionSession::Cut(selectedAssetPath);
                } else if (commandModifier && ImGui::IsKeyPressed(ImGuiKey_V) &&
                           viewModel.contentBrowserClipboard.mode != ContentBrowserClipboardMode::None) {
                    command = AssetBrowserInteractionSession::Paste(directory.absoluteCurrentPath);
                } else if (ImGui::IsKeyPressed(ImGuiKey_Escape) &&
                           viewModel.contentBrowserClipboard.mode != ContentBrowserClipboardMode::None) {
                    command = AssetBrowserInteractionSession::CancelClipboard();
                }
            }

            DrawAssetInfoPopup(popupEntry, openAssetInfo, context);
            DrawRenamePopup(popupEntry, renameBuffer, openRename, command, context);
            DrawDeletePopup(popupEntry, openDeleteConfirmation, command, context);
            DrawCreateFolderPopup(createFolderBuffer, openCreateFolder, directory, command, context);
        }
    }  // namespace

    AssetBrowserGridMetrics ComputeAssetBrowserGridMetrics(const float availableWidth) noexcept {
        const float safeWidth = std::max(1.0F, availableWidth);
        const auto columns = static_cast<std::size_t>(std::max(1.0F, std::floor((safeWidth + kCardGap) / (kMinimumCardWidth + kCardGap))));
        const float cardWidth = std::max(1.0F, (safeWidth - kCardGap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
        return {.columns = columns, .cardWidth = cardWidth};
    }

    /** @copydoc AssetBrowserPane::Attach */
    void AssetBrowserPane::Attach(IEditorGuiRenderer *guiRenderer) noexcept {
        m_guiRenderer = guiRenderer;
    }

    /** @copydoc AssetBrowserPane::Detach */
    void AssetBrowserPane::Detach() noexcept {
        if (m_guiRenderer != nullptr) {
            for (const auto &[path, preview] : m_previewTextures) {
                static_cast<void>(path);
                m_guiRenderer->DestroyTexture(preview.second);
            }
        }
        m_previewTextures.clear();
        m_guiRenderer = nullptr;
    }

    /** @copydoc AssetBrowserPane::Draw */
    void AssetBrowserPane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorWorkspaceViewModel &viewModel,
                                EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        ImFont *font = ResolveFont(context.theme.fonts.sansCompact);
        DrawAssetPane(contentOrigin, contentWidth, drawList, font, viewModel, command, context, m_interactionSession, m_guiRenderer,
                      m_previewTextures);
    }
}  // namespace Horo::Editor
