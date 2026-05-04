#pragma once

#include <string>

namespace Horo::Editor {

class EditorHelpPopup {
public:
    void Draw();

    bool IsOpen() const { return m_open; }
    void SetOpen(bool open) { m_open = open; }
    void SetSearchQuery(const std::string &query) { m_searchQuery = query; }

private:
    bool m_open = false;
    std::string m_searchQuery;
};

} // namespace Horo::Editor
