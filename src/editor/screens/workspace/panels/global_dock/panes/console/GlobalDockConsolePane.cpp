#include "editor/screens/workspace/panels/global_dock/panes/console/GlobalDockConsolePane.h"

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/Localization/ILocalizationService.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <format>
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

        [[nodiscard]] float ToolbarHeight() noexcept {
            return Ui::ScaledLayoutValue(28.0F);
        }

        constexpr float SearchWidth = 180.0F;

        [[nodiscard]] float ControlGap() noexcept {
            return Ui::ScaledLayoutValue(4.0F);
        }

        enum class ConsoleLevelGroup : std::size_t {
            Error,
            Warn,
            Info,
            Debug,
            Trace,
        };

        [[nodiscard]] ConsoleLevelGroup GroupForLevel(const Log::Level level) noexcept {
            switch (level) {
                case Log::Level::Critical:
                case Log::Level::Error:
                case Log::Level::Off:
                    return ConsoleLevelGroup::Error;
                case Log::Level::Warn:
                    return ConsoleLevelGroup::Warn;
                case Log::Level::Info:
                    return ConsoleLevelGroup::Info;
                case Log::Level::Debug:
                    return ConsoleLevelGroup::Debug;
                case Log::Level::Trace:
                    return ConsoleLevelGroup::Trace;
            }
            return ConsoleLevelGroup::Error;
        }

        [[nodiscard]] ImVec4 ConsoleLevelColor(const Log::Level level) noexcept {
            using enum Log::Level;
            switch (level) {
                case Critical:
                case Error:
                    return Theme::Err();
                case Warn:
                    return {0.91F, 0.64F, 0.24F, 1.0F};
                case Info:
                    return Theme::Muted();
                case Debug:
                    return {0.35F, 0.72F, 0.95F, 1.0F};
                case Trace:
                    return {0.72F, 0.55F, 0.94F, 1.0F};
                case Off:
                    return Theme::Dim();
            }
            return Theme::Muted();
        }

        [[nodiscard]] const char *ConsoleLevelLabel(const Log::Level level) noexcept {
            return level == Log::Level::Critical ? "CRITICAL" : Log::ToString(level);
        }

        [[nodiscard]] bool ContainsCaseInsensitive(const std::string_view text, const std::string_view needle) {
            if (needle.empty())
                return true;
            if (needle.size() > text.size())
                return false;
            return !std::ranges::search(text, needle, [](const char left, const char right) {
                return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
            }).empty();
        }

        [[nodiscard]] std::string FormatConsoleTimestamp(const std::chrono::system_clock::time_point timestamp) {
            const std::time_t value = std::chrono::system_clock::to_time_t(timestamp);
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()) % 1000;
            std::tm utc{};
#if defined(_WIN32)
            gmtime_s(&utc, &value);
#else
            gmtime_r(&value, &utc);
#endif
            return std::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}+00:00", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                               utc.tm_hour, utc.tm_min, utc.tm_sec, milliseconds.count());
        }
    }  // namespace

    /** @copydoc GlobalDockConsolePane::Attach */
    void GlobalDockConsolePane::Attach(const Log::IStructuredLogQuery *logQuery) noexcept {
        m_logQuery = logQuery;
        m_snapshot = {};
        m_revision = 0;
        m_filteredIndices.clear();
        m_selectableText.clear();
        m_lineLayouts.clear();
        m_filterDirty = true;
        m_initialFollowTail = true;
        m_textSelectionActive = false;
    }

    /** @copydoc GlobalDockConsolePane::Detach */
    void GlobalDockConsolePane::Detach() noexcept {
        m_logQuery = nullptr;
        m_snapshot = {};
        m_revision = 0;
        m_filteredIndices.clear();
        m_selectableText.clear();
        m_lineLayouts.clear();
        m_textSelectionActive = false;
    }

    /** @copydoc GlobalDockConsolePane::Draw */
    void GlobalDockConsolePane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) {
        const bool snapshotChanged = RefreshSnapshot();
        const auto &fonts = context.theme.fonts;
        ImDrawList *drawList = ImGui::GetWindowDrawList();

        // ── Toolbar bar ──────────────────────────────────────────
        const float barFullWidth = contentWidth + OuterPaddingX * 2.0F;
        const bool stackedToolbar = contentWidth < 720.0F;
        const float barTotalHeight =
            ToolbarPadY() * 2.0F + ToolbarHeight() * (stackedToolbar ? 2.0F : 1.0F) + (stackedToolbar ? ControlGap() : 0.0F);
        const ImVec2 barMin{contentOrigin.x, contentOrigin.y};
        const ImVec2 barMax{barMin.x + barFullWidth, barMin.y + barTotalHeight};

        drawList->AddRectFilled(barMin, barMax, Theme::U32(Theme::Bg2()));
        drawList->AddLine({barMin.x, barMax.y}, {barMax.x, barMax.y}, Theme::U32(Theme::Border()), 1.0F);

        if (fonts.sansCompact != nullptr)
            ImGui::PushFont(fonts.sansCompact);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0f, 4.0f});

        // ── Left: level filter pills ────────────────────────────
        ImGui::SetCursorScreenPos({barMin.x + ToolbarPadX(), barMin.y + ToolbarPadY()});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ControlGap(), 0.0F});

        const std::array filterKeys{
            "workspace.global_dock.console.filter.error", "workspace.global_dock.console.filter.warn",
            "workspace.global_dock.console.filter.info",  "workspace.global_dock.console.filter.debug",
            "workspace.global_dock.console.filter.trace",
        };
        if (stackedToolbar) {
            const std::array<std::string, 5> filterText{
                context.localization.Get("editor", filterKeys[0]), context.localization.Get("editor", filterKeys[1]),
                context.localization.Get("editor", filterKeys[2]), context.localization.Get("editor", filterKeys[3]),
                context.localization.Get("editor", filterKeys[4]),
            };
            const std::array<const char *, 5> filterLabels{filterText[0].c_str(), filterText[1].c_str(), filterText[2].c_str(),
                                                           filterText[3].c_str(), filterText[4].c_str()};
            const std::string &levelsLabel = context.localization.Get("editor", "workspace.global_dock.console.levels");
            if (Ui::MultiSelectField("##ConsoleLevels", levelsLabel.c_str(), filterLabels, m_levelEnabled, fonts, 104.0F,
                                     Ui::ComponentSize::Small))
                m_filterDirty = true;
        } else {
            for (std::size_t index = 0; index < filterKeys.size(); ++index) {
                const std::string &label = context.localization.Get("editor", filterKeys[index]);
                if (const Ui::ButtonProps button{
                        .label = label.c_str(),
                        .variant = m_levelEnabled[index] ? Ui::ButtonVariant::Primary : Ui::ButtonVariant::Secondary,
                        .font = fonts.sansCompact,
                        .componentSize = Ui::ComponentSize::Small,
                    };
                    Ui::Button(button)) {
                    m_levelEnabled[index] = !m_levelEnabled[index];
                    m_filterDirty = true;
                }
                ImGui::SameLine(0.0F, ControlGap());
            }
        }
        ImGui::PopStyleVar();  // ItemSpacing

        // ── Right: search + clear ───────────────────────────────
        const std::string &clearLabel = context.localization.Get("editor", "workspace.global_dock.console.clear");
        const float clearWidth = ImGui::CalcTextSize(clearLabel.c_str()).x + 16.0F;
        const std::string &hint = context.localization.Get("editor", "workspace.global_dock.console.search");
        const float resolvedSearchWidth =
            std::min(SearchWidth, std::max(80.0F, barFullWidth - ToolbarPadX() * 2.0F - clearWidth - ControlGap() - 120.0F));
        const float controlsWidth = resolvedSearchWidth + ControlGap() + clearWidth;
        const float controlsX = stackedToolbar ? barMin.x + ToolbarPadX() : barMax.x - ToolbarPadX() - controlsWidth;

        ImGui::SetCursorScreenPos({controlsX, barMin.y + ToolbarPadY() + (stackedToolbar ? ToolbarHeight() + ControlGap() : 0.0F)});
        if (Ui::InputTextControl("##ConsoleSearch", m_search.data(), m_search.size(), context.theme.fonts,
                                 Ui::InputTextOptions{.width = resolvedSearchWidth, .hint = hint.c_str()})) {
            m_filterDirty = true;
        }

        ImGui::SameLine(0.0F, ControlGap());
        if (Ui::Button(Ui::ButtonProps{.label = clearLabel.c_str(),
                                       .variant = Ui::ButtonVariant::Secondary,
                                       .font = fonts.sansCompact,
                                       .componentSize = Ui::ComponentSize::Small})) {
            m_search[0] = '\0';
            m_filterDirty = true;
        }

        ImGui::PopStyleVar();  // FramePadding

        if (fonts.sansCompact != nullptr)
            ImGui::PopFont();

        // ── Rebuild filter ──────────────────────────────────────
        const bool rebuildSelectableText = m_filterDirty && !m_textSelectionActive;
        if (rebuildSelectableText)
            RebuildFilter();

        // ── Content ─────────────────────────────────────────────
        const float contentY = barMax.y + 4.0F;
        const float contentHeight = std::max(1.0F, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - contentY);
        ImGui::SetCursorScreenPos({contentOrigin.x, contentY});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
        ImGui::BeginChild("##ConsoleLogScroll", {barFullWidth, contentHeight}, false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoSavedSettings);
        const bool wasAtBottom = ImGui::GetScrollY() >= std::max(0.0F, ImGui::GetScrollMaxY() - 2.0F);

        if (m_filteredIndices.empty()) {
            if (rebuildSelectableText) {
                m_selectableText.clear();
                m_lineLayouts.clear();
            }
            m_textSelectionActive = false;
            const std::string &empty = context.localization.Get("editor", "workspace.global_dock.console.empty");
            Theme::ScopedTextStyle textStyle(fonts.sans, Theme::FontPx::Sans * Theme::GetActiveTokens().sizes.uiScale, Theme::FontPx::Sans);
            ImGui::TextColored(Theme::Dim(), "%s", empty.c_str());
        } else {
            Theme::ScopedTextStyle textStyle(fonts.sans, Theme::FontPx::Sans * Theme::GetActiveTokens().sizes.uiScale, Theme::FontPx::Sans);
            const float referenceTimestampWidth = ImGui::CalcTextSize("2022-03-15T13:38:15.567+00:00").x;
            const float spaceWidth = ImGui::CalcTextSize(" ").x;
            const float contentColumnX = referenceTimestampWidth + spaceWidth * 3.0F;

            if (rebuildSelectableText) {
                m_selectableText.clear();
                m_lineLayouts.clear();
                m_lineLayouts.reserve(m_filteredIndices.size());
                for (const std::size_t recordIndex : m_filteredIndices) {
                    const Log::StructuredLogRecord &record = *m_snapshot.records[recordIndex];
                    const std::string timestamp = FormatConsoleTimestamp(record.timestampUtc);
                    const float timestampWidth = ImGui::CalcTextSize(timestamp.c_str()).x;
                    const float smartGap = std::max(spaceWidth, referenceTimestampWidth + spaceWidth * 3.0F - timestampWidth);

                    if (!m_selectableText.empty())
                        m_selectableText += '\n';
                    const std::size_t lineStartByteOffset = m_selectableText.size();
                    m_selectableText += timestamp;
                    m_selectableText.append(static_cast<std::size_t>(std::max(1.0F, std::ceil(smartGap / std::max(spaceWidth, 1.0F)))),
                                            ' ');
                    const std::size_t contentColumnByteOffset = m_selectableText.size();
                    m_selectableText += '[';
                    m_selectableText += ConsoleLevelLabel(record.level);
                    m_selectableText += ']';
                    m_selectableText += record.context;
                    m_selectableText += ' ';
                    m_selectableText += record.category;
                    m_selectableText += ": ";
                    m_selectableText += record.message;
                    m_lineLayouts.push_back({
                        .color = ConsoleLevelColor(record.level),
                        .alignedColumnByteOffset = contentColumnByteOffset - lineStartByteOffset,
                    });
                }
            }

            m_textSelectionActive = Ui::SelectableTextBlock("##ConsoleLogText", m_selectableText.data(), m_selectableText.size() + 1U,
                                                            m_lineLayouts, contentColumnX);
        }

        if (snapshotChanged && !m_textSelectionActive && (wasAtBottom || m_initialFollowTail)) {
            ImGui::SetScrollHereY(1.0F);
        }
        m_initialFollowTail = false;
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    bool GlobalDockConsolePane::RefreshSnapshot() {
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

    void GlobalDockConsolePane::RebuildFilter() {
        m_filteredIndices.clear();
        m_filteredIndices.reserve(m_snapshot.records.size());
        const std::string_view search{m_search.data()};
        for (std::size_t index = 0; index < m_snapshot.records.size(); ++index) {
            const Log::StructuredLogRecord &record = *m_snapshot.records[index];
            if (const auto group = static_cast<std::size_t>(GroupForLevel(record.level)); !m_levelEnabled[group])
                continue;
            if (!ContainsCaseInsensitive(record.category, search) && !ContainsCaseInsensitive(record.message, search) &&
                !ContainsCaseInsensitive(record.context, search)) {
                continue;
            }
            m_filteredIndices.push_back(index);
        }
        m_filterDirty = false;
    }
}  // namespace Horo::Editor
