#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/IWorkspacePanel.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserPane.h"
#include "editor/screens/workspace/panels/global_dock/panes/audio/GlobalDockAudioPane.h"
#include "editor/screens/workspace/panels/global_dock/panes/build_output/GlobalDockBuildOutputPane.h"
#include "editor/screens/workspace/panels/global_dock/panes/console/GlobalDockConsolePane.h"
#include "editor/screens/workspace/panels/global_dock/panes/localization/GlobalDockLocalizationPane.h"
#include "editor/screens/workspace/panels/global_dock/panes/mcp/GlobalDockMcpPane.h"
#include "editor/screens/workspace/panels/global_dock/panes/network/GlobalDockNetworkPane.h"
#include "editor/screens/workspace/panels/global_dock/panes/operations/GlobalDockOperationsPane.h"
#include "editor/screens/workspace/panels/global_dock/panes/performance/GlobalDockPerformancePane.h"
#include "editor/screens/workspace/panels/global_dock/panes/physics/GlobalDockPhysicsPane.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Horo::Editor {
    /** @brief Canonical tabs hosted by the embedded global dock surface. */
    enum class GlobalDockTab : std::uint8_t {
        Assets,
        Console,
        BuildOutput,
        Operations,
        Mcp,
        Performance,
        Physics,
        Audio,
        Network,
        Localization,
    };

    inline constexpr std::array kDefaultGlobalDockTabs{
        GlobalDockTab::Assets,  GlobalDockTab::Console,      GlobalDockTab::BuildOutput, GlobalDockTab::Operations,
        GlobalDockTab::Mcp,     GlobalDockTab::Performance,  GlobalDockTab::Physics,     GlobalDockTab::Audio,
        GlobalDockTab::Network, GlobalDockTab::Localization,
    };

    /** @brief Returns the stable default global-dock tab order. */
    [[nodiscard]] constexpr const std::array<GlobalDockTab, 10> &DefaultGlobalDockTabs() noexcept {
        return kDefaultGlobalDockTabs;
    }

    class GlobalDockPanel final : public IWorkspacePanel {
    public:
        /** @brief Creates the global dock with an optional restored active tab. */
        explicit GlobalDockPanel(GlobalDockTab activeTab = GlobalDockTab::Assets) noexcept : activeTab_(activeTab) {}

        [[nodiscard]] std::string GetId() const override {
            return "horo.global_dock";
        }

        [[nodiscard]] std::string GetDisplayName() const override {
            return "horo.panel.global_dock.title";
        }

        [[nodiscard]] WorkspaceDockArea GetDefaultDockArea() const override {
            return WorkspaceDockArea::Bottom;
        }

        [[nodiscard]] std::vector<std::string> GetObservedEventTypes() const override {
            return {};
        }

        void OnAttach(PanelContext &ctx) override;

        void OnDetach() override;

        void DrawIcon(ImDrawList *dl, const ImVec2 &pos, const ImVec2 &size, ImU32 color) override;

        void DrawPanel(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &vm, EditorWorkspaceViewCommandData &cmd,
                       const EditorGuiContext &ctx) override;

        /** @brief Returns the currently selected embedded global-dock tab. */
        [[nodiscard]] GlobalDockTab ActiveTab() const noexcept {
            return activeTab_;
        }

    private:
        GlobalDockTab activeTab_{GlobalDockTab::Assets};
        [[no_unique_address]] AssetBrowserPane assetsPane_;
        [[no_unique_address]] GlobalDockConsolePane consolePane_;
        [[no_unique_address]] GlobalDockBuildOutputPane buildOutputPane_;
        [[no_unique_address]] GlobalDockOperationsPane operationsPane_;
        [[no_unique_address]] GlobalDockMcpPane mcpPane_;
        [[no_unique_address]] GlobalDockPerformancePane performancePane_;
        [[no_unique_address]] GlobalDockPhysicsPane physicsPane_;
        [[no_unique_address]] GlobalDockAudioPane audioPane_;
        [[no_unique_address]] GlobalDockNetworkPane networkPane_;
        [[no_unique_address]] GlobalDockLocalizationPane localizationPane_;
    };
}  // namespace Horo::Editor
