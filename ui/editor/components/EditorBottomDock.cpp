#include "ui/editor/components/EditorBottomDock.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <ranges>

// Windows headers must come before GLFW
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
// clang-format on
#endif

// clang-format off
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
// clang-format on

#include <imgui.h>

#include "core/LogBuffer.h"
#include "mcp/McpController.h"
#include "ui/editor/EditorLayerInternal.h"
#include "ui/editor/EditorUiLogic.h"
#include "ui/editor/ProjectEntryFilter.h"
#include "ui/editor/components/EditorComponentContext.h"
#include "ui/editor/components/EditorUIWidgets.h"
#include "ui/HoroTheme.h"

using namespace Horo::Editor::Internal;

namespace Horo::Editor {

namespace {
constexpr uint32_t kProjectListingCacheFrames = 48;
}

void EditorBottomDock::Draw(Horo::Mcp::McpController* mcpController, GLFWwindow* window) {
    DrawBottomDock(mcpController, window);
}

void EditorBottomDock::DrawBottomDock(Horo::Mcp::McpController* mcpController,
                                      GLFWwindow* window) {
    const ImGuiIO& io = ImGui::GetIO();
    const float bottomDockH = ComputeEditorBottomDockHeight(io.DisplaySize.y);
    const float dockTop = io.DisplaySize.y - kEditorStatusH - bottomDockH;
    ImGui::SetNextWindowPos(ImVec2(0.0f, dockTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, bottomDockH), ImGuiCond_Always);
    ImGui::Begin(kEditorWorkspaceWindow, nullptr, kMainPanelWindowFlags);

    const auto& palette = Ui::GetEditorTheme().palette;
    ImGui::PushStyleColor(ImGuiCol_TabHovered, palette.cardHover);
    ImGui::PushStyleColor(ImGuiCol_TabActive, palette.accent);
    ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, palette.accent);
    if (ImGui::BeginTabBar("##bottom_tabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Assets")) {
            DrawAssetsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Console")) {
            DrawConsoleTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("MCP")) {
            DrawMcpTab(mcpController, window);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleColor(3);

    ImGui::End();
}

void EditorBottomDock::DrawAssetsTab() {
    if (m_assetsTabCallback) {
        m_assetsTabCallback();
    }
}

void EditorBottomDock::DrawConsoleTab() {
    using enum LogLevel;
    int nInfo = 0;
    int nWarn = 0;
    int nErr = 0;
    LogBuffer::Instance().GetCounts(&nInfo, &nWarn, &nErr);
    if (ImGui::SmallButton("Clear"))
        LogBuffer::Instance().Clear();
    ImGui::SameLine();
    if (ImGui::Checkbox("Info", &m_consoleShowInfo))
        ; // Workspace state will be marked dirty by caller
    ImGui::SameLine();
    if (ImGui::Checkbox("Warn", &m_consoleShowWarn))
        ; // Workspace state will be marked dirty by caller
    ImGui::SameLine();
    if (ImGui::Checkbox("Error", &m_consoleShowError))
        ; // Workspace state will be marked dirty by caller
    ImGui::SameLine();
    ImGui::TextDisabled("I:%d W:%d E:%d", nInfo, nWarn, nErr);

    ImGui::Separator();
    ImGui::BeginChild("##console_scroll", ImVec2(0, 0), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const LogBuffer& logBuf = LogBuffer::Instance();
    const uint64_t rev = logBuf.Revision();
    if (rev != m_consoleLogRevision) {
        logBuf.CopyLinesTo(&m_consoleLinesCache);
        m_consoleLogRevision = rev;
    }

    m_consoleVisibleScratch.clear();
    for (int i = 0; i < static_cast<int>(m_consoleLinesCache.size()); ++i) {
        const LogLine& entry = m_consoleLinesCache[static_cast<size_t>(i)];
        if (entry.level == Info && !m_consoleShowInfo)
            continue;
        if (entry.level == Warn && !m_consoleShowWarn)
            continue;
        if (entry.level == Error && !m_consoleShowError)
            continue;
        m_consoleVisibleScratch.push_back(i);
    }

    for (int row = 0; row < static_cast<int>(m_consoleVisibleScratch.size()); ++row) {
        const LogLine& entry = m_consoleLinesCache[static_cast<size_t>(
            m_consoleVisibleScratch[static_cast<size_t>(row)])];
        std::string timeBuf(32, '\0');
        FormatLogTime(entry, timeBuf.data(), timeBuf.size());
        const char* levelStr = "ERR";
        if (entry.level == Info)
            levelStr = "INFO";
        else if (entry.level == Warn)
            levelStr = "WARN";
        ImVec4 color(0.85f, 0.9f, 1.0f, 1.0f);
        if (entry.level == Warn)
            color = ImVec4(1.0f, 0.85f, 0.4f, 1.0f);
        else if (entry.level == Error)
            color = ImVec4(1.0f, 0.45f, 0.4f, 1.0f);

        const std::string lineLine = std::format("[{}] {} ({}:{}) {}", timeBuf.c_str(),
                                                  levelStr, entry.file, entry.line, entry.message);

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(lineLine.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

void EditorBottomDock::DrawMcpTab(Horo::Mcp::McpController* mcpController,
                                  GLFWwindow* window) {
    if (!mcpController)
        return;

    const Mcp::McpStatusSnapshot status = mcpController->GetStatusSnapshot();

    if (m_mcpSelectedActivityIndex >= static_cast<int>(status.recentActivity.size()))
        m_mcpSelectedActivityIndex = status.recentActivity.empty()
                                         ? 0
                                         : static_cast<int>(status.recentActivity.size()) - 1;

    ImGui::Text("Status: %s", status.running ? "Running" : "Stopped");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Enabled: %s", status.enabled ? "Yes" : "No");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Endpoint: %s", status.endpointUrl.c_str());

    ImGui::Text("Requests: %llu", static_cast<unsigned long long>(status.totalRequests));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Success: %llu", static_cast<unsigned long long>(status.successCount));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Failed: %llu", static_cast<unsigned long long>(status.failureCount));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Active: %d", status.activeRequests);

    ImGui::Text("Tools: %d", static_cast<int>(status.toolCount));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Resources: %d", static_cast<int>(status.resourceCount));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Last Request: %s", status.lastRequestTime.empty() ? "-" : status.lastRequestTime.c_str());

    if (!status.topTool.empty() || !status.topResource.empty()) {
        ImGui::Text("Top Tool: %s", status.topTool.empty() ? "-" : status.topTool.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Top Resource: %s",
                    status.topResource.empty() ? "-" : status.topResource.c_str());
    }

    if (!status.lastError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", status.lastError.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::SeparatorText("Quick Actions");
    if (ImGui::Button("Open Settings")) {
        // Settings modal would be accessed through context
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy Endpoint")) {
        glfwSetClipboardString(window, status.endpointUrl.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Request Log"))
        mcpController->ClearActivityLog();
    if (ImGui::IsItemClicked())
        m_mcpUiClearToggle = !m_mcpUiClearToggle;

    ImGui::SeparatorText("Clients");
    if (ImGui::BeginTable("##mcp_clients", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        DrawMcpClientCard("Codex", "Config path", "~/.codex/config.toml or .codex/config.toml",
                          "Use the built-in MCP server entry so /mcp can discover Horo Engine "
                          "automatically.",
                          mcpController->BuildCodexConfigSnippet(), "Codex MCP config copied",
                          mcpController, window);
        DrawMcpClientCard("Claude", "Config path", "~/.claude.json or .mcp.json",
                          "Claude Code can connect over HTTP MCP using the endpoint shown above.",
                          mcpController->BuildClaudeConfigSnippet(), "Claude MCP config copied",
                          mcpController, window);
        DrawMcpClientCard(
            "VS Code", "Config path", ".vscode/mcp.json or user mcp.json",
            "VS Code MCP can be set per workspace or globally from the Command Palette.",
            mcpController->BuildVsCodeConfigSnippet(), "VS Code MCP config copied", mcpController,
            window);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Live Requests");
    DrawMcpTabLiveRequests(status);

    ImGui::SeparatorText("Catalog");
    DrawMcpTabCatalog(status);
}

void EditorBottomDock::DrawMcpClientCard(const char* title, const char* pathLabel,
                                         const char* pathValue, const char* hint,
                                         std::string_view snippet,
                                         const char* toastLabel,
                                         Horo::Mcp::McpController* mcpController,
                                         GLFWwindow* window) {
    ImGui::TableNextColumn();
    ImGui::BeginChild(title, ImVec2(0, 132.0f), true);
    ImGui::TextUnformatted(title);
    ImGui::Separator();
    ImGui::TextWrapped("%s", hint);
    ImGui::TextDisabled("%s", pathLabel);
    ImGui::TextWrapped("%s", pathValue);
    if (ImGui::Button((std::string("Copy Config##") + title).c_str())) {
        glfwSetClipboardString(window, std::string(snippet).c_str());
        // Toast would be shown through context
    }
    ImGui::EndChild();
}

void EditorBottomDock::DrawMcpTabLiveRequests(const Mcp::McpStatusSnapshot& status) {
    if (!ImGui::BeginTable("##mcp_requests", 6,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                           ImVec2(0.0f, 200.0f)))
        return;
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("Ms", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Request", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Response", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableHeadersRow();
    for (int i = 0; i < static_cast<int>(status.recentActivity.size()); ++i) {
        const Mcp::McpActivityEntry& entry = status.recentActivity[static_cast<size_t>(i)];
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const bool selected = i == m_mcpSelectedActivityIndex;
        if (const std::string selectableLabel = std::format("{}##mcp_req_{}", entry.timeText, i);
            ImGui::Selectable(selectableLabel.c_str(), selected,
                              ImGuiSelectableFlags_SpanAllColumns))
            m_mcpSelectedActivityIndex = i;
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(entry.target.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(entry.ok ? ImVec4(0.75f, 0.95f, 0.75f, 1.0f)
                                     : ImVec4(1.0f, 0.55f, 0.5f, 1.0f),
                           "%s", entry.ok ? "OK" : "FAIL");
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%.1f", entry.durationMs);
        ImGui::TableSetColumnIndex(4);
        ImGui::TextWrapped("%s", entry.requestPreview.empty() ? "-" : entry.requestPreview.c_str());
        ImGui::TableSetColumnIndex(5);
        ImGui::TextWrapped("%s", entry.responsePreview.empty() ? "-" : entry.responsePreview.c_str());
    }
    ImGui::EndTable();

    if (status.recentActivity.empty()) {
        return;
    }
    const Mcp::McpActivityEntry& selected = status.recentActivity[static_cast<size_t>(m_mcpSelectedActivityIndex)];
    ImGui::SeparatorText("Request Detail");
    ImGui::Text("Timestamp: %s", selected.timestampText.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Method: %s", selected.mcpMethod.empty() ? "-" : selected.mcpMethod.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("HTTP: %d", selected.httpStatus);
    ImGui::Text("Operation: %s", selected.operation.empty() ? "-" : selected.operation.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Request ID: %s", selected.requestId.empty() ? "-" : selected.requestId.c_str());
    if (!selected.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.5f, 1.0f));
        ImGui::TextWrapped("Error: %s", selected.error.c_str());
        ImGui::PopStyleColor();
    }
}

void EditorBottomDock::DrawMcpTabCatalog(const Mcp::McpStatusSnapshot& status) const {
    if (!ImGui::BeginTable("##mcp_catalog", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
        return;
    ImGui::TableSetupColumn("Tools", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Resources", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::BeginChild("##mcp_tools_catalog", ImVec2(0, 132.0f), false);
    for (const Mcp::McpCatalogEntry& entry : status.toolCatalog) {
        ImGui::TextUnformatted(entry.name.c_str());
        ImGui::TextDisabled("%s", entry.description.c_str());
    }
    ImGui::EndChild();
    ImGui::TableSetColumnIndex(1);
    ImGui::BeginChild("##mcp_resources_catalog", ImVec2(0, 132.0f), false);
    for (const Mcp::McpCatalogEntry& entry : status.resourceCatalog) {
        ImGui::Text("%s", entry.name.c_str());
        ImGui::TextDisabled("%s", entry.description.c_str());
    }
    ImGui::EndChild();
    ImGui::EndTable();
}

const std::vector<std::pair<std::filesystem::path, bool>>*
EditorBottomDock::GetProjectDirListing(const std::filesystem::path& absPath) {
    namespace fs = std::filesystem;
    if (std::error_code existsEc; !fs::is_directory(absPath, existsEc) || existsEc)
        return nullptr;

    const std::string key = absPath.generic_string();
    const auto frame = static_cast<uint32_t>(ImGui::GetFrameCount());
    if (auto it = m_projectDirCache.find(key); it != m_projectDirCache.end()) {
        const uint32_t age = frame - it->second.cachedAtFrame;
        if (age < kProjectListingCacheFrames)
            return &it->second.entries;
    }

    std::vector<std::pair<fs::path, bool>> sorted;
    std::error_code ec;
    for (fs::directory_iterator dit(absPath, fs::directory_options::skip_permission_denied, ec), end;
         !ec && dit != end; dit.increment(ec)) {
        const fs::directory_entry ent = *dit;
        const std::string name = ent.path().filename().string();
        if (Editor::IsHiddenDotEntry(name))
            continue;
        std::error_code typEc;
        const bool isDir = ent.is_directory(typEc);
        if (isDir && !typEc && Editor::IsBlockedProjectDirName(name, &m_projectExtraBlocklist))
            continue;
        sorted.emplace_back(ent.path(), isDir && !typEc);
    }
    std::ranges::sort(sorted, [](const auto& a, const auto& b) {
        if (a.second != b.second)
            return a.second && !b.second;
        return a.first.filename() < b.first.filename();
    });

    ProjectDirCache slot;
    slot.cachedAtFrame = frame;
    slot.entries = std::move(sorted);
    m_projectDirCache[key] = std::move(slot);
    return &m_projectDirCache.at(key).entries;
}

void EditorBottomDock::InvalidateProjectBrowserCache() {
    m_projectDirCache.clear();
}

} // namespace Horo::Editor
