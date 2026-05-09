/** @file EditorSettingsModal.h
 *  @brief Modal dialog for editing MCP and editor settings. */
#pragma once

#include <string>

#include "mcp/McpController.h"

namespace Horo::Editor {

/** @brief Draws and manages the editor settings modal, including MCP configuration. */
class EditorSettingsModal {
public:
    /** @brief Draws the modal window; must be called every frame while it may be open. */
    void Draw();

    /** @brief Returns true if the modal is currently open.
     *  @return True when the modal is visible. */
    bool IsOpen() const { return m_open; }

    /** @brief Sets the open/closed state of the modal.
     *  @param open True to open the modal, false to close it. */
    void SetOpen(bool open) { m_open = open; }

    /** @brief Returns a pointer to the in-progress settings draft being edited.
     *  @return Non-null pointer to the draft McpSettings; valid for the lifetime of this object. */
    Mcp::McpSettings *GetDraft() { return &m_draft; }

    /** @brief Returns a pointer to the current error message string.
     *  @return Non-null pointer to the error string; empty when no error is present. */
    std::string *GetError() { return &m_error; }

    /** @brief Injects the MCP controller used to apply settings changes.
     *  @param controller Non-owning pointer to the MCP controller; must outlive this object. */
    void SetMcpController(Mcp::McpController *controller) { m_mcpController = controller; }

private:
    bool m_open = false;                        /**< True while the settings modal is visible. */
    Mcp::McpSettings m_draft;                   /**< Working copy of MCP settings being edited. */
    std::string m_error;                        /**< Error message shown inside the modal; empty when none. */
    Mcp::McpController *m_mcpController = nullptr; /**< Non-owning pointer to the MCP controller. */
};

} // namespace Horo::Editor
