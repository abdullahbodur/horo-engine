/** @file EditorSettingsModal.h
 *  @brief Tabbed editor settings modal with staged Apply/OK/Cancel semantics. */
#pragma once

#include <functional>
#include <string>

#include "mcp/McpController.h"
#include "mcp/McpSettings.h"
#include "ui/HoroTheme.h"
#include "ui/editor/EditorUserSettings.h"

namespace Horo::Editor {

/**
 * @brief Top-level Settings modal component.
 *
 * Owns drafts for each settings domain (MCP, Appearance) and originals used to
 * decide dirtiness and to discard on Cancel.  Cross-tab drafts are preserved
 * as long as the modal is open; Apply persists all dirty domains and keeps
 * the modal open, OK persists and closes on success, Cancel/Escape/close
 * affordance restore originals and close.
 *
 * Dependencies are injected as non-owning pointers / callbacks:
 * - SetMcpController() provides the MCP apply path.
 * - SetUserSettingsDocument() provides a non-owning pointer to the user
 *   settings document owned by EditorLayer.
 * - SetApplyThemePresetCallback() provides the hook used to apply the saved
 *   theme preset into the live editor style after a successful save.
 */
class EditorSettingsModal {
public:
    /** @brief Which tab the Settings modal currently shows. */
    enum class Tab {
        MCP,        /**< Built-in Model Context Protocol server settings. */
        Appearance, /**< Editor theme preset selection. */
    };

    /** @brief Callback type used to apply a theme preset into the live editor. */
    using ApplyThemePresetCallback = std::function<void(std::string_view)>;

    /** @brief Draws the modal for one frame; must be called every frame while it may be open. */
    void Draw();

    /**
     * @brief Returns true while the modal should be rendered.
     * @return True when the modal is currently visible, false otherwise.
     */
    bool IsOpen() const { return m_open; }

    /**
     * @brief Force-sets the open/closed state without seeding drafts.
     *
     * Prefer Open() for opening the modal so drafts/originals stay in sync
     * with live editor state. SetOpen(false) is safe for emergency close
     * paths; drafts remain stale until the next Open().
     * @param open True to mark the modal open, false to close.
     */
    void SetOpen(bool open) { m_open = open; }

    /**
     * @brief Opens the modal and seeds drafts and originals from current state.
     *
     * Both MCP and Appearance drafts are initialised from @p mcp and @p user
     * respectively; originals copy the same values so dirty-tracking has a
     * baseline.  Resets the selected tab to MCP and clears any pending error.
     * @param mcp  Current MCP settings snapshot to seed the MCP draft/original.
     * @param user Current editor user settings snapshot to seed the Appearance draft/original.
     */
    void Open(const Mcp::McpSettings &mcp, const EditorUserSettings &user);

    /**
     * @brief Injects the MCP controller used to apply settings changes.
     * @param controller Non-owning pointer to the MCP controller; must outlive this object.
     */
    void SetMcpController(Mcp::McpController *controller) { m_mcpController = controller; }

    /**
     * @brief Injects the user settings document owned by EditorLayer.
     *
     * A null document disables Appearance save with a visible error rather
     * than crashing.  The pointer must outlive the modal.
     * @param document Non-owning pointer to the user settings document.
     */
    void SetUserSettingsDocument(EditorUserSettingsDocument *document) {
        m_userSettingsDocument = document;
    }

    /**
     * @brief Installs the callback used to apply a theme preset into the live editor.
     *
     * Called from Save paths after successful persistence so the editor's
     * ImGui style reflects the freshly saved preset.
     * @param callback Callback invoked with the saved preset; may be null to no-op.
     */
    void SetApplyThemePresetCallback(ApplyThemePresetCallback callback) {
        m_applyThemePreset = std::move(callback);
    }

    /**
     * @brief Returns a pointer to the modal's error string (may be empty).
     * @return Non-null pointer to the error string.
     */
    std::string *GetError() { return &m_error; }

private:
    /** @brief True while the modal is open (draws each frame). */
    bool m_open = false;
    /** @brief One-frame trigger that tells Draw() to call ImGui::OpenPopup. */
    bool m_openRequested = false;
    /** @brief Currently selected tab; defaults to MCP on open. */
    Tab m_activeTab = Tab::MCP;

    Mcp::McpSettings m_mcpDraft{};           /**< In-progress MCP edits. */
    Mcp::McpSettings m_mcpOriginal{};        /**< MCP state at modal open; used by Cancel / dirty tracking. */
    EditorUserSettings m_userDraft{};        /**< In-progress appearance edits. */
    EditorUserSettings m_userOriginal{};     /**< Appearance state at modal open; used by Cancel / dirty tracking. */
    std::string m_error;                     /**< Inline error surface for save / validation failures. */

    Mcp::McpController *m_mcpController = nullptr;                  /**< Non-owning. */
    EditorUserSettingsDocument *m_userSettingsDocument = nullptr;   /**< Non-owning. */
    ApplyThemePresetCallback m_applyThemePreset;                    /**< May be empty. */

    /** @brief True when any draft differs from its corresponding original. */
    bool IsDirty() const;
    /** @brief Discards both drafts back to their original values and clears the error. */
    void ResetDrafts();
    /** @brief Renders the MCP tab content. */
    void DrawMcpTab(const Horo::Ui::EditorTheme& theme);
    /** @brief Renders the Appearance tab content. */
    void DrawAppearanceTab(const Horo::Ui::EditorTheme& theme);
    /**
     * @brief Attempts to validate and persist both MCP and Appearance drafts.
     *
     * Populates @ref m_error on failure and leaves originals untouched. On
     * success updates originals from the current state and triggers the
     * configured ApplyThemePreset callback with the saved preset.
     * @return True when every step succeeded, false otherwise.
     */
    bool SaveAll();
};

} // namespace Horo::Editor
