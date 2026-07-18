#pragma once

#include "Horo/Editor/EditorDataBus.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"
#include "editor/document/EditorViewportSceneExtractor.h"
#include "editor/project_model/EditorSelectionModel.h"
#include "editor/project_model/EditorViewportModel.h"

#include <string>

namespace Horo::Editor
{
    class EditorWorkspaceController
    {
    public:
        EditorWorkspaceController(std::string projectRoot, Runtime::RuntimeSceneService &runtimeScene);
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
        /** @brief Publishes a newly activated runtime scene to the cached viewport snapshot. */
        void SynchronizeRuntimeScenePreview();

    private:
        Runtime::RuntimeSceneService &m_runtimeScene;
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
    };
} // namespace Horo::Editor
