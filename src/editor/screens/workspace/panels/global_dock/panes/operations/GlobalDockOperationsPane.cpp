#include "editor/screens/workspace/panels/global_dock/panes/operations/GlobalDockOperationsPane.h"

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/screens/workspace/panels/global_dock/GlobalDockPaneChrome.h"
#include "editor/screens/workspace/panels/global_dock/GlobalDockPaneLayout.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace Horo::Editor {
    namespace {
        [[nodiscard]] GlobalDockTone StatusTone(const OperationState state) noexcept {
            using enum OperationState;
            switch (state) {
                case Running:
                case Succeeded:
                    return GlobalDockTone::Positive;
                case Waiting:
                case Cancelling:
                case Cancelled:
                    return GlobalDockTone::Warning;
                case Failed:
                    return GlobalDockTone::Error;
                case Queued:
                    return GlobalDockTone::Neutral;
            }
            return GlobalDockTone::Neutral;
        }

        [[nodiscard]] const char *TechnicalStatusText(const OperationState state) noexcept {
            using enum OperationState;
            switch (state) {
                case Queued:
                    return "QUEUED";
                case Running:
                    return "RUNNING";
                case Waiting:
                    return "WAITING";
                case Cancelling:
                    return "CANCELLING";
                case Succeeded:
                    return "OK";
                case Failed:
                    return "FAILED";
                case Cancelled:
                    return "CANCELLED";
            }
            return "QUEUED";
        }

        [[nodiscard]] const char *StatusLocalizationKey(const OperationState state) noexcept {
            using enum OperationState;
            switch (state) {
                case Queued:
                    return "workspace.global_dock.operations.state.queued";
                case Running:
                    return "workspace.global_dock.operations.state.running";
                case Waiting:
                    return "workspace.global_dock.operations.state.waiting";
                case Cancelling:
                    return "workspace.global_dock.operations.state.cancelling";
                case Succeeded:
                    return "workspace.global_dock.operations.state.succeeded";
                case Failed:
                    return "workspace.global_dock.operations.state.failed";
                case Cancelled:
                    return "workspace.global_dock.operations.state.cancelled";
            }
            return "workspace.global_dock.operations.state.queued";
        }

        [[nodiscard]] std::string OperationTitle(const OperationRecord &operation, const EditorGuiContext &context) {
            if (!operation.title.empty())
                return operation.title;
            using enum OperationKind;
            switch (operation.kind) {
                case Build:
                    return context.localization.Get("editor", "workspace.global_dock.operations.kind.build");
                case Cook:
                    return context.localization.Get("editor", "workspace.global_dock.operations.kind.cook");
                case Import:
                    return context.localization.Get("editor", "workspace.global_dock.operations.kind.import");
                case Index:
                    return context.localization.Get("editor", "workspace.global_dock.operations.kind.index");
                case Validation:
                    return context.localization.Get("editor", "workspace.global_dock.operations.kind.validation");
                case Other:
                    return context.localization.Get("editor", "workspace.global_dock.operations.kind.other");
            }
            return {};
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
            return std::format("{}%", static_cast<int>(std::clamp(*progress, 0.0F, 1.0F) * 100.0F + 0.5F));
        }

        [[nodiscard]] std::string FormatElapsed(const OperationRecord &operation) {
            if (operation.startedAt == std::chrono::steady_clock::time_point{})
                return "—";
            const auto end = operation.finishedAt.value_or(std::chrono::steady_clock::now());
            const auto elapsed =
                std::max(std::chrono::seconds{0}, std::chrono::duration_cast<std::chrono::seconds>(end - operation.startedAt));
            const long long totalSeconds = elapsed.count();
            return std::format("{:02}:{:02}", totalSeconds / 60, totalSeconds % 60);
        }

        [[nodiscard]] bool IsRunningState(const OperationState state) noexcept {
            return state == OperationState::Running || state == OperationState::Waiting || state == OperationState::Cancelling;
        }

        [[nodiscard]] bool CanCancel(const OperationRecord &operation) noexcept {
            using enum OperationState;
            return operation.cancellable && operation.state != Cancelling && operation.state != Succeeded && operation.state != Failed &&
                   operation.state != Cancelled;
        }

        [[nodiscard]] const char *ActionKey(const OperationRecord &operation) noexcept {
            if (operation.state == OperationState::Failed)
                return "workspace.global_dock.operations.details";
            if (operation.state == OperationState::Queued)
                return "workspace.global_dock.operations.remove";
            return "workspace.global_dock.operations.cancel";
        }
    }  // namespace

    /** @copydoc GlobalDockOperationsPane::Attach */
    void GlobalDockOperationsPane::Attach(const IOperationQuery *operationQuery, IOperationControl *operationControl) noexcept {
        m_operationQuery = operationQuery;
        m_operationControl = operationControl;
        m_snapshot = {};
        m_revision = 0;
        m_filteredIndices.clear();
        m_stateFilter = StateFilter::All;
        m_kindSelection = 0;
        m_search.fill('\0');
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
        if (m_filterDirty)
            RebuildFilter();

        const Theme::Fonts &fonts = context.theme.fonts;
        const float scale = std::max(Theme::GetActiveTokens().sizes.uiScale, 0.01F);
        const GlobalDockPaneMetrics metrics = ResolveGlobalDockPaneMetrics();
        const float availableHeight = std::max(1.0F, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - contentOrigin.y);
        const GlobalDockPaneRegions regions =
            ResolveGlobalDockPaneRegions(contentOrigin, contentWidth, availableHeight, {.hasToolbar = true, .hasFooter = true});

        std::size_t runningCount = 0U;
        std::size_t queuedCount = 0U;
        std::size_t failedCount = 0U;
        for (const OperationRecord &operation : m_snapshot.operations) {
            runningCount += IsRunningState(operation.state) ? 1U : 0U;
            queuedCount += operation.state == OperationState::Queued ? 1U : 0U;
            failedCount += operation.state == OperationState::Failed ? 1U : 0U;
        }

        DrawGlobalDockToolbarSurface(regions.toolbarOrigin, regions.toolbarWidth, metrics.toolbarHeight);
        const float controlY = regions.toolbarOrigin.y + (metrics.toolbarHeight - metrics.controlHeight) * 0.5F;
        const GlobalDockToolbarChipProps all{.id = "OperationsAll",
                                             .label = context.localization.Get("editor", "workspace.global_dock.operations.filter.all"),
                                             .active = m_stateFilter == StateFilter::All};
        const GlobalDockToolbarChipProps running{.id = "OperationsRunning",
                                                 .label =
                                                     context.localization.Get("editor", "workspace.global_dock.operations.filter.running"),
                                                 .count = runningCount,
                                                 .tone = GlobalDockTone::Accent,
                                                 .active = m_stateFilter == StateFilter::Running};
        const GlobalDockToolbarChipProps failed{.id = "OperationsFailed",
                                                .label =
                                                    context.localization.Get("editor", "workspace.global_dock.operations.filter.failed"),
                                                .count = failedCount,
                                                .tone = GlobalDockTone::Error,
                                                .active = m_stateFilter == StateFilter::Failed};
        const GlobalDockToolbarChipProps cancelAll{.id = "OperationsCancelAll",
                                                   .label =
                                                       context.localization.Get("editor", "workspace.global_dock.operations.cancel_all"),
                                                   .tone = GlobalDockTone::Error,
                                                   .toneLabel = true,
                                                   .icon = Ui::UiIcon::Delete};
        const float allWidth = MeasureGlobalDockToolbarChip(all, fonts);
        const float runningWidth = MeasureGlobalDockToolbarChip(running, fonts);
        const float failedWidth = MeasureGlobalDockToolbarChip(failed, fonts);
        const float cancelAllWidth = MeasureGlobalDockToolbarChip(cancelAll, fonts);
        const float typeWidth = 112.0F * scale;
        const float fixedWidth = allWidth + runningWidth + failedWidth + typeWidth + cancelAllWidth + metrics.toolbarGap * 6.0F + scale;
        const float searchWidth = std::max(180.0F * scale, regions.toolbarWidth - metrics.toolbarPaddingX * 2.0F - fixedWidth);
        float x = regions.toolbarOrigin.x + metrics.toolbarPaddingX;

        ImGui::SetCursorScreenPos({x, controlY});
        const std::string &searchHint = context.localization.Get("editor", "workspace.global_dock.operations.search");
        if (Ui::InputTextControl("##OperationsSearch", m_search.data(), m_search.size(), fonts,
                                 {.width = searchWidth / scale,
                                  .hint = searchHint.c_str(),
                                  .prefixIconWidth = 20.0F,
                                  .componentSize = Ui::ComponentSize::Small,
                                  .surface = Ui::InputTextSurface::BottomDockToolbar})) {
            m_filterDirty = true;
        }
        Ui::DrawEditorIcon(ImGui::GetWindowDrawList(), Ui::UiIcon::Search, {x + 8.0F * scale, controlY + 8.0F * scale},
                           {14.0F * scale, 14.0F * scale}, Theme::U32(Theme::Dim()), fonts.icon);
        x += searchWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, allWidth, all, fonts)) {
            m_stateFilter = StateFilter::All;
            m_filterDirty = true;
        }
        x += allWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, runningWidth, running, fonts)) {
            m_stateFilter = StateFilter::Running;
            m_filterDirty = true;
        }
        x += runningWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, failedWidth, failed, fonts)) {
            m_stateFilter = StateFilter::Failed;
            m_filterDirty = true;
        }
        x += failedWidth + metrics.toolbarGap;
        DrawGlobalDockToolbarSeparator(x, controlY);
        x += metrics.toolbarGap + scale;

        const std::array<std::string, 7> kindText{
            context.localization.Get("editor", "workspace.global_dock.operations.filter.all_types"),
            context.localization.Get("editor", "workspace.global_dock.operations.kind.build"),
            context.localization.Get("editor", "workspace.global_dock.operations.kind.cook"),
            context.localization.Get("editor", "workspace.global_dock.operations.kind.import"),
            context.localization.Get("editor", "workspace.global_dock.operations.kind.index"),
            context.localization.Get("editor", "workspace.global_dock.operations.kind.validation"),
            context.localization.Get("editor", "workspace.global_dock.operations.kind.other"),
        };
        const std::array<const char *, 7> kindItems{kindText[0].c_str(), kindText[1].c_str(), kindText[2].c_str(), kindText[3].c_str(),
                                                    kindText[4].c_str(), kindText[5].c_str(), kindText[6].c_str()};
        ImGui::SetCursorScreenPos({x, controlY});
        ImGui::SetNextItemWidth(typeWidth);
        if (Ui::ComboControl("OperationsType", &m_kindSelection, kindItems.data(), static_cast<int>(kindItems.size()), fonts,
                             {.height = GlobalDockLayout::ControlHeight,
                              .componentSize = Ui::ComponentSize::Small,
                              .surface = Ui::ComboControlSurface::BottomDockToolbar})) {
            m_filterDirty = true;
        }
        x += typeWidth + metrics.toolbarGap;
        if (DrawGlobalDockToolbarChip({x, controlY}, cancelAllWidth, cancelAll, fonts) && m_operationControl != nullptr) {
            for (const OperationRecord &operation : m_snapshot.operations) {
                if (CanCancel(operation))
                    static_cast<void>(m_operationControl->RequestCancel(operation.id));
            }
        }

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImVec2 headerMin = regions.contentOrigin;
        DrawGlobalDockTableHeaderSurface(headerMin, regions.contentWidth, metrics.tableHeaderHeight);
        const float operationX = headerMin.x + metrics.contentPadding;
        const float stateX = operationX + 120.0F * scale + metrics.columnGap;
        const float progressX = stateX + 100.0F * scale + metrics.columnGap;
        const float actionWidth = 72.0F * scale;
        const float elapsedWidth = 70.0F * scale;
        const float actionX = headerMin.x + regions.contentWidth - metrics.contentPadding - actionWidth;
        const float elapsedX = actionX - metrics.columnGap - elapsedWidth;
        const float headerY = headerMin.y + (metrics.tableHeaderHeight - Theme::TextPx::Caption()) * 0.5F;
        const auto headerText = [&](const float textX, const char *key) {
            drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(), {textX, headerY}, Theme::U32(Theme::Muted()),
                              context.localization.Get("editor", key).c_str());
        };
        headerText(operationX, "workspace.global_dock.operations.column.operation");
        headerText(stateX, "workspace.global_dock.operations.column.status");
        headerText(progressX, "workspace.global_dock.operations.column.progress");
        headerText(elapsedX, "workspace.global_dock.operations.column.elapsed");
        headerText(actionX, "workspace.global_dock.operations.column.action");

        const ImVec2 rowsOrigin{regions.contentOrigin.x, regions.contentOrigin.y + metrics.tableHeaderHeight};
        const float rowsHeight = std::max(1.0F, regions.contentHeight - metrics.tableHeaderHeight);
        ImGui::SetCursorScreenPos(rowsOrigin);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BottomDockContentSurface());
        ImGui::BeginChild("##OperationsRows", {regions.contentWidth, rowsHeight}, false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoSavedSettings);
        ImDrawList *rowsDrawList = ImGui::GetWindowDrawList();
        const bool wasAtBottom = ImGui::GetScrollY() >= std::max(0.0F, ImGui::GetScrollMaxY() - 2.0F);
        for (std::size_t visibleIndex = 0; visibleIndex < m_filteredIndices.size(); ++visibleIndex) {
            const OperationRecord &operation = m_snapshot.operations[m_filteredIndices[visibleIndex]];
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            ImGui::PushID(static_cast<int>(visibleIndex));
            ImGui::InvisibleButton("##operation", {regions.contentWidth, metrics.tableRowHeight});
            const bool hovered = ImGui::IsItemHovered();
            if (hovered)
                rowsDrawList->AddRectFilled(rowMin, {rowMin.x + regions.contentWidth, rowMin.y + metrics.tableRowHeight},
                                            Theme::U32(Theme::Hover()));
            rowsDrawList->AddLine({rowMin.x, rowMin.y + metrics.tableRowHeight - scale},
                                  {rowMin.x + regions.contentWidth, rowMin.y + metrics.tableRowHeight - scale},
                                  Theme::U32(Theme::Border()));
            const float textY = rowMin.y + (metrics.tableRowHeight - Theme::TextPx::Label()) * 0.5F;
            const std::string title = OperationTitle(operation, context);
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {operationX, textY},
                                      {stateX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), title);
            const std::string stateLabel = context.localization.Get("editor", StatusLocalizationKey(operation.state));
            static_cast<void>(DrawGlobalDockStatePill({stateX, rowMin.y + (metrics.tableRowHeight - 22.0F * scale) * 0.5F}, stateLabel,
                                                      StatusTone(operation.state), fonts));
            const float progressRight = elapsedX - metrics.columnGap;
            const std::string progressLabel = !operation.message.empty() ? operation.message
                                              : !operation.phase.empty() ? operation.phase
                                                                         : FormatProgress(operation.progress);
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Caption(), {progressX, textY},
                                      {progressRight, rowMin.y + metrics.tableRowHeight}, Theme::Muted(), progressLabel);
            if (operation.progress.has_value())
                DrawGlobalDockProgressBar({progressX, rowMin.y + metrics.tableRowHeight - 8.0F * scale},
                                          std::max(1.0F, progressRight - progressX), *operation.progress);
            const std::string elapsed = FormatElapsed(operation);
            DrawGlobalDockClippedText(*rowsDrawList, fonts.sansCompact, Theme::TextPx::Label(), {elapsedX, textY},
                                      {actionX - metrics.columnGap, rowMin.y + metrics.tableRowHeight}, Theme::Text(), elapsed);
            const bool canCancel = CanCancel(operation);
            const bool showDetails = operation.state == OperationState::Failed;
            if (canCancel || showDetails) {
                const GlobalDockToolbarChipProps action{.id = "OperationAction",
                                                        .label = context.localization.Get("editor", ActionKey(operation)),
                                                        .tone = canCancel ? GlobalDockTone::Error : GlobalDockTone::Neutral,
                                                        .toneLabel = canCancel};
                if (DrawGlobalDockToolbarChip({actionX, rowMin.y + (metrics.tableRowHeight - metrics.controlHeight) * 0.5F}, actionWidth,
                                              action, fonts) &&
                    canCancel && m_operationControl != nullptr) {
                    static_cast<void>(m_operationControl->RequestCancel(operation.id));
                }
            }
            ImGui::PopID();
        }
        if (snapshotChanged && (wasAtBottom || m_initialFollowTail))
            ImGui::SetScrollHereY(1.0F);
        m_initialFollowTail = false;
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        DrawGlobalDockFooterSurface(regions.footerOrigin, regions.footerWidth, metrics.footerHeight);
        const float footerY = regions.footerOrigin.y + (metrics.footerHeight - Theme::TextPx::Caption()) * 0.5F;
        const std::string summary =
            std::format("{} {}   {} {}   {} {}", runningCount,
                        context.localization.Get("editor", "workspace.global_dock.operations.footer.running"), queuedCount,
                        context.localization.Get("editor", "workspace.global_dock.operations.footer.queued"), failedCount,
                        context.localization.Get("editor", "workspace.global_dock.operations.footer.failed"));
        drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(), {regions.footerOrigin.x + metrics.contentPadding, footerY},
                          Theme::U32(Theme::Muted()), summary.c_str());
        const std::string &bounded = context.localization.Get("editor", "workspace.global_dock.operations.footer.bounded");
        const float boundedWidth = (fonts.sansCompact != nullptr ? fonts.sansCompact : ImGui::GetFont())
                                       ->CalcTextSizeA(Theme::TextPx::Caption(), FLT_MAX, 0.0F, bounded.c_str())
                                       .x;
        drawList->AddText(fonts.sansCompact, Theme::TextPx::Caption(),
                          {regions.footerOrigin.x + regions.footerWidth - metrics.contentPadding - boundedWidth, footerY},
                          Theme::U32(Theme::Muted()), bounded.c_str());
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
            if (const OperationRecord &operation = operations[index];
                !search.empty() && !ContainsCaseInsensitive(operation.title, search) && !ContainsCaseInsensitive(operation.phase, search) &&
                !ContainsCaseInsensitive(operation.message, search) &&
                !ContainsCaseInsensitive(TechnicalStatusText(operation.state), search))
                continue;
            projected.push_back(index);
        }
        return projected;
    }

    void GlobalDockOperationsPane::RebuildFilter() {
        const std::vector<std::size_t> searched = ProjectRecords(m_snapshot.operations, std::string_view{m_search.data()});
        m_filteredIndices.clear();
        m_filteredIndices.reserve(searched.size());
        for (const std::size_t index : searched) {
            const OperationRecord &operation = m_snapshot.operations[index];
            const bool stateMatches = m_stateFilter == StateFilter::All ||
                                      (m_stateFilter == StateFilter::Running && IsRunningState(operation.state)) ||
                                      (m_stateFilter == StateFilter::Failed && operation.state == OperationState::Failed);
            const bool kindMatches = m_kindSelection == 0 || static_cast<int>(operation.kind) + 1 == m_kindSelection;
            if (stateMatches && kindMatches)
                m_filteredIndices.push_back(index);
        }
        m_filterDirty = false;
    }
}  // namespace Horo::Editor
