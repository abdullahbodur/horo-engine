/** @file EditorImportAssetModal.h
 *  @brief Two-panel "Import Asset" modal with file browser and import settings.
 *
 *  Left panel: drag-and-drop zone + file browser with thumbnail grid.
 *  Right panel: selected file info + import settings (collision, materials, normals, etc.).
 *
 *  The modal owns a small draft (source path, asset id, display name, importer
 *  selection, import settings) and emits a polled signal back to the caller when
 *  the user clicks Import. The caller is responsible for assigning a stable GUID,
 *  running the import via @ref AssetImportService::ImportAssetFromSource, and
 *  pushing the result back into the modal via @ref SetLastResult so the result
 *  panel can surface diagnostics.
 *
 *  Decoupling import execution from the modal keeps the component testable
 *  without an OpenGL/ImGui context — state transitions and request emission
 *  are exercised in @c test_editor_unit. The actual @c ImGui::* draw path is
 *  guarded by @c ImGui::GetCurrentContext() so calling @c Draw on a Catch2
 *  test that has no ImGui context is a no-op rather than a crash.
 */
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ui/editor/AssetImporterRegistry.h"
#include "ui/editor/AssetImportService.h"
#include "ui/editor/AssetMetadata.h"
#include "ui/editor/components/EditorFileBrowser.h"

namespace Horo::Editor {

/** @brief Snapshot of the import request the modal wants to execute. */
struct ImportAssetRequest {
    std::string sourcePath;   /**< Absolute or project-relative source file path. */
    std::string assetId;      /**< Logical asset id derived from the source path. */
    std::string displayName;  /**< Human-readable display name; defaults to assetId. */
    std::string importerId;   /**< Stable importer id (e.g. @c builtin.fbx_static_mesh). */
    std::optional<TextureOverrides> textureOverrides; /**< Optional texture slot overrides. */
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> settings; /**< Import settings map. */
};

/** @brief Texture slot identifier matching the engine's material slots. */
enum class TextureSlotKind {
    Albedo,
    Normal,
    MetallicRoughness,
    Emissive,
    Occlusion,
    Count
};

/** @brief Returns the display label for a texture slot kind. */
const char *GetTextureSlotLabel(TextureSlotKind slot);

/** @brief Draft state for a single texture slot in the import modal. */
struct TextureSlotDraft {
    TextureSlotKind slot = TextureSlotKind::Albedo;
    std::string path;                    /**< Current path (empty = none). */
    std::string autoDetectedPath;        /**< Auto-detected from FBX; shown as placeholder. */
    bool hasAutoDetected = false;        /**< True when autoDetectedPath is valid. */
    bool userOverride = false;           /**< True when user manually set/overrode the path. */
    bool enabled = false;                /**< True when slot should be used (auto or override). */
};

/** @brief Import settings configured in the right panel. */
struct ImportSettings {
    std::string importType = "static_mesh";       /**< "static_mesh", "skeletal_mesh", "texture". */
    std::string contentLocation;                   /**< Absolute filesystem path for imported assets. */
    bool autoGenerateCollision = true;             /**< Generate collision mesh from imported geometry. */
    bool importMaterials = true;                   /**< Import material definitions. */
    bool importTextures = true;                    /**< Import embedded/linked textures. */
    bool combineMeshes = false;                    /**< Combine multiple meshes into one. */
    bool transformVertexToAbsolute = true;         /**< Transform vertices to world space. */
    std::string normalImportMethod = "import_normals"; /**< "import_normals", "compute_normals", "none". */
    std::string materialImportMethod = "create_new";   /**< "create_new", "reuse_existing", "none". */
    bool removeDegenerates = true;                 /**< Remove degenerate triangles. */
    bool optimizeMesh = true;                      /**< Optimize vertex cache and index buffer. */
    bool importAnimations = false;                 /**< Import animation tracks (disabled for now). */
};

/** @brief Complete draft state for an asset import operation. */
struct ImportAssetDraft {
    std::string sourcePath;                          /**< Source file path (empty for Manual tab). */
    std::string assetId;                             /**< Logical asset identifier. */
    std::string displayName;                         /**< Human-readable display name. */
    std::string importerId;                          /**< Selected importer ID. */
    std::string meshPath;                            /**< Manual tab: mesh path override. */
    std::string renderScale;                         /**< Render scale as "x,y,z". */
    std::array<TextureSlotDraft, 5> textureSlots;    /**< Per-slot texture state. */
    ImportSettings settings;                         /**< Import configuration settings. */
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
     *  @param projectRoot Project root directory for the file browser.
     */
    void Open(std::string_view initialSourcePath,
              const AssetImporterRegistry *registry,
              const std::filesystem::path& projectRoot);

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
    const ImportAssetDraft &DraftForTest() const { return m_draft; }

    /** @brief Test-only: read the most recent result snapshot. */
    const ImportAssetOutcome &LastResultForTest() const { return m_lastResult; }

    /** @brief Handles a file drop on the modal.
     *  @param x Drop X coordinate in screen space.
     *  @param y Drop Y coordinate in screen space.
     *  @param path Dropped file path.
     *  @return True when the drop was accepted and applied.
     */
    bool HandleFileDrop(float x, float y, const std::string &path);

    /** @brief Returns the current modal content rect for hit testing. */
    void SetModalRect(float minX, float minY, float maxX, float maxY) {
        m_modalMinX = minX;
        m_modalMinY = minY;
        m_modalMaxX = maxX;
        m_modalMaxY = maxY;
    }

private:
    /** @brief Modal tab selection. */
    enum class Tab {
        FromFile,
        Manual
    };

    /** @brief Recomputes @c m_draft.importerId from the current @c sourcePath if blank or stale. */
    void RefreshImporterFromExtension();

    /** @brief Recomputes @c m_draft.assetId / @c displayName from the current @c sourcePath when they were auto-derived. */
    void RefreshIdentitiesFromPath();

    /** @brief Seeds texture slots from FBX preview (From File tab only). */
    void RefreshTextureSlotsFromFbxPreview();

    /** @brief Draws the result/diagnostics panel when an import outcome is available. */
    void DrawResultPanel() const;

    /** @brief Draws the From File tab content (file browser + settings). */
    void DrawFromFileTab();

    /** @brief Draws the Manual tab content. */
    void DrawManualTab();

    /** @brief Renders the left panel: drag-drop zone + file browser. */
    void DrawLeftPanel();

    /** @brief Renders the right panel: selected file info + import settings. */
    void DrawRightPanel();

    /** @brief Renders the selected file info section. */
    void DrawSelectedFileInfo() const;

    /** @brief Renders the import settings section. */
    void DrawImportSettings();

    /** @brief Renders the asset id and display name input fields. */
    void DrawIdentitySection();

    /** @brief Renders the mesh path field (Manual tab only). */
    void DrawMeshPathSection();

    /** @brief Renders the render scale fields. */
    void DrawRenderScaleSection();

    /** @brief Renders all five texture slot rows. */
    void DrawTextureSlotsSection();

    /** @brief Renders a single texture slot row. */
    void DrawTextureSlotRow(TextureSlotDraft &slot);

    /** @brief Renders the Import / Cancel buttons; sets @c m_hasPendingRequest on click. */
    void DrawActionsSection();

    /** @brief Loads recent imports from ~/.horo/settings.json. */
    void LoadRecentImports();

    /** @brief Saves a recent import to ~/.horo/settings.json. */
    void SaveRecentImport(std::string_view path);

    /** @brief Clears the recent imports list. */
    void ClearRecentImports();

    /** @brief Builds the settings map from current ImportSettings. */
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> BuildSettingsMap() const;

    Tab m_tab = Tab::FromFile;
    bool m_open = false;
    bool m_hasPendingRequest = false;
    bool m_assetIdAutoDerived = true;
    bool m_displayNameAutoDerived = true;
    bool m_renderScaleAutoDerived = true;
    const AssetImporterRegistry *m_registry = nullptr;
    ImportAssetDraft m_draft;
    ImportAssetOutcome m_lastResult;
    bool m_hasResult = false;
    std::vector<std::string> m_recentImports;  /**< Rolling last 6 imports (per-user). */

    // File browser
    EditorFileBrowser m_fileBrowser;

    // Modal hit testing for drag-and-drop
    float m_modalMinX = 0, m_modalMinY = 0, m_modalMaxX = 0, m_modalMaxY = 0;
};

} // namespace Horo::Editor
