#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserInteractionSession.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace Horo::Editor {
    namespace {
        [[nodiscard]] bool IsAbsolute(const std::string_view path) {
            return !path.empty() && std::filesystem::path{path}.is_absolute();
        }

        [[nodiscard]] bool IsEntryName(const std::string_view name) {
            if (name.empty())
                return false;
            const std::filesystem::path path{name};
            return path == path.filename() && name != "." && name != "..";
        }
    }  // namespace

    /** @copydoc AssetBrowserInteractionSession::State */
    AssetBrowserInteractionState &AssetBrowserInteractionSession::State() noexcept {
        return m_state;
    }

    /** @copydoc AssetBrowserInteractionSession::State */
    const AssetBrowserInteractionState &AssetBrowserInteractionSession::State() const noexcept {
        return m_state;
    }

    /** @copydoc AssetBrowserInteractionSession::ProjectEntries */
    std::vector<std::size_t> AssetBrowserInteractionSession::ProjectEntries(const ContentBrowserDirectory &directory) const {
        return ProjectContentBrowserEntries(directory, ContentBrowserEntryQuery{
                                                           .name = m_state.search.data(),
                                                           .assetType = m_state.assetTypeFilter,
                                                           .sortField = m_state.sortField,
                                                           .sortDirection = m_state.sortDirection,
                                                       });
    }

    /** @copydoc AssetBrowserInteractionSession::Select */
    void AssetBrowserInteractionSession::Select(const std::string_view absolutePath) {
        m_state.selectedAbsolutePath = IsAbsolute(absolutePath) ? std::string{absolutePath} : std::string{};
    }

    /** @copydoc AssetBrowserInteractionSession::OpenRename */
    void AssetBrowserInteractionSession::OpenRename(const ContentBrowserEntry &entry) {
        m_state.popupEntry = entry;
        m_state.renameBuffer.fill('\0');
        const std::size_t copyLength = std::min(entry.displayName.size(), m_state.renameBuffer.size() - 1U);
        std::memcpy(m_state.renameBuffer.data(), entry.displayName.data(), copyLength);
        m_state.openRename = true;
    }

    /** @copydoc AssetBrowserInteractionSession::OpenInfo */
    void AssetBrowserInteractionSession::OpenInfo(const ContentBrowserEntry &entry) {
        m_state.popupEntry = entry;
        m_state.openAssetInfo = true;
    }

    /** @copydoc AssetBrowserInteractionSession::OpenDelete */
    void AssetBrowserInteractionSession::OpenDelete(const ContentBrowserEntry &entry) {
        m_state.popupEntry = entry;
        m_state.openDeleteConfirmation = true;
    }

    /** @copydoc AssetBrowserInteractionSession::OpenCreateFolder */
    void AssetBrowserInteractionSession::OpenCreateFolder() {
        m_state.createFolderBuffer.fill('\0');
        m_state.openCreateFolder = true;
    }

    /** @copydoc AssetBrowserInteractionSession::Navigate */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::Navigate(const std::string_view absolutePath) {
        return PathCommand(EditorWorkspaceViewCommand::NavigateContentBrowser, absolutePath);
    }

    /** @copydoc AssetBrowserInteractionSession::Navigate */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::Navigate(const EditorWorkspaceViewCommand navigationCommand) {
        using enum EditorWorkspaceViewCommand;
        switch (navigationCommand) {
            case NavigateContentBrowserBack:
            case NavigateContentBrowserForward:
            case NavigateContentBrowserUp:
                return {.command = navigationCommand};
            default:
                return {};
        }
    }

    /** @copydoc AssetBrowserInteractionSession::Refresh */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::Refresh() {
        return {.command = EditorWorkspaceViewCommand::RefreshContentBrowser};
    }

    /** @copydoc AssetBrowserInteractionSession::Rename */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::Rename(const std::string_view absolutePath,
                                                                          const std::string_view name) {
        if (!IsAbsolute(absolutePath) || !IsEntryName(name))
            return {};
        EditorWorkspaceViewCommandData result;
        result.command = EditorWorkspaceViewCommand::RenameContentBrowserEntry;
        result.stringPayload = std::string{absolutePath};
        result.secondaryStringPayload = std::string{name};
        return result;
    }

    /** @copydoc AssetBrowserInteractionSession::Delete */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::Delete(const std::string_view absolutePath) {
        return PathCommand(EditorWorkspaceViewCommand::DeleteContentBrowserEntry, absolutePath);
    }

    /** @copydoc AssetBrowserInteractionSession::CreateFolder */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::CreateFolder(const std::string_view absoluteDirectory,
                                                                                const std::string_view name) {
        if (!IsAbsolute(absoluteDirectory) || !IsEntryName(name))
            return {};
        EditorWorkspaceViewCommandData result;
        result.command = EditorWorkspaceViewCommand::CreateContentBrowserFolder;
        result.stringPayload = std::string{absoluteDirectory};
        result.secondaryStringPayload = std::string{name};
        return result;
    }

    /** @copydoc AssetBrowserInteractionSession::Duplicate */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::Duplicate(const std::string_view absolutePath) {
        return PathCommand(EditorWorkspaceViewCommand::DuplicateContentBrowserAsset, absolutePath);
    }

    /** @copydoc AssetBrowserInteractionSession::Copy */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::Copy(const std::string_view absolutePath) {
        return PathCommand(EditorWorkspaceViewCommand::CopyContentBrowserAsset, absolutePath);
    }

    /** @copydoc AssetBrowserInteractionSession::Cut */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::Cut(const std::string_view absolutePath) {
        return PathCommand(EditorWorkspaceViewCommand::CutContentBrowserAsset, absolutePath);
    }

    /** @copydoc AssetBrowserInteractionSession::Paste */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::Paste(const std::string_view absoluteDirectory) {
        return PathCommand(EditorWorkspaceViewCommand::PasteContentBrowserAsset, absoluteDirectory);
    }

    /** @copydoc AssetBrowserInteractionSession::CancelClipboard */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::CancelClipboard() {
        return {.command = EditorWorkspaceViewCommand::CancelContentBrowserClipboard};
    }

    /** @copydoc AssetBrowserInteractionSession::Transfer */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::Transfer(const std::string_view absoluteSource,
                                                                            const std::string_view absoluteDestinationDirectory,
                                                                            const ContentBrowserTransferMode mode) {
        if (!IsAbsolute(absoluteSource) || !IsAbsolute(absoluteDestinationDirectory)) {
            return {};
        }
        EditorWorkspaceViewCommandData result;
        result.command = EditorWorkspaceViewCommand::TransferContentBrowserAsset;
        result.contentBrowserTransfer = ContentBrowserAssetTransferRequest{
            .absoluteSourcePath = std::string{absoluteSource},
            .absoluteDestinationDirectory = std::string{absoluteDestinationDirectory},
            .mode = mode,
        };
        return result;
    }

    /** @copydoc AssetBrowserInteractionSession::Reimport */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::Reimport(const std::string_view absolutePath) {
        return PathCommand(EditorWorkspaceViewCommand::ReimportContentBrowserAsset, absolutePath);
    }

    /** @copydoc AssetBrowserInteractionSession::Reveal */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::Reveal(const std::string_view absolutePath) {
        return PathCommand(EditorWorkspaceViewCommand::RevealContentBrowserEntry, absolutePath);
    }

    /** @copydoc AssetBrowserInteractionSession::ImportHere */
    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::ImportHere(const std::string_view absoluteDirectory) {
        if (!IsAbsolute(absoluteDirectory))
            return {};
        EditorWorkspaceViewCommandData result;
        result.menuInvocation = EditorMenuInvocation{
            .action = EditorMenuAction::ImportAssets,
            .assetDestination = std::filesystem::path{absoluteDirectory}.lexically_normal(),
        };
        return result;
    }

    EditorWorkspaceViewCommandData AssetBrowserInteractionSession::PathCommand(const EditorWorkspaceViewCommand command,
                                                                               const std::string_view absolutePath) {
        if (!IsAbsolute(absolutePath))
            return {};
        EditorWorkspaceViewCommandData result;
        result.command = command;
        result.stringPayload = std::string{absolutePath};
        return result;
    }
}  // namespace Horo::Editor
