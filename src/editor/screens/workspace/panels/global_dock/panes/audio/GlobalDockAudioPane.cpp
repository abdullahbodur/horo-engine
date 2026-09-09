#include "editor/screens/workspace/panels/global_dock/panes/audio/GlobalDockAudioPane.h"

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

namespace Horo::Editor {
    namespace {
        struct AudioRow {
            const char *busKey;
            std::string_view gain;
            float level;
            std::string_view voices;
        };

        constexpr std::array Rows{
            AudioRow{"workspace.global_dock.audio.bus.master", "0.0 dB", 0.78F, "12"},
            AudioRow{"workspace.global_dock.audio.bus.music", "−6.0 dB", 0.58F, "2"},
            AudioRow{"workspace.global_dock.audio.bus.sfx", "−1.5 dB", 0.69F, "7"},
            AudioRow{"workspace.global_dock.audio.bus.ambient", "−9.0 dB", 0.39F, "3"},
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

    void GlobalDockAudioPane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) {
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
        const GlobalDockToolbarChipProps meters{.id = "AudioMeters",
                                                .label = localized("workspace.global_dock.audio.meters"),
                                                .active = m_viewSelection == 0};
        const GlobalDockToolbarChipProps voices{.id = "AudioVoices",
                                                .label = localized("workspace.global_dock.audio.voices"),
                                                .active = m_viewSelection == 1};
        const GlobalDockToolbarChipProps pause{.id = "AudioPause",
                                               .label = localized(m_paused ? "workspace.global_dock.audio.resume"
                                                                           : "workspace.global_dock.audio.pause"),
                                               .active = m_paused,
                                               .icon = m_paused ? Ui::UiIcon::Play : Ui::UiIcon::Pause};
        const GlobalDockToolbarChipProps muteAll{.id = "AudioMuteAll",
                                                 .label = localized(m_allMuted ? "workspace.global_dock.audio.unmute_all"
                                                                               : "workspace.global_dock.audio.mute_all"),
                                                 .tone = GlobalDockTone::Error,
                                                 .active = m_allMuted,
                                                 .toneLabel = true,
                                                 .icon = Ui::UiIcon::VolumeOff};
        const float metersWidth = MeasureGlobalDockToolbarChip(meters, fonts);
        const float voicesWidth = MeasureGlobalDockToolbarChip(voices, fonts);
        const float pauseWidth = MeasureGlobalDockToolbarChip(pause, fonts);
        const float muteWidth = MeasureGlobalDockToolbarChip(muteAll, fonts);
        const float deviceWidth = 138.0F * scale;
        const float fixedWidth = deviceWidth + metersWidth + voicesWidth + pauseWidth + muteWidth + metrics.toolbarGap * 6.0F + scale;
        const float searchWidth = std::max(180.0F * scale, regions.toolbarWidth - metrics.toolbarPaddingX * 2.0F - fixedWidth);
        float x = regions.toolbarOrigin.x + metrics.toolbarPaddingX;

        ImGui::SetCursorScreenPos({x, controlY});
        const std::string &searchHint = localized("workspace.global_dock.audio.search");
        static_cast<void>(Ui::InputTextControl("##AudioSearch", m_search.data(), m_search.size(), fonts,
                                               {.width = searchWidth / scale,
                                                .hint = searchHint.c_str(),
                                                .prefixIconWidth = 20.0F,
                                                .componentSize = Ui::ComponentSize::Small,
                                                .surface = Ui::InputTextSurface::BottomDockToolbar}));
        Ui::DrawEditorIcon(ImGui::GetWindowDrawList(), Ui::UiIcon::Search, {x + 8.0F * scale, controlY + 8.0F * scale},
                           {14.0F * scale, 14.0F * scale}, Theme::U32(Theme::Dim()), fonts.icon);
        x += searchWidth + metrics.toolbarGap;
        const std::array<std::string, 2> deviceLabels{localized("workspace.global_dock.audio.device.default"),
                                                      localized("workspace.global_dock.audio.device.headphones")};
        const std::array<const char *, 2> deviceItems{deviceLabels[0].c_str(), deviceLabels[1].c_str()};
        ImGui::SetCursorScreenPos({x, controlY});
        ImGui::SetNextItemWidth(deviceWidth);
        static_cast<void>(Ui::ComboControl("AudioDevice", &m_deviceSelection, deviceItems.data(), static_cast<int>(deviceItems.size()),
                                           fonts,
                                           {.height = GlobalDockLayout::ControlHeight,
                                            .componentSize = Ui::ComponentSize::Small,
                                            .surface = Ui::ComboControlSurface::BottomDockToolbar}));
        x += deviceWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, metersWidth, meters, fonts))
            m_viewSelection = 0;
        x += metersWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, voicesWidth, voices, fonts))
            m_viewSelection = 1;
        x += voicesWidth + metrics.toolbarGap;
        DrawGlobalDockToolbarSeparator(x, controlY);
        x += metrics.toolbarGap + scale;
        if (DrawGlobalDockToolbarChip({x, controlY}, pauseWidth, pause, fonts))
            m_paused = !m_paused;
        x += pauseWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, muteWidth, muteAll, fonts)) {
            m_allMuted = !m_allMuted;
            m_muted.fill(m_allMuted);
        }

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
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.audio.metric.master_peak"), .value = "−3.2 dB"},
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.audio.metric.voices"), .value = "12 / 64"},
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.audio.metric.callback"),
                                      .value = "0.31 ms",
                                      .valueTone = GlobalDockTone::Positive},
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.audio.metric.underruns"),
                                      .value = "0",
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
        const float busX = headerMin.x + metrics.contentPadding;
        const float gainX = busX + 110.0F * scale + metrics.columnGap;
        const float levelX = gainX + 70.0F * scale + metrics.columnGap;
        const float controlsX = headerMin.x + regions.contentWidth - metrics.contentPadding - 108.0F * scale;
        const float voicesX = controlsX - metrics.columnGap - 90.0F * scale;
        const float headerY = headerMin.y + (metrics.tableHeaderHeight - Theme::TextPx::Caption()) * 0.5F;
        const auto headerText = [&](const float textX, const char *key) {
            drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(), {textX, headerY}, Theme::U32(Theme::Muted()),
                              localized(key).c_str());
        };
        headerText(busX, "workspace.global_dock.audio.column.bus");
        headerText(gainX, "workspace.global_dock.audio.column.gain");
        headerText(levelX, "workspace.global_dock.audio.column.level");
        headerText(voicesX, "workspace.global_dock.audio.column.voices");
        headerText(controlsX, "workspace.global_dock.audio.column.controls");

        const ImVec2 rowsOrigin{headerMin.x, headerMin.y + metrics.tableHeaderHeight};
        const float rowsHeight = std::max(1.0F, regions.contentHeight - gridHeight - metrics.tableHeaderHeight);
        ImGui::SetCursorScreenPos(rowsOrigin);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BottomDockContentSurface());
        ImGui::BeginChild("##AudioRows", {regions.contentWidth, rowsHeight}, false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoSavedSettings);
        for (std::size_t index = 0; index < Rows.size(); ++index) {
            const AudioRow &row = Rows[index];
            const std::string &bus = localized(row.busKey);
            if (!ContainsCaseInsensitive(bus, std::string_view{m_search.data()}))
                continue;
            ImGui::PushID(static_cast<int>(index));
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##audio-row", {regions.contentWidth, metrics.tableRowHeight});
            DrawGlobalDockTableRowSurface(rowMin, regions.contentWidth, metrics.tableRowHeight, ImGui::IsItemHovered());
            ImDrawList *rowsDrawList = ImGui::GetWindowDrawList();
            const float textY = rowMin.y + (metrics.tableRowHeight - Theme::TextPx::Label()) * 0.5F;
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {busX, textY},
                                      {gainX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), bus);
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {gainX, textY},
                                      {levelX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), row.gain);
            DrawGlobalDockMeter({levelX, rowMin.y + (metrics.tableRowHeight - 6.0F * scale) * 0.5F},
                                std::max(1.0F, voicesX - metrics.columnGap - levelX), m_muted[index] ? 0.0F : row.level);
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {voicesX, textY},
                                      {controlsX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), row.voices);
            const GlobalDockToolbarChipProps mute{.id = "AudioMute", .label = "M", .active = m_muted[index]};
            const GlobalDockToolbarChipProps solo{.id = "AudioSolo", .label = "S", .active = m_solo[index]};
            const float buttonWidth = 30.0F * scale;
            const float buttonY = rowMin.y + (metrics.tableRowHeight - metrics.controlHeight) * 0.5F;
            if (DrawGlobalDockToolbarChip({controlsX, buttonY}, buttonWidth, mute, fonts))
                m_muted[index] = !m_muted[index];
            if (DrawGlobalDockToolbarChip({controlsX + buttonWidth + 4.0F * scale, buttonY}, buttonWidth, solo, fonts))
                m_solo[index] = !m_solo[index];
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        DrawGlobalDockFooterSurface(regions.footerOrigin, regions.footerWidth, metrics.footerHeight);
        const float footerY = regions.footerOrigin.y + (metrics.footerHeight - Theme::TextPx::Caption()) * 0.5F;
        const std::array<const char *, 3> footerKeys{"workspace.global_dock.audio.footer.format",
                                                     "workspace.global_dock.audio.footer.latency",
                                                     "workspace.global_dock.audio.footer.queue"};
        float footerX = regions.footerOrigin.x + metrics.contentPadding;
        for (const char *key : footerKeys) {
            const std::string &text = localized(key);
            drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(), {footerX, footerY}, Theme::U32(Theme::Muted()), text.c_str());
            footerX += TextWidth(fonts.sansCompact, Theme::TextPx::Caption(), text) + 10.0F * scale;
        }
        const std::string &healthy = localized("workspace.global_dock.audio.footer.healthy");
        drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(),
                          {regions.footerOrigin.x + regions.footerWidth - metrics.contentPadding -
                               TextWidth(fonts.sansCompact, Theme::TextPx::Caption(), healthy),
                           footerY},
                          Theme::U32(Theme::Muted()), healthy.c_str());
    }
}  // namespace Horo::Editor
