#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserDialogs.h"

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserInteractionSession.h"

#include <algorithm>
#include <format>
#include <string>

namespace Horo::Editor {
    namespace {
        [[nodiscard]] std::string FormatByteSize(const std::uintmax_t bytes) {
            if (bytes < 1024U)
                return std::format("{} B", bytes);
            if (bytes < 1024U * 1024U)
                return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
            return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        }

        void PushModalStyle() {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20.0F, 18.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8.0F, 8.0F});
            ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::Bg1());
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::BorderStrong());
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
            ImGui::PushStyleColor(ImGuiCol_Separator, Theme::Border());
            ImVec4 dim = Theme::Bg0();
            dim.w = 0.72F;
            ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, dim);
        }

        void PopModalStyle() {
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(4);
        }

        void DrawModalHeading(const std::string &title, const EditorGuiContext &context) {
            Theme::ScopedTextStyle textStyle(context.theme.fonts.sansEmphasis, 19.0F, Theme::FontPx::SansEmphasis);
            ImGui::TextColored(Theme::Text(), "%s", title.c_str());
            ImGui::Dummy({0.0F, 2.0F});
            ImGui::Separator();
            ImGui::Dummy({0.0F, 4.0F});
        }

        [[nodiscard]] bool DrawClosableModalHeading(const std::string &title, const EditorGuiContext &context) {
            constexpr float closeSize = 22.0F;
            const float headerY = ImGui::GetCursorPosY();
            {
                Theme::ScopedTextStyle textStyle(context.theme.fonts.sansEmphasis, 19.0F, Theme::FontPx::SansEmphasis);
                ImGui::TextColored(Theme::Text(), "%s", title.c_str());
            }
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeSize);
            ImGui::SetCursorPosY(headerY);
            const bool close = Ui::IconCloseButton("##CloseAssetInfo", {closeSize, closeSize});
            ImGui::SetCursorPosY(headerY + closeSize + 5.0F);
            ImGui::Separator();
            ImGui::Dummy({0.0F, 2.0F});
            return close;
        }

        void DrawInfoRow(const char *label, const char *value, const EditorGuiContext &context) {
            ImGui::PushID(label);
            if (!ImGui::BeginTable("##AssetInfoRow", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::PopID();
                return;
            }
            const float labelWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.30F, 72.0F, 132.0F);
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            {
                Theme::ScopedTextStyle textStyle(context.theme.fonts.sans, 14.0F, Theme::FontPx::Sans);
                ImGui::TextColored(Theme::Muted(), "%s", label);
            }
            ImGui::TableSetColumnIndex(1);
            {
                Theme::ScopedTextStyle textStyle(context.theme.fonts.sansCompact, 14.0F, Theme::FontPx::SansCompact);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                ImGui::PushTextWrapPos(0.0F);
                ImGui::TextWrapped("%s", value);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
            ImGui::Separator();
            ImGui::PopID();
        }

        void DrawInfoPopup(AssetBrowserInteractionState &state, const EditorGuiContext &context) {
            constexpr const char *popupId = "##ContentBrowserAssetInfo";
            if (state.openAssetInfo) {
                ImGui::OpenPopup(popupId);
                state.openAssetInfo = false;
            }
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            const float popupWidth = std::max(160.0F, std::min(560.0F, viewport->WorkSize.x - 32.0F));
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
            ImGui::SetNextWindowSizeConstraints({popupWidth, 0.0F}, {popupWidth, std::max(240.0F, viewport->WorkSize.y - 48.0F)});
            PushModalStyle();
            if (ImGui::BeginPopupModal(popupId, nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                           ImGuiWindowFlags_NoSavedSettings)) {
                if (DrawClosableModalHeading(context.localization.Get("editor", "workspace.content_browser.info.title"), context))
                    ImGui::CloseCurrentPopup();
                if (state.popupEntry.has_value()) {
                    const ContentBrowserEntry &entry = *state.popupEntry;
                    {
                        Theme::ScopedTextStyle textStyle(context.theme.fonts.sansEmphasis, 17.0F, Theme::FontPx::SansEmphasis);
                        ImGui::PushTextWrapPos(0.0F);
                        ImGui::TextWrapped("%s", entry.displayName.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::Dummy({0.0F, 2.0F});
                    DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.path").c_str(),
                                entry.absolutePath.c_str(), context);
                    if (entry.kind == ContentBrowserEntryKind::Asset) {
                        const std::string &none = context.localization.Get("editor", "workspace.content_browser.info.none");
                        const std::string size = FormatByteSize(entry.byteSize);
                        DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.type").c_str(),
                                    entry.assetType.empty() ? none.c_str() : entry.assetType.c_str(), context);
                        DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.asset_id").c_str(),
                                    entry.assetId.empty() ? none.c_str() : entry.assetId.c_str(), context);
                        DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.registration").c_str(),
                                    context.localization
                                        .Get("editor", entry.registered ? "workspace.content_browser.info.registered"
                                                                        : "workspace.content_browser.info.unregistered")
                                        .c_str(),
                                    context);
                        DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.size").c_str(), size.c_str(),
                                    context);
                        const std::string importer =
                            entry.importerContributionId.empty()
                                ? none
                                : entry.importerContributionId +
                                      (entry.importerVersion.empty() ? std::string{} : " @ " + entry.importerVersion) +
                                      (entry.importerChanged ? " \xE2\x86\x92 " + entry.activeImporterVersion : std::string{});
                        DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.importer").c_str(), importer.c_str(),
                                    context);
                        const std::string module =
                            entry.importerModuleId.empty()
                                ? none
                                : entry.importerModuleId +
                                      (entry.importerModuleVersion.empty() ? std::string{} : " @ " + entry.importerModuleVersion) +
                                      (entry.moduleChanged
                                           ? " \xE2\x86\x92 " + entry.activeImporterModuleId + " @ " + entry.activeImporterModuleVersion
                                           : std::string{});
                        DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.module").c_str(), module.c_str(),
                                    context);
                        DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.source").c_str(),
                                    entry.absoluteImportSourcePath.empty() ? none.c_str() : entry.absoluteImportSourcePath.c_str(),
                                    context);
                        std::string reimportState = context.localization.Get("editor", "workspace.content_browser.info.reimport_manual");
                        if (entry.sourceChanged)
                            reimportState = context.localization.Get("editor", "workspace.content_browser.info.reimport_source_stale");
                        if (entry.importerChanged || entry.moduleChanged) {
                            if (entry.sourceChanged)
                                reimportState += ", ";
                            else
                                reimportState.clear();
                            reimportState +=
                                context.localization.Get("editor", entry.moduleChanged
                                                                       ? "workspace.content_browser.info.reimport_module_changed"
                                                                       : "workspace.content_browser.info.reimport_importer_changed");
                        }
                        DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.reimport_status").c_str(),
                                    entry.canReimport ? reimportState.c_str() : none.c_str(), context);
                        std::string lastImport;
                        for (const Assets::AssetImportReason reason : entry.lastImportReasons) {
                            const char *key = nullptr;
                            switch (reason) {
                                case Assets::AssetImportReason::InitialImport:
                                    key = "workspace.content_browser.info.import_reason_initial";
                                    break;
                                case Assets::AssetImportReason::ManualReimport:
                                    key = "workspace.content_browser.info.import_reason_manual";
                                    break;
                                case Assets::AssetImportReason::SourceChanged:
                                    key = "workspace.content_browser.info.reimport_source_changed";
                                    break;
                                case Assets::AssetImportReason::ImporterChanged:
                                    key = "workspace.content_browser.info.reimport_importer_changed";
                                    break;
                                case Assets::AssetImportReason::ModuleChanged:
                                    key = "workspace.content_browser.info.reimport_module_changed";
                                    break;
                            }
                            if (!lastImport.empty())
                                lastImport += ", ";
                            lastImport += context.localization.Get("editor", key);
                        }
                        DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.last_import_reason").c_str(),
                                    lastImport.empty() ? none.c_str() : lastImport.c_str(), context);
                        DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.metadata").c_str(),
                                    entry.absoluteMetadataPath.empty() ? none.c_str() : entry.absoluteMetadataPath.c_str(), context);
                    }
                }
                ImGui::EndPopup();
            }
            PopModalStyle();
        }

        void DrawRenamePopup(AssetBrowserInteractionState &state, EditorWorkspaceViewCommandData &command,
                             const EditorGuiContext &context) {
            constexpr const char *popupId = "##ContentBrowserRename";
            if (state.openRename) {
                ImGui::OpenPopup(popupId);
                state.openRename = false;
            }
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            const float popupWidth = std::max(160.0F, std::min(460.0F, viewport->WorkSize.x - 32.0F));
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
            ImGui::SetNextWindowSizeConstraints({popupWidth, 0.0F}, {popupWidth, viewport->WorkSize.y - 48.0F});
            PushModalStyle();
            if (ImGui::BeginPopupModal(popupId, nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                           ImGuiWindowFlags_NoSavedSettings)) {
                DrawModalHeading(context.localization.Get("editor", "workspace.content_browser.rename.title"), context);
                Ui::FieldLabel(context.localization.Get("editor", "workspace.content_browser.rename.name").c_str(), context.theme.fonts);
                static_cast<void>(Ui::InputTextControl("##ContentBrowserRenameInput", state.renameBuffer.data(), state.renameBuffer.size(),
                                                       context.theme.fonts));
                ImGui::Dummy({0.0F, 12.0F});
                if (Ui::Button({
                        .label = context.localization.Get("editor", "workspace.content_browser.action.cancel").c_str(),
                        .size = {92.0F, 32.0F},
                        .variant = Ui::ButtonVariant::Secondary,
                        .font = context.theme.fonts.sans,
                    })) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (Ui::Button({
                        .label = context.localization.Get("editor", "workspace.content_browser.action.rename").c_str(),
                        .size = {92.0F, 32.0F},
                        .variant = Ui::ButtonVariant::Primary,
                        .enabled = state.renameBuffer[0] != '\0',
                        .font = context.theme.fonts.sans,
                    }) &&
                    state.popupEntry.has_value()) {
                    command = AssetBrowserInteractionSession::Rename(state.popupEntry->absolutePath, state.renameBuffer.data());
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            PopModalStyle();
        }

        void DrawDeletePopup(AssetBrowserInteractionState &state, EditorWorkspaceViewCommandData &command,
                             const EditorGuiContext &context) {
            constexpr const char *popupId = "##ContentBrowserDelete";
            if (state.openDeleteConfirmation) {
                ImGui::OpenPopup(popupId);
                state.openDeleteConfirmation = false;
            }
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            const float popupWidth = std::max(160.0F, std::min(480.0F, viewport->WorkSize.x - 32.0F));
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
            ImGui::SetNextWindowSizeConstraints({popupWidth, 0.0F}, {popupWidth, viewport->WorkSize.y - 48.0F});
            PushModalStyle();
            if (ImGui::BeginPopupModal(popupId, nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                           ImGuiWindowFlags_NoSavedSettings)) {
                DrawModalHeading(context.localization.Get("editor", "workspace.content_browser.delete.title"), context);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400.0F);
                ImGui::TextColored(Theme::Muted(), "%s",
                                   context.localization.Get("editor", "workspace.content_browser.delete.message").c_str());
                ImGui::PopTextWrapPos();
                if (state.popupEntry.has_value()) {
                    ImGui::Dummy({0.0F, 6.0F});
                    ImGui::TextColored(Theme::Text(), "%s", state.popupEntry->displayName.c_str());
                    if (state.popupEntry->kind == ContentBrowserEntryKind::Asset) {
                        const std::string dependencyCount = std::to_string(state.popupEntry->dependencyCount);
                        DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.delete.dependencies").c_str(),
                                    dependencyCount.c_str(), context);
                        if (state.popupEntry->dependencyCount > 0) {
                            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400.0F);
                            ImGui::TextColored(Theme::Warn(), "%s",
                                               context.localization.Get("editor", "workspace.content_browser.delete.dependency_warning")
                                                   .c_str());
                            ImGui::PopTextWrapPos();
                        }
                    }
                }
                ImGui::Dummy({0.0F, 12.0F});
                if (Ui::Button({
                        .label = context.localization.Get("editor", "workspace.content_browser.action.cancel").c_str(),
                        .size = {92.0F, 32.0F},
                        .variant = Ui::ButtonVariant::Secondary,
                        .font = context.theme.fonts.sans,
                    })) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (Ui::Button({
                        .label = context.localization.Get("editor", "workspace.content_browser.action.delete").c_str(),
                        .size = {92.0F, 32.0F},
                        .variant = Ui::ButtonVariant::Primary,
                        .font = context.theme.fonts.sans,
                    }) &&
                    state.popupEntry.has_value()) {
                    command = AssetBrowserInteractionSession::Delete(state.popupEntry->absolutePath);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            PopModalStyle();
        }

        void DrawCreateFolderPopup(AssetBrowserInteractionState &state, const ContentBrowserDirectory &directory,
                                   EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
            constexpr const char *popupId = "##ContentBrowserCreateFolder";
            if (state.openCreateFolder) {
                ImGui::OpenPopup(popupId);
                state.openCreateFolder = false;
            }
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            const float popupWidth = std::max(160.0F, std::min(460.0F, viewport->WorkSize.x - 32.0F));
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
            ImGui::SetNextWindowSizeConstraints({popupWidth, 0.0F}, {popupWidth, viewport->WorkSize.y - 48.0F});
            PushModalStyle();
            if (ImGui::BeginPopupModal(popupId, nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                           ImGuiWindowFlags_NoSavedSettings)) {
                DrawModalHeading(context.localization.Get("editor", "workspace.content_browser.create_folder.title"), context);
                Ui::FieldLabel(context.localization.Get("editor", "workspace.content_browser.create_folder.name").c_str(),
                               context.theme.fonts);
                static_cast<void>(Ui::InputTextControl("##ContentBrowserCreateFolderInput", state.createFolderBuffer.data(),
                                                       state.createFolderBuffer.size(), context.theme.fonts));
                ImGui::Dummy({0.0F, 12.0F});
                if (Ui::Button({
                        .label = context.localization.Get("editor", "workspace.content_browser.action.cancel").c_str(),
                        .size = {92.0F, 32.0F},
                        .variant = Ui::ButtonVariant::Secondary,
                        .font = context.theme.fonts.sans,
                    })) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (Ui::Button({
                        .label = context.localization.Get("editor", "workspace.content_browser.action.create_folder").c_str(),
                        .size = {112.0F, 32.0F},
                        .variant = Ui::ButtonVariant::Primary,
                        .enabled = state.createFolderBuffer[0] != '\0',
                        .font = context.theme.fonts.sans,
                    })) {
                    command = AssetBrowserInteractionSession::CreateFolder(directory.absoluteCurrentPath, state.createFolderBuffer.data());
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            PopModalStyle();
        }
    }  // namespace

    /** @copydoc DrawAssetBrowserDialogs */
    void DrawAssetBrowserDialogs(AssetBrowserInteractionState &state, const ContentBrowserDirectory &directory,
                                 EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
        DrawInfoPopup(state, context);
        DrawRenamePopup(state, command, context);
        DrawDeletePopup(state, command, context);
        DrawCreateFolderPopup(state, directory, command, context);
    }
}  // namespace Horo::Editor
