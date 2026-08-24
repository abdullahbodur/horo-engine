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

        [[nodiscard]] std::string FormatImporterInfo(const ContentBrowserEntry &entry, const std::string &none) {
            if (entry.importerContributionId.empty()) {
                return none;
            }
            std::string result = entry.importerContributionId;
            if (!entry.importerVersion.empty()) {
                result += " @ " + entry.importerVersion;
            }
            if (entry.importerChanged) {
                result += " \xE2\x86\x92 " + entry.activeImporterVersion;
            }
            return result;
        }

        [[nodiscard]] std::string FormatModuleInfo(const ContentBrowserEntry &entry, const std::string &none) {
            if (entry.importerModuleId.empty()) {
                return none;
            }
            std::string result = entry.importerModuleId;
            if (!entry.importerModuleVersion.empty()) {
                result += " @ " + entry.importerModuleVersion;
            }
            if (entry.moduleChanged) {
                result += " \xE2\x86\x92 " + entry.activeImporterModuleId + " @ " + entry.activeImporterModuleVersion;
            }
            return result;
        }

        [[nodiscard]] std::string FormatReimportState(const ContentBrowserEntry &entry, const ILocalizationService &localization) {
            std::string reimportState = localization.Get("editor", "workspace.content_browser.info.reimport_manual");
            if (entry.sourceChanged) {
                reimportState = localization.Get("editor", "workspace.content_browser.info.reimport_source_stale");
            }
            if (entry.importerChanged || entry.moduleChanged) {
                if (entry.sourceChanged) {
                    reimportState += ", ";
                } else {
                    reimportState.clear();
                }
                const char *key = entry.moduleChanged ? "workspace.content_browser.info.reimport_module_changed"
                                                      : "workspace.content_browser.info.reimport_importer_changed";
                reimportState += localization.Get("editor", key);
            }
            return reimportState;
        }

        [[nodiscard]] std::string FormatLastImportReasons(const ContentBrowserEntry &entry, const ILocalizationService &localization) {
            using enum Assets::AssetImportReason;
            std::string lastImport;
            for (const Assets::AssetImportReason reason : entry.lastImportReasons) {
                const char *key = nullptr;
                switch (reason) {
                    case InitialImport:
                        key = "workspace.content_browser.info.import_reason_initial";
                        break;
                    case ManualReimport:
                        key = "workspace.content_browser.info.import_reason_manual";
                        break;
                    case SourceChanged:
                        key = "workspace.content_browser.info.reimport_source_changed";
                        break;
                    case ImporterChanged:
                        key = "workspace.content_browser.info.reimport_importer_changed";
                        break;
                    case ModuleChanged:
                        key = "workspace.content_browser.info.reimport_module_changed";
                        break;
                }
                if (key != nullptr) {
                    if (!lastImport.empty()) {
                        lastImport += ", ";
                    }
                    lastImport += localization.Get("editor", key);
                }
            }
            return lastImport;
        }

        void DrawDeleteDialogEntryDetails(const ContentBrowserEntry &entry, const EditorGuiContext &context) {
            ImGui::Dummy({0.0F, 6.0F});
            ImGui::TextColored(Theme::Text(), "%s", entry.displayName.c_str());
            if (entry.kind != ContentBrowserEntryKind::Asset) {
                return;
            }
            const std::string dependencyCount = std::to_string(entry.dependencyCount);
            DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.delete.dependencies").c_str(),
                        dependencyCount.c_str(), context);
            if (entry.dependencyCount > 0) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400.0F);
                ImGui::TextColored(Theme::Warn(), "%s",
                                   context.localization.Get("editor", "workspace.content_browser.delete.dependency_warning").c_str());
                ImGui::PopTextWrapPos();
            }
        }

        void DrawAssetDetailsRows(const ContentBrowserEntry &entry, const EditorGuiContext &context) {
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
            DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.size").c_str(), size.c_str(), context);
            const std::string importer = FormatImporterInfo(entry, none);
            DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.importer").c_str(), importer.c_str(), context);
            const std::string moduleInfo = FormatModuleInfo(entry, none);
            DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.module").c_str(), moduleInfo.c_str(), context);
            DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.source").c_str(),
                        entry.absoluteImportSourcePath.empty() ? none.c_str() : entry.absoluteImportSourcePath.c_str(), context);
            const std::string reimportState = FormatReimportState(entry, context.localization);
            DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.reimport_status").c_str(),
                        entry.canReimport ? reimportState.c_str() : none.c_str(), context);
            const std::string lastImport = FormatLastImportReasons(entry, context.localization);
            DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.last_import_reason").c_str(),
                        lastImport.empty() ? none.c_str() : lastImport.c_str(), context);
            DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.metadata").c_str(),
                        entry.absoluteMetadataPath.empty() ? none.c_str() : entry.absoluteMetadataPath.c_str(), context);
        }

        void DrawInfoPopup(AssetBrowserInteractionState &state, const EditorGuiContext &context) {
            constexpr const char *popupId = "##ContentBrowserInfo";
            if (state.openInfo) {
                ImGui::OpenPopup(popupId);
                state.openInfo = false;
            }
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            const float popupWidth = std::max(160.0F, std::min(560.0F, viewport->WorkSize.x - 32.0F));
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
            ImGui::SetNextWindowSizeConstraints({popupWidth, 0.0F}, {popupWidth, viewport->WorkSize.y - 48.0F});
            PushModalStyle();
            if (ImGui::BeginPopupModal(popupId, nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                           ImGuiWindowFlags_NoSavedSettings)) {
                DrawModalHeading(context.localization.Get("editor", "workspace.content_browser.info.title"), context);
                if (state.popupEntry.has_value()) {
                    const auto &entry = *state.popupEntry;
                    DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.name").c_str(),
                                entry.displayName.c_str(), context);
                    ImGui::Dummy({0.0F, 2.0F});
                    DrawInfoRow(context.localization.Get("editor", "workspace.content_browser.info.path").c_str(),
                                entry.absolutePath.c_str(), context);
                    if (entry.kind == ContentBrowserEntryKind::Asset) {
                        DrawAssetDetailsRows(entry, context);
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
                    DrawDeleteDialogEntryDetails(*state.popupEntry, context);
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
