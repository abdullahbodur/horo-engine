#pragma once

#include <string>

#include "mcp/McpController.h"

namespace Horo::Editor {

class EditorSettingsModal {
public:
    void Draw();

    bool IsOpen() const { return m_open; }
    void SetOpen(bool open) { m_open = open; }
    Mcp::McpSettings *GetDraft() { return &m_draft; }
    std::string *GetError() { return &m_error; }
    void SetMcpController(Mcp::McpController *controller) { m_mcpController = controller; }

private:
    bool m_open = false;
    Mcp::McpSettings m_draft;
    std::string m_error;
    Mcp::McpController *m_mcpController = nullptr;
};

} // namespace Horo::Editor
