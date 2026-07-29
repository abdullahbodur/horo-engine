/**
 * @copydoc AssetImportModalPresentation.h
 *
 * HTML reference: docs/architecture/runtime/asset-import-modal.html
 * Layout: header → summary → importer info → tabs → [sidebar | content] → footer
 */

#include "AssetImportModalPresentation.h"

#include "Horo/Editor/AssetImportModal.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <imgui.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Editor {
    namespace {
        using namespace Theme;
        using namespace Ui;

        namespace ImportLayout {
            constexpr float ModalW = 1000.0f;
            constexpr float ModalH = 730.0f;
            constexpr float HeaderH = 44.0f;
            constexpr float SummaryH = 64.0f;
            constexpr float ImporterInfoH = 54.0f;
            constexpr float TabsH = 48.0f;
            constexpr float FooterH = Theme::Layout::FooterH;
            constexpr float SidebarW = 310.0f;
            constexpr float ViewportPad = 48.0f;
            constexpr float SplitColumnGap = 16.0f;
        }  // namespace ImportLayout

        /** @brief Active tab index. */
        enum class ImportTab : int {
            Queue = 0,
            Diagnostics,
            Settings,
            Destination,
            Count,
        };

        struct SplitColumns {
            float startX;
            float startY;
            float width;
            float gap;
        };

        [[nodiscard]] SplitColumns CurrentSplitColumns() {
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const ImVec2 cursor = ImGui::GetCursorPos();
            return {
                .startX = cursor.x,
                .startY = cursor.y,
                .width = std::max(0.0f, (availableWidth - ImportLayout::SplitColumnGap) * 0.5f),
                .gap = ImportLayout::SplitColumnGap,
            };
        }

        void MoveToSecondColumn(const SplitColumns &columns) {
            ImGui::SetCursorPos({columns.startX + columns.width + columns.gap, columns.startY});
        }

        void FinishSplitColumns(const SplitColumns &columns) {
            const float endY = ImGui::GetCursorPosY();
            ImGui::SetCursorPos({columns.startX, endY});
        }

        /**
         * @brief Converts a Unicode codepoint to a UTF-8 string and renders it
         *        using the icon font at @p pos with the given @p color.
         */
        void DrawIcon(ImDrawList *dl, ImVec2 pos, ImU32 codepoint, ImU32 color, const Theme::Fonts &fonts) {
            if (!fonts.icon || !dl)
                return;
            char utf8[5]{};
            if (codepoint < 0x80) {
                utf8[0] = static_cast<char>(codepoint);
            } else if (codepoint < 0x800) {
                utf8[0] = static_cast<char>(0xC0 | (codepoint >> 6));
                utf8[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
            } else if (codepoint < 0x10000) {
                utf8[0] = static_cast<char>(0xE0 | (codepoint >> 12));
                utf8[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                utf8[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
            } else {
                utf8[0] = static_cast<char>(0xF0 | (codepoint >> 18));
                utf8[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                utf8[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                utf8[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            dl->AddText(fonts.icon, fonts.icon->FontSize, pos, color, utf8);
        }

        [[nodiscard]] bool StartsWithInsensitive(std::string_view text, std::string_view prefix) {
            if (prefix.size() > text.size())
                return false;
            for (std::size_t i = 0; i < prefix.size(); ++i) {
                const unsigned char a = static_cast<unsigned char>(text[i]);
                const unsigned char b = static_cast<unsigned char>(prefix[i]);
                if (std::tolower(a) != std::tolower(b))
                    return false;
            }
            return true;
        }

        std::string DisplayFileName(const std::filesystem::path &path);
        std::string DisplayAssetLabel(const std::filesystem::path &path);

        [[nodiscard]] std::string CleanDiagnosticMessage(const std::filesystem::path &path, std::string_view message) {
            std::string_view cleaned = message;

            const std::string stem = DisplayAssetLabel(path);
            const std::string filename = DisplayFileName(path);

            auto stripPrefix = [&](std::string_view prefix) {
                if (prefix.empty() || !StartsWithInsensitive(cleaned, prefix))
                    return;
                cleaned.remove_prefix(prefix.size());
                while (!cleaned.empty() &&
                       (cleaned.front() == ' ' || cleaned.front() == ':' || cleaned.front() == '-' || cleaned.front() == '	')) {
                    cleaned.remove_prefix(1);
                }
            };

            stripPrefix(stem);
            stripPrefix(filename);

            return std::string(cleaned);
        }

        [[nodiscard]] const char *DiagnosticTimeLabel(std::size_t diagnosticIndex) {
            static const char *kTimes[] = {"14:02", "14:02", "14:03", "14:03", "14:03", "14:04"};
            return kTimes[diagnosticIndex % (sizeof(kTimes) / sizeof(kTimes[0]))];
        }

        /** @brief Returns the display label for a tab. */
        const char *TabLabel(int tab) {
            switch (static_cast<ImportTab>(tab)) {
                case ImportTab::Queue:
                    return "Overview";
                case ImportTab::Diagnostics:
                    return "Diagnostics";
                case ImportTab::Settings:
                    return "Importer Settings";
                case ImportTab::Destination:
                    return "Destination";
                case ImportTab::Count:
                    return "";
            }
        }

        std::string DisplayFileName(const std::filesystem::path &path) {
            const auto filename = path.filename().string();
            return filename.empty() ? path.string() : filename;
        }

        std::string DisplayAssetLabel(const std::filesystem::path &path) {
            const auto stem = path.stem().string();
            return stem.empty() ? DisplayFileName(path) : stem;
        }

        const char *AssetTypeLabel(const std::filesystem::path &path) {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });

            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".exr" ||
                extension == ".hdr")
                return "Texture";
            if (extension == ".wav" || extension == ".ogg" || extension == ".opus" || extension == ".flac")
                return "Audio";
            if (extension == ".fbx" || extension == ".obj" || extension == ".gltf" || extension == ".glb")
                return "Mesh";
            return "Asset";
        }

        std::string FriendlyImporterName(std::string_view contributionId) {
            const std::size_t lastDot = contributionId.find_last_of('.');
            std::string name{contributionId.substr(lastDot == std::string_view::npos ? 0 : lastDot + 1)};
            std::replace(name.begin(), name.end(), '-', ' ');

            bool wordStart = true;
            for (char &value : name) {
                if (value == ' ') {
                    wordStart = true;
                    continue;
                }
                value = wordStart ? static_cast<char>(std::toupper(static_cast<unsigned char>(value))) : value;
                wordStart = false;
            }

            if (name.rfind("Obj", 0) == 0)
                name.replace(0, 3, "OBJ");
            if (name.rfind("Fbx", 0) == 0)
                name.replace(0, 3, "FBX");
            return name;
        }

        [[nodiscard]] std::optional<std::filesystem::path> OpenFolderSelectionDialog(const char *prompt) {
#if defined(__APPLE__)
            std::string command = "osascript -e 'POSIX path of (choose folder with prompt \"";
            for (const char value : std::string_view(prompt ? prompt : "Select Folder")) {
                if (value == '"' || value == '\\' || value == '\'')
                    command += '\\';
                command += value;
            }
            command += "\")' 2>/dev/null";
            FILE *pipe = popen(command.c_str(), "r");
#elif defined(__linux__)
            std::string command = "zenity --file-selection --directory --title=\"";
            for (const char value : std::string_view(prompt ? prompt : "Select Folder")) {
                if (value == '"' || value == '\\' || value == '\'')
                    command += '\\';
                command += value;
            }
            command += "\" 2>/dev/null";
            FILE *pipe = popen(command.c_str(), "r");
#elif defined(_WIN32)
            std::string command = "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; $f = New-Object "
                                  "System.Windows.Forms.FolderBrowserDialog; $f.Description = '";
            for (const char value : std::string_view(prompt ? prompt : "Select Folder")) {
                if (value == '\'' || value == '"')
                    command += '`';
                command += value;
            }
            command += "'; if($f.ShowDialog() -eq 'OK'){ $f.SelectedPath }\" 2>nul";
            FILE *pipe = _popen(command.c_str(), "r");
#else
            static_cast<void>(prompt);
            return std::nullopt;
#endif
            if (!pipe)
                return std::nullopt;

            std::string buffer(1024, '\0');
            std::string result;
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
                result += buffer.c_str();
#if defined(_WIN32)
            const int status = _pclose(pipe);
#else
            const int status = pclose(pipe);
#endif
            if (status != 0)
                return std::nullopt;
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            if (result.empty())
                return std::nullopt;
            return std::filesystem::path{result};
        }

        [[nodiscard]] std::optional<std::string> ProjectRelativeFolder(const std::filesystem::path &projectRoot,
                                                                       const std::filesystem::path &selectedFolder) {
            if (projectRoot.empty())
                return std::nullopt;

            std::error_code error;
            const auto canonicalRoot = std::filesystem::weakly_canonical(projectRoot, error);
            if (error)
                return std::nullopt;
            const auto canonicalSelection = std::filesystem::weakly_canonical(selectedFolder, error);
            if (error)
                return std::nullopt;

            const auto relative = canonicalSelection.lexically_relative(canonicalRoot);
            if (relative.empty() || relative == ".")
                return std::string{};
            const auto first = relative.begin();
            if (first != relative.end() && *first == "..")
                return std::nullopt;
            return relative.generic_string();
        }

        void DrawDashedRect(ImDrawList *drawList, const ImVec2 &min, const ImVec2 &max, ImU32 color) {
            constexpr float dash = 6.0f;
            constexpr float gap = 4.0f;
            constexpr float thickness = 1.0f;

            for (float x = min.x; x < max.x; x += dash + gap) {
                const float end = std::min(x + dash, max.x);
                drawList->AddLine({x, min.y}, {end, min.y}, color, thickness);
                drawList->AddLine({x, max.y}, {end, max.y}, color, thickness);
            }
            for (float y = min.y; y < max.y; y += dash + gap) {
                const float end = std::min(y + dash, max.y);
                drawList->AddLine({min.x, y}, {min.x, end}, color, thickness);
                drawList->AddLine({max.x, y}, {max.x, end}, color, thickness);
            }
        }
    }  // namespace

    ModalFrameResult DrawAssetImportModalPresentation(AssetImportModal &modal, const Fonts &fonts) {
        const auto &snap = modal.Snapshot();
        ModalFrameResult frameResult = ModalFrameResult::None();

        ScopedModalShell modalShell(
            {
                .id = "Asset Import",
                .title = "Import New Asset...",
                .requestedSize = {ImportLayout::ModalW, ImportLayout::ModalH},
                .viewportPadding = ImportLayout::ViewportPad,
                .headerHeight = ImportLayout::HeaderH,
                .footerHeight = ImportLayout::FooterH,
                .titleFontSize = 16.0F,
            },
            fonts);
        if (modalShell.CloseRequested())
            frameResult = ModalFrameResult::RequestClose(ModalCloseReason::Cancelled);

        ImDrawList *dl = ImGui::GetWindowDrawList();
        std::size_t doneCount = 0, errorCount = 0, warningCount = 0;

        doneCount = 0;
        errorCount = 0;
        warningCount = 0;
        for (const auto &item : snap.items) {
            bool hasError = false;
            bool hasWarning = false;
            for (const auto &diagnostic : item.diagnostics) {
                hasError |= diagnostic.severity == Assets::ImportDiagnostic::Severity::Error;
                hasWarning |= diagnostic.severity == Assets::ImportDiagnostic::Severity::Warning;
            }

            if (hasError)
                ++errorCount;
            else if (item.result.has_value())
                ++doneCount;

            if (hasWarning)
                ++warningCount;
        }

        // ════════════════════════════════════════════════
        // SUMMARY
        // ════════════════════════════════════════════════
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{22.0f, 13.0f});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg2());
            ImGui::BeginChild("ImportSummary", ImVec2{0.0f, ImportLayout::SummaryH}, true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            const char *statusText = "SELECTING";
            switch (snap.phase) {
                case Assets::AssetImportPhase::Selecting:
                    statusText = "SELECTING";
                    break;
                case Assets::AssetImportPhase::Preparing:
                    statusText = "IMPORTING";
                    break;
                case Assets::AssetImportPhase::ReadyToCommit:
                    statusText = "READY";
                    break;
                case Assets::AssetImportPhase::Completed:
                    statusText = "COMPLETED";
                    break;
                case Assets::AssetImportPhase::Failed:
                    statusText = "FAILED";
                    break;
                case Assets::AssetImportPhase::Cancelled:
                    statusText = "CANCELLED";
                    break;
                case Assets::AssetImportPhase::Committing:
                    statusText = "IMPORTING";
                    break;
            }

            const ImVec2 pillPos = ImGui::GetCursorScreenPos();
            BadgeTone statusTone = BadgeTone::Accent;
            if (snap.phase == Assets::AssetImportPhase::ReadyToCommit || snap.phase == Assets::AssetImportPhase::Completed)
                statusTone = BadgeTone::Success;
            else if (snap.phase == Assets::AssetImportPhase::Failed)
                statusTone = BadgeTone::Error;
            else if (snap.phase == Assets::AssetImportPhase::Cancelled)
                statusTone = BadgeTone::Neutral;
            const BadgeProps statusBadge{
                .label = statusText,
                .tone = statusTone,
                .size = BadgeSize::Medium,
                .leadingIndicator = true,
            };
            constexpr float pillH = 30.0f;
            const float pillW = BadgeWidth(statusBadge, fonts);
            Badge(statusBadge, fonts);

            const float summaryY = pillPos.y + 1.0f;
            ImGui::SetCursorScreenPos({pillPos.x + pillW + 34.0f, summaryY});
            PushFont(fonts.sansCompact);
            ImGui::TextColored(Dim(), "Processed");
            ImGui::SetCursorScreenPos({pillPos.x + pillW + 34.0f, summaryY + 21.0f});
            ImGui::TextColored(Text(), "%zu / %zu", doneCount + errorCount, snap.items.size());

            const float trackX = pillPos.x + pillW + 145.0f;
            const float trackY = pillPos.y + 13.0f;
            const float trackW = 220.0f;
            const float total = static_cast<float>(snap.items.size());
            const float progress = total > 0.0f ? static_cast<float>(doneCount + errorCount) / total : 0.0f;
            dl->AddRectFilled({trackX, trackY}, {trackX + trackW, trackY + 5.0f}, U32(Bg3()), 2.5f);
            dl->AddRectFilled({trackX, trackY}, {trackX + trackW * progress, trackY + 5.0f}, U32(Accent()), 2.5f);

            ImGui::SetCursorScreenPos({trackX + trackW + 34.0f, summaryY});
            ImGui::TextColored(Dim(), "Warnings");
            ImGui::SetCursorScreenPos({trackX + trackW + 34.0f, summaryY + 21.0f});
            ImGui::TextColored(warningCount > 0 ? ImVec4{0.91f, 0.64f, 0.24f, 1.0f} : Text(), "%zu", warningCount);

            ImGui::SetCursorScreenPos({trackX + trackW + 135.0f, summaryY});
            ImGui::TextColored(Dim(), "Errors");
            ImGui::SetCursorScreenPos({trackX + trackW + 135.0f, summaryY + 21.0f});
            ImGui::TextColored(errorCount > 0 ? ImVec4{0.83f, 0.32f, 0.29f, 1.0f} : Text(), "%zu", errorCount);
            PopFont(fonts.sansCompact);

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        // ════════════════════════════════════════════════
        // TABS
        // ════════════════════════════════════════════════
        static int s_activeTab = static_cast<int>(ImportTab::Queue);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{22.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg0());
            ImGui::BeginChild("Tabs", ImVec2{0.0f, ImportLayout::TabsH}, true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImDrawList *tabsDrawList = ImGui::GetWindowDrawList();

            int conflictCount = 0;

            constexpr float tabPadX = 16.0f;
            float cursorX = 22.0f;
            PushFont(fonts.sansCompact);
            for (int i = 0; i < static_cast<int>(ImportTab::Count); ++i) {
                const bool active = i == s_activeTab;
                const char *label = TabLabel(i);
                const float labelW = ImGui::CalcTextSize(label).x;

                const float tabW = tabPadX * 2.0f + labelW;
                ImGui::SetCursorPos({cursorX, 0.0f});
                if (ImGui::InvisibleButton((std::string{"##ImportTab"} + std::to_string(i)).c_str(), {tabW, ImportLayout::TabsH - 2.0f}))
                    s_activeTab = i;

                const ImVec2 tabMin = ImGui::GetItemRectMin();
                const ImVec2 tabMax = ImGui::GetItemRectMax();
                const float textY = tabMin.y + (tabMax.y - tabMin.y - ImGui::GetTextLineHeight()) * 0.5f;
                tabsDrawList->AddText({tabMin.x + tabPadX, textY}, U32(active ? Text() : Dim()), label);

                if (active) {
                    tabsDrawList->AddRectFilled({tabMin.x, tabMax.y - 2.0f}, {tabMax.x, tabMax.y}, U32(Accent()));
                }
                cursorX += tabW + 2.0f;
            }
            PopFont(fonts.sansCompact);

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        // ════════════════════════════════════════════════
        // BODY: sidebar + content
        // ════════════════════════════════════════════════
        const float footerY = modalShell.FooterStartY();
        {
            const float bodyH = std::max(0.0f, footerY - ImGui::GetCursorPosY());

            // Sidebar
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{16.0f, 16.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{8.0f, 8.0f});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg0());
            ImGui::BeginChild("Sidebar", ImVec2{ImportLayout::SidebarW, bodyH}, true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImDrawList *sidebarDrawList = ImGui::GetWindowDrawList();

            LabeledSeparator("SOURCE", fonts);
            ImGui::Spacing();

            const float zoneW = ImGui::GetContentRegionAvail().x;
            constexpr float zoneH = 98.0f;
            const ImVec2 zonePos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##ImportDropZone", {zoneW, zoneH});
            const bool zoneHovered = ImGui::IsItemHovered();
            sidebarDrawList->AddRectFilled(zonePos, {zonePos.x + zoneW, zonePos.y + zoneH}, U32(zoneHovered ? Bg2() : Bg1()), 4.0f);
            DrawDashedRect(sidebarDrawList, zonePos, {zonePos.x + zoneW, zonePos.y + zoneH}, U32(zoneHovered ? Accent() : Border()));

            PushFont(fonts.sansEmphasis);
            const char *dropTitle = "Drop files here";
            const float dropTitleW = ImGui::CalcTextSize(dropTitle).x;
            sidebarDrawList->AddText({zonePos.x + (zoneW - dropTitleW) * 0.5f, zonePos.y + 31.0f}, U32(Text()), dropTitle);
            PopFont(fonts.sansEmphasis);
            PushFont(fonts.sansCompact);
            const char *dropSubtitle = "or click to browse";
            const float dropSubtitleW = ImGui::CalcTextSize(dropSubtitle).x;
            sidebarDrawList->AddText({zonePos.x + (zoneW - dropSubtitleW) * 0.5f, zonePos.y + 57.0f}, U32(Dim()), dropSubtitle);
            PopFont(fonts.sansCompact);

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("FILES")) {
                    std::string_view fileList(static_cast<const char *>(payload->Data), payload->DataSize);
                    std::vector<std::filesystem::path> droppedFiles;
                    std::size_t offset = 0;
                    while (offset < fileList.size()) {
                        auto lineEnd = fileList.find('\n', offset);
                        if (lineEnd == std::string_view::npos)
                            lineEnd = fileList.size();
                        auto value = fileList.substr(offset, lineEnd - offset);
                        while (!value.empty() && value.back() == '\0')
                            value.remove_suffix(1);
                        if (!value.empty())
                            droppedFiles.emplace_back(value);
                        offset = lineEnd + 1;
                    }
                    if (!droppedFiles.empty()) {
                        CancellationToken cancellation;
                        static_cast<void>(modal.BeginImport(droppedFiles, cancellation));
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::Dummy({0.0f, 12.0f});
            LabeledSeparator("QUEUE", fonts);
            ImGui::Spacing();

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0f, 5.0f});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg0());
            ImGui::BeginChild("QueueList", ImVec2{0.0f, 0.0f}, false);
            ImDrawList *queueDrawList = ImGui::GetWindowDrawList();

            for (std::size_t i = 0; i < snap.items.size(); ++i) {
                const auto &item = snap.items[i];
                bool hasError = false;
                bool hasWarning = false;
                for (const auto &diagnostic : item.diagnostics) {
                    hasError |= diagnostic.severity == Assets::ImportDiagnostic::Severity::Error;
                    hasWarning |= diagnostic.severity == Assets::ImportDiagnostic::Severity::Warning;
                }

                // Material Icons codepoints: error=0xE000, warning=0xE002, check_circle=0xE86C, circle=0xEF4A
                ImU32 icon = 0xEF4A;  // pending: outline circle
                if (item.result.has_value())
                    icon = 0xE86C;  // check_circle
                if (hasWarning)
                    icon = 0xE002;  // warning
                if (hasError)
                    icon = 0xE000;  // error

                const bool selected = i == snap.selectedItemIndex;
                const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                const float rowW = ImGui::GetContentRegionAvail().x;
                constexpr float rowH = 38.0f;
                if (ImGui::InvisibleButton((std::string{"##QueueItem"} + std::to_string(i)).c_str(), {rowW, rowH}))
                    modal.SelectItem(i);

                const ImVec2 rowMax{rowMin.x + rowW, rowMin.y + rowH};
                const ImU32 rowBg = U32(selected ? ImVec4{Accent().x, Accent().y, Accent().z, 0.12f} : Bg3());
                queueDrawList->AddRectFilled(rowMin, rowMax, rowBg, 4.0f);
                queueDrawList->AddRect(rowMin, rowMax, U32(selected ? Accent() : Border()), 4.0f);

                // Icon top-left anchored so the glyph is vertically centered in the row.
                // AddText's pos parameter is the glyph's top-left corner, not its baseline,
                // so centering requires subtracting half the glyph height from the row center.
                const float rowCenter = rowMin.y + rowH * 0.5f;
                const float iconY = rowCenter - fonts.icon->FontSize * 0.5f;
                const ImVec2 iconPos{rowMin.x + 14.0f, iconY};
                DrawIcon(queueDrawList, iconPos, icon, U32(Text()), fonts);

                PushFont(fonts.sansCompact);
                const float textY = rowMin.y + (rowH - fonts.sansCompact->FontSize) * 0.5f;
                queueDrawList->AddText({rowMin.x + 36.0f, textY}, U32(Text()),
                                       DisplayFileName(std::filesystem::path{item.sourceFile.String()}).c_str());
                PopFont(fonts.sansCompact);
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);

            // Content panel
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{22.0f, 22.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{8.0f, 8.0f});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg1());
            ImGui::BeginChild("Content", ImVec2{0.0f, bodyH}, true);

            switch (static_cast<ImportTab>(s_activeTab)) {
                case ImportTab::Queue: {
                    if (!snap.items.empty() && snap.selectedItemIndex < snap.items.size()) {
                        const auto &sel = snap.items[snap.selectedItemIndex];
                        LabeledSeparator("SELECTED FILE", fonts);
                        ImGui::Dummy({0.0f, 6.0f});
                        PushFont(fonts.sansCompact);
                        ImGui::TextColored(Dim(), "File");
                        ImGui::SameLine(0, 24);
                        ImGui::TextColored(Text(), "%s", sel.displayName.c_str());
                        ImGui::SameLine(0, 30);
                        ImGui::TextColored(Dim(), "Type");
                        ImGui::SameLine(0, 12);
                        ImGui::TextColored(Text(), ".%s", sel.sourceExtension.c_str());
                        ImGui::SameLine(0, 30);
                        ImGui::TextColored(Dim(), "Importer");
                        ImGui::SameLine(0, 12);
                        if (!sel.importerContributionId.empty())
                            ImGui::TextColored(Accent(), "%s", FriendlyImporterName(sel.importerContributionId).c_str());
                        else
                            ImGui::TextColored(ImVec4{0.83f, 0.32f, 0.29f, 1.0f}, "None for .%s", sel.sourceExtension.c_str());
                        PopFont(fonts.sansCompact);
                        ImGui::Dummy({0.0f, 12.0f});
                    }

                    if (snap.items.empty()) {
                        ImGui::TextColored(Dim(), "Select a file from the queue to view its overview.");
                    }
                    break;
                }
                case ImportTab::Diagnostics: {
                    if (snap.items.empty()) {
                        ImGui::TextColored(Dim(), "No diagnostics available.");
                        break;
                    }

                    struct DiagnosticRow {
                        std::string assetLabel;
                        std::string message;
                        Assets::ImportDiagnostic::Severity severity;
                    };

                    std::vector<DiagnosticRow> rows;
                    rows.reserve(16);

                    for (const auto &item : snap.items) {
                        const std::filesystem::path sourcePath{item.sourceFile.String()};
                        const std::string assetLabel = DisplayAssetLabel(sourcePath);

                        for (const auto &diagnostic : item.diagnostics) {
                            const std::string cleanedMessage = CleanDiagnosticMessage(sourcePath, diagnostic.message);
                            if (cleanedMessage.empty())
                                continue;

                            const auto isDuplicate = [&](const DiagnosticRow &row) {
                                return row.assetLabel == assetLabel && row.message == cleanedMessage && row.severity == diagnostic.severity;
                            };

                            if (std::find_if(rows.begin(), rows.end(), isDuplicate) == rows.end()) {
                                rows.push_back({
                                    .assetLabel = assetLabel,
                                    .message = cleanedMessage,
                                    .severity = diagnostic.severity,
                                });
                            }
                        }
                    }

                    if (rows.empty()) {
                        ImGui::TextColored(Dim(), "No diagnostics available.");
                        break;
                    }

                    constexpr float rowHeight = 48.0f;
                    constexpr float leftPadding = 18.0f;
                    constexpr float rightPadding = 14.0f;
                    constexpr float timeColumnWidth = 92.0f;
                    constexpr float gapAfterTime = 34.0f;
                    constexpr float gapAfterAsset = 34.0f;

                    ImDrawList *diagnosticsDrawList = ImGui::GetWindowDrawList();
                    PushFont(fonts.sansCompact);

                    const float availableWidth = ImGui::GetContentRegionAvail().x;
                    const float assetColumnWidth = std::clamp(availableWidth * 0.23f, 170.0f, 220.0f);

                    for (std::size_t diagnosticIndex = 0; diagnosticIndex < rows.size(); ++diagnosticIndex) {
                        const auto &row = rows[diagnosticIndex];

                        ImVec4 messageColor = ImVec4{0.72f, 0.71f, 0.68f, 1.0f};
                        if (row.severity == Assets::ImportDiagnostic::Severity::Error)
                            messageColor = ImVec4{0.83f, 0.32f, 0.29f, 1.0f};
                        else if (row.severity == Assets::ImportDiagnostic::Severity::Warning)
                            messageColor = ImVec4{0.91f, 0.64f, 0.24f, 1.0f};

                        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                        const float rowWidth = ImGui::GetContentRegionAvail().x;
                        ImGui::InvisibleButton((std::string{"##DiagnosticRow"} + std::to_string(diagnosticIndex)).c_str(),
                                               {rowWidth, rowHeight});
                        const ImVec2 rowMax{rowMin.x + rowWidth, rowMin.y + rowHeight};

                        const float textY = rowMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
                        const float timeX = rowMin.x + leftPadding;
                        const float assetX = timeX + timeColumnWidth + gapAfterTime;
                        const float messageX = assetX + assetColumnWidth + gapAfterAsset;

                        diagnosticsDrawList->PushClipRect({timeX, rowMin.y}, {timeX + timeColumnWidth, rowMax.y}, true);
                        diagnosticsDrawList->AddText({timeX, textY}, U32(Dim()), DiagnosticTimeLabel(diagnosticIndex));
                        diagnosticsDrawList->PopClipRect();

                        diagnosticsDrawList->PushClipRect({assetX, rowMin.y}, {assetX + assetColumnWidth, rowMax.y}, true);
                        diagnosticsDrawList->AddText({assetX, textY}, U32(Accent()), row.assetLabel.c_str());
                        diagnosticsDrawList->PopClipRect();

                        diagnosticsDrawList->PushClipRect({messageX, rowMin.y}, {rowMax.x - rightPadding, rowMax.y}, true);
                        diagnosticsDrawList->AddText({messageX, textY}, U32(messageColor), row.message.c_str());
                        diagnosticsDrawList->PopClipRect();
                    }

                    PopFont(fonts.sansCompact);
                    break;
                }
                case ImportTab::Settings: {
                    const bool hasSelection = !snap.items.empty() && snap.selectedItemIndex < snap.items.size();
                    if (!hasSelection) {
                        ImGui::TextColored(Dim(), "Select a file from the queue to view its importer settings.");
                        break;
                    }

                    const auto &sel = snap.items[snap.selectedItemIndex];

                    // Show importer info
                    LabeledSeparator("IMPORTER", fonts);
                    ImGui::Dummy({0.0f, 4.0f});

                    const auto *contrib = modal.Catalog().FindContributionByExtension(sel.sourceExtension);
                    if (!contrib) {
                        PushFont(fonts.sansCompact);
                        ImGui::TextColored(ImVec4{0.83f, 0.32f, 0.29f, 1.0f}, "No importer available for .%s files.",
                                           sel.sourceExtension.c_str());
                        ImGui::Dummy({0.0f, 4.0f});
                        ImGui::TextColored(Dim(), "Install or enable an importer extension that handles .%s files.",
                                           sel.sourceExtension.c_str());
                        PopFont(fonts.sansCompact);
                        break;
                    }

                    // Importer name with built-in badge
                    PushFont(fonts.sansCompact);
                    ImGui::TextColored(Dim(), "Name");
                    ImGui::SameLine(0, 24);
                    ImGui::TextColored(Accent(), "%s", FriendlyImporterName(contrib->contributionId).c_str());
                    if (contrib->builtIn) {
                        ImGui::SameLine(0, 10);
                        ImGui::TextColored(ImVec4{Accent().x, Accent().y, Accent().z, 0.7f}, "BUILT-IN");
                    }
                    PopFont(fonts.sansCompact);

                    ImGui::Dummy({0.0f, 8.0f});

                    // Dynamic settings based on declarative schema
                    if (contrib->settings.empty()) {
                        ImGui::TextColored(Dim(), "This importer has no configurable settings.");
                    } else {
                        LabeledSeparator("SETTINGS", fonts);
                        ImGui::Dummy({0.0f, 4.0f});

                        for (const auto &setting : contrib->settings) {
                            PushFont(fonts.sansCompact);
                            FieldLabel(setting.labelKey.c_str(), fonts);

                            // Use the item's persistent settings map, keyed by settingId.
                            auto &settingsMap = const_cast<Assets::AssetImportItem &>(sel).settings;
                            const std::string key = "settings." + setting.id;

                            switch (setting.kind) {
                                case Assets::ImportSettingKind::Boolean: {
                                    bool val = settingsMap.contains(key) ? (settingsMap[key] == "true")
                                                                         : (std::holds_alternative<bool>(setting.defaultValue)
                                                                                ? std::get<bool>(setting.defaultValue)
                                                                                : false);
                                    if (Ui::ToggleControl(("##Setting_" + setting.id).c_str(), &val, fonts, true))
                                        settingsMap[key] = val ? "true" : "false";
                                    break;
                                }
                                case Assets::ImportSettingKind::Choice: {
                                    std::vector<const char *> labels;
                                    for (const auto &c : setting.choices)
                                        labels.push_back(c.labelKey.c_str());
                                    int current = settingsMap.contains(key) ? std::stoi(settingsMap[key]) : 0;
                                    if (Ui::ComboControl(("##Setting_" + setting.id).c_str(), &current, labels.data(),
                                                         static_cast<int>(labels.size()), fonts))
                                        settingsMap[key] = std::to_string(current);
                                    break;
                                }
                                case Assets::ImportSettingKind::Integer: {
                                    int val = settingsMap.contains(key)
                                                  ? std::stoi(settingsMap[key])
                                                  : (std::holds_alternative<std::int64_t>(setting.defaultValue)
                                                         ? static_cast<int>(std::get<std::int64_t>(setting.defaultValue))
                                                         : 0);
                                    Ui::InputIntControl(("##Setting_" + setting.id).c_str(), &val, fonts);
                                    settingsMap[key] = std::to_string(val);
                                    break;
                                }
                                case Assets::ImportSettingKind::Float: {
                                    float val = settingsMap.contains(key)
                                                    ? std::stof(settingsMap[key])
                                                    : (std::holds_alternative<double>(setting.defaultValue)
                                                           ? static_cast<float>(std::get<double>(setting.defaultValue))
                                                           : 0.0f);
                                    Ui::InputFloatControl(("##Setting_" + setting.id).c_str(), &val, fonts);
                                    settingsMap[key] = std::to_string(val);
                                    break;
                                }
                                case Assets::ImportSettingKind::Text: {
                                    static char buf[256];
                                    if (!settingsMap.contains(key))
                                        settingsMap[key] = "";
                                    // Copy current value into buffer for editing
                                    std::strncpy(buf, settingsMap[key].c_str(), sizeof(buf) - 1);
                                    buf[sizeof(buf) - 1] = '\0';
                                    if (Ui::InputTextControl(("##Setting_" + setting.id).c_str(), buf, sizeof(buf), fonts))
                                        settingsMap[key] = buf;
                                    break;
                                }
                            }
                            PopFont(fonts.sansCompact);
                        }
                    }

                    ImGui::Dummy({0.0f, 4.0f});
                    if (sel.result.has_value()) {
                        ImGui::TextColored(ImVec4{0.37f, 0.72f, 0.54f, 1.0f}, "✓ This file has been imported.");
                    } else {
                        ImGui::TextColored(Dim(), "Configure settings above, then click Import in the footer.");
                    }
                    break;
                }
                case ImportTab::Destination: {
                    const bool hasSelection = !snap.items.empty() && snap.selectedItemIndex < snap.items.size();

                    // Get mutable reference to selected item for read/write
                    auto *selItem = hasSelection ? &const_cast<Assets::AssetImportItem &>(snap.items[snap.selectedItemIndex]) : nullptr;

                    int subfolderByType = selItem ? selItem->subfolderByType : 0;
                    int assetIdStrategy = selItem ? selItem->assetIdStrategy : 0;
                    bool createMetaSidecar = selItem ? selItem->createMetaSidecar : true;
                    bool overwriteWithoutPrompt = selItem ? selItem->overwriteWithoutPrompt : false;

                    static const char *kSubfolderModes[] = {"Meshes / Textures / Audio", "Mirror source structure", "Flat"};
                    static const char *kAssetIdModes[] = {"New GUID", "Stable hash"};

                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0f, 7.0f});

                    FieldLabel("ASSET NAME", fonts);
                    std::string assetName = selItem ? selItem->displayName : std::string{};
                    if (Ui::InputTextControl("##AssetName", assetName, 256, fonts) && selItem)
                        selItem->displayName = std::move(assetName);
                    ImGui::Dummy({0.0f, 6.0f});

                    FieldLabel("TARGET FOLDER", fonts);

                    constexpr float browseButtonWidth = 38.0f;
                    constexpr float browseGap = 8.0f;
                    const float targetInputWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - browseButtonWidth - browseGap);
                    std::string targetFolder = "assets";
                    if (selItem && !selItem->destinationFolder.empty())
                        targetFolder = selItem->destinationFolder;
                    if (Ui::InputTextControl("##TargetFolder", targetFolder, 256, fonts, false, targetInputWidth) && selItem)
                        selItem->destinationFolder = std::move(targetFolder);
                    ImGui::SameLine(0.0f, browseGap);
                    if (IconButton({
                            .id = "##BrowseTargetFolder",
                            .glyph = IconButtonGlyph::Folder,
                            .size = {browseButtonWidth, ImGui::GetFrameHeight()},
                            .tooltip = "Browse project folders",
                            .enabled = selItem != nullptr,
                        })) {
                        if (const auto selectedFolder = OpenFolderSelectionDialog("Select asset destination")) {
                            if (const auto relative = ProjectRelativeFolder(modal.ProjectRoot(), *selectedFolder))
                                selItem->destinationFolder = relative->empty() ? "assets" : *relative;
                        }
                    }
                    ImGui::Dummy({0.0f, 6.0f});

                    {
                        const SplitColumns columns = CurrentSplitColumns();

                        ImGui::BeginGroup();
                        FieldLabel("SUBFOLDER BY TYPE", fonts);
                        ImGui::SetNextItemWidth(columns.width);
                        if (Ui::ComboControl("##SubfolderByType", &subfolderByType, kSubfolderModes, IM_ARRAYSIZE(kSubfolderModes),
                                             fonts)) {
                            if (selItem)
                                selItem->subfolderByType = subfolderByType;
                        }
                        ImGui::EndGroup();

                        MoveToSecondColumn(columns);
                        ImGui::BeginGroup();
                        FieldLabel("ASSETID STRATEGY", fonts);
                        ImGui::SetNextItemWidth(columns.width);
                        if (Ui::ComboControl("##AssetIdStrategy", &assetIdStrategy, kAssetIdModes, IM_ARRAYSIZE(kAssetIdModes), fonts)) {
                            if (selItem)
                                selItem->assetIdStrategy = assetIdStrategy;
                        }
                        ImGui::EndGroup();

                        FinishSplitColumns(columns);
                    }

                    ImGui::Dummy({0.0f, 12.0f});
                    if (Ui::CheckboxControl("Create .meta sidecar for each asset", &createMetaSidecar, fonts)) {
                        if (selItem)
                            selItem->createMetaSidecar = createMetaSidecar;
                    }
                    ImGui::Dummy({0.0f, 5.0f});
                    if (Ui::CheckboxControl("Overwrite existing assets without prompt", &overwriteWithoutPrompt, fonts)) {
                        if (selItem)
                            selItem->overwriteWithoutPrompt = overwriteWithoutPrompt;
                    }

                    ImGui::PopStyleVar();
                    break;
                }
                default:
                    break;
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }

        // ════════════════════════════════════════════════
        // FOOTER
        // ════════════════════════════════════════════════
        {
            modalShell.BeginFooter({28.0F, 0.0F}, true);

            constexpr float actionH = 32.0f;
            const float actionY = (ImGui::GetWindowHeight() - actionH) * 0.5f;
            ImGui::SetCursorPosY(actionY);
            PushFont(fonts.sansCompact);
            ImGui::AlignTextToFramePadding();
            std::string statusText = std::to_string(snap.items.size()) + " file(s)";
            std::size_t imported = 0;
            for (const auto &it : snap.items)
                if (it.result.has_value())
                    ++imported;
            if (imported > 0)
                statusText += " — " + std::to_string(imported) + " imported";
            ImGui::TextColored(Dim(), "%s", statusText.c_str());
            PopFont(fonts.sansCompact);

            constexpr float cancelW = 100.0f;
            constexpr float importW = 100.0f;
            constexpr float gap = 12.0f;
            constexpr float presetW = 176.0f;
            constexpr float presetAddW = 36.0f;
            constexpr float presetGap = 6.0f;
            constexpr float presetActionsGap = 18.0f;
            const float actionsW = presetW + presetAddW + presetGap + presetActionsGap + cancelW + importW + gap;
            ImGui::SameLine(ImGui::GetWindowWidth() - actionsW - 28.0f);
            ImGui::SetCursorPosY(actionY);

            const bool hasSelected = !snap.items.empty() && snap.selectedItemIndex < snap.items.size();
            const auto *selItem = hasSelected ? &snap.items[snap.selectedItemIndex] : nullptr;
            const bool hasImporter = selItem && !selItem->importerContributionId.empty();
            std::vector<std::string> presetNames =
                hasImporter ? modal.PresetNames(snap.selectedItemIndex) : std::vector<std::string>{"Default"};
            std::vector<const char *> presetLabels;
            presetLabels.reserve(presetNames.size());
            for (const auto &presetName : presetNames)
                presetLabels.push_back(presetName.c_str());

            int presetIndex = 0;
            if (hasSelected) {
                const auto activePreset = modal.ActivePresetName(snap.selectedItemIndex);
                const auto active = std::find(presetNames.begin(), presetNames.end(), activePreset);
                if (active != presetNames.end())
                    presetIndex = static_cast<int>(std::distance(presetNames.begin(), active));
            }
            ImGui::SetNextItemWidth(presetW);
            if (Ui::ComboControl("##ImportPreset", &presetIndex, presetLabels.data(), static_cast<int>(presetLabels.size()), fonts, false,
                                 actionH) &&
                hasSelected) {
                static_cast<void>(modal.ApplyPreset(snap.selectedItemIndex, presetNames[presetIndex]));
            }

            ImGui::SameLine(0.0f, presetGap);
            static char presetNameBuffer[96]{};
            static bool presetNameError = false;
            if (IconButton({
                    .id = "##CreateImportPreset",
                    .glyph = IconButtonGlyph::Plus,
                    .size = {presetAddW, actionH},
                    .tooltip = "Create preset from current importer settings",
                    .enabled = hasImporter,
                })) {
                presetNameBuffer[0] = '\0';
                presetNameError = false;
                ImGui::OpenPopup("Create Import Preset");
            }

            if (ImGui::BeginPopupModal("Create Import Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                FieldLabel("PRESET NAME", fonts);
                static_cast<void>(Ui::InputTextControl("##NewImportPresetName", presetNameBuffer, sizeof(presetNameBuffer), fonts,
                                                       presetNameError, 280.0f));
                ImGui::Dummy({0.0f, 10.0f});

                ButtonProps dismissPresetProps{
                    .label = "Cancel",
                    .size = {90.0f, actionH},
                    .variant = ButtonVariant::Secondary,
                };
                if (Button(dismissPresetProps))
                    ImGui::CloseCurrentPopup();
                ImGui::SameLine(0.0f, gap);
                ButtonProps createPresetProps{
                    .label = "Create",
                    .size = {90.0f, actionH},
                    .variant = ButtonVariant::Primary,
                    .enabled = presetNameBuffer[0] != '\0',
                };
                if (Button(createPresetProps)) {
                    if (modal.CreatePreset(snap.selectedItemIndex, presetNameBuffer)) {
                        presetNameError = false;
                        ImGui::CloseCurrentPopup();
                    } else {
                        presetNameError = true;
                    }
                }
                if (presetNameError)
                    ImGui::TextColored(ImVec4{0.83f, 0.32f, 0.29f, 1.0f}, "Use a unique preset name.");
                ImGui::EndPopup();
            }

            ImGui::SameLine(0.0f, presetActionsGap);
            ButtonProps cancelProps{
                .label = "Cancel",
                .size = {cancelW, actionH},
                .variant = ButtonVariant::Secondary,
                .enabled = true,
            };
            const bool canImport =
                selItem && !selItem->displayName.empty() && !selItem->result.has_value() && !selItem->importerContributionId.empty();
            ButtonProps importProps{
                .label = "Import",
                .size = {importW, actionH},
                .variant = ButtonVariant::Primary,
                .enabled = canImport,
            };

            if (Button(cancelProps))
                frameResult = ModalFrameResult::RequestClose(ModalCloseReason::Cancelled);
            ImGui::SameLine(0.0f, gap);
            if (Button(importProps)) {
                CancellationToken cancellation;
                static_cast<void>(modal.ImportSingleItem(snap.selectedItemIndex, cancellation));
            }

            modalShell.EndFooter();
        }

        return frameResult;
    }
}  // namespace Horo::Editor
