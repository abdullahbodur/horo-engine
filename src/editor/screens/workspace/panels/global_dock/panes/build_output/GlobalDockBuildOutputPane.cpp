#include "editor/screens/workspace/panels/global_dock/panes/build_output/GlobalDockBuildOutputPane.h"

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>
#include <utility>

namespace Horo::Editor {
    namespace {
        constexpr float OuterPaddingX = 10.0F;
        constexpr float SearchWidth = 200.0F;
        constexpr float ActionControlWidth = 104.0F;
        constexpr float ControlGap = 8.0F;

        [[nodiscard]] float ToolbarPadX() noexcept {
            return Ui::ScaledLayoutValue(12.0F);
        }

        [[nodiscard]] float ToolbarPadY() noexcept {
            return Ui::ScaledLayoutValue(5.0F);
        }

        [[nodiscard]] float ToolbarHeight() noexcept {
            return DesignSystem::MetricsFor(Theme::GetActiveTokens(), Ui::ComponentSize::Small).minimumHeight;
        }

        [[nodiscard]] ImVec4 StatusColor(const BuildOutputRecord &record) noexcept {
            using enum BuildOutputResult;
            switch (record.result) {
                case Failed:
                case TimedOut:
                    return Theme::Err();
                case Cached:
                    return Theme::Dim();
                case Cancelled:
                    return Theme::Warn();
                case Succeeded:
                    return Theme::Ok();
                case None:
                    break;
            }
            switch (record.severity) {
                case DiagnosticSeverity::Error:
                    return Theme::Err();
                case DiagnosticSeverity::Warning:
                    return Theme::Warn();
                case DiagnosticSeverity::Note:
                    return Theme::Accent();
            }
            return Theme::Text();
        }

        [[nodiscard]] const char *TechnicalStatusText(const BuildOutputRecord &record) noexcept {
            using enum BuildOutputResult;
            switch (record.result) {
                case Succeeded:
                    return "OK";
                case Failed:
                    return "FAILED";
                case Cached:
                    return "CACHED";
                case Cancelled:
                    return "CANCELLED";
                case TimedOut:
                    return "TIMED OUT";
                case None:
                    break;
            }
            switch (record.severity) {
                case DiagnosticSeverity::Error:
                    return "ERROR";
                case DiagnosticSeverity::Warning:
                    return "WARNING";
                case DiagnosticSeverity::Note:
                    return "INFO";
            }
            return "INFO";
        }

        [[nodiscard]] const char *StatusLocalizationKey(const BuildOutputRecord &record) noexcept {
            using enum BuildOutputResult;
            switch (record.result) {
                case Succeeded:
                    return "workspace.global_dock.build_output.row_status.succeeded";
                case Failed:
                    return "workspace.global_dock.build_output.row_status.failed";
                case Cached:
                    return "workspace.global_dock.build_output.row_status.cached";
                case Cancelled:
                    return "workspace.global_dock.build_output.row_status.cancelled";
                case TimedOut:
                    return "workspace.global_dock.build_output.row_status.timed_out";
                case None:
                    break;
            }
            switch (record.severity) {
                case DiagnosticSeverity::Error:
                    return "workspace.global_dock.build_output.row_status.failed";
                case DiagnosticSeverity::Warning:
                    return "workspace.global_dock.build_output.row_status.warning";
                case DiagnosticSeverity::Note:
                    return "workspace.global_dock.build_output.row_status.info";
            }
            return "workspace.global_dock.build_output.row_status.info";
        }

        [[nodiscard]] bool IsOkRecord(const BuildOutputRecord &record) noexcept {
            return record.result == BuildOutputResult::Succeeded ||
                   (record.result == BuildOutputResult::None && record.severity == DiagnosticSeverity::Note);
        }

        [[nodiscard]] bool IsFailedRecord(const BuildOutputRecord &record) noexcept {
            return record.result == BuildOutputResult::Failed || record.result == BuildOutputResult::Cancelled ||
                   record.result == BuildOutputResult::TimedOut ||
                   (record.result == BuildOutputResult::None && record.severity == DiagnosticSeverity::Error);
        }

        [[nodiscard]] bool ContainsCaseInsensitive(const std::string_view text, const std::string_view needle) {
            if (needle.empty())
                return true;
            return std::ranges::search(text, needle, [](const char left, const char right) {
                return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
            }).begin() != text.end();
        }

        [[nodiscard]] std::string FormatTimeOfDay(const std::chrono::system_clock::time_point &timestampUtc) {
            const std::time_t timeT = std::chrono::system_clock::to_time_t(timestampUtc);
            std::tm tmValue{};
#if defined(_WIN32)
            gmtime_s(&tmValue, &timeT);
#else
            gmtime_r(&timeT, &tmValue);
#endif
            return std::format("{:02d}:{:02d}:{:02d}", tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec);
        }

        [[nodiscard]] std::string FormatSource(const std::optional<DiagnosticSourceLocation> &source) {
            if (!source.has_value())
                return {};
            if (source->line == 0U)
                return source->absolutePath;
            if (source->column == 0U)
                return std::format("{}:{}", source->absolutePath, source->line);
            return std::format("{}:{}:{}", source->absolutePath, source->line, source->column);
        }
    }  // namespace

    /** @copydoc GlobalDockBuildOutputPane::Attach */
    void GlobalDockBuildOutputPane::Attach(const IBuildOutputQuery *buildOutputQuery) noexcept {
        m_buildOutputQuery = buildOutputQuery;
        m_snapshot = {};
        m_revision = 0;
        m_filteredIndices.clear();
        m_filterDirty = true;
        m_initialFollowTail = true;
    }

    /** @copydoc GlobalDockBuildOutputPane::Detach */
    void GlobalDockBuildOutputPane::Detach() noexcept {
        m_buildOutputQuery = nullptr;
        m_snapshot = {};
        m_revision = 0;
        m_filteredIndices.clear();
    }

    /** @copydoc GlobalDockBuildOutputPane::Draw */
    void GlobalDockBuildOutputPane::Draw(const ImVec2 &contentOrigin, const float contentWidth, EditorWorkspaceViewCommandData &command,
                                         const EditorGuiContext &context) {
        const bool snapshotChanged = RefreshSnapshot();
        const auto &fonts = context.theme.fonts;
        const float barFullWidth = contentWidth + OuterPaddingX * 2.0F;
        const float scale = std::max(Theme::GetActiveTokens().sizes.uiScale, 0.01F);
        const bool stackedToolbar = contentWidth / scale < 460.0F;
        const float barTotalHeight = ToolbarPadY() * 2.0F + ToolbarHeight() * (stackedToolbar ? 2.0F : 1.0F) +
                                     (stackedToolbar ? Ui::ScaledLayoutValue(ControlGap) : 0.0F);

        ImGui::SetCursorScreenPos(contentOrigin);
        {
            Ui::ScopedCard toolbar("##BuildOutputToolbar", {barFullWidth, barTotalHeight}, ToolbarPadX(), ToolbarPadY());
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float availableLogicalWidth = availableWidth / scale;
            const float searchWidth =
                std::min(SearchWidth, std::max(1.0F, availableLogicalWidth - ActionControlWidth * 3.0F - ControlGap * 3.0F));
            const ImVec2 controlOrigin = ImGui::GetCursorScreenPos();

            const std::array<std::string, 4> statusText{
                context.localization.Get("editor", "workspace.global_dock.build_output.status.all"),
                context.localization.Get("editor", "workspace.global_dock.build_output.status.ok"),
                context.localization.Get("editor", "workspace.global_dock.build_output.status.failed"),
                context.localization.Get("editor", "workspace.global_dock.build_output.status.cached"),
            };
            const std::array<const char *, 4> statusLabels{statusText[0].c_str(), statusText[1].c_str(), statusText[2].c_str(),
                                                           statusText[3].c_str()};
            auto selectedStatus = static_cast<int>(m_statusFilter);
            ImGui::SetNextItemWidth(Ui::ScaledLayoutValue(ActionControlWidth));
            if (Ui::ComboControl("##BuildOutputStatusFilter", &selectedStatus, statusLabels.data(), static_cast<int>(statusLabels.size()),
                                 fonts, Ui::ComboControlOptions{.componentSize = Ui::ComponentSize::Small})) {
                m_statusFilter = static_cast<StatusFilter>(selectedStatus);
                m_filterDirty = true;
            }

            const float rightControlsWidth = Ui::ScaledLayoutValue(searchWidth + ActionControlWidth * 2.0F + ControlGap * 2.0F);
            ImGui::SetCursorScreenPos(
                {stackedToolbar ? controlOrigin.x : controlOrigin.x + std::max(0.0F, availableWidth - rightControlsWidth),
                 stackedToolbar ? controlOrigin.y + ToolbarHeight() + Ui::ScaledLayoutValue(ControlGap) : controlOrigin.y});
            if (const std::string &hint = context.localization.Get("editor", "workspace.global_dock.build_output.search");
                Ui::InputTextControl("##BuildOutputSearch", m_search.data(), m_search.size(), fonts,
                                     Ui::InputTextOptions{.width = searchWidth,
                                                          .hint = hint.c_str(),
                                                          .componentSize = Ui::ComponentSize::Small}))
                m_filterDirty = true;

            ImGui::SameLine(0.0F, Ui::ScaledLayoutValue(ControlGap));
            if (const std::string &clearLabel = context.localization.Get("editor", "workspace.global_dock.build_output.clear");
                Ui::Button({.label = clearLabel.c_str(),
                            .size = {ActionControlWidth, 0.0F},
                            .variant = Ui::ButtonVariant::Secondary,
                            .font = fonts.sansCompact,
                            .baseFontSize = Theme::FontPx::SansCompact,
                            .componentSize = Ui::ComponentSize::Small})) {
                m_search[0] = '\0';
                m_filterDirty = true;
            }

            ImGui::SameLine(0.0F, Ui::ScaledLayoutValue(ControlGap));
            const std::array<std::string, 4> columnText{
                context.localization.Get("editor", "workspace.global_dock.build_output.column.time"),
                context.localization.Get("editor", "workspace.global_dock.build_output.column.status"),
                context.localization.Get("editor", "workspace.global_dock.build_output.column.message"),
                context.localization.Get("editor", "workspace.global_dock.build_output.column.source"),
            };
            const std::array<const char *, 4> columnLabels{columnText[0].c_str(), columnText[1].c_str(), columnText[2].c_str(),
                                                           columnText[3].c_str()};
            const std::string &columnsLabel = context.localization.Get("editor", "workspace.global_dock.build_output.columns");
            static_cast<void>(Ui::MultiSelectField("##BuildOutputColumns", columnsLabel.c_str(), columnLabels, m_columnVisible, fonts,
                                                   ActionControlWidth, Ui::ComponentSize::Small));
        }

        if (m_filterDirty)
            RebuildFilter();

        const float contentY = contentOrigin.y + barTotalHeight;
        const float contentHeight = std::max(1.0F, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - contentY);
        ImGui::SetCursorScreenPos({contentOrigin.x, contentY});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
        ImGui::BeginChild("##BuildOutputScroll", {barFullWidth, contentHeight}, false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoSavedSettings);
        const bool wasAtBottom = ImGui::GetScrollY() >= std::max(0.0F, ImGui::GetScrollMaxY() - 2.0F);

        if (m_snapshot.droppedRecordCount > 0U) {
            const std::string notice =
                std::format("{} {}", context.localization.Get("editor", "workspace.global_dock.build_output.dropped"),
                            m_snapshot.droppedRecordCount);
            Ui::Hint(notice.c_str(), fonts);
        }

        const std::array<Ui::TableColumn, 4> columns{
            Ui::TableColumn{"time", context.localization.Get("editor", "workspace.global_dock.build_output.column.time"), 120.0F,
                            m_columnVisible[0]},
            Ui::TableColumn{"status", context.localization.Get("editor", "workspace.global_dock.build_output.column.status"), 92.0F,
                            m_columnVisible[1]},
            Ui::TableColumn{"message", context.localization.Get("editor", "workspace.global_dock.build_output.column.message"), 0.0F,
                            m_columnVisible[2]},
            Ui::TableColumn{"source", context.localization.Get("editor", "workspace.global_dock.build_output.column.source"), 220.0F,
                            m_columnVisible[3]},
        };
        std::vector<Ui::TableRow> rows;
        rows.reserve(m_filteredIndices.size());
        for (const std::size_t recordIndex : m_filteredIndices) {
            const BuildOutputRecord &record = m_snapshot.records[recordIndex];
            rows.push_back({.cells = {{FormatTimeOfDay(record.timestampUtc), Theme::Muted()},
                                      {context.localization.Get("editor", StatusLocalizationKey(record)), StatusColor(record)},
                                      {record.message, Theme::Text()},
                                      {FormatSource(record.source), record.source.has_value() ? Theme::Accent() : Theme::Muted()}}});
        }
        if (const Ui::TableInteraction interaction =
                Ui::DrawTable({.id = "##BuildOutputTable", .componentSize = Ui::ComponentSize::Small, .selectableCells = true}, columns,
                              rows, fonts);
            interaction.activatedRow.has_value() && interaction.activatedColumn == 3U) {
            const BuildOutputRecord &record = m_snapshot.records[m_filteredIndices[*interaction.activatedRow]];
            if (record.source.has_value()) {
                command.command = EditorWorkspaceViewCommand::OpenDiagnosticSource;
                command.diagnosticSource = DiagnosticSourceRequest{.absolutePath = record.source->absolutePath,
                                                                   .line = record.source->line,
                                                                   .column = record.source->column};
            }
        }

        if (snapshotChanged && (wasAtBottom || m_initialFollowTail))
            ImGui::SetScrollHereY(1.0F);
        m_initialFollowTail = false;
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    bool GlobalDockBuildOutputPane::RefreshSnapshot() {
        if (m_buildOutputQuery == nullptr)
            return false;
        auto changed = m_buildOutputQuery->SnapshotIfChanged(m_revision);
        if (!changed.has_value())
            return false;
        m_snapshot = std::move(*changed);
        m_revision = m_snapshot.revision;
        m_filterDirty = true;
        return true;
    }

    bool GlobalDockBuildOutputPane::PassesStatusFilter(const BuildOutputRecord &record, const StatusFilter filter) noexcept {
        switch (filter) {
            case StatusFilter::All:
                return true;
            case StatusFilter::Ok:
                return IsOkRecord(record);
            case StatusFilter::Failed:
                return IsFailedRecord(record);
            case StatusFilter::Cached:
                return record.result == BuildOutputResult::Cached;
        }
        return true;
    }

    std::vector<std::size_t> GlobalDockBuildOutputPane::ProjectRecords(const std::span<const BuildOutputRecord> records,
                                                                       const StatusFilter statusFilter, const std::string_view search) {
        std::vector<std::size_t> projected;
        projected.reserve(records.size());
        for (std::size_t index = 0; index < records.size(); ++index) {
            const BuildOutputRecord &record = records[index];
            const std::string source = FormatSource(record.source);
            if (!PassesStatusFilter(record, statusFilter))
                continue;
            if (!search.empty() && !ContainsCaseInsensitive(record.stage, search) &&
                !ContainsCaseInsensitive(record.code.Value(), search) && !ContainsCaseInsensitive(record.message, search) &&
                !ContainsCaseInsensitive(source, search) && !ContainsCaseInsensitive(TechnicalStatusText(record), search))
                continue;
            projected.push_back(index);
        }
        return projected;
    }

    void GlobalDockBuildOutputPane::RebuildFilter() {
        m_filteredIndices = ProjectRecords(m_snapshot.records, m_statusFilter, std::string_view{m_search.data()});
        m_filterDirty = false;
    }
}  // namespace Horo::Editor
