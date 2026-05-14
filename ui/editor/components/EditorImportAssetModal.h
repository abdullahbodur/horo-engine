/** @file EditorImportAssetModal.h
 *  @brief Dedicated "Import Asset" modal that drives @ref AssetImportService through the importer registry.
 *
 *  The modal owns a small draft (source path, asset id, display name, importer
 *  selection) and emits a polled signal back to the caller when the user clicks
 *  Import. The caller is responsible for assigning a stable GUID, running the
 *  import via @ref AssetImportService::ImportAssetFromSource, and pushing the
 *  result back into the modal via @ref SetLastResult so the result panel can
 *  surface diagnostics.
 *
 *  Decoupling import execution from the modal keeps the component testable
 *  without an OpenGL/ImGui context — state transitions and request emission
 *  are exercised in @c test_editor_unit. The actual @c ImGui::* draw path is
 *  guarded by @c ImGui::GetCurrentContext() so calling @c Draw on a Catch2
 *  test that has no ImGui context is a no-op rather than a crash.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ui/editor/AssetImporterRegistry.h"
#include "ui/editor/AssetMetadata.h"

namespace Horo::Editor {
    /** @brief Snapshot of the import request the modal wants to execute. */
    struct ImportAssetRequest {
        std::string sourcePath;  /**< Absolute or project-relative source file path. */
        std::string assetId;     /**< Logical asset id derived from the source path. */
        std::string displayName; /**< Human-readable display name; defaults to assetId. */
        std::string importerId;  /**< Stable importer id (e.g. @c builtin.fbx_static_mesh) the user picked. */
    };

    /** @brief Polled outcome the modal renders after the caller drives an import.
     *
     *  The modal does not run the importer itself; the caller calls
     *  @ref SetLastResult after @ref AssetImportService::ImportAssetFromSource
     *  returns. A populated @c error string opens the result panel; an empty
     *  @c error with non-empty @c diagnostics still opens the panel to show
     *  warnings. Successful imports with no diagnostics close the modal.
     */
    struct ImportAssetOutcome {
        bool ok = false;                                /**< True on success. */
        std::string error;                              /**< Top-level error text. */
        std::string assetMesh;                          /**< Echoed @c AssetDef::mesh on success. */
        std::string assetAlbedoMap;                     /**< Echoed @c AssetDef::albedoMap on success. */
        std::vector<AssetImportDiagnostic> diagnostics; /**< Warnings / info diagnostics from the importer. */
    };

    /** @brief Modal component for importing a single asset through the importer registry. */
    class EditorImportAssetModal {
    public:
        /** @brief Returns true while the modal should be rendered. */
        bool IsOpen() const { return m_open; }

        /** @brief Opens the modal with a fresh draft seeded from @p initialSourcePath.
         *  @param initialSourcePath Absolute path to seed; pass an empty string to start blank.
         *  @param registry Non-owning pointer to the importer registry; must outlive this object.
         */
        void Open(std::string_view initialSourcePath,
                  const AssetImporterRegistry *registry);

        /** @brief Closes the modal and clears any pending request / outcome state. */
        void Close();

        /** @brief Draws the modal for one frame; safe to call when @c IsOpen() is false. */
        void Draw();

        /** @brief Returns true when the user has clicked Import and the caller should run the importer.
         *
         *  Callers should poll this every frame; on @c true, call @ref ConsumePendingRequest to
         *  obtain the request and clear the flag, run the importer, then call @ref SetLastResult.
         */
        bool HasPendingRequest() const { return m_hasPendingRequest; }

        /** @brief Returns the pending request and clears the flag.
         *  @pre @ref HasPendingRequest must return true.
         */
        ImportAssetRequest ConsumePendingRequest();

        /** @brief Records the outcome of the most recent import for the result panel.
         *
         *  When @c outcome.ok is true and the importer emitted no diagnostics, the modal
         *  closes itself. Otherwise the result panel stays open so the user can inspect
         *  diagnostics without re-issuing the file picker.
         */
        void SetLastResult(const ImportAssetOutcome &outcome);

        // ---- Test seam ---------------------------------------------------------

        /** @brief Test-only: directly seed the draft state without opening a file picker. */
        void SetDraftForTest(std::string_view sourcePath,
                              std::string_view assetId,
                              std::string_view displayName,
                              std::string_view importerId);

        /** @brief Test-only: emulate the user clicking Import. */
        void RequestImportForTest();

        /** @brief Test-only: read the current draft. */
        const ImportAssetRequest &DraftForTest() const { return m_draft; }

        /** @brief Test-only: read the most recent result snapshot. */
        const ImportAssetOutcome &LastResultForTest() const { return m_lastResult; }

    private:
        /** @brief Recomputes @c m_draft.importerId from the current @c sourcePath if blank or stale. */
        void RefreshImporterFromExtension();

        /** @brief Recomputes @c m_draft.assetId / @c displayName from the current @c sourcePath when they were auto-derived. */
        void RefreshIdentitiesFromPath();

        /** @brief Draws the result/diagnostics panel when an import outcome is available. */
        void DrawResultPanel() const;

        /** @brief Renders the path field + Browse button; updates draft + auto-derived state. */
        void DrawPathSection();

        /** @brief Renders the importer dropdown for the current registry / draft. */
        void DrawImporterSection();

        /** @brief Renders the asset id and display name input fields. */
        void DrawIdentitySection();

        /** @brief Renders the Import / Cancel buttons; sets @c m_hasPendingRequest on click. */
        void DrawActionsSection();

        bool m_open = false;
        bool m_hasPendingRequest = false;
        bool m_assetIdAutoDerived = true;
        bool m_displayNameAutoDerived = true;
        const AssetImporterRegistry *m_registry = nullptr;
        ImportAssetRequest m_draft;
        ImportAssetOutcome m_lastResult;
        bool m_hasResult = false;
    };
} // namespace Horo::Editor
