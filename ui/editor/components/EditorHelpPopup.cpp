#include "ui/editor/components/EditorHelpPopup.h"

#include <imgui.h>

#include "ui/editor/EditorSearch.h"

namespace Horo::Editor {

void EditorHelpPopup::Draw() {
    if (!m_open)
        return;
    const std::span<const ShortcutRow> shortcuts = GetEditorShortcuts();

    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(620.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - 620.0f) * 0.5f,
                                   (io.DisplaySize.y - 420.0f) * 0.5f),
                            ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Help - Keyboard Shortcuts", &m_open,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Search by category, command, or key");
    std::string searchBuf(256, '\0');
    m_searchQuery.copy(searchBuf.data(), searchBuf.size() - 1);
    if (ImGui::InputTextWithHint("##shortcut_search", "Find shortcut...",
                                 searchBuf.data(), searchBuf.size()))
        m_searchQuery = searchBuf.data();

    ImGui::Separator();
    ImGui::Columns(3, "shortcut_columns", false);
    ImGui::SetColumnWidth(0, 130.0f);
    ImGui::SetColumnWidth(1, 300.0f);
    ImGui::TextUnformatted("Category");
    ImGui::NextColumn();
    ImGui::TextUnformatted("Command");
    ImGui::NextColumn();
    ImGui::TextUnformatted("Shortcut");
    ImGui::NextColumn();
    ImGui::Separator();

    int shownCount = 0;
    for (const auto &row : shortcuts) {
        if (!MatchesShortcutQuery(row, m_searchQuery))
            continue;

        ImGui::TextDisabled("%s", row.category);
        ImGui::NextColumn();
        ImGui::TextUnformatted(row.command);
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s", row.keys);
        ImGui::NextColumn();
        ++shownCount;
    }

    ImGui::Columns(1);
    if (shownCount == 0)
        ImGui::TextDisabled("No shortcut matches '%s'", m_searchQuery.c_str());

    ImGui::Separator();
    ImGui::TextDisabled("Tip: press ? or F1 to close this window quickly.");
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        m_open = false;
        m_searchQuery.clear();
    }
    ImGui::End();
}

} // namespace Horo::Editor
