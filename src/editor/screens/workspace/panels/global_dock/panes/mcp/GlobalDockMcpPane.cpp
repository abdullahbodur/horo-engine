#include "editor/screens/workspace/panels/global_dock/panes/mcp/GlobalDockMcpPane.h"

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
        enum class Permission : unsigned char {
            Read,
            Mutation,
        };

        enum class Status : unsigned char {
            Done,
            Approval,
            Denied,
        };

        struct AuditRow {
            std::string_view time;
            std::string_view tool;
            Permission permission;
            std::string_view requestKey;
            Status status;
        };

        constexpr std::array AuditRows{
            AuditRow{"12:08:14", "scene.query", Permission::Read, "workspace.global_dock.mcp.request.scene_query", Status::Done},
            AuditRow{"12:08:08", "asset.list", Permission::Read, "workspace.global_dock.mcp.request.asset_list", Status::Done},
            AuditRow{"12:07:51", "scene.move", Permission::Mutation, "workspace.global_dock.mcp.request.scene_move", Status::Approval},
            AuditRow{"12:07:39", "build.trigger", Permission::Mutation, "workspace.global_dock.mcp.request.build_trigger", Status::Denied},
        };

        [[nodiscard]] bool ContainsCaseInsensitive(const std::string_view value, const std::string_view query) {
            if (query.empty())
                return true;
            return std::search(value.begin(), value.end(), query.begin(), query.end(), [](const char lhs, const char rhs) {
                return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
            }) != value.end();
        }

        [[nodiscard]] const char *PermissionKey(const Permission permission) noexcept {
            return permission == Permission::Read ? "workspace.global_dock.mcp.permission.read"
                                                  : "workspace.global_dock.mcp.permission.mutation";
        }

        [[nodiscard]] const char *StatusKey(const Status status) noexcept {
            switch (status) {
                case Status::Done:
                    return "workspace.global_dock.mcp.status.done";
                case Status::Approval:
                    return "workspace.global_dock.mcp.status.approval";
                case Status::Denied:
                    return "workspace.global_dock.mcp.status.denied";
            }
            return "workspace.global_dock.mcp.status.denied";
        }

        [[nodiscard]] GlobalDockTone StatusTone(const Status status) noexcept {
            switch (status) {
                case Status::Done:
                    return GlobalDockTone::Positive;
                case Status::Approval:
                    return GlobalDockTone::Warning;
                case Status::Denied:
                    return GlobalDockTone::Error;
            }
            return GlobalDockTone::Neutral;
        }

        [[nodiscard]] float TextWidth(ImFont *font, const float size, const std::string &text) {
            ImFont *resolved = font != nullptr ? font : ImGui::GetFont();
            return resolved->CalcTextSizeA(size, FLT_MAX, 0.0F, text.c_str()).x;
        }
    }  // namespace

    void GlobalDockMcpPane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) {
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
        const GlobalDockToolbarChipProps all{.id = "McpAll",
                                             .label = localized("workspace.global_dock.mcp.filter.all"),
                                             .count = 18U,
                                             .active = m_filter == Filter::All};
        const GlobalDockToolbarChipProps mutations{.id = "McpMutations",
                                                   .label = localized("workspace.global_dock.mcp.filter.mutations"),
                                                   .count = 3U,
                                                   .tone = GlobalDockTone::Warning,
                                                   .active = m_filter == Filter::Mutations};
        const GlobalDockToolbarChipProps errors{.id = "McpErrors",
                                                .label = localized("workspace.global_dock.mcp.filter.errors"),
                                                .count = 1U,
                                                .tone = GlobalDockTone::Error,
                                                .active = m_filter == Filter::Errors};
        const GlobalDockToolbarChipProps pause{.id = "McpPause",
                                               .label = localized(m_paused ? "workspace.global_dock.mcp.resume"
                                                                           : "workspace.global_dock.mcp.pause"),
                                               .active = m_paused,
                                               .icon = m_paused ? Ui::UiIcon::Play : Ui::UiIcon::Pause};
        const GlobalDockToolbarChipProps exportAudit{.id = "McpExport",
                                                     .label = localized("workspace.global_dock.mcp.export"),
                                                     .icon = Ui::UiIcon::Download};
        const float allWidth = MeasureGlobalDockToolbarChip(all, fonts);
        const float mutationWidth = MeasureGlobalDockToolbarChip(mutations, fonts);
        const float errorWidth = MeasureGlobalDockToolbarChip(errors, fonts);
        const float pauseWidth = MeasureGlobalDockToolbarChip(pause, fonts);
        const float exportWidth = MeasureGlobalDockToolbarChip(exportAudit, fonts);
        const float sessionWidth = 128.0F * scale;
        const float fixedWidth =
            allWidth + mutationWidth + errorWidth + sessionWidth + pauseWidth + exportWidth + metrics.toolbarGap * 7.0F + scale;
        const float searchWidth = std::max(180.0F * scale, regions.toolbarWidth - metrics.toolbarPaddingX * 2.0F - fixedWidth);
        float x = regions.toolbarOrigin.x + metrics.toolbarPaddingX;

        ImGui::SetCursorScreenPos({x, controlY});
        const std::string &searchHint = localized("workspace.global_dock.mcp.search");
        static_cast<void>(Ui::InputTextControl("##McpSearch", m_search.data(), m_search.size(), fonts,
                                               {.width = searchWidth / scale,
                                                .hint = searchHint.c_str(),
                                                .prefixIconWidth = 20.0F,
                                                .componentSize = Ui::ComponentSize::Small,
                                                .surface = Ui::InputTextSurface::BottomDockToolbar}));
        Ui::DrawEditorIcon(ImGui::GetWindowDrawList(), Ui::UiIcon::Search, {x + 8.0F * scale, controlY + 8.0F * scale},
                           {14.0F * scale, 14.0F * scale}, Theme::U32(Theme::Dim()), fonts.icon);
        x += searchWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, allWidth, all, fonts))
            m_filter = Filter::All;
        x += allWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, mutationWidth, mutations, fonts))
            m_filter = Filter::Mutations;
        x += mutationWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, errorWidth, errors, fonts))
            m_filter = Filter::Errors;
        x += errorWidth + metrics.toolbarGap;
        DrawGlobalDockToolbarSeparator(x, controlY);
        x += metrics.toolbarGap + scale;

        const std::array<std::string, 2> sessionLabels{localized("workspace.global_dock.mcp.session.current"),
                                                       localized("workspace.global_dock.mcp.session.last_hour")};
        const std::array<const char *, 2> sessionItems{sessionLabels[0].c_str(), sessionLabels[1].c_str()};
        ImGui::SetCursorScreenPos({x, controlY});
        ImGui::SetNextItemWidth(sessionWidth);
        static_cast<void>(Ui::ComboControl("McpSession", &m_sessionSelection, sessionItems.data(), static_cast<int>(sessionItems.size()),
                                           fonts,
                                           {.height = GlobalDockLayout::ControlHeight,
                                            .componentSize = Ui::ComponentSize::Small,
                                            .surface = Ui::ComboControlSurface::BottomDockToolbar}));
        x += sessionWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, pauseWidth, pause, fonts))
            m_paused = !m_paused;
        x += pauseWidth + metrics.toolbarGap;
        static_cast<void>(DrawGlobalDockToolbarChip({x, controlY}, exportWidth, exportAudit, fonts));

        const ImVec2 headerMin = regions.contentOrigin;
        DrawGlobalDockTableHeaderSurface(headerMin, regions.contentWidth, metrics.tableHeaderHeight);
        const float timeX = headerMin.x + metrics.contentPadding;
        const float toolX = timeX + 72.0F * scale + metrics.columnGap;
        const float permissionX = toolX + 118.0F * scale + metrics.columnGap;
        const float requestX = permissionX + 100.0F * scale + metrics.columnGap;
        const float statusX = headerMin.x + regions.contentWidth - metrics.contentPadding - 90.0F * scale;
        const float headerY = headerMin.y + (metrics.tableHeaderHeight - Theme::TextPx::Caption()) * 0.5F;
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const auto headerText = [&](const float textX, const char *key) {
            drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(), {textX, headerY}, Theme::U32(Theme::Muted()),
                              localized(key).c_str());
        };
        headerText(timeX, "workspace.global_dock.mcp.column.time");
        headerText(toolX, "workspace.global_dock.mcp.column.tool");
        headerText(permissionX, "workspace.global_dock.mcp.column.permission");
        headerText(requestX, "workspace.global_dock.mcp.column.request");
        headerText(statusX, "workspace.global_dock.mcp.column.status");

        const ImVec2 rowsOrigin{regions.contentOrigin.x, regions.contentOrigin.y + metrics.tableHeaderHeight};
        const float rowsHeight = std::max(1.0F, regions.contentHeight - metrics.tableHeaderHeight);
        ImGui::SetCursorScreenPos(rowsOrigin);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BottomDockContentSurface());
        ImGui::BeginChild("##McpRows", {regions.contentWidth, rowsHeight}, false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoSavedSettings);
        for (std::size_t index = 0; index < AuditRows.size(); ++index) {
            const AuditRow &row = AuditRows[index];
            const std::string permission = localized(PermissionKey(row.permission));
            const std::string request = localized(row.requestKey.data());
            const std::string status = localized(StatusKey(row.status));
            const std::string_view search{m_search.data()};
            const bool filterMatches = m_filter == Filter::All ||
                                       (m_filter == Filter::Mutations && row.permission == Permission::Mutation) ||
                                       (m_filter == Filter::Errors && row.status == Status::Denied);
            const bool searchMatches = ContainsCaseInsensitive(row.tool, search) || ContainsCaseInsensitive(permission, search) ||
                                       ContainsCaseInsensitive(request, search) || ContainsCaseInsensitive(status, search);
            if (!filterMatches || !searchMatches)
                continue;

            ImGui::PushID(static_cast<int>(index));
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##audit-row", {regions.contentWidth, metrics.tableRowHeight});
            DrawGlobalDockTableRowSurface(rowMin, regions.contentWidth, metrics.tableRowHeight, ImGui::IsItemHovered());
            ImDrawList *rowsDrawList = ImGui::GetWindowDrawList();
            const float textY = rowMin.y + (metrics.tableRowHeight - Theme::TextPx::Label()) * 0.5F;
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {timeX, textY},
                                      {toolX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Muted(), row.time);
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {toolX, textY},
                                      {permissionX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Accent(), row.tool);
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {permissionX, textY},
                                      {requestX - metrics.columnGap, rowMin.y + metrics.tableRowHeight},
                                      row.permission == Permission::Mutation ? GlobalDockToneColor(GlobalDockTone::Warning) : Theme::Text(),
                                      permission);
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {requestX, textY},
                                      {statusX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), request);
            static_cast<void>(DrawGlobalDockStatePill({statusX, rowMin.y + (metrics.tableRowHeight - 22.0F * scale) * 0.5F}, status,
                                                      StatusTone(row.status), fonts));
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        DrawGlobalDockFooterSurface(regions.footerOrigin, regions.footerWidth, metrics.footerHeight);
        const float footerY = regions.footerOrigin.y + (metrics.footerHeight - Theme::TextPx::Caption()) * 0.5F;
        const std::string &bridge = localized("workspace.global_dock.mcp.footer.bridge_active");
        const float bridgeWidth = DrawGlobalDockStatePill({regions.footerOrigin.x + metrics.contentPadding,
                                                           regions.footerOrigin.y + (metrics.footerHeight - 22.0F * scale) * 0.5F},
                                                          bridge, GlobalDockTone::Positive, fonts);
        drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(),
                          {regions.footerOrigin.x + metrics.contentPadding + bridgeWidth + metrics.toolbarGap, footerY},
                          Theme::U32(Theme::Muted()), "horo.mcp.bridge@0.4.0");
        const std::string &audit = localized("workspace.global_dock.mcp.footer.audit");
        drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(),
                          {regions.footerOrigin.x + regions.footerWidth - metrics.contentPadding -
                               TextWidth(fonts.sansCompact, Theme::TextPx::Caption(), audit),
                           footerY},
                          Theme::U32(Theme::Muted()), audit.c_str());
    }
}  // namespace Horo::Editor
