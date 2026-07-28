#pragma once

#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/ProjectMutation.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"
#include "editor/document/EditorViewportSceneExtractor.h"
#include "editor/project_model/EditorSelectionModel.h"
#include "editor/project_model/EditorViewportModel.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Horo::Editor
{
    class EditorWorkspaceController
    {
    public:
        EditorWorkspaceController(std::string projectRoot, Runtime::RuntimeSceneService &runtimeScene,
                                  const Assets::AssetRegistrySnapshot& assetRegistry = {},
                                  Assets::AssetRegistry* mutableAssetRegistry = nullptr,
                                  ProjectMutationCoordinator* mutations = nullptr,
                                  DurableFileSystem* durableFiles = nullptr,
                                  const Assets::AssetImporterCatalogSnapshot* importerCatalog = nullptr);
        ~EditorWorkspaceController() = default;

        [[nodiscard]] const EditorWorkspaceViewModel& ViewModel() const noexcept
        {
            return m_viewModel;
        }

        [[nodiscard]] EditorDataBus& DataBus() noexcept
        {
            return m_dataBus;
        }

        /** @brief Returns the current monotonic authoritative selection revision. */
        [[nodiscard]] SelectionRevision CurrentSelectionRevision() const noexcept
        {
            return m_selection.Current().revision;
        }

        /** @brief Returns the current monotonic authoritative viewport revision. */
        [[nodiscard]] ViewportRevision CurrentViewportRevision() const noexcept
        {
            return m_viewport.Current().revision;
        }

        /** @brief Returns the latest owning render snapshot extracted from the authoritative document. */
        [[nodiscard]] const EditorViewportSceneSnapshot& ViewportScene() const noexcept
        {
            return m_viewportScene;
        }

        void ProcessCommand(const EditorWorkspaceViewCommandData& cmd);
        void UpdateFps(float fps);
        /** @brief Replaces the Content Browser projection when a newer registry revision is published. */
        void RefreshAssets(const Assets::AssetRegistrySnapshot& assetRegistry);
        /** @brief Advances a requested synchronous Content Browser refresh without blocking the request frame. */
        void UpdateContentBrowser();
        /** @brief Publishes a newly activated runtime scene to the cached viewport snapshot. */
        void SynchronizeRuntimeScenePreview();

    private:
        Runtime::RuntimeSceneService &m_runtimeScene;
        Assets::AssetRegistrySnapshot m_assetRegistry;
        Assets::AssetRegistry* m_mutableAssetRegistry{};
        ProjectMutationCoordinator* m_mutations{};
        DurableFileSystem* m_durableFiles{};
        const Assets::AssetImporterCatalogSnapshot* m_importerCatalog{};
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
        DocumentRevision m_queuedRuntimeRevision{};
        Runtime::SceneDefinitionRevision m_activeRuntimeRevision{};
        Runtime::SceneDefinitionRevision m_queuedDefinitionRevision{};
        Runtime::SceneDefinitionId m_previewSceneId{1};

        void RefreshSceneProjections();
        void QueueRuntimeScene(SceneDocumentSnapshot snapshot);
        void HandleCreatePrimitive(Runtime::PrimitiveId primitive, std::optional<SceneObjectId> parent);
        void HandleDuplicateObject(SceneObjectId object);
        void HandleDeleteObject(SceneObjectId object);
        void HandleDocumentCommandResult(Result<SceneCommandResult> result, const char* operation);
        void PreviewObjectTransform(SceneObjectId object, const Math::Transform& transform);
        void CancelObjectTransformPreview();
        void RefreshSelectionProjection();
        void NavigateContentBrowser(const std::filesystem::path& absoluteDirectory, bool recordHistory);
        void NavigateContentBrowserBack();
        void NavigateContentBrowserForward();
        void NavigateContentBrowserUp();
        void RenameContentBrowserEntry(const std::string& absolutePath, const std::string& newName);
        void DeleteContentBrowserEntry(const std::string& absolutePath);
        void DuplicateContentBrowserAsset(const std::string& absolutePath);
        void SetContentBrowserClipboard(
            const std::string& absolutePath, ContentBrowserClipboardMode mode);
        void PasteContentBrowserAsset(const std::string& absoluteDirectory);
        void TransferContentBrowserAsset(
            const ContentBrowserAssetTransferRequest& request);
        void CreateContentBrowserFolder(
            const std::string& absoluteDirectory, const std::string& name);
        void ReimportContentBrowserAsset(const std::string& absolutePath);
        void RevealContentBrowserEntry(const std::string& absolutePath);
        [[nodiscard]] bool CopyContentBrowserAssetTo(
            const std::filesystem::path& absoluteSource,
            const std::filesystem::path& absoluteDestinationDirectory);
        [[nodiscard]] bool MoveContentBrowserAssetTo(
            const std::filesystem::path& absoluteSource,
            const std::filesystem::path& absoluteDestinationDirectory);
        void ClearContentBrowserClipboard() noexcept;
        void RefreshContentBrowserAfterMutation();
        void RequestContentBrowserRefresh();
        void ReconcileContentBrowserNavigation();
    };
} // namespace Horo::Editor
