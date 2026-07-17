#include "ContentBrowserPanel.h"
#include "ContentBrowserPanelLayout.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"

#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "Horo/Foundation/DataBus.h"

#include <imgui.h>

#include <array>
#include <cassert>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
class TestLocalization final : public Horo::Editor::ILocalizationService
{
  public:
    [[nodiscard]] const std::string &Get(const std::string_view, const std::string_view localKey) const override
    {
        std::string_view value = localKey;
        if (localKey == "workspace.global_dock.tab.assets")
            value = "Assets";
        else if (localKey == "workspace.global_dock.tab.console")
            value = "Console";
        else if (localKey == "workspace.global_dock.tab.mcp")
            value = "MCP";
        else if (localKey == "workspace.global_dock.tab.performance")
            value = "Perf";
        else if (localKey == "workspace.global_dock.tab.physics")
            value = "Physics";
        else if (localKey == "workspace.global_dock.tab.audio")
            value = "Audio";
        else if (localKey == "workspace.global_dock.tab.network")
            value = "Net";
        else if (localKey == "workspace.global_dock.tab.localization")
            value = "L10n";

        const auto [entry, inserted] = values_.try_emplace(std::string(localKey), value);
        static_cast<void>(inserted);
        return entry->second;
    }

  private:
    mutable std::unordered_map<std::string, std::string> values_;
};

void VerifyResponsiveGridMetrics()
{
    using Horo::Editor::ComputeContentBrowserGridMetrics;
    using Horo::Editor::kGlobalDockMinimumFontSize;

    static_assert(kGlobalDockMinimumFontSize == Horo::Editor::Theme::FontPx::SansCompact);

    const auto wide = ComputeContentBrowserGridMetrics(580.0F);
    assert(wide.columns == 8);
    assert(std::abs(wide.cardWidth - 67.25F) < 0.001F);

    const auto narrow = ComputeContentBrowserGridMetrics(240.0F);
    assert(narrow.columns == 3);
    assert(std::abs(narrow.cardWidth - 76.0F) < 0.001F);
}

void VerifyDefaultGlobalDockTabs()
{
    using namespace Horo::Editor;

    constexpr std::array expected{
        GlobalDockTab::Assets,  GlobalDockTab::Console, GlobalDockTab::Mcp,     GlobalDockTab::Performance,
        GlobalDockTab::Physics, GlobalDockTab::Audio,   GlobalDockTab::Network, GlobalDockTab::Localization,
    };
    assert(DefaultGlobalDockTabs() == expected);

    ContentBrowserPanel panel;
    assert(panel.ActiveTab() == GlobalDockTab::Assets);
}

void RenderAtWidth(const float width, const char *windowId, Horo::Editor::ContentBrowserPanel &panel,
                   const Horo::Editor::EditorGuiContext &context)
{
    using namespace Horo::Editor;

    EditorWorkspaceViewModel viewModel;
    EditorWorkspaceViewCommandData command;
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(width + 20.0F, 260.0F));
    ImGui::Begin(windowId, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    panel.DrawPanel(ImGui::GetCursorScreenPos(), ImVec2(width, 220.0F), viewModel, command, context);
    ImGui::End();
}

void RenderEveryGlobalDockTab(const Horo::Editor::EditorGuiContext &context)
{
    using namespace Horo::Editor;

    for (const GlobalDockTab tab : DefaultGlobalDockTabs())
    {
        ContentBrowserPanel panel{tab};
        ImGui::NewFrame();
        RenderAtWidth(900.0F, "GlobalDockTabMatrix", panel, context);
        ImGui::Render();
        assert(panel.ActiveTab() == tab);
    }
}
} // namespace

int main()
{
    using namespace Horo;
    using namespace Horo::Editor;

    VerifyResponsiveGridMetrics();
    VerifyDefaultGlobalDockTabs();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0F, 720.0F);
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());

    EngineDataBus engineEvents;
    EditorDataBus editorEvents;
    TestLocalization localization;
    ImFont *defaultFont = io.Fonts->Fonts.front();
    const Theme::Fonts fonts{.sans = defaultFont, .sansCompact = defaultFont, .sansEmphasis = defaultFont};
    const ThemeContext theme{.fonts = fonts};
    const EditorSettingsSnapshot settings{};
    const EditorGuiContext context{.engineEvents = engineEvents,
                                   .editorEvents = editorEvents,
                                   .localization = localization,
                                   .theme = theme,
                                   .settings = settings};
    ContentBrowserPanel panel;

    ImGui::NewFrame();
    RenderAtWidth(600.0F, "ContentBrowserWide", panel, context);
    ImGui::Render();

    ImGui::NewFrame();
    RenderAtWidth(260.0F, "ContentBrowserNarrow", panel, context);
    ImGui::Render();

    RenderEveryGlobalDockTab(context);

    ImGui::DestroyContext();
    return 0;
}
