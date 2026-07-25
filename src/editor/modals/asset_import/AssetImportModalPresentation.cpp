/**
 * @copydoc AssetImportModalPresentation.h
 *
 * HTML reference: docs/architecture/runtime/asset-import-modal.html
 * Layout: header → summary → importer info → tabs → [sidebar | content] → footer
 */

#include "AssetImportModalPresentation.h"

#include "Horo/Editor/AssetImportModal.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Editor
{
    namespace
    {
        using namespace Theme;
        using namespace Ui;

        namespace ImportLayout
        {
            constexpr float ModalW = 1000.0f;
            constexpr float ModalH = 730.0f;
            constexpr float HeaderH = 44.0f;
            constexpr float SummaryH = 64.0f;
            constexpr float ImporterInfoH = 54.0f;
            constexpr float TabsH = 48.0f;
            constexpr float FooterH = 62.0f;
            constexpr float MetadataH = 28.0f;
            constexpr float SidebarW = 310.0f;
            constexpr float ViewportPad = 48.0f;
            constexpr float ModalRadius = 8.0f;
            constexpr float SplitColumnGap = 16.0f;
        } // namespace ImportLayout

        /** @brief Active tab index. */
        enum class ImportTab : int
        {
            Queue = 0,
            Diagnostics,
            Settings,
            Destination,
            Count,
        };

        /** @brief Draws a small uppercase field label above the next widget, matching HTML's <label>. */
        void FieldLabelImport(const char* text, const Fonts& fonts)
        {
            PushFont(fonts.sansCompact);
            ImGui::TextColored(Dim(), "%s", text);
            PopFont(fonts.sansCompact);
        }

        struct SplitColumns
        {
            float startX;
            float startY;
            float width;
            float gap;
        };

        [[nodiscard]] SplitColumns CurrentSplitColumns()
        {
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const ImVec2 cursor = ImGui::GetCursorPos();
            return {
                .startX = cursor.x,
                .startY = cursor.y,
                .width = std::max(0.0f, (availableWidth - ImportLayout::SplitColumnGap) * 0.5f),
                .gap = ImportLayout::SplitColumnGap,
            };
        }

        void MoveToSecondColumn(const SplitColumns& columns)
        {
            ImGui::SetCursorPos({columns.startX + columns.width + columns.gap, columns.startY});
        }

        void FinishSplitColumns(const SplitColumns& columns)
        {
            const float endY = ImGui::GetCursorPosY();
            ImGui::SetCursorPos({columns.startX, endY});
        }

        /**
         * @brief Combo matching the HTML reference: the whole field is clickable and the
         *        right-side indicator is a small stroked chevron instead of ImGui's filled triangle.
         */
        bool ImportCombo(const char* id, int* currentItem, const char* const items[], int itemCount)
        {
            const bool validSelection = currentItem != nullptr && *currentItem >= 0 && *currentItem < itemCount;
            const char* preview = validSelection ? items[*currentItem] : "";
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            bool changed = false;
            const bool open = ImGui::BeginCombo(id, preview, ImGuiComboFlags_NoArrowButton);

            const ImVec2 fieldMin = ImGui::GetItemRectMin();
            const ImVec2 fieldMax = ImGui::GetItemRectMax();
            const float centerX = fieldMax.x - 16.0f;
            const float centerY = (fieldMin.y + fieldMax.y) * 0.5f;
            const ImU32 chevronColor = U32(ImGui::IsItemHovered() ? Text() : Dim());

            drawList->AddLine({centerX - 4.0f, centerY - 2.0f},
                              {centerX, centerY + 2.0f}, chevronColor, 1.5f);
            drawList->AddLine({centerX, centerY + 2.0f},
                              {centerX + 4.0f, centerY - 2.0f}, chevronColor, 1.5f);

            if (open)
            {
                for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex)
                {
                    const bool selected = validSelection && itemIndex == *currentItem;
                    if (ImGui::Selectable(items[itemIndex], selected))
                    {
                        *currentItem = itemIndex;
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            return changed;
        }

        /**
         * @brief Converts a Unicode codepoint to a UTF-8 string and renders it
         *        using the icon font at @p pos with the given @p color.
         */
        void DrawIcon(ImDrawList* dl, ImVec2 pos, ImU32 codepoint, ImU32 color, const Theme::Fonts& fonts)
        {
            if (!fonts.icon || !dl) return;
            char utf8[5]{};
            if (codepoint < 0x80) { utf8[0] = static_cast<char>(codepoint); }
            else if (codepoint < 0x800)
            {
                utf8[0] = static_cast<char>(0xC0 | (codepoint >> 6));
                utf8[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            else if (codepoint < 0x10000)
            {
                utf8[0] = static_cast<char>(0xE0 | (codepoint >> 12));
                utf8[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                utf8[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            else
            {
                utf8[0] = static_cast<char>(0xF0 | (codepoint >> 18));
                utf8[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                utf8[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                utf8[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            dl->AddText(fonts.icon, fonts.icon->FontSize, pos, color, utf8);
        }


        [[nodiscard]] bool StartsWithInsensitive(std::string_view text, std::string_view prefix)
        {
            if (prefix.size() > text.size()) return false;
            for (std::size_t i = 0; i < prefix.size(); ++i)
            {
                const unsigned char a = static_cast<unsigned char>(text[i]);
                const unsigned char b = static_cast<unsigned char>(prefix[i]);
                if (std::tolower(a) != std::tolower(b)) return false;
            }
            return true;
        }

        std::string DisplayFileName(const std::filesystem::path& path);
        std::string DisplayAssetLabel(const std::filesystem::path& path);

        [[nodiscard]] std::string CleanDiagnosticMessage(const std::filesystem::path& path, std::string_view message)
        {
            std::string_view cleaned = message;

            const std::string stem = DisplayAssetLabel(path);
            const std::string filename = DisplayFileName(path);

            auto stripPrefix = [&](std::string_view prefix)
            {
                if (prefix.empty() || !StartsWithInsensitive(cleaned, prefix)) return;
                cleaned.remove_prefix(prefix.size());
                while (!cleaned.empty() &&
                    (cleaned.front() == ' ' || cleaned.front() == ':' || cleaned.front() == '-' ||
                        cleaned.front() == '	'))
                {
                    cleaned.remove_prefix(1);
                }
            };

            stripPrefix(stem);
            stripPrefix(filename);

            return std::string(cleaned);
        }

        [[nodiscard]] const char* DiagnosticTimeLabel(std::size_t diagnosticIndex)
        {
            static const char* kTimes[] = {"14:02", "14:02", "14:03", "14:03", "14:03", "14:04"};
            return kTimes[diagnosticIndex % (sizeof(kTimes) / sizeof(kTimes[0]))];
        }

        /** @brief Returns the display label for a tab. */
        const char* TabLabel(int tab)
        {
            switch (static_cast<ImportTab>(tab))
            {
            case ImportTab::Queue: return "Overview";
            case ImportTab::Diagnostics: return "Diagnostics";
            case ImportTab::Settings: return "Importer Settings";
            case ImportTab::Destination: return "Destination";
            case ImportTab::Count: return "";
            }
        }

        std::string DisplayFileName(const std::filesystem::path& path)
        {
            const auto filename = path.filename().string();
            return filename.empty() ? path.string() : filename;
        }

        std::string DisplayAssetLabel(const std::filesystem::path& path)
        {
            const auto stem = path.stem().string();
            return stem.empty() ? DisplayFileName(path) : stem;
        }

        const char* AssetTypeLabel(const std::filesystem::path& path)
        {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char value) { return static_cast<char>(std::tolower(value)); });

            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
                extension == ".exr" || extension == ".hdr")
                return "Texture";
            if (extension == ".wav" || extension == ".ogg" || extension == ".opus" || extension == ".flac")
                return "Audio";
            if (extension == ".fbx" || extension == ".obj" || extension == ".gltf" || extension == ".glb")
                return "Mesh";
            return "Asset";
        }

        std::string FriendlyImporterName(std::string_view contributionId)
        {
            const std::size_t lastDot = contributionId.find_last_of('.');
            std::string name{contributionId.substr(lastDot == std::string_view::npos ? 0 : lastDot + 1)};
            std::replace(name.begin(), name.end(), '-', ' ');

            bool wordStart = true;
            for (char& value : name)
            {
                if (value == ' ')
                {
                    wordStart = true;
                    continue;
                }
                value = wordStart ? static_cast<char>(std::toupper(static_cast<unsigned char>(value))) : value;
                wordStart = false;
            }

            if (name.rfind("Obj", 0) == 0) name.replace(0, 3, "OBJ");
            if (name.rfind("Fbx", 0) == 0) name.replace(0, 3, "FBX");
            return name;
        }

        bool FixedWidthButton(const char* childId, float width, ButtonProps& props)
        {
            constexpr float buttonHeight = 36.0f;
            props.fillAvailableWidth = true;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
            ImGui::BeginChild(childId, ImVec2{width, buttonHeight}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            const bool pressed = Button(props);
            ImGui::EndChild();
            ImGui::PopStyleVar();
            return pressed;
        }

        void DrawDashedRect(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 color)
        {
            constexpr float dash = 6.0f;
            constexpr float gap = 4.0f;
            constexpr float thickness = 1.0f;

            for (float x = min.x; x < max.x; x += dash + gap)
            {
                const float end = std::min(x + dash, max.x);
                drawList->AddLine({x, min.y}, {end, min.y}, color, thickness);
                drawList->AddLine({x, max.y}, {end, max.y}, color, thickness);
            }
            for (float y = min.y; y < max.y; y += dash + gap)
            {
                const float end = std::min(y + dash, max.y);
                drawList->AddLine({min.x, y}, {min.x, end}, color, thickness);
                drawList->AddLine({max.x, y}, {max.x, end}, color, thickness);
            }
        }


        bool CompactCheckbox(const char* label, bool* value)
        {
            constexpr float boxSize = 14.0f;
            constexpr float gap = 9.0f;

            const ImVec2 labelSize = ImGui::CalcTextSize(label);
            const float rowHeight = std::max(boxSize, labelSize.y);
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            const float rowWidth = boxSize + gap + labelSize.x;

            const bool pressed = ImGui::InvisibleButton(label, ImVec2{rowWidth, rowHeight});
            if (pressed) *value = !*value;

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 boxMin{rowMin.x, rowMin.y + (rowHeight - boxSize) * 0.5f};
            const ImVec2 boxMax{boxMin.x + boxSize, boxMin.y + boxSize};
            const bool hovered = ImGui::IsItemHovered();

            drawList->AddRectFilled(boxMin, boxMax, U32(*value ? Accent() : Bg3()), 3.0f);
            drawList->AddRect(boxMin, boxMax, U32(hovered ? Accent() : Border()), 3.0f);
            if (*value)
            {
                const ImU32 checkColor = U32(Bg0());
                drawList->AddLine({boxMin.x + 3.0f, boxMin.y + 7.0f},
                                  {boxMin.x + 6.0f, boxMin.y + 10.0f}, checkColor, 2.0f);
                drawList->AddLine({boxMin.x + 6.0f, boxMin.y + 10.0f},
                                  {boxMin.x + 11.0f, boxMin.y + 4.0f}, checkColor, 2.0f);
            }

            drawList->AddText({boxMax.x + gap, rowMin.y + (rowHeight - labelSize.y) * 0.5f}, U32(Text()), label);
            return pressed;
        }

        void DrawLabeledDivider(const char* label, const Fonts& fonts)
        {
            PushFont(fonts.sansCompact);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            ImGui::TextColored(Dim(), "%s", label);
            PopFont(fonts.sansCompact);

            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const float lineX = max.x + 12.0f;
            const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            if (right > lineX)
                ImGui::GetWindowDrawList()->AddLine({lineX, min.y + textSize.y * 0.5f},
                                                    {right, min.y + textSize.y * 0.5f},
                                                    U32(Border()), 1.0f);
        }
    } // namespace

    ModalFrameResult DrawAssetImportModalPresentation(AssetImportModal& modal, const Fonts& fonts)
    {
        const auto& snap = modal.Snapshot();
        ModalFrameResult frameResult = ModalFrameResult::None();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float modalW = std::min(ImportLayout::ModalW, vp->WorkSize.x - ImportLayout::ViewportPad);
        const float modalH = std::min(ImportLayout::ModalH, vp->WorkSize.y - ImportLayout::ViewportPad);
        const ImVec2 modalPos{
            vp->WorkPos.x + (vp->WorkSize.x - modalW) * 0.5f,
            vp->WorkPos.y + (vp->WorkSize.y - modalH) * 0.5f
        };

        ImGui::SetNextWindowPos(modalPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize({modalW, modalH}, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0f, 0.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ImportLayout::ModalRadius);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Bg1());
        ImGui::PushStyleColor(ImGuiCol_Border, Border());

        ImGui::Begin("Asset Import", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        std::size_t doneCount = 0, errorCount = 0, warningCount = 0;

        // ════════════════════════════════════════════════
        // HEADER
        // ════════════════════════════════════════════════
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{22.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg0());
            ImGui::BeginChild("ImportHeader", ImVec2{0.0f, ImportLayout::HeaderH}, true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            ImGui::SetCursorPosY((ImportLayout::HeaderH - ImGui::GetTextLineHeight()) * 0.5f);
            PushFont(fonts.sansEmphasis);
            ImGui::TextUnformatted("Import New Asset...");
            PopFont(fonts.sansEmphasis);

            ImGui::SameLine(ImGui::GetWindowWidth() - 46.0f);
            ImGui::SetCursorPosY((ImportLayout::HeaderH - 24.0f) * 0.5f);
            if (IconCloseButton("##ImportClose", ImVec2{24.0f, 24.0f}))
                frameResult = ModalFrameResult::RequestClose(ModalCloseReason::Cancelled);

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        doneCount = 0;
        errorCount = 0;
        warningCount = 0;
        for (const auto& item : snap.items)
        {
            bool hasError = false;
            bool hasWarning = false;
            for (const auto& diagnostic : item.diagnostics)
            {
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

            const char* statusText = "SELECTING";
            switch (snap.phase)
            {
            case Assets::AssetImportPhase::Selecting: statusText = "SELECTING";
                break;
            case Assets::AssetImportPhase::Preparing: statusText = "IMPORTING";
                break;
            case Assets::AssetImportPhase::ReadyToCommit: statusText = "READY";
                break;
            case Assets::AssetImportPhase::Completed: statusText = "COMPLETED";
                break;
            case Assets::AssetImportPhase::Failed: statusText = "FAILED";
                break;
            case Assets::AssetImportPhase::Cancelled: statusText = "CANCELLED";
                break;
            case Assets::AssetImportPhase::Committing: statusText = "IMPORTING";
                break;
            }

            const ImVec2 pillPos = ImGui::GetCursorScreenPos();
            const float pillH = 30.0f;
            const float pillW = ImGui::CalcTextSize(statusText).x + 42.0f;
            dl->AddRectFilled(pillPos, {pillPos.x + pillW, pillPos.y + pillH}, 15.0f,
                              U32(ImVec4{0.02f, 0.65f, 0.99f, 0.18f}));
            dl->AddCircleFilled({pillPos.x + 16.0f, pillPos.y + pillH * 0.5f}, 4.0f, U32(Accent()));
            ImGui::SetCursorScreenPos({pillPos.x + 28.0f, pillPos.y + 6.0f});
            PushFont(fonts.sansCompact);
            ImGui::TextColored(Accent(), "%s", statusText);
            PopFont(fonts.sansCompact);

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
            dl->AddRectFilled({trackX, trackY}, {trackX + trackW, trackY + 5.0f}, 2.5f, U32(Bg3()));
            dl->AddRectFilled({trackX, trackY}, {trackX + trackW * progress, trackY + 5.0f}, 2.5f, U32(Accent()));

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
            ImDrawList* tabsDrawList = ImGui::GetWindowDrawList();

            int conflictCount = 0;

            constexpr float tabPadX = 16.0f;
            float cursorX = 22.0f;
            PushFont(fonts.sansCompact);
            for (int i = 0; i < static_cast<int>(ImportTab::Count); ++i)
            {
                const bool active = i == s_activeTab;
                const char* label = TabLabel(i);
                const float labelW = ImGui::CalcTextSize(label).x;

                const float tabW = tabPadX * 2.0f + labelW;
                ImGui::SetCursorPos({cursorX, 0.0f});
                if (ImGui::InvisibleButton((std::string{"##ImportTab"} + std::to_string(i)).c_str(),
                                           {tabW, ImportLayout::TabsH - 2.0f}))
                    s_activeTab = i;

                const ImVec2 tabMin = ImGui::GetItemRectMin();
                const ImVec2 tabMax = ImGui::GetItemRectMax();
                const float textY = tabMin.y + (tabMax.y - tabMin.y - ImGui::GetTextLineHeight()) * 0.5f;
                tabsDrawList->AddText({tabMin.x + tabPadX, textY}, U32(active ? Text() : Dim()), label);

                if (active)
                {
                    tabsDrawList->AddRectFilled({tabMin.x, tabMax.y - 2.0f}, {tabMax.x, tabMax.y},
                                                U32(Accent()));
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
        {
            const float bodyH = ImGui::GetContentRegionAvail().y - ImportLayout::FooterH - ImportLayout::MetadataH;

            // Sidebar
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{16.0f, 16.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{8.0f, 8.0f});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg0());
            ImGui::BeginChild("Sidebar", ImVec2{ImportLayout::SidebarW, bodyH}, true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImDrawList* sidebarDrawList = ImGui::GetWindowDrawList();

            DrawLabeledDivider("SOURCE", fonts);
            ImGui::Spacing();

            const float zoneW = ImGui::GetContentRegionAvail().x;
            constexpr float zoneH = 98.0f;
            const ImVec2 zonePos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##ImportDropZone", {zoneW, zoneH});
            const bool zoneHovered = ImGui::IsItemHovered();
            sidebarDrawList->AddRectFilled(zonePos, {zonePos.x + zoneW, zonePos.y + zoneH},
                                           U32(zoneHovered ? Bg2() : Bg1()), 4.0f);
            DrawDashedRect(sidebarDrawList, zonePos, {zonePos.x + zoneW, zonePos.y + zoneH},
                           U32(zoneHovered ? Accent() : Border()));

            PushFont(fonts.sansEmphasis);
            const char* dropTitle = "Drop files here";
            const float dropTitleW = ImGui::CalcTextSize(dropTitle).x;
            sidebarDrawList->AddText({zonePos.x + (zoneW - dropTitleW) * 0.5f, zonePos.y + 31.0f},
                                     U32(Text()), dropTitle);
            PopFont(fonts.sansEmphasis);
            PushFont(fonts.sansCompact);
            const char* dropSubtitle = "or click to browse";
            const float dropSubtitleW = ImGui::CalcTextSize(dropSubtitle).x;
            sidebarDrawList->AddText({zonePos.x + (zoneW - dropSubtitleW) * 0.5f, zonePos.y + 57.0f},
                                     U32(Dim()), dropSubtitle);
            PopFont(fonts.sansCompact);

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILES"))
                {
                    std::string_view fileList(static_cast<const char*>(payload->Data), payload->DataSize);
                    std::vector<std::filesystem::path> droppedFiles;
                    std::size_t offset = 0;
                    while (offset < fileList.size())
                    {
                        auto lineEnd = fileList.find('\n', offset);
                        if (lineEnd == std::string_view::npos) lineEnd = fileList.size();
                        auto value = fileList.substr(offset, lineEnd - offset);
                        while (!value.empty() && value.back() == '\0') value.remove_suffix(1);
                        if (!value.empty()) droppedFiles.emplace_back(value);
                        offset = lineEnd + 1;
                    }
                    if (!droppedFiles.empty())
                    {
                        CancellationToken cancellation;
                        static_cast<void>(modal.BeginImport(droppedFiles, cancellation));
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::Dummy({0.0f, 12.0f});
            DrawLabeledDivider("QUEUE", fonts);
            ImGui::Spacing();

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0f, 5.0f});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg0());
            ImGui::BeginChild("QueueList", ImVec2{0.0f, 0.0f}, false);
            ImDrawList* queueDrawList = ImGui::GetWindowDrawList();

            for (std::size_t i = 0; i < snap.items.size(); ++i)
            {
                const auto& item = snap.items[i];
                bool hasError = false;
                bool hasWarning = false;
                for (const auto& diagnostic : item.diagnostics)
                {
                    hasError |= diagnostic.severity == Assets::ImportDiagnostic::Severity::Error;
                    hasWarning |= diagnostic.severity == Assets::ImportDiagnostic::Severity::Warning;
                }

                // Material Icons codepoints: error=0xE000, warning=0xE002, check_circle=0xE86C, circle=0xEF4A
                ImU32 icon = 0xEF4A; // pending: outline circle
                if (item.result.has_value())
                    icon = 0xE86C; // check_circle
                if (hasWarning)
                    icon = 0xE002; // warning
                if (hasError)
                    icon = 0xE000; // error

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
                queueDrawList->AddText({rowMin.x + 36.0f, textY},
                                       U32(Text()),
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

            switch (static_cast<ImportTab>(s_activeTab))
            {
            case ImportTab::Queue:
                {
                    if (!snap.items.empty() && snap.selectedItemIndex < snap.items.size())
                    {
                        const auto& sel = snap.items[snap.selectedItemIndex];
                        DrawLabeledDivider("SELECTED FILE", fonts);
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
                            ImGui::TextColored(Accent(), "%s",
                                               FriendlyImporterName(sel.importerContributionId).c_str());
                        else
                            ImGui::TextColored(ImVec4{0.83f, 0.32f, 0.29f, 1.0f}, "None for .%s",
                                               sel.sourceExtension.c_str());
                        PopFont(fonts.sansCompact);
                        ImGui::Dummy({0.0f, 12.0f});
                    }

                    DrawLabeledDivider("QUEUE", fonts);
                    ImGui::Dummy({0.0f, 4.0f});

                    if (snap.items.empty())
                    {
                        ImGui::TextColored(Dim(), "No files in queue. Drop files or use File > Import Assets...");
                    }
                    else
                    {
                        constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
                        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{10.0f, 9.0f});
                        if (ImGui::BeginTable("QueueTable", 4, tableFlags))
                        {
                            ImGui::TableSetupColumn("##QueueFile", ImGuiTableColumnFlags_WidthStretch, 2.3f);
                            ImGui::TableSetupColumn("##QueueType", ImGuiTableColumnFlags_WidthStretch, 0.7f);
                            ImGui::TableSetupColumn("##QueueState", ImGuiTableColumnFlags_WidthStretch, 0.7f);
                            ImGui::TableSetupColumn("##QueueProgress", ImGuiTableColumnFlags_WidthStretch, 0.5f);

                            ImGui::TableNextRow(ImGuiTableRowFlags_None, 30.0f);
                            static const char* kHeaders[] = {"FILE", "TYPE", "STATE", "PROGRESS"};
                            PushFont(fonts.sansCompact);
                            for (int column = 0; column < 4; ++column)
                            {
                                ImGui::TableSetColumnIndex(column);
                                ImGui::TextColored(Dim(), "%s", kHeaders[column]);
                            }
                            PopFont(fonts.sansCompact);

                            for (const auto& item : snap.items)
                            {
                                bool hasError = false;
                                bool hasWarning = false;
                                for (const auto& diagnostic : item.diagnostics)
                                {
                                    hasError |= diagnostic.severity == Assets::ImportDiagnostic::Severity::Error;
                                    hasWarning |= diagnostic.severity == Assets::ImportDiagnostic::Severity::Warning;
                                }

                                ImGui::TableNextRow(ImGuiTableRowFlags_None, 38.0f);
                                ImGui::TableSetColumnIndex(0);
                                const std::string fileName = DisplayFileName(std::filesystem::path{
                                    item.sourceFile.String()
                                });
                                ImGui::TextUnformatted(fileName.c_str());
                                ImGui::TableSetColumnIndex(1);
                                ImGui::TextColored(Dim(), "%s",
                                                   AssetTypeLabel(std::filesystem::path{item.sourceFile.String()}));
                                ImGui::TableSetColumnIndex(2);
                                if (hasError)
                                    ImGui::TextColored(ImVec4{0.83f, 0.32f, 0.29f, 1.0f}, "Error");
                                else if (hasWarning)
                                    ImGui::TextColored(ImVec4{0.91f, 0.64f, 0.24f, 1.0f}, "Warning");
                                else if (item.result.has_value())
                                    ImGui::TextColored(ImVec4{0.37f, 0.72f, 0.54f, 1.0f}, "Done");
                                else
                                    ImGui::TextColored(Dim(), "Pending");
                                ImGui::TableSetColumnIndex(3);
                                ImGui::TextColored(
                                    Dim(), item.result.has_value() || hasWarning || hasError ? "100%%" : "—");
                            }
                            ImGui::EndTable();
                        }
                        ImGui::PopStyleVar();
                    }
                    break;
                }
            case ImportTab::Diagnostics:
                {
                    if (snap.items.empty())
                    {
                        ImGui::TextColored(Dim(), "No diagnostics available.");
                        break;
                    }

                    struct DiagnosticRow
                    {
                        std::string assetLabel;
                        std::string message;
                        Assets::ImportDiagnostic::Severity severity;
                    };

                    std::vector<DiagnosticRow> rows;
                    rows.reserve(16);

                    for (const auto& item : snap.items)
                    {
                        const std::filesystem::path sourcePath{item.sourceFile.String()};
                        const std::string assetLabel = DisplayAssetLabel(sourcePath);

                        for (const auto& diagnostic : item.diagnostics)
                        {
                            const std::string cleanedMessage = CleanDiagnosticMessage(sourcePath, diagnostic.message);
                            if (cleanedMessage.empty()) continue;

                            const auto isDuplicate = [&](const DiagnosticRow& row)
                            {
                                return row.assetLabel == assetLabel &&
                                    row.message == cleanedMessage &&
                                    row.severity == diagnostic.severity;
                            };

                            if (std::find_if(rows.begin(), rows.end(), isDuplicate) == rows.end())
                            {
                                rows.push_back({
                                    .assetLabel = assetLabel,
                                    .message = cleanedMessage,
                                    .severity = diagnostic.severity,
                                });
                            }
                        }
                    }

                    if (rows.empty())
                    {
                        ImGui::TextColored(Dim(), "No diagnostics available.");
                        break;
                    }

                    constexpr float rowHeight = 48.0f;
                    constexpr float leftPadding = 18.0f;
                    constexpr float rightPadding = 14.0f;
                    constexpr float timeColumnWidth = 92.0f;
                    constexpr float gapAfterTime = 34.0f;
                    constexpr float gapAfterAsset = 34.0f;

                    ImDrawList* diagnosticsDrawList = ImGui::GetWindowDrawList();
                    PushFont(fonts.sansCompact);

                    const float availableWidth = ImGui::GetContentRegionAvail().x;
                    const float assetColumnWidth = std::clamp(availableWidth * 0.23f, 170.0f, 220.0f);

                    for (std::size_t diagnosticIndex = 0; diagnosticIndex < rows.size(); ++diagnosticIndex)
                    {
                        const auto& row = rows[diagnosticIndex];

                        ImVec4 messageColor = ImVec4{0.72f, 0.71f, 0.68f, 1.0f};
                        if (row.severity == Assets::ImportDiagnostic::Severity::Error)
                            messageColor = ImVec4{0.83f, 0.32f, 0.29f, 1.0f};
                        else if (row.severity == Assets::ImportDiagnostic::Severity::Warning)
                            messageColor = ImVec4{0.91f, 0.64f, 0.24f, 1.0f};

                        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                        const float rowWidth = ImGui::GetContentRegionAvail().x;
                        ImGui::InvisibleButton(
                            (std::string{"##DiagnosticRow"} + std::to_string(diagnosticIndex)).c_str(),
                            {rowWidth, rowHeight});
                        const ImVec2 rowMax{rowMin.x + rowWidth, rowMin.y + rowHeight};

                        const float textY = rowMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
                        const float timeX = rowMin.x + leftPadding;
                        const float assetX = timeX + timeColumnWidth + gapAfterTime;
                        const float messageX = assetX + assetColumnWidth + gapAfterAsset;

                        diagnosticsDrawList->PushClipRect({timeX, rowMin.y},
                                                          {timeX + timeColumnWidth, rowMax.y}, true);
                        diagnosticsDrawList->AddText({timeX, textY}, U32(Dim()), DiagnosticTimeLabel(diagnosticIndex));
                        diagnosticsDrawList->PopClipRect();

                        diagnosticsDrawList->PushClipRect({assetX, rowMin.y},
                                                          {assetX + assetColumnWidth, rowMax.y}, true);
                        diagnosticsDrawList->AddText({assetX, textY}, U32(Accent()), row.assetLabel.c_str());
                        diagnosticsDrawList->PopClipRect();

                        diagnosticsDrawList->PushClipRect({messageX, rowMin.y},
                                                          {rowMax.x - rightPadding, rowMax.y}, true);
                        diagnosticsDrawList->AddText({messageX, textY}, U32(messageColor), row.message.c_str());
                        diagnosticsDrawList->PopClipRect();
                    }

                    PopFont(fonts.sansCompact);
                    break;
                }
            case ImportTab::Settings:
                {
                    const bool hasSelection = !snap.items.empty() && snap.selectedItemIndex < snap.items.size();
                    if (!hasSelection)
                    {
                        ImGui::TextColored(Dim(), "Select a file from the queue to view its importer settings.");
                        break;
                    }

                    const auto& sel = snap.items[snap.selectedItemIndex];

                    // Show importer info
                    DrawLabeledDivider("IMPORTER", fonts);
                    ImGui::Dummy({0.0f, 4.0f});

                    const auto* contrib = modal.Catalog().FindContributionByExtension(sel.sourceExtension);
                    if (!contrib)
                    {
                        PushFont(fonts.sansCompact);
                        ImGui::TextColored(ImVec4{0.83f, 0.32f, 0.29f, 1.0f},
                                           "No importer available for .%s files.", sel.sourceExtension.c_str());
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
                    ImGui::TextColored(Accent(), "%s",
                                       FriendlyImporterName(contrib->contributionId).c_str());
                    if (contrib->builtIn)
                    {
                        ImGui::SameLine(0, 10);
                        ImGui::TextColored(ImVec4{Accent().x, Accent().y, Accent().z, 0.7f}, "BUILT-IN");
                    }
                    PopFont(fonts.sansCompact);

                    ImGui::Dummy({0.0f, 8.0f});

                    // Dynamic settings based on declarative schema
                    if (contrib->settings.empty())
                    {
                        ImGui::TextColored(Dim(), "This importer has no configurable settings.");
                    }
                    else
                    {
                        DrawLabeledDivider("SETTINGS", fonts);
                        ImGui::Dummy({0.0f, 4.0f});

                        for (const auto& setting : contrib->settings)
                        {
                            PushFont(fonts.sansCompact);
                            FieldLabelImport(setting.labelKey.c_str(), fonts);

                            // Use the item's persistent settings map, keyed by settingId.
                            auto& settingsMap = const_cast<Assets::AssetImportItem&>(sel).settings;
                            const std::string key = "settings." + setting.id;

                            switch (setting.kind)
                            {
                            case Assets::ImportSettingKind::Boolean:
                                {
                                    bool val = settingsMap.contains(key)
                                                   ? (settingsMap[key] == "true")
                                                   : (std::holds_alternative<bool>(setting.defaultValue)
                                                          ? std::get<bool>(setting.defaultValue)
                                                          : false);
                                    if (Ui::ToggleControl(("##Setting_" + setting.id).c_str(), &val, fonts, true))
                                        settingsMap[key] = val ? "true" : "false";
                                    break;
                                }
                            case Assets::ImportSettingKind::Choice:
                                {
                                    std::vector<const char*> labels;
                                    for (const auto& c : setting.choices)
                                        labels.push_back(c.labelKey.c_str());
                                    int current = settingsMap.contains(key)
                                                      ? std::stoi(settingsMap[key])
                                                      : 0;
                                    if (Ui::ComboControl(("##Setting_" + setting.id).c_str(), &current,
                                                         labels.data(), static_cast<int>(labels.size()), fonts))
                                        settingsMap[key] = std::to_string(current);
                                    break;
                                }
                            case Assets::ImportSettingKind::Integer:
                                {
                                    int val = settingsMap.contains(key)
                                                  ? std::stoi(settingsMap[key])
                                                  : (std::holds_alternative<std::int64_t>(setting.defaultValue)
                                                         ? static_cast<int>(std::get<
                                                             std::int64_t>(setting.defaultValue))
                                                         : 0);
                                    Ui::InputIntControl(("##Setting_" + setting.id).c_str(), &val, fonts);
                                    settingsMap[key] = std::to_string(val);
                                    break;
                                }
                            case Assets::ImportSettingKind::Float:
                                {
                                    float val = settingsMap.contains(key)
                                                    ? std::stof(settingsMap[key])
                                                    : (std::holds_alternative<double>(setting.defaultValue)
                                                           ? static_cast<float>(std::get<double>(setting.defaultValue))
                                                           : 0.0f);
                                    Ui::InputFloatControl(("##Setting_" + setting.id).c_str(), &val, fonts);
                                    settingsMap[key] = std::to_string(val);
                                    break;
                                }
                            case Assets::ImportSettingKind::Text:
                                {
                                    static char buf[256];
                                    if (!settingsMap.contains(key))
                                        settingsMap[key] = "";
                                    // Copy current value into buffer for editing
                                    std::strncpy(buf, settingsMap[key].c_str(), sizeof(buf) - 1);
                                    buf[sizeof(buf) - 1] = '\0';
                                    if (Ui::InputTextControl(("##Setting_" + setting.id).c_str(), buf, sizeof(buf),
                                                             fonts))
                                        settingsMap[key] = buf;
                                    break;
                                }
                            }
                            PopFont(fonts.sansCompact);
                        }
                    }

                    ImGui::Dummy({0.0f, 4.0f});
                    if (sel.result.has_value())
                    {
                        ImGui::TextColored(ImVec4{0.37f, 0.72f, 0.54f, 1.0f}, "✓ This file has been imported.");
                    }
                    else
                    {
                        ImGui::TextColored(Dim(), "Configure settings above, then click Import in the footer.");
                    }
                    break;
                }
            case ImportTab::Destination:
                {
                    static char s_targetFolder[256] = "assets/Props/";
                    static int s_namingConvention = 0;
                    static int s_subfolderByType = 0;
                    static int s_assetIdStrategy = 0;
                    static bool s_createMetaSidecar = true;
                    static bool s_overwriteWithoutPrompt = false;

                    static const char* kNamingModes[] = {
                        "Preserve source name", "Lowercase + underscore", "AssetId prefix"
                    };
                    static const char* kSubfolderModes[] = {
                        "Meshes / Textures / Audio", "Mirror source structure", "Flat"
                    };
                    static const char* kAssetIdModes[] = {"New GUID", "Stable hash"};

                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0f, 7.0f});

                    FieldLabelImport("TARGET FOLDER", fonts);
                    Ui::InputTextControl("##TargetFolder", s_targetFolder, IM_ARRAYSIZE(s_targetFolder), fonts);
                    ImGui::Dummy({0.0f, 6.0f});

                    FieldLabelImport("NAMING CONVENTION", fonts);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImportCombo("##NamingConvention", &s_namingConvention, kNamingModes,
                                IM_ARRAYSIZE(kNamingModes));
                    ImGui::Dummy({0.0f, 6.0f});

                    {
                        const SplitColumns columns = CurrentSplitColumns();

                        ImGui::BeginGroup();
                        FieldLabelImport("SUBFOLDER BY TYPE", fonts);
                        ImGui::SetNextItemWidth(columns.width);
                        ImportCombo("##SubfolderByType", &s_subfolderByType, kSubfolderModes,
                                    IM_ARRAYSIZE(kSubfolderModes));
                        ImGui::EndGroup();

                        MoveToSecondColumn(columns);
                        ImGui::BeginGroup();
                        FieldLabelImport("ASSETID STRATEGY", fonts);
                        ImGui::SetNextItemWidth(columns.width);
                        ImportCombo("##AssetIdStrategy", &s_assetIdStrategy, kAssetIdModes,
                                    IM_ARRAYSIZE(kAssetIdModes));
                        ImGui::EndGroup();

                        FinishSplitColumns(columns);
                    }

                    ImGui::Dummy({0.0f, 12.0f});
                    CompactCheckbox("Create .meta sidecar for each asset", &s_createMetaSidecar);
                    ImGui::Dummy({0.0f, 5.0f});
                    CompactCheckbox("Overwrite existing assets without prompt", &s_overwriteWithoutPrompt);

                    ImGui::PopStyleVar();
                    break;
                }
            default: break;
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }

        // ════════════════════════════════════════════════
        // FOOTER
        // ════════════════════════════════════════════════
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{28.0f, 16.0f});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg0());
            ImGui::BeginChild("ImportFooter", ImVec2{0.0f, ImportLayout::FooterH}, true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            constexpr float actionH = 36.0f;
            ImGui::SetCursorPosY((ImportLayout::FooterH - actionH) * 0.5f);
            PushFont(fonts.sansCompact);
            ImGui::AlignTextToFramePadding();
            std::string statusText = std::to_string(snap.items.size()) + " file(s)";
            std::size_t imported = 0;
            for (const auto& it : snap.items) if (it.result.has_value()) ++imported;
            if (imported > 0) statusText += " — " + std::to_string(imported) + " imported";
            ImGui::TextColored(Dim(), "%s", statusText.c_str());
            PopFont(fonts.sansCompact);

            constexpr float cancelW = 100.0f;
            constexpr float importW = 100.0f;
            constexpr float gap = 12.0f;
            const float actionsW = cancelW + importW + gap;
            ImGui::SameLine(ImGui::GetWindowWidth() - actionsW - 28.0f);
            ImGui::SetCursorPosY((ImportLayout::FooterH - actionH) * 0.5f);

            ButtonProps cancelProps{
                .label = "Cancel", .variant = ButtonVariant::Secondary, .enabled = true
            };

            const bool hasSelected = !snap.items.empty() && snap.selectedItemIndex < snap.items.size();
            const auto* selItem = hasSelected ? &snap.items[snap.selectedItemIndex] : nullptr;
            const bool canImport = selItem && !selItem->result.has_value() && !selItem->importerContributionId.empty();
            ButtonProps importProps{
                .label = "Import", .variant = ButtonVariant::Primary, .componentSize = ButtonSize::Medium,
                .enabled = canImport
            };

            if (FixedWidthButton("##ImportCancel", cancelW, cancelProps))
                frameResult = ModalFrameResult::RequestClose(ModalCloseReason::Cancelled);
            ImGui::SameLine(0.0f, gap);
            if (FixedWidthButton("##ImportSelected", importW, importProps))
            {
                CancellationToken cancellation;
                static_cast<void>(modal.ImportSingleItem(snap.selectedItemIndex, cancellation));
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        // ════════════════════════════════════════════════
        // EMBEDDED METADATA STRIP
        // ════════════════════════════════════════════════
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg1());
            ImGui::BeginChild("ImportMetadata", ImVec2{0.0f, ImportLayout::MetadataH}, true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            const char* metadataText = "Metadata: embedded · Schema: v7 · Parallel: 4 workers";
            PushFont(fonts.sansCompact);
            const ImVec2 textSize = ImGui::CalcTextSize(metadataText);
            ImGui::SetCursorPos({
                std::max(14.0f, (ImGui::GetWindowWidth() - textSize.x) * 0.5f),
                (ImportLayout::MetadataH - textSize.y) * 0.5f
            });
            ImGui::TextColored(Dim(), "%s", metadataText);
            PopFont(fonts.sansCompact);

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);

        return frameResult;
    }
} // namespace Horo::Editor
