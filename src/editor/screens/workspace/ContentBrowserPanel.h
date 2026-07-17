#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include <imgui.h>

#include "Horo/Editor/IWorkspacePanel.h"

#include <array>
#include <cstdint>

namespace Horo::Editor
{
    /** @brief Canonical tabs hosted by the embedded global dock surface. */
    enum class GlobalDockTab : std::uint8_t
    {
        Assets,
        Console,
        Mcp,
        Performance,
        Physics,
        Audio,
        Network,
        Localization,
    };

    inline constexpr std::array kDefaultGlobalDockTabs{
        GlobalDockTab::Assets, GlobalDockTab::Console, GlobalDockTab::Mcp, GlobalDockTab::Performance,
        GlobalDockTab::Physics, GlobalDockTab::Audio, GlobalDockTab::Network, GlobalDockTab::Localization,
    };

    /** @brief Returns the stable default global-dock tab order. */
    [[nodiscard]] constexpr const std::array<GlobalDockTab, 8>& DefaultGlobalDockTabs() noexcept
    {
        return kDefaultGlobalDockTabs;
    }

    class ContentBrowserPanel final : public IWorkspacePanel
    {
    public:
        /** @brief Creates the global dock with an optional restored active tab. */
        explicit ContentBrowserPanel(GlobalDockTab activeTab = GlobalDockTab::Assets) noexcept : activeTab_(activeTab)
        {
        }

        [[nodiscard]] std::string GetId() const override
        {
            return "horo.content_browser";
        }

        [[nodiscard]] std::string GetDisplayName() const override
        {
            return "horo.panel.content_browser.title";
        }

        [[nodiscard]] WorkspaceDockArea GetDefaultDockArea() const override
        {
            return WorkspaceDockArea::Bottom;
        }

        [[nodiscard]] std::vector<std::string> GetObservedEventTypes() const override
        {
            return {};
        }

        void OnAttach(PanelContext& ctx) override
        {
        }

        void OnDetach() override
        {
        }

        void DrawIcon(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, ImU32 color) override;

        void DrawPanel(const ImVec2& pos, const ImVec2& size, const EditorWorkspaceViewModel& vm,
                       EditorWorkspaceViewCommandData& cmd, const EditorGuiContext& ctx) override;

        /** @brief Returns the currently selected embedded global-dock tab. */
        [[nodiscard]] GlobalDockTab ActiveTab() const noexcept
        {
            return activeTab_;
        }

    private:
        GlobalDockTab activeTab_{GlobalDockTab::Assets};
    };
} // namespace Horo::Editor
