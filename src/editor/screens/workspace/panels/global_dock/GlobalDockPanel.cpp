#include "editor/screens/workspace/panels/global_dock/GlobalDockPanel.h"

#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"

#include <algorithm>
#include <array>
#include <ranges>

namespace Horo::Editor {
    namespace {
        constexpr float TabHeight = 28.0F;
        constexpr float OuterPaddingX = 10.0F;
    }  // namespace

    /** @copydoc GlobalDockPanel::DrawIcon */
    void GlobalDockPanel::DrawIcon(ImDrawList *drawList, const ImVec2 &position, const ImVec2 &size, const ImU32 color) {
        const float x = position.x + (size.x - 14.0F) * 0.5F;
        const float y = position.y + (size.y - 14.0F) * 0.5F;
        drawList->AddLine({x + 2.0F, y + 4.0F}, {x + 5.0F, y + 4.0F}, color, 1.5F);
        drawList->AddLine({x + 5.0F, y + 4.0F}, {x + 6.0F, y + 6.0F}, color, 1.5F);
        drawList->AddLine({x + 6.0F, y + 6.0F}, {x + 12.0F, y + 6.0F}, color, 1.5F);
        drawList->AddLine({x + 12.0F, y + 6.0F}, {x + 12.0F, y + 11.0F}, color, 1.5F);
        drawList->AddLine({x + 12.0F, y + 11.0F}, {x + 2.0F, y + 11.0F}, color, 1.5F);
        drawList->AddLine({x + 2.0F, y + 11.0F}, {x + 2.0F, y + 4.0F}, color, 1.5F);
    }

    /** @copydoc GlobalDockPanel::DrawPanel */
    void GlobalDockPanel::DrawPanel(const ImVec2 &position, const ImVec2 &size, const EditorWorkspaceViewModel &viewModel,
                                    EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
        static_cast<void>(position);
        const std::array tabNames{
            context.localization.Get("editor", "workspace.global_dock.tab.assets").c_str(),
            context.localization.Get("editor", "workspace.global_dock.tab.console").c_str(),
            context.localization.Get("editor", "workspace.global_dock.tab.mcp").c_str(),
            context.localization.Get("editor", "workspace.global_dock.tab.performance").c_str(),
            context.localization.Get("editor", "workspace.global_dock.tab.physics").c_str(),
            context.localization.Get("editor", "workspace.global_dock.tab.audio").c_str(),
            context.localization.Get("editor", "workspace.global_dock.tab.network").c_str(),
            context.localization.Get("editor", "workspace.global_dock.tab.localization").c_str(),
        };
        const auto &tabOrder = DefaultGlobalDockTabs();
        const auto active = std::ranges::find(tabOrder, activeTab_);
        const int activeIndex = active == tabOrder.end() ? 0 : static_cast<int>(active - tabOrder.begin());
        const int selectedIndex = Ui::DrawDockTabs(tabNames, activeIndex, context.theme.fonts);
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(tabOrder.size())) {
            activeTab_ = tabOrder[static_cast<std::size_t>(selectedIndex)];
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
        ImGui::BeginChild("##Content", {size.x, std::max(1.0F, size.y - TabHeight)}, false, ImGuiWindowFlags_NoSavedSettings);
        const ImVec2 contentOrigin = ImGui::GetCursorScreenPos();
        const float contentWidth = std::max(1.0F, size.x - OuterPaddingX * 2.0F);

        switch (activeTab_) {
            case GlobalDockTab::Assets:
                assetsPane_.Draw(contentOrigin, contentWidth, viewModel, command, context);
                break;
            case GlobalDockTab::Console:
                consolePane_.Draw(contentOrigin, contentWidth, context);
                break;
            case GlobalDockTab::Mcp:
                mcpPane_.Draw(contentOrigin, contentWidth, context);
                break;
            case GlobalDockTab::Performance:
                performancePane_.Draw(contentOrigin, contentWidth, context);
                break;
            case GlobalDockTab::Physics:
                physicsPane_.Draw(contentOrigin, contentWidth, context);
                break;
            case GlobalDockTab::Audio:
                audioPane_.Draw(contentOrigin, contentWidth, context);
                break;
            case GlobalDockTab::Network:
                networkPane_.Draw(contentOrigin, contentWidth, context);
                break;
            case GlobalDockTab::Localization:
                localizationPane_.Draw(contentOrigin, contentWidth, context);
                break;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    /** @copydoc GlobalDockPanel::OnAttach */
    void GlobalDockPanel::OnAttach(PanelContext &context) {
        assetsPane_.Attach(context.guiRenderer);
        consolePane_.Attach(context.logQuery);
    }

    /** @copydoc GlobalDockPanel::OnDetach */
    void GlobalDockPanel::OnDetach() {
        assetsPane_.Detach();
        consolePane_.Detach();
    }
}  // namespace Horo::Editor
