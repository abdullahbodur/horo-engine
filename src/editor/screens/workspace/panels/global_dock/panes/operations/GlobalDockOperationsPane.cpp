#include "editor/screens/workspace/panels/global_dock/panes/operations/GlobalDockOperationsPane.h"

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/Localization/ILocalizationService.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace Horo::Editor {
    namespace {
        constexpr float OuterPaddingX = 10.0F;
        constexpr float SearchWidth = 180.0F;
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

        [[nodiscard]] ImVec4 StatusColor(const OperationState state) noexcept {
            switch (state) {
                case OperationState::Failed:
                    return Theme::Err();
                case OperationState::Running:
                    return Theme::Accent();
                case OperationState::Waiting:
                case OperationState::Cancelling:
                case OperationState::Cancelled:
                    return Theme::Warn();
                case OperationState::Succeeded:
                    return Theme::Ok();
                case OperationState::Queued:
                    return Theme::Muted();
            }
            return Theme::Text();
        }

        [[nodiscard]] const char *TechnicalStatusText(const OperationState state) noexcept {
            switch (state) {
                case OperationState::Queued:
                    return "QUEUED";
                case OperationState::Running:
                    return "RUNNING";
                case OperationState::Waiting:
                    return "WAITING";
                case OperationState::Cancelling:
                    return "CANCELLING";
                case OperationState::Succeeded:
                    return "OK";
                case OperationState::Failed:
                    return "FAILED";
                case OperationState::Cancelled:
                    return "CANCELLED";
            }
            return "QUEUED";
        }

        [[nodiscard]] const char *StatusLocalizationKey(const OperationState state) noexcept {
            switch (state) {
                case OperationState::Queued:
                    return "workspace.global_dock.operations.state.queued";
                case OperationState::Running:
                    return "workspace.global_dock.operations.state.running";
                case OperationState::Waiting:
                    return "workspace.global_dock.operations.state.waiting";
                case OperationState::Cancelling:
                    return "workspace.global_dock.operations.state.cancelling";
                case OperationState::Succeeded:
                    return "workspace.global_dock.operations.state.succeeded";
                case OperationState::Failed:
                    return "workspace.global_dock.operations.state.failed";
                case OperationState::Cancelled:
                    return "workspace.global_dock.operations.state.cancelled";
            }
            return "workspace.global_dock.operations.state.queued";
        }

        [[nodiscard]] std::string OperationTitle(const OperationRecord &operation, const EditorGuiContext &context) {
            switch (operation.kind) {
                case OperationKind::Build:
                    return context.localization.Get("editor", "workspace.global_dock.operations.kind.build");
                case OperationKind::Cook:
                    return context.localization.Get("editor", "workspace.global_dock.operations.kind.cook");
                case OperationKind::Import:
                    return context.localization.Get("editor", "workspace.global_dock.operations.kind.import");
                case OperationKind::Index:
                    return context.localization.Get("editor", "workspace.global_dock.operations.kind.index");
                case OperationKind::Validation:
                    return context.localization.Get("editor", "workspace.global_dock.operations.kind.validation");
                case OperationKind::Other:
                    return operation.title;
            }
            return operation.title;
        }

        [[nodiscard]] bool ContainsCaseInsensitive(const std::string_view text, const std::string_view needle) {
            if (needle.empty())
                return true;
            if (needle.size() > text.size())
                return false;
            return std::ranges::search(text, needle, [](const char left, const char right) {
                return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
            }).begin() != text.end();
        }

        [[nodiscard]] std::string FormatProgress(const std::optional<float> progress) {
            if (!progress.has_value())
                return "—";
            char text[8];
            std::snprintf(text, sizeof(text), "%d%%", static_cast<int>(std::clamp(*progress, 0.0F, 1.0F) * 100.0F + 0.5F));
            return text;
        }
    }  // namespace

    /** @copydoc GlobalDockOperationsPane::Attach */
    void GlobalDockOperationsPane::Attach(const IOperationQuery *operationQuery, IOperationControl *operationControl) noexcept {
        m_operationQuery = operationQuery;
        m_operationControl = operationControl;
        m_snapshot = {};
        m_revision = 0;
        m_filteredIndices.clear();
        m_filterDirty = true;
        m_initialFollowTail = true;
    }

    /** @copydoc GlobalDockOperationsPane::Detach */
    void GlobalDockOperationsPane::Detach() noexcept {
        m_operationQuery = nullptr;
        m_operationControl = nullptr;
        m_snapshot = {};
        m_revision = 0;
        m_filteredIndices.clear();
    }

    /** @copydoc GlobalDockOperationsPane::Draw */
    void GlobalDockOperationsPane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) {
        const bool snapshotChanged = RefreshSnapshot();
        const auto &fonts = context.theme.fonts;
        const float barFullWidth = contentWidth + OuterPaddingX * 2.0F;
        const float scale = std::max(Theme::GetActiveTokens().sizes.uiScale, 0.01F);
        const bool stackedToolbar = contentWidth / scale < 360.0F;
        const float barTotalHeight = ToolbarPadY() * 2.0F + ToolbarHeight() * (stackedToolbar ? 2.0F : 1.0F) +
                                     (stackedToolbar ? Ui::ScaledLayoutValue(ControlGap) : 0.0F);
        ImGui::SetCursorScreenPos(contentOrigin);
        {
            Ui::ScopedCard toolbar("##OperationsToolbar", {barFullWidth, barTotalHeight}, ToolbarPadX(), ToolbarPadY());
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float availableLogicalWidth = availableWidth / scale;
            const float searchWidth =
                std::min(SearchWidth, std::max(1.0F, availableLogicalWidth - ActionControlWidth * 2.0F - ControlGap * 2.0F));
            const float controlsWidth = Ui::ScaledLayoutValue(searchWidth + ActionControlWidth * 2.0F + ControlGap * 2.0F);
            const ImVec2 controlOrigin = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(
                {stackedToolbar ? controlOrigin.x : controlOrigin.x + std::max(0.0F, availableWidth - controlsWidth),
                 stackedToolbar ? controlOrigin.y + ToolbarHeight() + Ui::ScaledLayoutValue(ControlGap) : controlOrigin.y});

            const std::string &hint = context.localization.Get("editor", "workspace.global_dock.operations.search");
            if (Ui::InputTextControl("##OperationsSearch", m_search.data(), m_search.size(), fonts, false, searchWidth, hint.c_str(), 0.0F,
                                     Ui::ComponentSize::Small))
                m_filterDirty = true;

            ImGui::SameLine(0.0F, Ui::ScaledLayoutValue(ControlGap));
            const std::string &clearLabel = context.localization.Get("editor", "workspace.global_dock.operations.clear");
            if (Ui::Button({.label = clearLabel.c_str(),
                            .size = {ActionControlWidth, 0.0F},
                            .variant = Ui::ButtonVariant::Secondary,
                            .font = fonts.sansCompact,
                            .baseFontSize = Theme::FontPx::SansCompact,
                            .componentSize = Ui::ComponentSize::Small})) {
                m_search[0] = '\0';
                m_filterDirty = true;
            }

            ImGui::SameLine(0.0F, Ui::ScaledLayoutValue(ControlGap));
            const std::array<std::string, 6> columnText{
                context.localization.Get("editor", "workspace.global_dock.operations.column.status"),
                context.localization.Get("editor", "workspace.global_dock.operations.column.operation"),
                context.localization.Get("editor", "workspace.global_dock.operations.column.phase"),
                context.localization.Get("editor", "workspace.global_dock.operations.column.progress"),
                context.localization.Get("editor", "workspace.global_dock.operations.column.message"),
                context.localization.Get("editor", "workspace.global_dock.operations.column.action"),
            };
            const std::array<const char *, 6> columnLabels{columnText[0].c_str(), columnText[1].c_str(), columnText[2].c_str(),
                                                           columnText[3].c_str(), columnText[4].c_str(), columnText[5].c_str()};
            const std::string &columnsLabel = context.localization.Get("editor", "workspace.global_dock.operations.columns");
            static_cast<void>(Ui::MultiSelectField("##OperationsColumns", columnsLabel.c_str(), columnLabels, m_columnVisible, fonts,
                                                   ActionControlWidth, Ui::ComponentSize::Small));
        }

        if (m_filterDirty)
            RebuildFilter();

        const float contentY = contentOrigin.y + barTotalHeight;
        const float contentHeight = std::max(1.0F, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - contentY);
        ImGui::SetCursorScreenPos({contentOrigin.x, contentY});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
        ImGui::BeginChild("##OperationsScroll", {barFullWidth, contentHeight}, false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoSavedSettings);
        const bool wasAtBottom = ImGui::GetScrollY() >= std::max(0.0F, ImGui::GetScrollMaxY() - 2.0F);

        if (m_snapshot.droppedTerminalCount > 0U) {
            const std::string notice = context.localization.Get("editor", "workspace.global_dock.operations.dropped") + " " +
                                       std::to_string(m_snapshot.droppedTerminalCount);
            Ui::Hint(notice.c_str(), fonts);
        }

        const std::array<Ui::TableColumn, 6> columns{
            Ui::TableColumn{"status", context.localization.Get("editor", "workspace.global_dock.operations.column.status"), 100.0F,
                            m_columnVisible[0]},
            Ui::TableColumn{"operation", context.localization.Get("editor", "workspace.global_dock.operations.column.operation"), 170.0F,
                            m_columnVisible[1]},
            Ui::TableColumn{"phase", context.localization.Get("editor", "workspace.global_dock.operations.column.phase"), 130.0F,
                            m_columnVisible[2]},
            Ui::TableColumn{"progress", context.localization.Get("editor", "workspace.global_dock.operations.column.progress"), 90.0F,
                            m_columnVisible[3]},
            Ui::TableColumn{"message", context.localization.Get("editor", "workspace.global_dock.operations.column.message"), 0.0F,
                            m_columnVisible[4]},
            Ui::TableColumn{"action", context.localization.Get("editor", "workspace.global_dock.operations.column.action"), 100.0F,
                            m_columnVisible[5]},
        };
        const std::string &cancelLabel = context.localization.Get("editor", "workspace.global_dock.operations.cancel");
        std::vector<Ui::TableRow> rows;
        rows.reserve(m_filteredIndices.size());
        for (const std::size_t operationIndex : m_filteredIndices) {
            const OperationRecord &operation = m_snapshot.operations[operationIndex];
            const bool canCancel = operation.cancellable && operation.state != OperationState::Cancelling &&
                                   operation.state != OperationState::Succeeded && operation.state != OperationState::Failed &&
                                   operation.state != OperationState::Cancelled;
            rows.push_back(
                {.cells = {{context.localization.Get("editor", StatusLocalizationKey(operation.state)), StatusColor(operation.state)},
                           {OperationTitle(operation, context), Theme::Text()},
                           {operation.phase, Theme::Muted()},
                           {FormatProgress(operation.progress), Theme::Muted()},
                           {operation.message, Theme::Muted()},
                           {canCancel ? cancelLabel : std::string{}, canCancel ? Theme::Warn() : Theme::Muted()}}});
        }
        const Ui::TableInteraction interaction =
            Ui::DrawTable({.id = "##OperationsTable", .componentSize = Ui::ComponentSize::Small, .selectableCells = true}, columns, rows,
                          fonts);
        if (interaction.activatedRow.has_value() && interaction.activatedColumn == 5U && m_operationControl != nullptr) {
            const OperationRecord &operation = m_snapshot.operations[m_filteredIndices[*interaction.activatedRow]];
            static_cast<void>(m_operationControl->RequestCancel(operation.id));
        }

        if (snapshotChanged && (wasAtBottom || m_initialFollowTail))
            ImGui::SetScrollHereY(1.0F);
        m_initialFollowTail = false;
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    bool GlobalDockOperationsPane::RefreshSnapshot() {
        if (m_operationQuery == nullptr)
            return false;
        auto changed = m_operationQuery->SnapshotIfChanged(m_revision);
        if (!changed.has_value())
            return false;
        m_snapshot = std::move(*changed);
        m_revision = m_snapshot.revision;
        m_filterDirty = true;
        return true;
    }

    std::vector<std::size_t> GlobalDockOperationsPane::ProjectRecords(const std::span<const OperationRecord> operations,
                                                                      const std::string_view search) {
        std::vector<std::size_t> projected;
        projected.reserve(operations.size());
        for (std::size_t index = 0; index < operations.size(); ++index) {
            const OperationRecord &operation = operations[index];
            if (!search.empty() && !ContainsCaseInsensitive(operation.title, search) && !ContainsCaseInsensitive(operation.phase, search) &&
                !ContainsCaseInsensitive(operation.message, search) &&
                !ContainsCaseInsensitive(TechnicalStatusText(operation.state), search))
                continue;
            projected.push_back(index);
        }
        return projected;
    }

    void GlobalDockOperationsPane::RebuildFilter() {
        m_filteredIndices = ProjectRecords(m_snapshot.operations, std::string_view{m_search.data()});
        m_filterDirty = false;
    }
}  // namespace Horo::Editor
