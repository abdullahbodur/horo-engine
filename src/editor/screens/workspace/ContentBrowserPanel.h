#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include <imgui.h>

#include "Horo/Editor/IWorkspacePanel.h"
#include "Horo/Foundation/Logging/StructuredLogStore.h"
#include "editor/screens/workspace/ContentBrowserModel.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

        void OnAttach(PanelContext& ctx) override;

        void OnDetach() override;

        void DrawIcon(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, ImU32 color) override;

        void DrawPanel(const ImVec2& pos, const ImVec2& size, const EditorWorkspaceViewModel& vm,
                       EditorWorkspaceViewCommandData& cmd, const EditorGuiContext& ctx) override;

        /** @brief Returns the currently selected embedded global-dock tab. */
        [[nodiscard]] GlobalDockTab ActiveTab() const noexcept
        {
            return activeTab_;
        }

    private:
        [[nodiscard]] bool RefreshConsoleSnapshot();
        void RebuildConsoleFilter();
        void DrawConsolePane(const ImVec2& contentOrigin, float contentWidth,
                             const EditorGuiContext& context);

        GlobalDockTab activeTab_{GlobalDockTab::Assets};
        std::optional<ContentBrowserEntry> popupEntry_;
        std::string selectedAssetPath_;
        std::array<char, 160> assetSearch_{};
        std::string assetTypeFilter_;
        ContentBrowserSortField assetSortField_{
            ContentBrowserSortField::Name};
        ContentBrowserSortDirection assetSortDirection_{
            ContentBrowserSortDirection::Ascending};
        std::array<char, 256> renameBuffer_{};
        std::array<char, 256> createFolderBuffer_{};
        bool openAssetInfo_{false};
        bool openRename_{false};
        bool openDeleteConfirmation_{false};
        bool openCreateFolder_{false};
        IEditorGuiRenderer* guiRenderer_{nullptr};
        const Log::IStructuredLogQuery* logQuery_{nullptr};
        Log::StructuredLogSnapshot consoleSnapshot_;
        std::uint64_t consoleRevision_{};
        std::array<bool, 5> consoleLevelEnabled_{true, true, true, true, true};
        std::array<char, 160> consoleSearch_{};
        std::vector<std::size_t> consoleFilteredIndices_;
        bool consoleFilterDirty_{true};
        bool consoleInitialFollowTail_{true};
        std::unordered_map<std::string, std::pair<std::uint64_t, std::uintptr_t>> previewTextures_;
    };
} // namespace Horo::Editor
