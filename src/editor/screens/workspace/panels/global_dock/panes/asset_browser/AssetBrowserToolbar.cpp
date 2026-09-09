#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserToolbar.h"

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorIcons.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserInteractionSession.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserPaneLayout.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <ranges>
#include <string>
#include <vector>

namespace Horo::Editor {
    namespace {
        [[nodiscard]] float Scale() noexcept {
            return Theme::GetActiveTokens().sizes.uiScale;
        }

        [[nodiscard]] ImVec4 ControlSurface() noexcept {
            return Theme::BottomDockControlSurface();
        }

        [[nodiscard]] ImFont *ResolveFont(ImFont *preferred) {
            return preferred != nullptr ? preferred : ImGui::GetFont();
        }

        [[nodiscard]] bool DrawToolbarButton(const ImVec2 position, const char *id, const Ui::UiIcon icon, const bool enabled,
                                             const char *tooltip, ImFont *iconFont) {
            const float scale = Scale();
            const float size = AssetBrowserLayout::ToolbarControlHeight * scale;
            ImGui::SetCursorScreenPos(position);
            ImGui::PushID(id);
            ImGui::BeginDisabled(!enabled);
            const bool clicked = ImGui::InvisibleButton("##button", {size, size});
            const bool hovered = enabled && ImGui::IsItemHovered();
            const bool active = enabled && ImGui::IsItemActive();
            ImGui::EndDisabled();

            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const ImVec2 maximum{position.x + size, position.y + size};
            drawList->AddRectFilled(position, maximum, Theme::U32(hovered || active ? Theme::Hover() : ControlSurface()), 4.0F * scale);
            drawList->AddRect(position, maximum, Theme::U32(hovered ? Theme::BorderStrong() : Theme::Border()), 4.0F * scale);
            ImVec4 iconColor = enabled ? (hovered ? Theme::Text() : Theme::Muted()) : Theme::Muted();
            if (!enabled)
                iconColor.w *= 0.35F;
            const float iconSize = 16.0F * scale;
            Ui::DrawEditorIcon(drawList, icon, {position.x + (size - iconSize) * 0.5F, position.y + (size - iconSize) * 0.5F},
                               {iconSize, iconSize}, Theme::U32(iconColor), iconFont);
            if (hovered && tooltip != nullptr && tooltip[0] != '\0')
                Ui::ShowTooltip(tooltip);
            ImGui::PopID();
            return enabled && clicked;
        }

        [[nodiscard]] float DrawBreadcrumb(const ImVec2 position, const ContentBrowserDirectory &directory,
                                           EditorWorkspaceViewCommandData &command, ImFont *font, const float maximumWidth) {
            if (directory.breadcrumbs.empty() || maximumWidth <= 0.0F)
                return 0.0F;

            const float scale = Scale();
            const float fontSize = GlobalDockLabelFontSize();
            const float slashWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0F, "/").x;
            const float gap = 6.0F * scale;
            const float minimumWidth = 70.0F * scale;
            float x = position.x;
            const float right = position.x + maximumWidth;
            for (std::size_t index = 0; index < directory.breadcrumbs.size(); ++index) {
                const ContentBrowserBreadcrumb &segment = directory.breadcrumbs[index];
                const float labelWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0F, segment.label.c_str()).x;
                if (x + labelWidth > right)
                    break;
                ImGui::SetCursorScreenPos({x, position.y});
                ImGui::PushID(segment.absolutePath.c_str());
                if (ImGui::InvisibleButton("##crumb", {labelWidth, AssetBrowserLayout::ToolbarControlHeight * scale}))
                    command = AssetBrowserInteractionSession::Navigate(segment.absolutePath);
                const bool hovered = ImGui::IsItemHovered();
                ImGui::GetWindowDrawList()->AddText(font, fontSize,
                                                    {x, position.y + (AssetBrowserLayout::ToolbarControlHeight * scale - fontSize) * 0.5F},
                                                    Theme::U32(hovered ? Theme::Text() : Theme::Muted()), segment.label.c_str());
                ImGui::PopID();
                x += labelWidth;
                if (index + 1 < directory.breadcrumbs.size() && x + gap + slashWidth <= right) {
                    x += gap;
                    ImGui::GetWindowDrawList()->AddText(font, fontSize,
                                                        {x,
                                                         position.y + (AssetBrowserLayout::ToolbarControlHeight * scale - fontSize) * 0.5F},
                                                        Theme::U32(Theme::Dim()), "/");
                    x += slashWidth + gap;
                }
            }
            return std::max(minimumWidth, x - position.x);
        }

        void DrawSearch(const ImVec2 position, const float width, AssetBrowserInteractionState &state, const EditorGuiContext &context,
                        const bool showShortcut) {
            const float scale = Scale();
            const float height = AssetBrowserLayout::ToolbarControlHeight * scale;
            const float fontSize = GlobalDockLabelFontSize();
            if ((ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper) && ImGui::IsKeyPressed(ImGuiKey_K))
                ImGui::SetKeyboardFocusHere();
            ImGui::SetCursorScreenPos(position);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {32.0F * scale, (height - fontSize) * 0.5F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0F * scale);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ControlSurface());
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::Hover());
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ControlSurface());
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, Theme::Dim());
            ImGui::PushItemWidth(width);
            {
                Theme::ScopedTextStyle textStyle{context.theme.fonts.sans, fontSize, Theme::FontPx::Sans};
                static_cast<void>(ImGui::InputTextWithHint("##ContentBrowserSearch",
                                                           context.localization.Get("editor", "workspace.content_browser.search").c_str(),
                                                           state.search.data(), state.search.size()));
            }
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(6);
            ImGui::PopStyleVar(3);

            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const float iconSize = 16.0F * scale;
            Ui::DrawEditorIcon(drawList, Ui::UiIcon::Search, {position.x + 7.0F * scale, position.y + (height - iconSize) * 0.5F},
                               {iconSize, iconSize}, Theme::U32(Theme::Dim()), context.theme.fonts.icon);
            if (showShortcut) {
                ImFont *font = ResolveFont(context.theme.fonts.sansCompact);
                constexpr const char *shortcut = "⌘K";
                const float shortcutFontSize = AssetBrowserLayout::SecondaryFontSize();
                const ImVec2 textSize = font->CalcTextSizeA(shortcutFontSize, FLT_MAX, 0.0F, shortcut);
                const ImVec2 badgeMin{position.x + width - textSize.x - 17.0F * scale, position.y + 6.0F * scale};
                const ImVec2 badgeMax{position.x + width - 7.0F * scale, position.y + height - 6.0F * scale};
                drawList->AddRect(badgeMin, badgeMax, Theme::U32(Theme::Border()), 4.0F * scale);
                drawList->AddText(font, shortcutFontSize, {badgeMin.x + 5.0F * scale, badgeMin.y + 1.0F * scale}, Theme::U32(Theme::Dim()),
                                  shortcut);
            }
        }

        void DrawViewSwitch(const ImVec2 position, AssetBrowserInteractionState &state, const EditorGuiContext &context) {
            const float scale = Scale();
            const ImVec2 size{66.0F * scale, AssetBrowserLayout::ToolbarControlHeight * scale};
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(position, {position.x + size.x, position.y + size.y}, Theme::U32(ControlSurface()), 4.0F * scale);
            for (int index = 0; index < 2; ++index) {
                const ImVec2 buttonMin{position.x + (1.0F + 32.0F * static_cast<float>(index)) * scale, position.y + 1.0F * scale};
                ImGui::SetCursorScreenPos(buttonMin);
                ImGui::PushID(index);
                if (ImGui::InvisibleButton("##view", {32.0F * scale, 28.0F * scale}))
                    state.viewMode = index == 0 ? AssetBrowserViewMode::Grid : AssetBrowserViewMode::List;
                const bool active = (index == 0) == (state.viewMode == AssetBrowserViewMode::Grid);
                const bool hovered = ImGui::IsItemHovered();
                if (active || hovered) {
                    drawList->AddRectFilled(buttonMin, {buttonMin.x + 32.0F * scale, buttonMin.y + 28.0F * scale},
                                            Theme::U32(active ? Theme::Hover() : Theme::AccentSoft()));
                }
                const ImU32 color = Theme::U32(active || hovered ? Theme::Text() : Theme::Dim());
                const float iconSize = 16.0F * scale;
                Ui::DrawEditorIcon(drawList, index == 0 ? Ui::UiIcon::GridView : Ui::UiIcon::ViewList,
                                   {buttonMin.x + (32.0F * scale - iconSize) * 0.5F, buttonMin.y + (28.0F * scale - iconSize) * 0.5F},
                                   {iconSize, iconSize}, color, context.theme.fonts.icon);
                if (hovered) {
                    Ui::ShowTooltip(context.localization
                                        .Get("editor",
                                             index == 0 ? "workspace.content_browser.view.grid" : "workspace.content_browser.view.list")
                                        .c_str(),
                                    &context.theme.fonts);
                }
                ImGui::PopID();
            }
            drawList->AddLine({position.x + 33.0F * scale, position.y}, {position.x + 33.0F * scale, position.y + size.y},
                              Theme::U32(Theme::Border()));
            drawList->AddRect(position, {position.x + size.x, position.y + size.y}, Theme::U32(Theme::Border()), 4.0F * scale);
        }

        [[nodiscard]] bool DrawImportButton(const ImVec2 position, const bool compact, const EditorGuiContext &context) {
            const float scale = Scale();
            const float width = (compact ? 34.0F : 84.0F) * scale;
            const float height = AssetBrowserLayout::ToolbarControlHeight * scale;
            ImGui::SetCursorScreenPos(position);
            const bool clicked = ImGui::InvisibleButton("##ContentBrowserImport", {width, height});
            const bool hovered = ImGui::IsItemHovered();
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const ImU32 top = Theme::U32(hovered ? Theme::AccentHover() : Theme::Accent());
            const ImU32 bottom = Theme::U32(hovered ? Theme::Accent() : Theme::AccentActive());
            drawList->AddRectFilledMultiColor(position, {position.x + width, position.y + height}, top, top, bottom, bottom);
            drawList->AddRect(position, {position.x + width, position.y + height}, Theme::U32(Theme::AccentActive()), 4.0F * scale);
            const float iconSize = 16.0F * scale;
            const float iconCenterX = position.x + (compact ? width * 0.5F : 15.0F * scale);
            Ui::DrawEditorIcon(drawList, Ui::UiIcon::Create, {iconCenterX - iconSize * 0.5F, position.y + (height - iconSize) * 0.5F},
                               {iconSize, iconSize}, Theme::U32(Theme::DarkText()), context.theme.fonts.icon);
            if (!compact) {
                ImFont *font = ResolveFont(context.theme.fonts.sansCompact);
                const std::string &label = context.localization.Get("editor", "workspace.content_browser.action.import");
                const float fontSize = GlobalDockLabelFontSize();
                drawList->AddText(font, fontSize, {position.x + 29.0F * scale, position.y + (height - fontSize) * 0.5F},
                                  Theme::U32(Theme::DarkText()), label.c_str());
            }
            return clicked;
        }
    }  // namespace

    /** @copydoc DrawAssetBrowserToolbar */
    void DrawAssetBrowserToolbar(const ImVec2 &position, const float availableWidth, const EditorWorkspaceViewModel &viewModel,
                                 EditorWorkspaceViewCommandData &command, AssetBrowserInteractionState &state,
                                 const EditorGuiContext &context) {
        const float scale = Scale();
        const float gap = AssetBrowserLayout::ToolbarGap * scale;
        const float control = AssetBrowserLayout::ToolbarControlHeight * scale;
        const float safeWidth = std::max(1.0F, availableWidth);
        const bool compact = safeWidth < 640.0F * scale;
        const bool narrow = safeWidth < 900.0F * scale;
        float x = position.x;

        const std::string &back = context.localization.Get("editor", "workspace.content_browser.navigation.back");
        const std::string &forward = context.localization.Get("editor", "workspace.content_browser.navigation.forward");
        const std::string &up = context.localization.Get("editor", "workspace.content_browser.navigation.up");
        if (DrawToolbarButton({x, position.y}, "ContentBrowserBack", Ui::UiIcon::ArrowBack, viewModel.contentBrowserCanNavigateBack,
                              back.c_str(), context.theme.fonts.icon)) {
            command = AssetBrowserInteractionSession::Navigate(EditorWorkspaceViewCommand::NavigateContentBrowserBack);
        }
        x += control + gap;
        if (DrawToolbarButton({x, position.y}, "ContentBrowserForward", Ui::UiIcon::ArrowForward,
                              viewModel.contentBrowserCanNavigateForward, forward.c_str(), context.theme.fonts.icon)) {
            command = AssetBrowserInteractionSession::Navigate(EditorWorkspaceViewCommand::NavigateContentBrowserForward);
        }
        x += control + gap;
        if (DrawToolbarButton({x, position.y}, "ContentBrowserUp", Ui::UiIcon::ArrowUpward,
                              viewModel.contentBrowser.absoluteCurrentPath != viewModel.contentBrowser.absoluteRootPath, up.c_str(),
                              context.theme.fonts.icon)) {
            command = AssetBrowserInteractionSession::Navigate(EditorWorkspaceViewCommand::NavigateContentBrowserUp);
        }
        x += control;

        if (!narrow) {
            x += gap;
            ImGui::GetWindowDrawList()->AddLine({x, position.y + 5.0F * scale}, {x, position.y + 25.0F * scale},
                                                Theme::U32(Theme::Border()));
            x += 1.0F * scale + gap;
            const float breadcrumbWidth = DrawBreadcrumb({x, position.y}, viewModel.contentBrowser, command,
                                                         ResolveFont(context.theme.fonts.sansCompact), 220.0F * scale);
            x += breadcrumbWidth + gap;
        } else {
            x += gap;
        }

        const float typeWidth = (narrow ? 94.0F : 106.0F) * scale;
        const float sortWidth = (narrow ? 104.0F : 154.0F) * scale;
        const float viewWidth = 66.0F * scale;
        const float importWidth = (compact ? 34.0F : 84.0F) * scale;
        const float fixedWidth =
            compact ? importWidth + gap + control : typeWidth + gap + sortWidth + gap + viewWidth + gap + importWidth + gap + control;
        const float rightEdge = position.x + safeWidth;
        const float fixedX = rightEdge - fixedWidth;
        DrawSearch({x, position.y}, std::max(60.0F * scale, fixedX - x - gap), state, context, !narrow);

        float fixedCursor = fixedX;
        if (!compact) {
            std::vector<std::string> typeLabels{context.localization.Get("editor", "workspace.content_browser.filter.all_types")};
            for (const ContentBrowserEntry &entry : viewModel.contentBrowser.entries) {
                if (entry.kind == ContentBrowserEntryKind::Asset && !entry.assetType.empty() &&
                    std::ranges::find(typeLabels, entry.assetType) == typeLabels.end()) {
                    typeLabels.push_back(entry.assetType);
                }
            }
            std::ranges::sort(typeLabels.begin() + 1, typeLabels.end());
            int typeIndex = 0;
            if (!state.assetTypeFilter.empty()) {
                const auto selected = std::ranges::find(typeLabels, state.assetTypeFilter);
                if (selected == typeLabels.end())
                    state.assetTypeFilter.clear();
                else
                    typeIndex = static_cast<int>(std::distance(typeLabels.begin(), selected));
            }
            std::vector<const char *> typeItems;
            typeItems.reserve(typeLabels.size());
            for (const std::string &label : typeLabels)
                typeItems.push_back(label.c_str());
            ImGui::SetCursorScreenPos({fixedCursor, position.y});
            ImGui::SetNextItemWidth(typeWidth);
            if (Ui::ComboControl("ContentBrowserTypeFilter", &typeIndex, typeItems.data(), static_cast<int>(typeItems.size()),
                                 context.theme.fonts,
                                 Ui::ComboControlOptions{.height = AssetBrowserLayout::ToolbarControlHeight,
                                                         .componentSize = Ui::ComponentSize::Medium,
                                                         .surface = Ui::ComboControlSurface::BottomDockToolbar})) {
                state.assetTypeFilter = typeIndex == 0 ? std::string{} : typeLabels[static_cast<std::size_t>(typeIndex)];
            }
            fixedCursor += typeWidth + gap;

            const std::array sortLabels{
                context.localization.Get("editor", "workspace.content_browser.sort.name"),
                context.localization.Get("editor", "workspace.content_browser.sort.type"),
            };
            const std::array sortItems{sortLabels[0].c_str(), sortLabels[1].c_str()};
            int sortIndex = state.sortField == ContentBrowserSortField::Name ? 0 : 1;
            ImGui::SetCursorScreenPos({fixedCursor, position.y});
            ImGui::SetNextItemWidth(sortWidth);
            if (Ui::ComboControl("ContentBrowserSort", &sortIndex, sortItems.data(), static_cast<int>(sortItems.size()),
                                 context.theme.fonts,
                                 Ui::ComboControlOptions{.height = AssetBrowserLayout::ToolbarControlHeight,
                                                         .componentSize = Ui::ComponentSize::Medium,
                                                         .surface = Ui::ComboControlSurface::BottomDockToolbar})) {
                state.sortField = sortIndex == 0 ? ContentBrowserSortField::Name : ContentBrowserSortField::Type;
            }
            fixedCursor += sortWidth + gap;
            DrawViewSwitch({fixedCursor, position.y}, state, context);
            fixedCursor += viewWidth + gap;
        }

        if (DrawImportButton({fixedCursor, position.y}, compact, context))
            command = AssetBrowserInteractionSession::ImportHere(viewModel.contentBrowser.absoluteCurrentPath);
        fixedCursor += importWidth + gap;
        const std::string &newFolder = context.localization.Get("editor", "workspace.content_browser.action.create_folder");
        if (DrawToolbarButton({fixedCursor, position.y}, "ContentBrowserNewFolder", Ui::UiIcon::CreateNewFolder, true, newFolder.c_str(),
                              context.theme.fonts.icon)) {
            state.createFolderBuffer.fill('\0');
            state.openCreateFolder = true;
        }
    }
}  // namespace Horo::Editor
