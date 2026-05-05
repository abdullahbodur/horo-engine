#pragma once

#include <cstdint>
#include <filesystem>
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

    class EditorBottomDock {
    public:
        void Draw(Horo::Mcp::McpController* mcpController, GLFWwindow* window);

        // Accessors for tab content state
        bool IsConsoleShowInfo() const { return m_consoleShowInfo; }
        void SetConsoleShowInfo(bool show) { m_consoleShowInfo = show; }

        bool IsConsoleShowWarn() const { return m_consoleShowWarn; }
        void SetConsoleShowWarn(bool show) { m_consoleShowWarn = show; }

        bool IsConsoleShowError() const { return m_consoleShowError; }
        void SetConsoleShowError(bool show) { m_consoleShowError = show; }

        const std::filesystem::path& GetProjectBrowserRoot() const {
            return m_projectBrowserRoot;
        }
        void SetProjectBrowserRoot(const std::filesystem::path& root) {
            m_projectBrowserRoot = root;
            m_projectBrowserRootValid = !root.empty();
            InvalidateProjectBrowserCache();
        }

        const std::filesystem::path& GetProjectBrowserCwd() const {
            return m_projectBrowserCwd;
        }
        void SetProjectBrowserCwd(const std::filesystem::path& cwd) {
            m_projectBrowserCwd = cwd;
            m_projectBrowserCwdValid = true;
        }

        const std::filesystem::path& GetSavedProjectBrowserCwd() const {
            return m_savedProjectBrowserCwd;
        }
        void SetSavedProjectBrowserCwd(const std::filesystem::path& cwd) {
            m_savedProjectBrowserCwd = cwd;
        }

        void SetProjectExtraBlocklist(
            const std::unordered_set<std::string, StringHash, std::equal_to<>>& names) {
            m_projectExtraBlocklist = names;
        }

        void InvalidateProjectBrowserCache();

    private:
        // Project browser state
        std::filesystem::path m_projectBrowserRoot;
        bool m_projectBrowserRootValid = false;
        std::filesystem::path m_projectBrowserCwd;
        bool m_projectBrowserCwdValid = false;
        std::filesystem::path m_savedProjectBrowserCwd;
        std::unordered_set<std::string, StringHash, std::equal_to<>>
            m_projectExtraBlocklist;

        struct ProjectDirCache {
            std::vector<std::pair<std::filesystem::path, bool>> entries;
            uint32_t cachedAtFrame = 0;
        };

        std::unordered_map<std::string, ProjectDirCache, StringHash, std::equal_to<>>
            m_projectDirCache;

        // Console state
        bool m_consoleShowInfo = true;
        bool m_consoleShowWarn = true;
        bool m_consoleShowError = true;
        std::vector<LogLine> m_consoleLinesCache;
        std::vector<int> m_consoleVisibleScratch;
        uint64_t m_consoleLogRevision = UINT64_MAX;

        // MCP state
        int m_mcpSelectedActivityIndex = 0;
        bool m_mcpUiClearToggle = false;

        // Drawing functions
        void DrawBottomDock(Horo::Mcp::McpController* mcpController,
                            GLFWwindow* window);

        void DrawProjectBrowserTab();

        void DrawProjectBrowserBreadcrumbs(std::filesystem::path& nextCwd,
                                           bool& cwdChanged) const;

        void DrawProjectBrowserTiles(std::filesystem::path& nextCwd, bool& cwdChanged);

        void DrawConsoleTab();

        void DrawMcpTab(Horo::Mcp::McpController* mcpController,
                        GLFWwindow* window);

        void DrawMcpClientCard(const char* title, const char* pathLabel,
                               const char* pathValue, const char* hint,
                               std::string_view snippet, const char* toastLabel,
                               Horo::Mcp::McpController* mcpController,
                               class GLFWwindow* window);

        void DrawMcpTabLiveRequests(const Horo::Mcp::McpStatusSnapshot& status);

        void DrawMcpTabCatalog(const Horo::Mcp::McpStatusSnapshot& status) const;

        const std::vector<std::pair<std::filesystem::path, bool>>*
        GetProjectDirListing(const std::filesystem::path& absPath);
    };
}
