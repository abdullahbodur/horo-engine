#include "editor/screens/workspace/panels/global_dock/panes/network/GlobalDockNetworkPane.h"

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/screens/workspace/panels/global_dock/GlobalDockPaneChrome.h"
#include "editor/screens/workspace/panels/global_dock/GlobalDockPaneLayout.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <string>
#include <string_view>
#include <utility>

namespace Horo::Editor {
    namespace {
        struct NetworkRow {
            std::string_view connection;
            const char *stateKey;
            GlobalDockTone stateTone;
            std::string_view rtt;
            std::string_view traffic;
            const char *queueKey;
        };

        constexpr std::array Rows{
            NetworkRow{"local-server:7777", "workspace.global_dock.network.state.open", GlobalDockTone::Positive, "42 ms", "15.4 kb/s",
                       "workspace.global_dock.network.queue.local"},
            NetworkRow{"editor-presence", "workspace.global_dock.network.state.open", GlobalDockTone::Positive, "18 ms", "1.2 kb/s",
                       "workspace.global_dock.network.queue.presence"},
            NetworkRow{"asset-sync", "workspace.global_dock.network.state.idle", GlobalDockTone::Warning, "—", "0 kb/s",
                       "workspace.global_dock.network.queue.asset"},
        };

        [[nodiscard]] bool ContainsCaseInsensitive(const std::string_view value, const std::string_view query) {
            if (query.empty())
                return true;
            return std::search(value.begin(), value.end(), query.begin(), query.end(), [](const char lhs, const char rhs) {
                return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
            }) != value.end();
        }

        [[nodiscard]] float TextWidth(ImFont *font, const float size, const std::string &text) {
            ImFont *resolved = font != nullptr ? font : ImGui::GetFont();
            return resolved->CalcTextSizeA(size, FLT_MAX, 0.0F, text.c_str()).x;
        }
    }  // namespace

    void GlobalDockNetworkPane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) {
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
        const GlobalDockToolbarChipProps connections{.id = "NetworkConnections",
                                                     .label = localized("workspace.global_dock.network.connections"),
                                                     .active = m_viewSelection == 0};
        const GlobalDockToolbarChipProps queues{.id = "NetworkQueues",
                                                .label = localized("workspace.global_dock.network.queues"),
                                                .active = m_viewSelection == 1};
        const GlobalDockToolbarChipProps trace{.id = "NetworkTrace",
                                               .label = localized("workspace.global_dock.network.trace"),
                                               .active = m_viewSelection == 2};
        const GlobalDockToolbarChipProps pause{.id = "NetworkPause",
                                               .label = localized(m_paused ? "workspace.global_dock.network.resume_capture"
                                                                           : "workspace.global_dock.network.pause_capture"),
                                               .tone = GlobalDockTone::Accent,
                                               .active = true,
                                               .icon = m_paused ? Ui::UiIcon::Play : Ui::UiIcon::Pause};
        const GlobalDockToolbarChipProps clear{.id = "NetworkClear",
                                               .label = localized("workspace.global_dock.network.clear"),
                                               .icon = Ui::UiIcon::ClearAll};
        const float connectionsWidth = MeasureGlobalDockToolbarChip(connections, fonts);
        const float queuesWidth = MeasureGlobalDockToolbarChip(queues, fonts);
        const float traceWidth = MeasureGlobalDockToolbarChip(trace, fonts);
        const float pauseWidth = MeasureGlobalDockToolbarChip(pause, fonts);
        const float clearWidth = MeasureGlobalDockToolbarChip(clear, fonts);
        const float sessionWidth = 144.0F * scale;
        const float fixedWidth =
            sessionWidth + connectionsWidth + queuesWidth + traceWidth + pauseWidth + clearWidth + metrics.toolbarGap * 7.0F + scale;
        const float searchWidth = std::max(180.0F * scale, regions.toolbarWidth - metrics.toolbarPaddingX * 2.0F - fixedWidth);
        float x = regions.toolbarOrigin.x + metrics.toolbarPaddingX;

        ImGui::SetCursorScreenPos({x, controlY});
        const std::string &searchHint = localized("workspace.global_dock.network.search");
        static_cast<void>(Ui::InputTextControl("##NetworkSearch", m_search.data(), m_search.size(), fonts,
                                               {.width = searchWidth / scale,
                                                .hint = searchHint.c_str(),
                                                .prefixIconWidth = 20.0F,
                                                .componentSize = Ui::ComponentSize::Small,
                                                .surface = Ui::InputTextSurface::BottomDockToolbar}));
        Ui::DrawEditorIcon(ImGui::GetWindowDrawList(), Ui::UiIcon::Search, {x + 8.0F * scale, controlY + 8.0F * scale},
                           {14.0F * scale, 14.0F * scale}, Theme::U32(Theme::Dim()), fonts.icon);
        x += searchWidth + metrics.toolbarGap;
        const std::array<std::string, 2> sessionLabels{localized("workspace.global_dock.network.session.current"),
                                                       localized("workspace.global_dock.network.session.all")};
        const std::array<const char *, 2> sessionItems{sessionLabels[0].c_str(), sessionLabels[1].c_str()};
        ImGui::SetCursorScreenPos({x, controlY});
        ImGui::SetNextItemWidth(sessionWidth);
        static_cast<void>(Ui::ComboControl("NetworkSession", &m_sessionSelection, sessionItems.data(),
                                           static_cast<int>(sessionItems.size()), fonts,
                                           {.height = GlobalDockLayout::ControlHeight,
                                            .componentSize = Ui::ComponentSize::Small,
                                            .surface = Ui::ComboControlSurface::BottomDockToolbar}));
        x += sessionWidth + metrics.toolbarGap;
        const std::array<std::pair<const GlobalDockToolbarChipProps *, int>, 3> views{std::pair{&connections, 0}, std::pair{&queues, 1},
                                                                                      std::pair{&trace, 2}};
        const std::array viewWidths{connectionsWidth, queuesWidth, traceWidth};
        for (std::size_t index = 0; index < views.size(); ++index) {
            if (DrawGlobalDockToolbarChip({x, controlY}, viewWidths[index], *views[index].first, fonts))
                m_viewSelection = views[index].second;
            x += viewWidths[index] + metrics.toolbarGap;
        }
        DrawGlobalDockToolbarSeparator(x, controlY);
        x += metrics.toolbarGap + scale;
        if (DrawGlobalDockToolbarChip({x, controlY}, pauseWidth, pause, fonts))
            m_paused = !m_paused;
        x += pauseWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, clearWidth, clear, fonts))
            m_cleared = true;

        const bool narrow = regions.contentWidth < 900.0F * scale;
        const int columns = narrow ? 2 : 4;
        const int metricRows = narrow ? 2 : 1;
        const float gridPadding = 10.0F * scale;
        const float cardGap = 8.0F * scale;
        const float cardHeight = 64.0F * scale;
        const float gridHeight =
            gridPadding * 2.0F + cardHeight * static_cast<float>(metricRows) + cardGap * static_cast<float>(metricRows - 1);
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
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.network.metric.rtt"), .value = "42 ms"},
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.network.metric.receive"), .value = "12.3", .note = "kb/s"},
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.network.metric.send"), .value = "3.1", .note = "kb/s"},
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.network.metric.loss"),
                                      .value = "0.2%",
                                      .valueTone = GlobalDockTone::Positive},
        };
        for (std::size_t index = 0; index < cards.size(); ++index) {
            const int column = static_cast<int>(index) % columns;
            const int metricRow = static_cast<int>(index) / columns;
            DrawGlobalDockMetricCard({regions.contentOrigin.x + gridPadding + static_cast<float>(column) * (cardWidth + cardGap),
                                      regions.contentOrigin.y + gridPadding + static_cast<float>(metricRow) * (cardHeight + cardGap)},
                                     {cardWidth, cardHeight}, cards[index], fonts);
        }

        const ImVec2 headerMin{regions.contentOrigin.x, regions.contentOrigin.y + gridHeight};
        DrawGlobalDockTableHeaderSurface(headerMin, regions.contentWidth, metrics.tableHeaderHeight);
        const float connectionX = headerMin.x + metrics.contentPadding;
        const float stateX = connectionX + 126.0F * scale + metrics.columnGap;
        const float rttX = stateX + 84.0F * scale + metrics.columnGap;
        const float trafficX = rttX + 90.0F * scale + metrics.columnGap;
        const float queueX = trafficX + 90.0F * scale + metrics.columnGap;
        const float headerY = headerMin.y + (metrics.tableHeaderHeight - Theme::TextPx::Caption()) * 0.5F;
        const auto headerText = [&](const float textX, const char *key) {
            drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(), {textX, headerY}, Theme::U32(Theme::Muted()),
                              localized(key).c_str());
        };
        headerText(connectionX, "workspace.global_dock.network.column.connection");
        headerText(stateX, "workspace.global_dock.network.column.state");
        headerText(rttX, "workspace.global_dock.network.column.rtt");
        headerText(trafficX, "workspace.global_dock.network.column.traffic");
        headerText(queueX, "workspace.global_dock.network.column.queues");

        const ImVec2 rowsOrigin{headerMin.x, headerMin.y + metrics.tableHeaderHeight};
        const float rowsHeight = std::max(1.0F, regions.contentHeight - gridHeight - metrics.tableHeaderHeight);
        ImGui::SetCursorScreenPos(rowsOrigin);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BottomDockContentSurface());
        ImGui::BeginChild("##NetworkRows", {regions.contentWidth, rowsHeight}, false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoSavedSettings);
        if (!m_cleared) {
            for (std::size_t index = 0; index < Rows.size(); ++index) {
                const NetworkRow &row = Rows[index];
                const std::string &state = localized(row.stateKey);
                const std::string &queue = localized(row.queueKey);
                const std::string_view search{m_search.data()};
                if (!ContainsCaseInsensitive(row.connection, search) && !ContainsCaseInsensitive(state, search) &&
                    !ContainsCaseInsensitive(queue, search))
                    continue;
                ImGui::PushID(static_cast<int>(index));
                const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##network-row", {regions.contentWidth, metrics.tableRowHeight});
                DrawGlobalDockTableRowSurface(rowMin, regions.contentWidth, metrics.tableRowHeight, ImGui::IsItemHovered());
                ImDrawList *rowsDrawList = ImGui::GetWindowDrawList();
                const float textY = rowMin.y + (metrics.tableRowHeight - Theme::TextPx::Label()) * 0.5F;
                DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {connectionX, textY},
                                          {stateX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), row.connection);
                static_cast<void>(DrawGlobalDockStatePill({stateX, rowMin.y + (metrics.tableRowHeight - 22.0F * scale) * 0.5F}, state,
                                                          row.stateTone, fonts));
                DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {rttX, textY},
                                          {trafficX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), row.rtt);
                DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {trafficX, textY},
                                          {queueX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), row.traffic);
                DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {queueX, textY},
                                          {rowMin.x + regions.contentWidth - metrics.contentPadding, rowMin.y + metrics.tableRowHeight},
                                          Theme::Text(), queue);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        DrawGlobalDockFooterSurface(regions.footerOrigin, regions.footerWidth, metrics.footerHeight);
        const float footerY = regions.footerOrigin.y + (metrics.footerHeight - Theme::TextPx::Caption()) * 0.5F;
        const std::array<const char *, 3> footerKeys{"workspace.global_dock.network.footer.peer",
                                                     "workspace.global_dock.network.footer.objects",
                                                     "workspace.global_dock.network.footer.tick"};
        float footerX = regions.footerOrigin.x + metrics.contentPadding;
        for (const char *key : footerKeys) {
            const std::string &text = localized(key);
            drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(), {footerX, footerY}, Theme::U32(Theme::Muted()), text.c_str());
            footerX += TextWidth(fonts.sansCompact, Theme::TextPx::Caption(), text) + 10.0F * scale;
        }
        const std::string &tracing = localized("workspace.global_dock.network.footer.tracing");
        drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(),
                          {regions.footerOrigin.x + regions.footerWidth - metrics.contentPadding -
                               TextWidth(fonts.sansCompact, Theme::TextPx::Caption(), tracing),
                           footerY},
                          Theme::U32(Theme::Muted()), tracing.c_str());
    }
}  // namespace Horo::Editor
