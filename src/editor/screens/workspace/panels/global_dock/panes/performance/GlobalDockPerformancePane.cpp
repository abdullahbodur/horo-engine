#include "editor/screens/workspace/panels/global_dock/panes/performance/GlobalDockPerformancePane.h"

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/screens/workspace/panels/global_dock/GlobalDockPaneChrome.h"
#include "editor/screens/workspace/panels/global_dock/GlobalDockPaneLayout.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <string>

namespace Horo::Editor {
    namespace {
        struct PerformanceRow {
            const char *subsystemKey;
            std::string_view p50;
            std::string_view p95;
            const char *notesKey;
        };

        constexpr std::array FrameSamples{0.31F, 0.40F, 0.37F, 0.64F, 0.45F, 0.78F, 0.49F, 0.61F, 0.39F, 0.68F, 0.51F};
        constexpr std::array CpuSamples{0.24F, 0.31F, 0.21F, 0.43F, 0.36F, 0.49F, 0.34F, 0.45F, 0.39F};
        constexpr std::array GpuSamples{0.35F, 0.42F, 0.64F, 0.45F, 0.74F, 0.54F, 0.63F, 0.42F, 0.67F, 0.58F};
        constexpr std::array MemorySamples{0.21F, 0.23F, 0.28F, 0.32F, 0.37F, 0.45F, 0.49F, 0.53F, 0.57F};
        constexpr std::array Rows{
            PerformanceRow{"workspace.global_dock.performance.subsystem.renderer", "8.2 ms", "15.8 ms",
                           "workspace.global_dock.performance.notes.renderer"},
            PerformanceRow{"workspace.global_dock.performance.subsystem.physics", "0.72 ms", "1.14 ms",
                           "workspace.global_dock.performance.notes.physics"},
            PerformanceRow{"workspace.global_dock.performance.subsystem.audio", "0.18 ms", "0.31 ms",
                           "workspace.global_dock.performance.notes.audio"},
        };

        [[nodiscard]] float TextWidth(ImFont *font, const float size, const std::string &text) {
            ImFont *resolved = font != nullptr ? font : ImGui::GetFont();
            return resolved->CalcTextSizeA(size, FLT_MAX, 0.0F, text.c_str()).x;
        }
    }  // namespace

    void GlobalDockPerformancePane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) {
        const Theme::Fonts &fonts = context.theme.fonts;
        const float scale = std::max(Theme::GetActiveTokens().sizes.uiScale, 0.01F);
        const GlobalDockPaneMetrics metrics = ResolveGlobalDockPaneMetrics();
        const float availableHeight = std::max(1.0F, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - contentOrigin.y);
        const GlobalDockPaneRegions regions =
            ResolveGlobalDockPaneRegions(contentOrigin, contentWidth, availableHeight, {.hasToolbar = true, .hasFooter = true});
        const auto localized = [&](const char *key) -> const std::string & {
            return context.localization.Get("editor", key);
        };

        DrawGlobalDockToolbarSurface(regions.toolbarOrigin, regions.toolbarWidth, metrics.toolbarHeight);
        const float controlY = regions.toolbarOrigin.y + (metrics.toolbarHeight - metrics.controlHeight) * 0.5F;
        const GlobalDockToolbarChipProps live{.id = "PerformanceLive",
                                              .label = localized("workspace.global_dock.performance.live"),
                                              .tone = GlobalDockTone::Positive,
                                              .active = m_live};
        const GlobalDockToolbarChipProps capture{.id = "PerformanceCapture",
                                                 .label = localized("workspace.global_dock.performance.capture"),
                                                 .tone = GlobalDockTone::Accent,
                                                 .active = true,
                                                 .icon = Ui::UiIcon::Record};
        const float liveWidth = MeasureGlobalDockToolbarChip(live, fonts);
        const float captureWidth = MeasureGlobalDockToolbarChip(capture, fonts);
        const float windowWidth = 148.0F * scale;
        const float subsystemWidth = 142.0F * scale;
        const float fixedWidth = windowWidth + subsystemWidth + liveWidth + captureWidth + metrics.toolbarGap * 5.0F + scale;
        const float searchWidth = std::max(180.0F * scale, regions.toolbarWidth - metrics.toolbarPaddingX * 2.0F - fixedWidth);
        float x = regions.toolbarOrigin.x + metrics.toolbarPaddingX;

        ImGui::SetCursorScreenPos({x, controlY});
        const std::string &searchHint = localized("workspace.global_dock.performance.search");
        static_cast<void>(Ui::InputTextControl("##PerformanceSearch", m_search.data(), m_search.size(), fonts,
                                               {.width = searchWidth / scale,
                                                .hint = searchHint.c_str(),
                                                .prefixIconWidth = 20.0F,
                                                .componentSize = Ui::ComponentSize::Small,
                                                .surface = Ui::InputTextSurface::BottomDockToolbar}));
        Ui::DrawEditorIcon(ImGui::GetWindowDrawList(), Ui::UiIcon::Search, {x + 8.0F * scale, controlY + 8.0F * scale},
                           {14.0F * scale, 14.0F * scale}, Theme::U32(Theme::Dim()), fonts.icon);
        x += searchWidth + metrics.toolbarGap;

        const std::array<std::string, 3> windowLabels{localized("workspace.global_dock.performance.window.ten_seconds"),
                                                      localized("workspace.global_dock.performance.window.minute"),
                                                      localized("workspace.global_dock.performance.window.five_minutes")};
        const std::array<const char *, 3> windowItems{windowLabels[0].c_str(), windowLabels[1].c_str(), windowLabels[2].c_str()};
        ImGui::SetCursorScreenPos({x, controlY});
        ImGui::SetNextItemWidth(windowWidth);
        static_cast<void>(Ui::ComboControl("PerformanceWindow", &m_windowSelection, windowItems.data(),
                                           static_cast<int>(windowItems.size()), fonts,
                                           {.height = GlobalDockLayout::ControlHeight,
                                            .componentSize = Ui::ComponentSize::Small,
                                            .surface = Ui::ComboControlSurface::BottomDockToolbar}));
        x += windowWidth + metrics.toolbarGap;

        const std::array<std::string, 4> subsystemLabels{localized("workspace.global_dock.performance.subsystem.all"),
                                                         localized("workspace.global_dock.performance.subsystem.renderer"),
                                                         localized("workspace.global_dock.performance.subsystem.physics"),
                                                         localized("workspace.global_dock.performance.subsystem.audio")};
        const std::array<const char *, 4> subsystemItems{subsystemLabels[0].c_str(), subsystemLabels[1].c_str(), subsystemLabels[2].c_str(),
                                                         subsystemLabels[3].c_str()};
        ImGui::SetCursorScreenPos({x, controlY});
        ImGui::SetNextItemWidth(subsystemWidth);
        static_cast<void>(Ui::ComboControl("PerformanceSubsystem", &m_subsystemSelection, subsystemItems.data(),
                                           static_cast<int>(subsystemItems.size()), fonts,
                                           {.height = GlobalDockLayout::ControlHeight,
                                            .componentSize = Ui::ComponentSize::Small,
                                            .surface = Ui::ComboControlSurface::BottomDockToolbar}));
        x += subsystemWidth + metrics.toolbarGap;
        DrawGlobalDockToolbarSeparator(x, controlY);
        x += metrics.toolbarGap + scale;
        if (DrawGlobalDockToolbarChip({x, controlY}, liveWidth, live, fonts))
            m_live = !m_live;
        x += liveWidth + metrics.toolbarGap;
        static_cast<void>(DrawGlobalDockToolbarChip({x, controlY}, captureWidth, capture, fonts));

        const bool narrow = regions.contentWidth < 900.0F * scale;
        const int columns = narrow ? 2 : 4;
        const float gridPadding = 10.0F * scale;
        const float cardGap = 8.0F * scale;
        const float cardHeight = 106.0F * scale;
        const int rows = narrow ? 2 : 1;
        const float gridHeight = gridPadding * 2.0F + cardHeight * static_cast<float>(rows) + cardGap * static_cast<float>(rows - 1);
        const float cardWidth =
            std::max(120.0F * scale,
                     (regions.contentWidth - gridPadding * 2.0F - cardGap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(regions.contentOrigin,
                                {regions.contentOrigin.x + regions.contentWidth, regions.contentOrigin.y + gridHeight},
                                Theme::U32(Theme::BottomDockContentSurface()));
        drawList->AddLine({regions.contentOrigin.x, regions.contentOrigin.y + gridHeight - scale},
                          {regions.contentOrigin.x + regions.contentWidth, regions.contentOrigin.y + gridHeight - scale},
                          Theme::U32(Theme::Border()));
        const std::array<GlobalDockMetricCardProps, 4> cards{
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.performance.metric.frame_time"),
                                      .value = "16.7 ms",
                                      .note = localized("workspace.global_dock.performance.metric.frame_budget"),
                                      .noteTone = GlobalDockTone::Warning,
                                      .samples = FrameSamples,
                                      .budget = 0.52F},
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.performance.metric.cpu"),
                                      .value = "4.1 ms",
                                      .valueTone = GlobalDockTone::Positive,
                                      .samples = CpuSamples},
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.performance.metric.gpu"),
                                      .value = "15.8 ms",
                                      .valueTone = GlobalDockTone::Warning,
                                      .samples = GpuSamples},
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.performance.metric.memory"),
                                      .value = "628 MB",
                                      .note = "+8.4",
                                      .samples = MemorySamples},
        };
        for (std::size_t index = 0; index < cards.size(); ++index) {
            const int column = static_cast<int>(index) % columns;
            const int row = static_cast<int>(index) / columns;
            DrawGlobalDockMetricCard({regions.contentOrigin.x + gridPadding + static_cast<float>(column) * (cardWidth + cardGap),
                                      regions.contentOrigin.y + gridPadding + static_cast<float>(row) * (cardHeight + cardGap)},
                                     {cardWidth, cardHeight}, cards[index], fonts);
        }

        const ImVec2 headerMin{regions.contentOrigin.x, regions.contentOrigin.y + gridHeight};
        DrawGlobalDockTableHeaderSurface(headerMin, regions.contentWidth, metrics.tableHeaderHeight);
        const float subsystemX = headerMin.x + metrics.contentPadding;
        const float p50X = subsystemX + 68.0F * scale + metrics.columnGap;
        const float p95X = p50X + 74.0F * scale + metrics.columnGap;
        const float notesX = p95X + 96.0F * scale + metrics.columnGap;
        const float headerY = headerMin.y + (metrics.tableHeaderHeight - Theme::TextPx::Caption()) * 0.5F;
        const auto headerText = [&](const float textX, const char *key) {
            drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(), {textX, headerY}, Theme::U32(Theme::Muted()),
                              localized(key).c_str());
        };
        headerText(subsystemX, "workspace.global_dock.performance.column.subsystem");
        headerText(p50X, "workspace.global_dock.performance.column.p50");
        headerText(p95X, "workspace.global_dock.performance.column.p95");
        headerText(notesX, "workspace.global_dock.performance.column.notes");

        const ImVec2 rowsOrigin{headerMin.x, headerMin.y + metrics.tableHeaderHeight};
        const float rowsHeight = std::max(1.0F, regions.contentHeight - gridHeight - metrics.tableHeaderHeight);
        ImGui::SetCursorScreenPos(rowsOrigin);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BottomDockContentSurface());
        ImGui::BeginChild("##PerformanceRows", {regions.contentWidth, rowsHeight}, false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoSavedSettings);
        for (std::size_t index = 0; index < Rows.size(); ++index) {
            if (m_subsystemSelection != 0 && static_cast<std::size_t>(m_subsystemSelection - 1) != index)
                continue;
            const PerformanceRow &row = Rows[index];
            ImGui::PushID(static_cast<int>(index));
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##performance-row", {regions.contentWidth, metrics.tableRowHeight});
            DrawGlobalDockTableRowSurface(rowMin, regions.contentWidth, metrics.tableRowHeight, ImGui::IsItemHovered());
            ImDrawList *rowsDrawList = ImGui::GetWindowDrawList();
            const float textY = rowMin.y + (metrics.tableRowHeight - Theme::TextPx::Label()) * 0.5F;
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {subsystemX, textY},
                                      {p50X - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(),
                                      localized(row.subsystemKey));
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {p50X, textY},
                                      {p95X - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), row.p50);
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {p95X, textY},
                                      {notesX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), row.p95);
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {notesX, textY},
                                      {rowMin.x + regions.contentWidth - metrics.contentPadding, rowMin.y + metrics.tableRowHeight},
                                      Theme::Text(), localized(row.notesKey));
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        DrawGlobalDockFooterSurface(regions.footerOrigin, regions.footerWidth, metrics.footerHeight);
        const float footerY = regions.footerOrigin.y + (metrics.footerHeight - Theme::TextPx::Caption()) * 0.5F;
        const std::string &sampling = localized("workspace.global_dock.performance.footer.sampling");
        const std::string &misses = localized("workspace.global_dock.performance.footer.misses");
        drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(), {regions.footerOrigin.x + metrics.contentPadding, footerY},
                          Theme::U32(Theme::Muted()), sampling.c_str());
        drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(),
                          {regions.footerOrigin.x + metrics.contentPadding +
                               TextWidth(fonts.sansCompact, Theme::TextPx::Caption(), sampling) + 10.0F * scale,
                           footerY},
                          Theme::U32(Theme::Muted()), misses.c_str());
        const std::string &captureAvailable = localized("workspace.global_dock.performance.footer.capture_available");
        drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(),
                          {regions.footerOrigin.x + regions.footerWidth - metrics.contentPadding -
                               TextWidth(fonts.sansCompact, Theme::TextPx::Caption(), captureAvailable),
                           footerY},
                          Theme::U32(Theme::Muted()), captureAvailable.c_str());
    }
}  // namespace Horo::Editor
