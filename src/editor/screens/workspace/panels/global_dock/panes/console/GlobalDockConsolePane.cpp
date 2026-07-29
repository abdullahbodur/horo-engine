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
            switch (level) {
                case Log::Level::Critical:
                case Log::Level::Error:
                    return Theme::Err();
                case Log::Level::Warn:
                    return {0.91F, 0.64F, 0.24F, 1.0F};
                case Log::Level::Info:
                    return Theme::Muted();
                case Log::Level::Debug:
                    return {0.35F, 0.72F, 0.95F, 1.0F};
                case Log::Level::Trace:
                    return {0.72F, 0.55F, 0.94F, 1.0F};
                case Log::Level::Off:
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
            return std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](const char left, const char right) {
                return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
            }) != text.end();
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
        m_filterDirty = true;
    }

    /** @copydoc GlobalDockConsolePane::Detach */
    void GlobalDockConsolePane::Detach() noexcept {
        m_logQuery = nullptr;
        m_textSelectionActive = false;
    }

    /** @copydoc GlobalDockConsolePane::Draw */
    void GlobalDockConsolePane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) {
        const bool snapshotChanged = RefreshSnapshot();
        constexpr float toolbarPadX = 10.0F;
        constexpr float toolbarPadY = 6.0F;
        constexpr float buttonHeight = 26.0F;
        constexpr float buttonGap = 4.0F;
        constexpr float searchWidth = 220.0F;
        const bool stackedToolbar = contentWidth < 620.0F;
        const float toolbarHeight = stackedToolbar ? 66.0F : 38.0F;
        const auto &fonts = context.theme.fonts;

        ImGui::SetCursorScreenPos({contentOrigin.x + toolbarPadX, contentOrigin.y + toolbarPadY});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {buttonGap, 0.0F});
        ImGui::BeginChild("##ConsoleToolbar", {contentWidth, toolbarHeight}, false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);

        const std::array filterKeys{
            "workspace.global_dock.console.filter.error", "workspace.global_dock.console.filter.warn",
            "workspace.global_dock.console.filter.info",  "workspace.global_dock.console.filter.debug",
            "workspace.global_dock.console.filter.trace",
        };
        for (std::size_t index = 0; index < filterKeys.size(); ++index) {
            const std::string &label = context.localization.Get("editor", filterKeys[index]);
            const float width = std::max(44.0F, ImGui::CalcTextSize(label.c_str()).x + 16.0F);
            const Ui::ButtonProps button{
                .label = label.c_str(),
                .size = {width, buttonHeight},
                .variant = m_levelEnabled[index] ? Ui::ButtonVariant::Primary : Ui::ButtonVariant::Secondary,
                .font = fonts.sansCompact,
            };
            if (Ui::Button(button)) {
                m_levelEnabled[index] = !m_levelEnabled[index];
                m_filterDirty = true;
            }
            if (index + 1 < filterKeys.size())
                ImGui::SameLine(0.0F, buttonGap);
        }

        const std::string &searchLabel = context.localization.Get("editor", "workspace.global_dock.console.search");
        const float searchLabelWidth = ImGui::CalcTextSize(searchLabel.c_str()).x;
        const float resolvedSearchWidth = std::min(searchWidth, std::max(80.0F, contentWidth - searchLabelWidth - 12.0F));
        if (stackedToolbar) {
            ImGui::SetCursorPos({0.0F, 34.0F});
        } else {
            ImGui::SameLine(
                std::max(ImGui::GetCursorPosX() + buttonGap, ImGui::GetWindowWidth() - searchLabelWidth - 8.0F - resolvedSearchWidth));
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(Theme::Dim(), "%s", searchLabel.c_str());
        ImGui::SameLine(0.0F, 8.0F);
        if (Ui::InputTextControl("##ConsoleSearch", m_search.data(), m_search.size(), fonts, false, resolvedSearchWidth)) {
            m_filterDirty = true;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        const bool rebuildSelectableText = m_filterDirty && !m_textSelectionActive;
        if (rebuildSelectableText)
            RebuildFilter();

        const float listY = contentOrigin.y + toolbarPadY + toolbarHeight + 4.0F;
        const float listHeight = std::max(1.0F, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - listY);
        ImGui::SetCursorScreenPos({contentOrigin.x, listY});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.0F, 6.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4.0F, 2.0F});
        ImGui::BeginChild("##ConsoleLogScroll", {contentWidth + OuterPaddingX * 2.0F, listHeight}, false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoSavedSettings);
        const bool wasAtBottom = ImGui::GetScrollY() >= std::max(0.0F, ImGui::GetScrollMaxY() - 2.0F);

        if (m_filteredIndices.empty()) {
            if (rebuildSelectableText) {
                m_selectableText.clear();
                m_lineLayouts.clear();
            }
            m_textSelectionActive = false;
            const std::string &empty = context.localization.Get("editor", "workspace.global_dock.console.empty");
            ImGui::TextColored(Theme::Dim(), "%s", empty.c_str());
        } else {
            Theme::ScopedTextStyle textStyle(fonts.sansCompact, 14.0F, Theme::FontPx::SansCompact);
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
        ImGui::PopStyleVar(2);
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
            if (const std::size_t group = static_cast<std::size_t>(GroupForLevel(record.level)); !m_levelEnabled[group])
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
