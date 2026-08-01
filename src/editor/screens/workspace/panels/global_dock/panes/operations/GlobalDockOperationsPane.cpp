#include "editor/screens/workspace/panels/global_dock/panes/operations/GlobalDockOperationsPane.h"

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/Localization/ILocalizationService.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace Horo::Editor {
    namespace {
        constexpr float OuterPaddingX = 10.0F;

        [[nodiscard]] float ToolbarPadX() noexcept {
            return Ui::ScaledLayoutValue(12.0F);
        }

        [[nodiscard]] float ToolbarPadY() noexcept {
            return Ui::ScaledLayoutValue(5.0F);
        }

        constexpr float SearchWidth = 180.0F;
        constexpr float ActionControlWidth = 104.0F;
        constexpr float ControlGap = 8.0F;
        constexpr std::string_view OpCategoryPrefix = "Op.";

        [[nodiscard]] float ToolbarHeight() noexcept {
            return DesignSystem::MetricsFor(Theme::GetActiveTokens(), Ui::ComponentSize::Small).minimumHeight;
        }

        [[nodiscard]] ImVec4 StatusColor(const std::string_view status) noexcept {
            if (status == "FAILED")
                return Theme::Err();
            if (status == "RUNNING")
                return Theme::Accent();
            if (status == "CANCELLED")
                return Theme::Warn();
            return Theme::Ok();
        }

        [[nodiscard]] bool ContainsCaseInsensitive(const std::string_view text, const std::string_view needle) {
            if (needle.empty())
                return true;
            if (needle.size() > text.size())
                return false;
            return std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](const char left, const char right) {
                return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
            }) != text.end();
        }

        [[nodiscard]] std::string_view StripOpPrefix(const std::string_view category) noexcept {
            return category.size() > OpCategoryPrefix.size() ? category.substr(OpCategoryPrefix.size()) : category;
        }

        [[nodiscard]] bool IsOperationStatus(const std::string_view value) noexcept {
            return value == "RUNNING" || value == "OK" || value == "FAILED" || value == "CANCELLED";
        }

        [[nodiscard]] std::string_view OperationStatus(const Log::StructuredLogRecord &record) noexcept {
            if (IsOperationStatus(record.context))
                return record.context;
            const std::string_view name = StripOpPrefix(record.category);
            const std::size_t separator = name.rfind('.');
            if (separator != std::string_view::npos && IsOperationStatus(name.substr(separator + 1)))
                return name.substr(separator + 1);
            return "OK";
        }

        [[nodiscard]] std::string_view OperationName(const Log::StructuredLogRecord &record) noexcept {
            const std::string_view name = StripOpPrefix(record.category);
            const std::size_t separator = name.rfind('.');
            if (separator != std::string_view::npos && IsOperationStatus(name.substr(separator + 1)))
                return name.substr(0, separator);
            return name;
        }

    }  // namespace

    /** @copydoc GlobalDockOperationsPane::Attach */
    void GlobalDockOperationsPane::Attach(const Log::IStructuredLogQuery *logQuery) noexcept {
        m_logQuery = logQuery;
        m_filterDirty = true;
    }

    /** @copydoc GlobalDockOperationsPane::Detach */
    void GlobalDockOperationsPane::Detach() noexcept {
        m_logQuery = nullptr;
        m_textSelectionActive = false;
    }

    /** @copydoc GlobalDockOperationsPane::Draw */
    void GlobalDockOperationsPane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) {
        const bool snapshotChanged = RefreshSnapshot();
        const auto &fonts = context.theme.fonts;

        const float barFullWidth = contentWidth + OuterPaddingX * 2.0F;
        const float barTotalHeight = ToolbarPadY() * 2.0F + ToolbarHeight();
        ImGui::SetCursorScreenPos(contentOrigin);
        {
            Ui::ScopedCard toolbar("##OperationsToolbar", {barFullWidth, barTotalHeight}, ToolbarPadX(), ToolbarPadY());
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float scale = Theme::GetActiveTokens().sizes.uiScale;
            const float availableLogicalWidth = availableWidth / std::max(scale, 0.01F);
            const float searchWidth =
                std::min(SearchWidth, std::max(1.0F, availableLogicalWidth - ActionControlWidth * 2.0F - ControlGap * 2.0F));
            const float controlsWidth = Ui::ScaledLayoutValue(searchWidth + ActionControlWidth * 2.0F + ControlGap * 2.0F);
            const ImVec2 controlOrigin = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos({controlOrigin.x + std::max(0.0F, availableWidth - controlsWidth), controlOrigin.y});

            const std::string &hint = context.localization.Get("editor", "workspace.global_dock.operations.search");
            if (Ui::InputTextControl("##OperationsSearch", m_search.data(), m_search.size(), fonts, false, searchWidth, hint.c_str(), 0.0F,
                                     Ui::ComponentSize::Small)) {
                m_filterDirty = true;
            }

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
            const std::array<const char *, 3> columnLabels{"Status", "Operation", "Message"};
            if (Ui::MultiSelectField("##OperationsColumns", "Columns", columnLabels, m_columnVisible, fonts, ActionControlWidth,
                                     Ui::ComponentSize::Small)) {
                m_filterDirty = true;
            }
        }

        // ── Rebuild filter ──────────────────────────────────────
        const bool rebuildSelectableText = m_filterDirty && !m_textSelectionActive;
        if (rebuildSelectableText)
            RebuildFilter();

        // ── Content ─────────────────────────────────────────────
        const float contentY = contentOrigin.y + barTotalHeight;
        const float contentHeight = std::max(1.0F, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - contentY);
        ImGui::SetCursorScreenPos({contentOrigin.x, contentY});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
        ImGui::BeginChild("##OperationsScroll", {barFullWidth, contentHeight}, false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoSavedSettings);
        const bool wasAtBottom = ImGui::GetScrollY() >= std::max(0.0F, ImGui::GetScrollMaxY() - 2.0F);

        if (m_filteredIndices.empty()) {
            if (rebuildSelectableText) {
                m_selectableText.clear();
                m_lineLayouts.clear();
            }
            m_textSelectionActive = false;
            const std::array<const Ui::TableColumn, 3> columns{Ui::TableColumn{"status", "Status", 92.0F, m_columnVisible[0]},
                                                               Ui::TableColumn{"operation", "Operation", 150.0F, m_columnVisible[1]},
                                                               Ui::TableColumn{"message", "Message", 0.0F, m_columnVisible[2]}};
            const std::array<Ui::TableRow, 0> rows{};
            Ui::DrawTable({.id = "##OperationsEmptyTable", .componentSize = Ui::ComponentSize::Small, .selectableCells = false}, columns,
                          rows, context.theme.fonts);
        } else {
            std::vector<Ui::TableColumn> columns{
                {"status", "STATUS", 92.0F, m_columnVisible[0]},
                {"operation", "OPERATION", 150.0F, m_columnVisible[1]},
                {"message", "MESSAGE", 0.0F, m_columnVisible[2]},
            };
            std::vector<Ui::TableRow> rows;
            rows.reserve(m_filteredIndices.size());
            for (const std::size_t recordIndex : m_filteredIndices) {
                const Log::StructuredLogRecord &record = *m_snapshot.records[recordIndex];
                const std::string_view status = OperationStatus(record);
                rows.push_back({
                    .cells =
                        {
                            {std::string(status), StatusColor(status)},
                            {std::string(OperationName(record)), Theme::Text()},
                            {record.message, Theme::Muted()},
                        },
                });
            }
            Ui::DrawTable({.id = "##OperationsTable", .componentSize = Ui::ComponentSize::Small, .selectableCells = true}, columns, rows,
                          fonts);
            m_textSelectionActive = true;
        }

        if (snapshotChanged && !m_textSelectionActive && (wasAtBottom || m_initialFollowTail)) {
            ImGui::SetScrollHereY(1.0F);
        }
        m_initialFollowTail = false;
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    bool GlobalDockOperationsPane::RefreshSnapshot() {
        if (m_logQuery == nullptr)
            return false;
        auto changed = m_logQuery->SnapshotIfChanged(m_revision);
        if (!changed.has_value())
            return false;
        m_snapshot = std::move(*changed);
        m_revision = m_snapshot.revision;
        m_filterDirty = true;
        return true;
    }

    void GlobalDockOperationsPane::RebuildFilter() {
        m_filteredIndices.clear();
        m_filteredIndices.reserve(m_snapshot.records.size());
        const std::string_view search{m_search.data()};
        for (std::size_t index = 0; index < m_snapshot.records.size(); ++index) {
            const Log::StructuredLogRecord &record = *m_snapshot.records[index];

            if (record.category.size() < OpCategoryPrefix.size() ||
                record.category.compare(0, OpCategoryPrefix.size(), OpCategoryPrefix) != 0) {
                continue;
            }

            if (!search.empty() && !ContainsCaseInsensitive(record.category, search) && !ContainsCaseInsensitive(record.message, search) &&
                !ContainsCaseInsensitive(record.context, search)) {
                continue;
            }
            m_filteredIndices.push_back(index);
        }
        m_filterDirty = false;
    }
}  // namespace Horo::Editor
