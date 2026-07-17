#include "ContentBrowserPanel.h"
#include "ContentBrowserPanelLayout.h"

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Horo::Editor
{
    namespace
    {
        constexpr float kTabHeight = 28.0F;
        constexpr float kOuterPaddingX = 10.0F;
        constexpr float kOuterPaddingY = 8.0F;
        constexpr float kHeaderHeight = 18.0F;
        constexpr float kHeaderToGridGap = 8.0F;
        constexpr float kCardGap = 6.0F;
        constexpr float kMinimumCardWidth = 66.0F;
        constexpr float kCardFooterHeight = 20.0F;
        constexpr float kCardRadius = 3.0F;
        constexpr float kHeaderFontSize = kGlobalDockMinimumFontSize;
        constexpr float kCardFontSize = kGlobalDockMinimumFontSize;
        constexpr float kPreviewRowHeight = 20.0F;

        enum class AssetGlyph
        {
            None,
            Prefab,
            Play,
        };

        enum class PreviewTone
        {
            Normal,
            Dim,
            Warning,
            Error,
        };

        struct AssetCardPresentation
        {
            const char* name;
            ImU32 gradientStart;
            ImU32 gradientMid;
            ImU32 gradientEnd;
            AssetGlyph glyph;
        };

        struct PreviewRow
        {
            const char* label;
            const char* valueKey;
            PreviewTone tone{PreviewTone::Normal};
        };

        constexpr std::array kAssets{
            AssetCardPresentation{
                "M Floor", IM_COL32(24, 32, 42, 255), IM_COL32(27, 36, 48, 255), IM_COL32(29, 40, 54, 255),
                AssetGlyph::None
            },
            AssetCardPresentation{
                "SM Crate", IM_COL32(19, 32, 26, 255), IM_COL32(22, 37, 29, 255), IM_COL32(25, 42, 32, 255),
                AssetGlyph::None
            },
            AssetCardPresentation{
                "T Wall", IM_COL32(26, 21, 32, 255), IM_COL32(30, 23, 38, 255), IM_COL32(34, 25, 44, 255),
                AssetGlyph::None
            },
            AssetCardPresentation{
                "S Music", IM_COL32(28, 26, 18, 255), IM_COL32(33, 31, 22, 255), IM_COL32(38, 35, 26, 255),
                AssetGlyph::None
            },
            AssetCardPresentation{
                "PF Enemy", IM_COL32(14, 24, 32, 255), IM_COL32(18, 28, 40, 255), IM_COL32(21, 32, 48, 255),
                AssetGlyph::Prefab
            },
            AssetCardPresentation{
                "AS Run", IM_COL32(32, 20, 20, 255), IM_COL32(38, 24, 24, 255), IM_COL32(44, 28, 28, 255),
                AssetGlyph::Play
            },
        };

        constexpr std::array kConsoleRows{
            PreviewRow{"12:04:31", "workspace.global_dock.console.session_started"},
            PreviewRow{"12:04:33", "workspace.global_dock.console.texture_converted", PreviewTone::Warning},
            PreviewRow{"12:04:33", "workspace.global_dock.console.scene_loaded"},
            PreviewRow{"12:04:47", "workspace.global_dock.console.missing_animation", PreviewTone::Error},
            PreviewRow{"12:07:02", "workspace.global_dock.console.selection"},
        };

        constexpr std::array kMcpRows{
            PreviewRow{"BRIDGE", "workspace.global_dock.mcp.bridge"},
            PreviewRow{"TOOLS", "workspace.global_dock.mcp.tools"},
            PreviewRow{"", "workspace.global_dock.mcp.awaiting", PreviewTone::Dim},
        };

        constexpr std::array kPerformanceRows{
            PreviewRow{"GPU", "workspace.global_dock.performance.gpu"},
            PreviewRow{"CPU", "workspace.global_dock.performance.cpu"},
            PreviewRow{"MEM", "workspace.global_dock.performance.memory"},
        };

        constexpr std::array kPhysicsRows{
            PreviewRow{"SOLVER", "workspace.global_dock.physics.solver"},
            PreviewRow{"LAYERS", "workspace.global_dock.physics.layers"},
            PreviewRow{"MEM", "workspace.global_dock.physics.memory"},
        };

        constexpr std::array kAudioRows{
            PreviewRow{"MASTER", "workspace.global_dock.audio.master"},
            PreviewRow{"BUSSES", "workspace.global_dock.audio.busses"},
            PreviewRow{"DEVICE", "workspace.global_dock.audio.device"},
        };

        constexpr std::array kNetworkRows{
            PreviewRow{"PING", "workspace.global_dock.network.ping"},
            PreviewRow{"REP", "workspace.global_dock.network.replication"},
            PreviewRow{"CONN", "workspace.global_dock.network.connection"},
        };

        constexpr std::array kLocalizationRows{
            PreviewRow{"LOCALE", "workspace.global_dock.localization.locale"},
            PreviewRow{"STRINGS", "workspace.global_dock.localization.strings"},
            PreviewRow{"FONTS", "workspace.global_dock.localization.fonts"},
        };

        [[nodiscard]] ImFont* ResolveFont(ImFont* preferred)
        {
            return preferred != nullptr ? preferred : ImGui::GetFont();
        }

        [[nodiscard]] ImU32 PreviewColor(const PreviewTone tone)
        {
            switch (tone)
            {
            case PreviewTone::Normal:
                return Theme::U32(Theme::Muted());
            case PreviewTone::Dim:
                return Theme::U32(Theme::Dim());
            case PreviewTone::Warning:
                return Theme::U32(Theme::Warn());
            case PreviewTone::Error:
                return Theme::U32(Theme::Err());
            }
            return Theme::U32(Theme::Muted());
        }

        void DrawAssetGlyph(ImDrawList* drawList, const AssetGlyph glyph, const ImVec2 center)
        {
            const ImU32 color = Theme::U32(Theme::Text());
            if (glyph == AssetGlyph::Prefab)
            {
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
                drawList->AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_Closed, 1.5F);
            }
            else if (glyph == AssetGlyph::Play)
            {
                drawList->AddTriangleFilled(ImVec2{center.x - 4.0F, center.y - 6.0F},
                                            ImVec2{center.x - 4.0F, center.y + 6.0F},
                                            ImVec2{center.x + 6.0F, center.y}, color);
            }
        }

        void DrawAssetCard(ImDrawList* drawList, ImFont* font, const float fontSize, const AssetCardPresentation& asset,
                           const ImVec2 cardMin, const float cardWidth, const bool hovered)
        {
            const ImVec2 cardMax{cardMin.x + cardWidth, cardMin.y + cardWidth + kCardFooterHeight};
            const ImVec2 thumbMax{cardMax.x, cardMin.y + cardWidth};

            drawList->AddRectFilled(cardMin, cardMax, Theme::U32(Theme::Bg3()), kCardRadius);
            drawList->AddRectFilled(cardMin, thumbMax, asset.gradientStart, kCardRadius, ImDrawFlags_RoundCornersTop);
            drawList->AddRectFilledMultiColor(ImVec2{cardMin.x + 1.0F, cardMin.y + 1.0F},
                                              ImVec2{thumbMax.x - 1.0F, thumbMax.y},
                                              asset.gradientStart, asset.gradientMid, asset.gradientMid,
                                              asset.gradientEnd);
            if (hovered)
            {
                drawList->AddRectFilled(cardMin, thumbMax, IM_COL32(255, 255, 255, 10), kCardRadius,
                                        ImDrawFlags_RoundCornersTop);
            }

            DrawAssetGlyph(drawList, asset.glyph, ImVec2{cardMin.x + cardWidth * 0.5F, cardMin.y + cardWidth * 0.5F});
            drawList->AddLine(ImVec2{cardMin.x, thumbMax.y}, thumbMax, Theme::U32(Theme::Border()), 1.0F);
            drawList->AddRect(cardMin, cardMax, Theme::U32(hovered ? Theme::BorderStrong() : Theme::Border()),
                              kCardRadius,
                              ImDrawFlags_RoundCornersAll, 1.0F);

            const ImVec2 textSize = font->CalcTextSizeA(fontSize, cardWidth - 8.0F, 0.0F, asset.name);
            const ImVec2 textPos{
                cardMin.x + (cardWidth - textSize.x) * 0.5F,
                thumbMax.y + (kCardFooterHeight - fontSize) * 0.5F - 1.0F
            };
            const ImVec4 clipRect{cardMin.x + 4.0F, thumbMax.y, cardMax.x - 4.0F, cardMax.y};
            drawList->AddText(font, fontSize, textPos, Theme::U32(Theme::Muted()), asset.name, nullptr, 0.0F,
                              &clipRect);
        }

        void DrawPreviewHeader(ImDrawList* drawList, ImFont* font, const ImVec2 contentOrigin,
                               const ILocalizationService& localization, const char* descriptionKey)
        {
            const std::string& embedded = localization.Get("editor", "workspace.content_browser.embedded");
            const std::string& description = localization.Get("editor", descriptionKey);
            const ImVec2 headerPos{contentOrigin.x + kOuterPaddingX, contentOrigin.y + kOuterPaddingY};
            drawList->AddText(font, kHeaderFontSize, headerPos, Theme::U32(Theme::Dim()), embedded.c_str());
            const float embeddedWidth = font->CalcTextSizeA(kHeaderFontSize, 1000.0F, 0.0F, embedded.c_str()).x;
            drawList->AddText(font, kHeaderFontSize, ImVec2{headerPos.x + embeddedWidth + 8.0F, headerPos.y},
                              Theme::U32(Theme::Muted()), description.c_str());
        }

        template <std::size_t RowCount>
        void DrawPreviewPane(const ImVec2 contentOrigin, const float contentWidth, ImDrawList* drawList, ImFont* font,
                             const ILocalizationService& localization, const char* descriptionKey,
                             const std::array<PreviewRow, RowCount>& rows)
        {
            DrawPreviewHeader(drawList, font, contentOrigin, localization, descriptionKey);
            const float firstRowY = contentOrigin.y + kOuterPaddingY + kHeaderHeight + kHeaderToGridGap;
            for (std::size_t index = 0; index < rows.size(); ++index)
            {
                const PreviewRow& row = rows[index];
                const float y = firstRowY + static_cast<float>(index) * kPreviewRowHeight;
                float valueX = contentOrigin.x + kOuterPaddingX;
                if (row.label[0] != '\0')
                {
                    drawList->AddText(font, kHeaderFontSize, ImVec2{valueX, y}, Theme::U32(Theme::Dim()), row.label);
                    valueX += font->CalcTextSizeA(kHeaderFontSize, contentWidth, 0.0F, row.label).x + 10.0F;
                }
                const std::string& value = localization.Get("editor", row.valueKey);
                const ImVec4 clipRect{
                    valueX, y, contentOrigin.x + kOuterPaddingX + contentWidth, y + kPreviewRowHeight
                };
                drawList->AddText(font, kHeaderFontSize, ImVec2{valueX, y}, PreviewColor(row.tone), value.c_str(),
                                  nullptr,
                                  0.0F, &clipRect);
            }
            ImGui::SetCursorScreenPos(
                ImVec2{
                    contentOrigin.x + kOuterPaddingX, firstRowY + static_cast<float>(rows.size()) * kPreviewRowHeight
                });
            ImGui::Dummy(ImVec2{contentWidth, 1.0F});
        }

        void DrawAssetPane(const ImVec2 contentOrigin, const float contentWidth, ImDrawList* drawList, ImFont* font,
                           const ILocalizationService& localization)
        {
            DrawPreviewHeader(drawList, font, contentOrigin, localization,
                              "workspace.content_browser.project_asset_dock");
            const ContentBrowserGridMetrics metrics = ComputeContentBrowserGridMetrics(contentWidth);
            const float gridY = contentOrigin.y + kOuterPaddingY + kHeaderHeight + kHeaderToGridGap;
            for (std::size_t index = 0; index < kAssets.size(); ++index)
            {
                const std::size_t row = index / metrics.columns;
                const std::size_t column = index % metrics.columns;
                const ImVec2 cardMin{
                    contentOrigin.x + kOuterPaddingX + static_cast<float>(column) * (metrics.cardWidth + kCardGap),
                    gridY + static_cast<float>(row) * (metrics.cardWidth + kCardFooterHeight + kCardGap),
                };

                ImGui::PushID(static_cast<int>(index));
                ImGui::SetCursorScreenPos(cardMin);
                ImGui::InvisibleButton("##AssetCard", ImVec2{metrics.cardWidth, metrics.cardWidth + kCardFooterHeight});
                DrawAssetCard(drawList, font, kCardFontSize, kAssets[index], cardMin, metrics.cardWidth,
                              ImGui::IsItemHovered());
                ImGui::PopID();
            }

            const std::size_t rowCount = (kAssets.size() + metrics.columns - 1) / metrics.columns;
            const float gridHeight = static_cast<float>(rowCount) * (metrics.cardWidth + kCardFooterHeight) +
                static_cast<float>(rowCount - 1) * kCardGap;
            ImGui::SetCursorScreenPos(ImVec2{contentOrigin.x + kOuterPaddingX, gridY + gridHeight});
            ImGui::Dummy(ImVec2{contentWidth, 1.0F});
        }
    } // namespace

    ContentBrowserGridMetrics ComputeContentBrowserGridMetrics(const float availableWidth) noexcept
    {
        const float safeWidth = std::max(1.0F, availableWidth);
        const auto columns =
            static_cast<std::size_t>(
                std::max(1.0F, std::floor((safeWidth + kCardGap) / (kMinimumCardWidth + kCardGap))));
        const float cardWidth =
            std::max(1.0F, (safeWidth - kCardGap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
        return {.columns = columns, .cardWidth = cardWidth};
    }

    void ContentBrowserPanel::DrawIcon(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, const ImU32 color)
    {
        const float ox = pos.x + (size.x - 14.0F) * 0.5F;
        const float oy = pos.y + (size.y - 14.0F) * 0.5F;

        dl->AddLine(ImVec2(ox + 2.0F, oy + 4.0F), ImVec2(ox + 5.0F, oy + 4.0F), color, 1.5F);
        dl->AddLine(ImVec2(ox + 5.0F, oy + 4.0F), ImVec2(ox + 6.0F, oy + 6.0F), color, 1.5F);
        dl->AddLine(ImVec2(ox + 6.0F, oy + 6.0F), ImVec2(ox + 12.0F, oy + 6.0F), color, 1.5F);
        dl->AddLine(ImVec2(ox + 12.0F, oy + 6.0F), ImVec2(ox + 12.0F, oy + 11.0F), color, 1.5F);
        dl->AddLine(ImVec2(ox + 12.0F, oy + 11.0F), ImVec2(ox + 2.0F, oy + 11.0F), color, 1.5F);
        dl->AddLine(ImVec2(ox + 2.0F, oy + 11.0F), ImVec2(ox + 2.0F, oy + 4.0F), color, 1.5F);
    }

    void ContentBrowserPanel::DrawPanel(const ImVec2& pos, const ImVec2& size, const EditorWorkspaceViewModel& vm,
                                        EditorWorkspaceViewCommandData& cmd, const EditorGuiContext& ctx)
    {
        static_cast<void>(pos);
        static_cast<void>(vm);
        static_cast<void>(cmd);

        const std::array tabNames{
            ctx.localization.Get("editor", "workspace.global_dock.tab.assets").c_str(),
            ctx.localization.Get("editor", "workspace.global_dock.tab.console").c_str(),
            ctx.localization.Get("editor", "workspace.global_dock.tab.mcp").c_str(),
            ctx.localization.Get("editor", "workspace.global_dock.tab.performance").c_str(),
            ctx.localization.Get("editor", "workspace.global_dock.tab.physics").c_str(),
            ctx.localization.Get("editor", "workspace.global_dock.tab.audio").c_str(),
            ctx.localization.Get("editor", "workspace.global_dock.tab.network").c_str(),
            ctx.localization.Get("editor", "workspace.global_dock.tab.localization").c_str(),
        };
        const auto& tabOrder = DefaultGlobalDockTabs();
        const auto activeIt = std::find(tabOrder.begin(), tabOrder.end(), activeTab_);
        const int activeIndex = activeIt == tabOrder.end() ? 0 : static_cast<int>(activeIt - tabOrder.begin());
        const int selectedIndex = Ui::DrawDockTabs(tabNames, activeIndex, ctx.theme.fonts);
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(tabOrder.size()))
        {
            activeTab_ = tabOrder[static_cast<std::size_t>(selectedIndex)];
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
        ImGui::BeginChild("##Content", ImVec2(size.x, std::max(1.0F, size.y - kTabHeight)), false,
                          ImGuiWindowFlags_NoSavedSettings);

        const ImVec2 contentOrigin = ImGui::GetCursorScreenPos();
        const float contentWidth = std::max(1.0F, size.x - kOuterPaddingX * 2.0F);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImFont* font = ResolveFont(ctx.theme.fonts.sansCompact);

        switch (activeTab_)
        {
        case GlobalDockTab::Assets:
            DrawAssetPane(contentOrigin, contentWidth, drawList, font, ctx.localization);
            break;
        case GlobalDockTab::Console:
            DrawPreviewPane(contentOrigin, contentWidth, drawList, font, ctx.localization,
                            "workspace.global_dock.preview.console", kConsoleRows);
            break;
        case GlobalDockTab::Mcp:
            DrawPreviewPane(contentOrigin, contentWidth, drawList, font, ctx.localization,
                            "workspace.global_dock.preview.mcp", kMcpRows);
            break;
        case GlobalDockTab::Performance:
            DrawPreviewPane(contentOrigin, contentWidth, drawList, font, ctx.localization,
                            "workspace.global_dock.preview.performance", kPerformanceRows);
            break;
        case GlobalDockTab::Physics:
            DrawPreviewPane(contentOrigin, contentWidth, drawList, font, ctx.localization,
                            "workspace.global_dock.preview.physics", kPhysicsRows);
            break;
        case GlobalDockTab::Audio:
            DrawPreviewPane(contentOrigin, contentWidth, drawList, font, ctx.localization,
                            "workspace.global_dock.preview.audio", kAudioRows);
            break;
        case GlobalDockTab::Network:
            DrawPreviewPane(contentOrigin, contentWidth, drawList, font, ctx.localization,
                            "workspace.global_dock.preview.network", kNetworkRows);
            break;
        case GlobalDockTab::Localization:
            DrawPreviewPane(contentOrigin, contentWidth, drawList, font, ctx.localization,
                            "workspace.global_dock.preview.localization", kLocalizationRows);
            break;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
} // namespace Horo::Editor
