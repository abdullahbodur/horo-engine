#pragma once

#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/ProjectMutation.h"
#include "editor/document/EditorViewportSceneExtractor.h"
#include "editor/document/SceneDocumentComparison.h"
#include "editor/document/SceneDocumentPersistence.h"
#include "editor/document/SceneFileWatchService.h"
#include "editor/project_model/EditorSelectionModel.h"
#include "editor/project_model/EditorViewportModel.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Horo::Editor {
    class EditorWorkspaceController {
    public:
        EditorWorkspaceController(std::string projectRoot, Runtime::RuntimeSceneService &runtimeScene,
                                  const Assets::AssetRegistrySnapshot &assetRegistry = {},
                                  Assets::AssetRegistry *mutableAssetRegistry = nullptr, ProjectMutationCoordinator *mutations = nullptr,
                                  DurableFileSystem *durableFiles = nullptr,
                                  const Assets::AssetImporterCatalogSnapshot *importerCatalog = nullptr, JobSystem *jobs = nullptr);
        ~EditorWorkspaceController() = default;

        [[nodiscard]] const EditorWorkspaceViewModel &ViewModel() const noexcept {
            return m_viewModel;
        }

        [[nodiscard]] EditorDataBus &DataBus() noexcept {
            return m_dataBus;
        }

        /** @brief Returns the current monotonic authoritative selection revision. */
        [[nodiscard]] SelectionRevision CurrentSelectionRevision() const noexcept {
            return m_selection.Current().revision;
        }

        /** @brief Returns the current monotonic authoritative viewport revision. */
        [[nodiscard]] ViewportRevision CurrentViewportRevision() const noexcept {
            return m_viewport.Current().revision;
        }

        /** @brief Returns the latest owning render snapshot extracted from the authoritative document.
         */
        [[nodiscard]] const EditorViewportSceneSnapshot &ViewportScene() const noexcept {
            return m_viewportScene;
        }

        /** @brief Returns the active document's absolute canonical scene path when available. */
        [[nodiscard]] const std::optional<std::filesystem::path> &CurrentScenePath() const noexcept {
            return m_defaultScenePath;
        }

        /** @brief Returns a scene initialization failure that prevents workspace activation. */
        [[nodiscard]] const std::optional<Error> &InitializationError() const noexcept {
            return m_initializationError;
        }

        void ProcessCommand(const EditorWorkspaceViewCommandData &cmd);
        void UpdateFps(float fps);
        /** @brief Replaces the Content Browser projection when a newer registry revision is published.
         */
        void RefreshAssets(const Assets::AssetRegistrySnapshot &assetRegistry);
        /** @brief Advances a requested synchronous Content Browser refresh without blocking the request
         * frame. */
        void UpdateContentBrowser();
        /**
         * @brief Advances dirty-scene autosave scheduling from committed editor policy.
         * @param elapsedSeconds Owner-thread elapsed frame time.
         * @param intervalMinutes Zero disables autosave; positive values schedule recovery writes.
         */
        void UpdateAutosave(float elapsedSeconds, int intervalMinutes);
        /**
         * @brief Schedules and drains non-blocking canonical scene identity inspections.
         * @param elapsedSeconds Owner-thread elapsed frame time.
         */
        void UpdateExternalSceneWatch(float elapsedSeconds);
        /**
         * @brief Captures immutable owner-thread input for a background canonical comparison.
         * @return Absolute canonical location and active document snapshot.
         */
        [[nodiscard]] Result<SceneDocumentComparisonRequest> CaptureExternalSceneComparison() const;
        /** @brief Writes the latest dirty scene to recovery storage at a lifecycle checkpoint. */
        void FlushAutosave();
        /** @brief Publishes a newly activated runtime scene to the cached viewport snapshot. */
        void SynchronizeRuntimeScenePreview();

    private:
        Runtime::RuntimeSceneService &m_runtimeScene;
        Assets::AssetRegistrySnapshot m_assetRegistry;
        Assets::AssetRegistry *m_mutableAssetRegistry{};
        ProjectMutationCoordinator *m_mutations{};
        DurableFileSystem *m_durableFiles{};
        const Assets::AssetImporterCatalogSnapshot *m_importerCatalog{};
        std::optional<std::filesystem::path> m_defaultScenePath;
        std::optional<SceneFileFingerprint> m_sceneFingerprint;
        std::optional<Error> m_initializationError;
        std::unique_ptr<SceneFileWatchService> m_sceneFileWatch;
        EditorWorkspaceViewModel m_viewModel;
        EditorDataBus m_dataBus;
        SceneDocument m_document;
        EditorHistory m_history;
        SceneDocumentCommandExecutor m_documentCommands{m_document, m_history};
        CreateSceneObjectUseCase m_createSceneObject{m_document, m_documentCommands};
        EditorSelectionModel m_selection{m_document, m_dataBus};
        EditorViewportModel m_viewport{m_dataBus};
        Runtime::PrimitiveMeshCache m_primitiveMeshCache;
        EditorViewportSceneSnapshot m_viewportScene;
        std::optional<SceneDocumentSnapshot> m_deferredRuntimeSnapshot;
        std::vector<std::filesystem::path> m_contentBrowserBackHistory;
        std::vector<std::filesystem::path> m_contentBrowserForwardHistory;
        bool m_contentBrowserRefreshPending{false};
        bool m_contentBrowserLoadingPresented{false};
        float m_autosaveElapsedSeconds{0.0F};
        float m_autosaveRetryDelaySeconds{0.0F};
        float m_sceneFileWatchElapsedSeconds{0.0F};
        bool m_sceneFileWatchErrorPresented{false};
        DocumentStateId m_lastAutosavedState{};
        bool m_autosaveSuppressedForDiscard{false};
        DocumentRevision m_queuedRuntimeRevision{};
        Runtime::SceneDefinitionRevision m_activeRuntimeRevision{};
        Runtime::SceneDefinitionRevision m_queuedDefinitionRevision{};
        Runtime::SceneDefinitionId m_previewSceneId{1};

        void RefreshSceneProjections();
        /** @brief Atomically commits the current default-scene snapshot and updates dirty state on
         * success. */
        void SaveScene(bool overwriteConflict = false);
        /** @brief Writes to a selected destination, optionally preserving active document identity. */
        void SaveSceneToPath(const std::filesystem::path &absolutePath, bool copyOnly);
        /** @brief Replaces the active session from the validated external canonical scene. */
        void ReloadExternalScene();
        /** @brief Restores validated recovery content into a new dirty document session. */
        void RestoreSceneRecovery();
        /** @brief Explicitly discards recovery state and suppresses leave-checkpoint autosave. */
        void DiscardSceneRecovery();
        /** @brief Attempts one recovery write when the active document has unsaved content. */
        void WriteAutosaveRecovery();
        void QueueRuntimeScene(SceneDocumentSnapshot snapshot);
        void HandleCreatePrimitive(Runtime::PrimitiveId primitive, std::optional<SceneObjectId> parent);
        void HandleDuplicateObject(SceneObjectId object);
        void HandleDeleteObject(SceneObjectId object);
        void HandleDocumentCommandResult(Result<SceneCommandResult> result, const char *operation);
        void PreviewObjectTransform(SceneObjectId object, const Math::Transform &transform);
        void CancelObjectTransformPreview();
        void RefreshSelectionProjection();
        void NavigateContentBrowser(const std::filesystem::path &absoluteDirectory, bool recordHistory);
        void NavigateContentBrowserBack();
        void NavigateContentBrowserForward();
        void NavigateContentBrowserUp();
        void RenameContentBrowserEntry(const std::string &absolutePath, const std::string &newName);
        void DeleteContentBrowserEntry(const std::string &absolutePath);
        void DuplicateContentBrowserAsset(const std::string &absolutePath);
        void SetContentBrowserClipboard(const std::string &absolutePath, ContentBrowserClipboardMode mode);
        void PasteContentBrowserAsset(const std::string &absoluteDirectory);
        void TransferContentBrowserAsset(const ContentBrowserAssetTransferRequest &request);
        void CreateContentBrowserFolder(const std::string &absoluteDirectory, const std::string &name);
        void ReimportContentBrowserAsset(const std::string &absolutePath);
        void RevealContentBrowserEntry(const std::string &absolutePath);
        [[nodiscard]] bool CopyContentBrowserAssetTo(const std::filesystem::path &absoluteSource,
                                                     const std::filesystem::path &absoluteDestinationDirectory);
        [[nodiscard]] bool MoveContentBrowserAssetTo(const std::filesystem::path &absoluteSource,
                                                     const std::filesystem::path &absoluteDestinationDirectory);
        void ClearContentBrowserClipboard() noexcept;
        void RefreshContentBrowserAfterMutation();
        void RequestContentBrowserRefresh();
        void ReconcileContentBrowserNavigation();
    };
}  // namespace Horo::Editor
