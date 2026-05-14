/** @file EditorImportAssetModal.cpp
 *  @brief Dedicated "Import Asset" modal implementation. See EditorImportAssetModal.h.
 *
 *  Uses ImGui to draw the modal when an ImGui context is current; otherwise the
 *  draw call is a no-op so unit tests can exercise state transitions without a
 *  GPU/ImGui setup.
 */
#include "ui/editor/components/EditorImportAssetModal.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

#include <imgui.h>

#include "ui/editor/AssetIdentity.h"
#include "ui/editor/EditorAssetImport.h"
#include "ui/editor/EditorFilePickerUtils.h"

namespace Horo::Editor {
    /** @copydoc EditorImportAssetModal::Open */
    void EditorImportAssetModal::Open(std::string_view initialSourcePath,
                                       const AssetImporterRegistry *registry) {
        m_open = true;
        m_hasPendingRequest = false;
        m_hasResult = false;
        m_lastResult = {};
        m_registry = registry;
        m_assetIdAutoDerived = true;
        m_displayNameAutoDerived = true;
        m_draft = {};
        m_draft.sourcePath = initialSourcePath;
        RefreshImporterFromExtension();
        RefreshIdentitiesFromPath();
    }

    /** @copydoc EditorImportAssetModal::Close */
    void EditorImportAssetModal::Close() {
        m_open = false;
        m_hasPendingRequest = false;
        m_hasResult = false;
        m_lastResult = {};
        m_draft = {};
        m_assetIdAutoDerived = true;
        m_displayNameAutoDerived = true;
    }

    /** @copydoc EditorImportAssetModal::ConsumePendingRequest */
    ImportAssetRequest EditorImportAssetModal::ConsumePendingRequest() {
        m_hasPendingRequest = false;
        return m_draft;
    }

    /** @copydoc EditorImportAssetModal::SetLastResult */
    void EditorImportAssetModal::SetLastResult(const ImportAssetOutcome &outcome) {
        m_lastResult = outcome;
        m_hasResult = true;
        if (outcome.ok && outcome.diagnostics.empty())
            Close();
    }

    /** @copydoc EditorImportAssetModal::SetDraftForTest */
    void EditorImportAssetModal::SetDraftForTest(std::string_view sourcePath,
                                                  std::string_view assetId,
                                                  std::string_view displayName,
                                                  std::string_view importerId) {
        m_draft.sourcePath = sourcePath;
        m_draft.assetId = assetId;
        m_draft.displayName = displayName;
        m_draft.importerId = importerId;
        m_assetIdAutoDerived = false;
        m_displayNameAutoDerived = false;
    }

    /** @copydoc EditorImportAssetModal::RequestImportForTest */
    void EditorImportAssetModal::RequestImportForTest() {
        m_hasPendingRequest = true;
    }

    /** @copydoc EditorImportAssetModal::RefreshImporterFromExtension */
    void EditorImportAssetModal::RefreshImporterFromExtension() {
        if (m_registry == nullptr || m_draft.sourcePath.empty())
            return;
        if (const AssetImporter *imp =
                m_registry->FindByExtension(m_draft.sourcePath);
            imp != nullptr) {
            m_draft.importerId = imp->ImporterId();
        }
    }

    /** @copydoc EditorImportAssetModal::RefreshIdentitiesFromPath */
    void EditorImportAssetModal::RefreshIdentitiesFromPath() {
        if (m_draft.sourcePath.empty())
            return;
        if (m_assetIdAutoDerived) {
            m_draft.assetId = AssetIdFromImportedPath(m_draft.sourcePath);
        }
        if (m_displayNameAutoDerived) {
            m_draft.displayName = m_draft.assetId;
        }
    }

    /** @copydoc EditorImportAssetModal::Draw */
    void EditorImportAssetModal::Draw() {
        if (!m_open)
            return;
        if (ImGui::GetCurrentContext() == nullptr)
            return; // Headless / unit-test context: state is exercised via the test seams.

        constexpr const char *kPopupId = "Import Asset##HoroEditor";
        if (!ImGui::IsPopupOpen(kPopupId))
            ImGui::OpenPopup(kPopupId);

        if (!ImGui::BeginPopupModal(kPopupId, &m_open,
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        DrawPathSection();
        DrawImporterSection();
        DrawIdentitySection();
        if (m_hasResult)
            DrawResultPanel();
        DrawActionsSection();

        ImGui::EndPopup();

        if (!m_open)
            ImGui::CloseCurrentPopup();
    }

    /** @copydoc EditorImportAssetModal::DrawPathSection */
    void EditorImportAssetModal::DrawPathSection() {
        ImGui::TextUnformatted("Source file");
        char pathBuffer[1024]{}; // NOSONAR - ImGui InputText requires mutable char buffer
        const std::size_t copyLen =
            std::min<std::size_t>(m_draft.sourcePath.size(), sizeof(pathBuffer) - 1);
        std::memcpy(pathBuffer, m_draft.sourcePath.data(), copyLen);
        if (ImGui::InputText("##ImportPath", pathBuffer, sizeof(pathBuffer))) {
            m_draft.sourcePath.assign(pathBuffer);
            RefreshImporterFromExtension();
            RefreshIdentitiesFromPath();
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse...##ImportBrowse")) {
            const std::string picked = PickMeshFilePath();
            if (!picked.empty()) {
                m_draft.sourcePath = picked;
                RefreshImporterFromExtension();
                RefreshIdentitiesFromPath();
            }
        }
    }

    /** @copydoc EditorImportAssetModal::DrawImporterSection */
    void EditorImportAssetModal::DrawImporterSection() {
        ImGui::Spacing();
        ImGui::TextUnformatted("Importer");
        std::vector<std::string> importerIds;
        if (m_registry != nullptr)
            importerIds = m_registry->RegisteredImporterIds();
        const char *currentLabel = m_draft.importerId.empty()
                                       ? "(no importer matched extension)"
                                       : m_draft.importerId.c_str();
        if (ImGui::BeginCombo("##ImportImporter", currentLabel)) {
            for (const std::string &id: importerIds) {
                const bool selected = (id == m_draft.importerId);
                if (ImGui::Selectable(id.c_str(), selected))
                    m_draft.importerId = id;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (m_draft.importerId.empty())
            ImGui::TextDisabled("Pick a path with a supported extension (.obj / .fbx / image)");
    }

    /** @copydoc EditorImportAssetModal::DrawIdentitySection */
    void EditorImportAssetModal::DrawIdentitySection() {
        ImGui::Spacing();
        char idBuffer[256]{}; // NOSONAR - ImGui InputText requires mutable char buffer
        const std::size_t idLen =
            std::min<std::size_t>(m_draft.assetId.size(), sizeof(idBuffer) - 1);
        std::memcpy(idBuffer, m_draft.assetId.data(), idLen);
        if (ImGui::InputText("Asset ID##ImportId", idBuffer, sizeof(idBuffer))) {
            m_draft.assetId.assign(idBuffer);
            m_assetIdAutoDerived = false;
        }

        char displayBuffer[256]{}; // NOSONAR - ImGui InputText requires mutable char buffer
        const std::size_t displayLen = std::min<std::size_t>(
            m_draft.displayName.size(), sizeof(displayBuffer) - 1);
        std::memcpy(displayBuffer, m_draft.displayName.data(), displayLen);
        if (ImGui::InputText("Display Name##ImportName", displayBuffer,
                             sizeof(displayBuffer))) {
            m_draft.displayName.assign(displayBuffer);
            m_displayNameAutoDerived = false;
        }
    }

    /** @copydoc EditorImportAssetModal::DrawActionsSection */
    void EditorImportAssetModal::DrawActionsSection() {
        ImGui::Spacing();
        ImGui::Separator();
        const bool canImport = !m_draft.sourcePath.empty() &&
                               !m_draft.assetId.empty() &&
                               !m_draft.importerId.empty();
        ImGui::BeginDisabled(!canImport);
        if (ImGui::Button("Import##ImportConfirm"))
            m_hasPendingRequest = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel##ImportCancel"))
            Close();
    }

    /** @copydoc EditorImportAssetModal::DrawResultPanel */
    void EditorImportAssetModal::DrawResultPanel() const {
        ImGui::Spacing();
        ImGui::Separator();
        if (m_lastResult.ok)
            ImGui::TextUnformatted("Imported successfully.");
        else
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s",
                                m_lastResult.error.empty() ? "(no detail)"
                                                            : m_lastResult.error.c_str());
        for (const AssetImportDiagnostic &d: m_lastResult.diagnostics) {
            ImVec4 colour{0.8f, 0.8f, 1.0f, 1.0f};
            if (d.severity == AssetDiagnosticSeverity::Error)
                colour = {1.0f, 0.4f, 0.4f, 1.0f};
            else if (d.severity == AssetDiagnosticSeverity::Warning)
                colour = {1.0f, 0.8f, 0.3f, 1.0f};
            ImGui::TextColored(colour, "[%s] %s", d.code.c_str(),
                                d.message.c_str());
        }
    }
} // namespace Horo::Editor
