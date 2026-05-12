/** @file EditorHelpPopup.h
 *  @brief Searchable keyboard-shortcuts help popup for the editor. */
#pragma once

#include <string>
#include <string_view>

namespace Horo::Editor {

/** @brief Draws and manages the editor help popup displaying keyboard shortcuts and usage hints. */
class EditorHelpPopup {
public:
    /** @brief Draws the help popup window; must be called every frame while it may be open. */
    void Draw();

    /** @brief Returns true if the help popup is currently open.
     *  @return True when the popup is visible. */
    bool IsOpen() const { return m_open; }

    /** @brief Sets the open/closed state of the help popup.
     *  @param open True to open the popup, false to close it. */
    void SetOpen(bool open) { m_open = open; }

    /** @brief Pre-populates the search field with the given query string.
     *  @param query Initial search text to display in the filter box. */
    void SetSearchQuery(std::string_view query) { m_searchQuery = query; }

private:
    bool m_open = false;          /**< True while the help popup is visible. */
    std::string m_searchQuery;    /**< Current text entered in the shortcut search filter. */
};

} // namespace Horo::Editor
