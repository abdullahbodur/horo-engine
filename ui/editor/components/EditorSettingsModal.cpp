#include "ui/editor/components/EditorSettingsModal.h"

#include <format>
#include <imgui.h>

#include "mcp/McpSettings.h"

namespace Horo::Editor {

void EditorSettingsModal::Draw() {
    if (m_open && m_mcpController)
        ImGui::OpenPopup("Editor Settings");

    if (!ImGui::BeginPopupModal("Editor Settings", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextDisabled("Built-in MCP");
    ImGui::Checkbox("Enable built-in MCP", &m_draft.enabled);
    ImGui::Checkbox("Auto-start when editor opens",
                    &m_draft.autoStart);

    if (int port = m_draft.port; ImGui::InputInt("Port", &port))
        m_draft.port = std::max(1, std::min(65535, port));

    ImGui::Text("Host: %s", Mcp::kDefaultMcpHost);
    m_draft.host = Mcp::kDefaultMcpHost;

    const auto endpoint =
        std::format("{}://{}:{}/mcp", Mcp::kMcpUrlScheme, m_draft.host,
                    m_draft.port);
    ImGui::TextWrapped("Endpoint: %s", endpoint.c_str());

    if (!m_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", m_error.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    if (ImGui::Button("Apply", ImVec2(120.0f, 0.0f))) {
        std::string err;
        if (m_mcpController && m_mcpController->ApplySettings(m_draft, &err)) {
            m_draft = m_mcpController->GetSettings();
            m_error.clear();
            m_open = false;
            ImGui::CloseCurrentPopup();
        } else {
            m_error = err;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
        m_open = false;
        if (m_mcpController)
            m_draft = m_mcpController->GetSettings();
        m_error.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

} // namespace Horo::Editor
