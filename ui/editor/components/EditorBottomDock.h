/** @file EditorBottomDock.h
 *  @brief Bottom dock panel hosting the Assets, Console, and MCP tabs. */
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/LogBuffer.h"
#include "core/StringHash.h"
#include "mcp/McpController.h"

struct GLFWwindow;

namespace Horo::Mcp {
    class McpController;
}

namespace Horo::Editor {
    struct EditorComponentContext;

    /** @brief Callback type used by EditorBottomDock to delegate Assets-tab rendering. */
    using AssetsTabDrawCallback = std::function<void()>;

    /** @brief Draws and manages the collapsible bottom dock with Assets, Console, and MCP tabs. */
    class EditorBottomDock {
    public:
        /** @brief Selectable tab identifiers for the bottom dock. */
        enum class Tab {
            Assets,  /**< Asset browser tab content. */
            Console, /**< Runtime/editor log console tab content. */
            MCP      /**< MCP status, activity, and catalog tab content. */
        };

        /** @brief Draws the full bottom dock, including tab bar and active tab content.
         *  @param mcpController  MCP controller used to populate the MCP tab.
         *  @param window         Host GLFW window, forwarded to child panels.
         *  @param leftDockWidth  Width in pixels of the left-side dock, used for layout.
         *  @param bottomDockHeight Height in pixels allocated to the dock. */
        void Draw(Horo::Mcp::McpController* mcpController, GLFWwindow* window,
                  float leftDockWidth, float bottomDockHeight);

        /** @brief Registers the callback that renders content inside the Assets tab.
         *  @param callback Callable invoked once per frame while the Assets tab is active. */
        void SetAssetsTabCallback(AssetsTabDrawCallback callback) {
            m_assetsTabCallback = std::move(callback);
        }

        /** @brief Returns the currently selected tab.
         *  @return The active Tab value. */
        Tab GetSelectedTab() const { return m_selectedTab; }

        /** @brief Draws only the content area for the currently selected tab.
         *  @param mcpController MCP controller forwarded to the MCP tab.
         *  @param window        Host GLFW window forwarded to child tabs. */
        void DrawSelectedTabContent(Horo::Mcp::McpController* mcpController,
                                     GLFWwindow* window);

        // Accessors for tab content state
        /** @brief Returns true if Info-level messages are shown in the Console tab. */
        bool IsConsoleShowInfo() const { return m_consoleShowInfo; }

        /** @brief Sets whether Info-level messages are shown in the Console tab.
         *  @param show True to show, false to hide. */
        void SetConsoleShowInfo(bool show) { m_consoleShowInfo = show; }

        /** @brief Returns true if Warning-level messages are shown in the Console tab. */
        bool IsConsoleShowWarn() const { return m_consoleShowWarn; }

        /** @brief Sets whether Warning-level messages are shown in the Console tab.
         *  @param show True to show, false to hide. */
        void SetConsoleShowWarn(bool show) { m_consoleShowWarn = show; }

        /** @brief Returns true if Error-level messages are shown in the Console tab. */
        bool IsConsoleShowError() const { return m_consoleShowError; }

        /** @brief Sets whether Error-level messages are shown in the Console tab.
         *  @param show True to show, false to hide. */
        void SetConsoleShowError(bool show) { m_consoleShowError = show; }

        /** @brief Returns the root directory used for the project browser.
         *  @return Absolute path to the project browser root. */
        const std::filesystem::path& GetProjectBrowserRoot() const {
            return m_projectBrowserRoot;
        }

        /** @brief Sets the root directory for the project browser and invalidates cached listings.
         *  @param root Absolute path to the new project root. */
        void SetProjectBrowserRoot(const std::filesystem::path& root) {
            m_projectBrowserRoot = root;
            m_projectBrowserRootValid = !root.empty();
            InvalidateProjectBrowserCache();
        }

        /** @brief Returns the current working directory shown in the project browser.
         *  @return Absolute path to the current browser directory. */
        const std::filesystem::path& GetProjectBrowserCwd() const {
            return m_projectBrowserCwd;
        }

        /** @brief Sets the current working directory for the project browser.
         *  @param cwd Absolute path to the directory to display. */
        void SetProjectBrowserCwd(const std::filesystem::path& cwd) {
            m_projectBrowserCwd = cwd;
            m_projectBrowserCwdValid = true;
        }

        /** @brief Returns the saved (bookmark) working directory for the project browser.
         *  @return Previously saved browser directory path. */
        const std::filesystem::path& GetSavedProjectBrowserCwd() const {
            return m_savedProjectBrowserCwd;
        }

        /** @brief Saves a bookmark directory for the project browser.
         *  @param cwd Absolute path to persist as the saved browser location. */
        void SetSavedProjectBrowserCwd(const std::filesystem::path& cwd) {
            m_savedProjectBrowserCwd = cwd;
        }

        /** @brief Extends the project browser's blocklist with additional entry names to hide.
         *  @param names Set of file or directory names that should not appear in the browser. */
        void SetProjectExtraBlocklist(
            const std::unordered_set<std::string, StringHash, std::equal_to<>>& names) {
            m_projectExtraBlocklist = names;
        }

        /** @brief Discards all cached directory listings so they are re-read on the next frame. */
        void InvalidateProjectBrowserCache();

    private:
        // Project browser state
        std::filesystem::path m_projectBrowserRoot;  /**< Absolute root path of the project browser. */
        bool m_projectBrowserRootValid = false;       /**< True when m_projectBrowserRoot has been set. */
        std::filesystem::path m_projectBrowserCwd;   /**< Current working directory in the project browser. */
        bool m_projectBrowserCwdValid = false;        /**< True when m_projectBrowserCwd has been set. */
        std::filesystem::path m_savedProjectBrowserCwd; /**< Persisted bookmark directory for the project browser. */
        std::unordered_set<std::string, StringHash, std::equal_to<>>
            m_projectExtraBlocklist; /**< Additional entry names suppressed from the browser view. */

        /** @brief Cached directory listing with its frame-stamp for invalidation. */
        struct ProjectDirCache {
            std::vector<std::pair<std::filesystem::path, bool>> entries; /**< Sorted entries; bool is true for directories. */
            uint32_t cachedAtFrame = 0; /**< Frame index when this listing was last populated. */
        };

        std::unordered_map<std::string, ProjectDirCache, StringHash, std::equal_to<>>
            m_projectDirCache; /**< Per-directory listing cache keyed by absolute path string. */

        // Console state
        bool m_consoleShowInfo = true;              /**< Whether Info log lines are visible in the console. */
        bool m_consoleShowWarn = true;              /**< Whether Warning log lines are visible in the console. */
        bool m_consoleShowError = true;             /**< Whether Error log lines are visible in the console. */
        std::vector<LogLine> m_consoleLinesCache;   /**< Cached snapshot of log lines for the current frame. */
        std::vector<int> m_consoleVisibleScratch;   /**< Scratch buffer of indices into m_consoleLinesCache after filtering. */
        uint64_t m_consoleLogRevision = UINT64_MAX; /**< Revision token from the log buffer; used to detect new entries. */

        // MCP state
        int m_mcpSelectedActivityIndex = 0; /**< Index of the currently selected activity in the MCP tab. */
        bool m_mcpUiClearToggle = false;    /**< Toggle flag used to clear the MCP activity display. */

        AssetsTabDrawCallback m_assetsTabCallback; /**< Delegate invoked to render Assets-tab content. */

        Tab m_selectedTab = Tab::Assets; /**< Currently active tab in the bottom dock. */

        // Drawing functions
        /** @brief Draws the full dock chrome (tab bar + selected content).
         *  @param mcpController MCP controller forwarded to child drawers.
         *  @param window        Host GLFW window.
         *  @param leftDockWidth Width of the left-side dock for layout purposes.
         *  @param bottomDockHeight Total height allocated to this dock. */
        void DrawBottomDock(Horo::Mcp::McpController* mcpController,
                            GLFWwindow* window,
                            float leftDockWidth, float bottomDockHeight);

        /** @brief Renders the Assets tab content by invoking the registered callback. */
        void DrawAssetsTab();

        /** @brief Renders the Console tab content, applying the active log-level filters. */
        void DrawConsoleTab();

        /** @brief Renders the MCP tab content.
         *  @param mcpController MCP controller supplying status and tool catalog data.
         *  @param window        Host GLFW window, used for clipboard operations. */
        void DrawMcpTab(Horo::Mcp::McpController* mcpController,
                        GLFWwindow* window);

        /** @brief Draws a single MCP client card showing connection details and a config snippet.
         *  @param title        Display title for the client card.
         *  @param pathLabel    Label shown next to the config file path.
         *  @param pathValue    Actual config file path string.
         *  @param hint         Supplementary hint text shown below the path.
         *  @param snippet      JSON or text snippet shown in the code block.
         *  @param toastLabel   Label used in the clipboard toast when the snippet is copied.
         *  @param mcpController MCP controller used to query connection state.
         *  @param window        Host GLFW window used for clipboard writes. */
        void DrawMcpClientCard(const char* title, const char* pathLabel,
                               const char* pathValue, const char* hint,
                               std::string_view snippet, const char* toastLabel,
                               Horo::Mcp::McpController* mcpController,
                               struct GLFWwindow* window);

        /** @brief Draws the live-requests section of the MCP tab.
         *  @param status Current MCP status snapshot containing in-flight requests. */
        void DrawMcpTabLiveRequests(const Horo::Mcp::McpStatusSnapshot& status);

        /** @brief Draws the tool-catalog section of the MCP tab.
         *  @param status Current MCP status snapshot containing the registered tools. */
        void DrawMcpTabCatalog(const Horo::Mcp::McpStatusSnapshot& status) const;

        /** @brief Returns a cached directory listing for the given absolute path, refreshing if stale.
         *  @param absPath Absolute directory path to list.
         *  @return Pointer to the cached entry vector, or nullptr if the path cannot be read. */
        const std::vector<std::pair<std::filesystem::path, bool>>*
        GetProjectDirListing(const std::filesystem::path& absPath);
    };
}
