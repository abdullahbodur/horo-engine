#include "editor/screens/workspace/panels/global_dock/panes/physics/GlobalDockPhysicsPane.h"

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
        struct PhysicsRow {
            std::string_view body;
            const char *stateKey;
            GlobalDockTone stateTone;
            const char *shapeKey;
            const char *layerKey;
            const char *timingKey;
        };

        constexpr std::array Rows{
            PhysicsRow{"Player", "workspace.global_dock.physics.state.active", GlobalDockTone::Positive,
                       "workspace.global_dock.physics.shape.capsule", "workspace.global_dock.physics.layer.player",
                       "workspace.global_dock.physics.timing.player"},
            PhysicsRow{"PF Enemy", "workspace.global_dock.physics.state.active", GlobalDockTone::Positive,
                       "workspace.global_dock.physics.shape.convex", "workspace.global_dock.physics.layer.enemy",
                       "workspace.global_dock.physics.timing.enemy"},
            PhysicsRow{"Floor 000", "workspace.global_dock.physics.state.static", GlobalDockTone::Neutral,
                       "workspace.global_dock.physics.shape.mesh", "workspace.global_dock.physics.layer.terrain",
                       "workspace.global_dock.physics.timing.floor"},
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

    void GlobalDockPhysicsPane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) {
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
        const GlobalDockToolbarChipProps colliders{.id = "PhysicsColliders",
                                                   .label = localized("workspace.global_dock.physics.colliders"),
                                                   .active = m_colliders};
        const GlobalDockToolbarChipProps contacts{.id = "PhysicsContacts",
                                                  .label = localized("workspace.global_dock.physics.contacts"),
                                                  .active = m_contacts};
        const GlobalDockToolbarChipProps constraints{.id = "PhysicsConstraints",
                                                     .label = localized("workspace.global_dock.physics.constraints"),
                                                     .active = m_constraints};
        const GlobalDockToolbarChipProps pause{.id = "PhysicsPause",
                                               .label = localized(m_paused ? "workspace.global_dock.physics.resume"
                                                                           : "workspace.global_dock.physics.pause"),
                                               .tone = GlobalDockTone::Accent,
                                               .active = true,
                                               .icon = m_paused ? Ui::UiIcon::Play : Ui::UiIcon::Pause};
        const GlobalDockToolbarChipProps step{.id = "PhysicsStep",
                                              .label = localized("workspace.global_dock.physics.step"),
                                              .disabled = !m_paused,
                                              .icon = Ui::UiIcon::Play};
        const float collidersWidth = MeasureGlobalDockToolbarChip(colliders, fonts);
        const float contactsWidth = MeasureGlobalDockToolbarChip(contacts, fonts);
        const float constraintsWidth = MeasureGlobalDockToolbarChip(constraints, fonts);
        const float pauseWidth = MeasureGlobalDockToolbarChip(pause, fonts);
        const float stepWidth = MeasureGlobalDockToolbarChip(step, fonts);
        const float worldWidth = 132.0F * scale;
        const float fixedWidth =
            worldWidth + collidersWidth + contactsWidth + constraintsWidth + pauseWidth + stepWidth + metrics.toolbarGap * 7.0F + scale;
        const float searchWidth = std::max(180.0F * scale, regions.toolbarWidth - metrics.toolbarPaddingX * 2.0F - fixedWidth);
        float x = regions.toolbarOrigin.x + metrics.toolbarPaddingX;

        ImGui::SetCursorScreenPos({x, controlY});
        const std::string &searchHint = localized("workspace.global_dock.physics.search");
        static_cast<void>(Ui::InputTextControl("##PhysicsSearch", m_search.data(), m_search.size(), fonts,
                                               {.width = searchWidth / scale,
                                                .hint = searchHint.c_str(),
                                                .prefixIconWidth = 20.0F,
                                                .componentSize = Ui::ComponentSize::Small,
                                                .surface = Ui::InputTextSurface::BottomDockToolbar}));
        Ui::DrawEditorIcon(ImGui::GetWindowDrawList(), Ui::UiIcon::Search, {x + 8.0F * scale, controlY + 8.0F * scale},
                           {14.0F * scale, 14.0F * scale}, Theme::U32(Theme::Dim()), fonts.icon);
        x += searchWidth + metrics.toolbarGap;

        const std::array<std::string, 2> worldLabels{localized("workspace.global_dock.physics.world.room"),
                                                     localized("workspace.global_dock.physics.world.all")};
        const std::array<const char *, 2> worldItems{worldLabels[0].c_str(), worldLabels[1].c_str()};
        ImGui::SetCursorScreenPos({x, controlY});
        ImGui::SetNextItemWidth(worldWidth);
        static_cast<void>(Ui::ComboControl("PhysicsWorld", &m_worldSelection, worldItems.data(), static_cast<int>(worldItems.size()), fonts,
                                           {.height = GlobalDockLayout::ControlHeight,
                                            .componentSize = Ui::ComponentSize::Small,
                                            .surface = Ui::ComboControlSurface::BottomDockToolbar}));
        x += worldWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, collidersWidth, colliders, fonts))
            m_colliders = !m_colliders;
        x += collidersWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, contactsWidth, contacts, fonts))
            m_contacts = !m_contacts;
        x += contactsWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, constraintsWidth, constraints, fonts))
            m_constraints = !m_constraints;
        x += constraintsWidth + metrics.toolbarGap;
        DrawGlobalDockToolbarSeparator(x, controlY);
        x += metrics.toolbarGap + scale;
        if (DrawGlobalDockToolbarChip({x, controlY}, pauseWidth, pause, fonts))
            m_paused = !m_paused;
        x += pauseWidth + metrics.toolbarGap;
        static_cast<void>(DrawGlobalDockToolbarChip({x, controlY}, stepWidth, step, fonts));

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
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.physics.metric.step"),
                                      .value = "0.72 ms",
                                      .valueTone = GlobalDockTone::Positive},
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.physics.metric.active_sleeping"),
                                      .value = "28",
                                      .note = "/ 64"},
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.physics.metric.contacts_pairs"),
                                      .value = "14",
                                      .note = "/ 37"},
            GlobalDockMetricCardProps{.label = localized("workspace.global_dock.physics.metric.snapshot"),
                                      .value = "8 / 16",
                                      .note = localized("workspace.global_dock.physics.metric.dropped"),
                                      .noteTone = GlobalDockTone::Positive},
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
        const float bodyX = headerMin.x + metrics.contentPadding;
        const float stateX = bodyX + 110.0F * scale + metrics.columnGap;
        const float shapeX = stateX + 90.0F * scale + metrics.columnGap;
        const float layerX = shapeX + 90.0F * scale + metrics.columnGap;
        const float timingX = layerX + 100.0F * scale + metrics.columnGap;
        const float headerY = headerMin.y + (metrics.tableHeaderHeight - Theme::TextPx::Caption()) * 0.5F;
        const auto headerText = [&](const float textX, const char *key) {
            drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(), {textX, headerY}, Theme::U32(Theme::Muted()),
                              localized(key).c_str());
        };
        headerText(bodyX, "workspace.global_dock.physics.column.body");
        headerText(stateX, "workspace.global_dock.physics.column.state");
        headerText(shapeX, "workspace.global_dock.physics.column.shape");
        headerText(layerX, "workspace.global_dock.physics.column.layer");
        headerText(timingX, "workspace.global_dock.physics.column.timing");

        const ImVec2 rowsOrigin{headerMin.x, headerMin.y + metrics.tableHeaderHeight};
        const float rowsHeight = std::max(1.0F, regions.contentHeight - gridHeight - metrics.tableHeaderHeight);
        ImGui::SetCursorScreenPos(rowsOrigin);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BottomDockContentSurface());
        ImGui::BeginChild("##PhysicsRows", {regions.contentWidth, rowsHeight}, false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoSavedSettings);
        for (std::size_t index = 0; index < Rows.size(); ++index) {
            const PhysicsRow &row = Rows[index];
            const std::string &state = localized(row.stateKey);
            const std::string &shape = localized(row.shapeKey);
            const std::string &layer = localized(row.layerKey);
            const std::string &timing = localized(row.timingKey);
            const std::string_view search{m_search.data()};
            if (!ContainsCaseInsensitive(row.body, search) && !ContainsCaseInsensitive(state, search) &&
                !ContainsCaseInsensitive(shape, search) && !ContainsCaseInsensitive(layer, search))
                continue;
            ImGui::PushID(static_cast<int>(index));
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##physics-row", {regions.contentWidth, metrics.tableRowHeight});
            DrawGlobalDockTableRowSurface(rowMin, regions.contentWidth, metrics.tableRowHeight, ImGui::IsItemHovered());
            ImDrawList *rowsDrawList = ImGui::GetWindowDrawList();
            const float textY = rowMin.y + (metrics.tableRowHeight - Theme::TextPx::Label()) * 0.5F;
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {bodyX, textY},
                                      {stateX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), row.body);
            static_cast<void>(
                DrawGlobalDockStatePill({stateX, rowMin.y + (metrics.tableRowHeight - 22.0F * scale) * 0.5F}, state, row.stateTone, fonts));
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {shapeX, textY},
                                      {layerX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), shape);
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {layerX, textY},
                                      {timingX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), layer);
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {timingX, textY},
                                      {rowMin.x + regions.contentWidth - metrics.contentPadding, rowMin.y + metrics.tableRowHeight},
                                      Theme::Text(), timing);
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        DrawGlobalDockFooterSurface(regions.footerOrigin, regions.footerWidth, metrics.footerHeight);
        const float footerY = regions.footerOrigin.y + (metrics.footerHeight - Theme::TextPx::Caption()) * 0.5F;
        const std::array<const char *, 3> footerKeys{"workspace.global_dock.physics.footer.broadphase",
                                                     "workspace.global_dock.physics.footer.narrowphase",
                                                     "workspace.global_dock.physics.footer.solver"};
        float footerX = regions.footerOrigin.x + metrics.contentPadding;
        for (const char *key : footerKeys) {
            const std::string &text = localized(key);
            drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(), {footerX, footerY}, Theme::U32(Theme::Muted()), text.c_str());
            footerX += TextWidth(fonts.sansCompact, Theme::TextPx::Caption(), text) + 10.0F * scale;
        }
        const std::string &snapshot = localized("workspace.global_dock.physics.footer.snapshot");
        drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(),
                          {regions.footerOrigin.x + regions.footerWidth - metrics.contentPadding -
                               TextWidth(fonts.sansCompact, Theme::TextPx::Caption(), snapshot),
                           footerY},
                          Theme::U32(Theme::Muted()), snapshot.c_str());
    }
}  // namespace Horo::Editor
