#pragma once

#include "Horo/Editor/ActivityBarLayout.h"
#include "Horo/Editor/EditorMenuModel.h"
#include "Horo/Editor/EditorWorkspaceEvents.h"
#include "Horo/Editor/WorkspacePanelHost.h"
#include "editor/document/SceneDocument.h"
#include "editor/project_model/EditorViewportModel.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Horo::Editor
{
    /** @brief Typed presentation kind projected from authored scene components. */
    enum class SceneObjectKind : std::uint8_t
    {
        Mesh,
        Empty,
        Camera,
        Light,
        TriggerVolume,
        AudioSource,
    };

    /** @brief Read-only presentation projection of one authored scene object. */
    struct SceneObject
    {
        SceneObjectId id;
        std::optional<SceneObjectId> parent;
        std::string name;
        SceneObjectKind kind{SceneObjectKind::Empty};
        Math::Transform localTransform;
    };

    /** @brief Active viewport interaction tool exposed by the workspace toolbar. */
    enum class EditorTransformTool
    {
        Select,
        Move,
        Rotate,
        Scale,
    };

    /** @brief Orientation basis used by viewport transform handles. */
    enum class EditorTransformSpace
    {
        Local,
        World,
    };

    enum class EditorWorkspaceViewCommand
    {
        None,
        ReturnToWelcome,
        SaveScene,
        UndoScene,
        RedoScene,
        CreatePrimitive,
        DuplicateObject,
        DeleteObject,
        SelectObject,
        PickViewport,
        NavigateViewport,
        ChangeViewportProjection,
        FocusViewportSelection,
        ChangeTransformTool,
        ChangeTransformSpace,
        PreviewObjectTransform,
        CommitObjectTransform,
        CancelObjectTransformPreview,
        UpdateObjectTransform,
        UpdateObjectName,
        ChangeActivePanel,
        ReorderActivityBarItem,
        DockWorkspacePanel,
        ResizePanel,
    };

    enum class BottomDockMode
    {
        Full,
        Split,
    };

    enum class BottomDockSlot
    {
        Left,
        Right,
    };

    enum class SideDockMode
    {
        Full,
        Split,
    };

    enum class SideDockSlot
    {
        Top,
        Bottom,
    };

    struct WorkspacePanelDropTarget
    {
        std::string targetNodeId;
        WorkspacePanelHost::DropKind kind = WorkspacePanelHost::DropKind::TabCenter;
    };

    /** @brief Normalized viewport click forwarded to the workspace controller for scene picking. */
    struct ViewportPickRequest
    {
        float normalizedX{0.0F};
        float normalizedY{0.0F};
        float aspect{1.0F};
    };

    struct EditorWorkspaceViewCommandData
    {
        EditorWorkspaceViewCommand command = EditorWorkspaceViewCommand::None;
        std::optional<EditorMenuInvocation> menuInvocation = std::nullopt;
        std::optional<int> targetIndex = std::nullopt;
        std::optional<SceneObjectId> objectPayload = std::nullopt;
        std::optional<Runtime::PrimitiveId> primitivePayload = std::nullopt;
        std::optional<Math::Transform> transformPayload = std::nullopt;
        std::optional<ViewportPickRequest> viewportPickPayload = std::nullopt;
        std::optional<EditorViewportNavigationDelta> viewportNavigationPayload = std::nullopt;
        std::optional<Runtime::CameraProjection> viewportProjectionPayload = std::nullopt;
        std::optional<EditorTransformTool> transformToolPayload = std::nullopt;
        std::optional<EditorTransformSpace> transformSpacePayload = std::nullopt;
        std::optional<std::string> stringPayload = std::nullopt;
        std::optional<float> floatPayload = std::nullopt;
        std::optional<WorkspaceLayoutSize> layoutPayload = std::nullopt;
        std::optional<ActivityBarSlot> activityBarSlot = std::nullopt;
        std::optional<BottomDockSlot> bottomDockSlot = std::nullopt;
        std::optional<SideDockSlot> sideDockSlot = std::nullopt;
        std::optional<WorkspacePanelDropTarget> workspaceDropTarget = std::nullopt;
    };

    struct EditorWorkspaceViewModel
    {
        std::string projectRoot;
        DocumentRevision documentRevision;
        std::vector<SceneObject> objects;
        std::optional<SceneObjectId> hierarchyRevealObject;
        DocumentRevision hierarchyRevealRevision{};
        std::optional<SceneObjectId> primarySelection;
        EditorTransformTool activeTransformTool{EditorTransformTool::Select};
        EditorTransformSpace activeTransformSpace{EditorTransformSpace::Local};
        EditorViewportCamera viewportCamera;
        std::optional<Math::Mat4> primarySelectionWorldTransform;
        std::optional<Math::Mat4> primarySelectionParentWorldTransform;
        std::optional<Math::Aabb> primarySelectionWorldBounds;
        bool isDirty = false;
        bool canUndo = false;
        bool canRedo = false;
        float fps = 0.0F;

        std::string activeLeftPanelId = "horo.hierarchy";
        std::string activeRightPanelId = "horo.inspector";
        std::string activeLeftTopPanelId;
        std::string activeLeftBottomPanelId;
        std::string activeRightTopPanelId;
        std::string activeRightBottomPanelId;
        SideDockMode leftDockMode = SideDockMode::Full;
        SideDockMode rightDockMode = SideDockMode::Full;
        std::string activeBottomPanelId = "horo.content_browser";
        std::string activeBottomLeftPanelId;
        std::string activeBottomRightPanelId;
        BottomDockMode bottomDockMode = BottomDockMode::Full;
        std::string activeDocumentPanelId = "horo.viewport";

        float leftPanelWidth = 230.0F;
        float rightPanelWidth = 260.0F;
        float bottomPanelHeight = 238.0F;

        std::unordered_map<PanelId, WorkspaceDockArea> panelDockAreas;

        ActivityBarLayout activityBarLayout;
        WorkspacePanelHost workspacePanelHost;
    };
} // namespace Horo::Editor
