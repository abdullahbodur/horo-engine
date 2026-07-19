#include "editor/screens/workspace/EditorWorkspaceController.h"
#include "Horo/Editor/EditorWorkspaceEvents.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "editor/document/EditorViewportPicking.h"
#include "editor/document/RuntimeSceneConversion.h"

namespace Horo::Editor
{
    namespace
    {
        [[nodiscard]] bool TryGetDockArea(const int value, WorkspaceDockArea& area) noexcept
        {
            switch (value)
            {
            case 0:
                area = WorkspaceDockArea::Left;
                return true;
            case 1:
                area = WorkspaceDockArea::Right;
                return true;
            case 2:
                area = WorkspaceDockArea::Bottom;
                return true;
            case 3:
                area = WorkspaceDockArea::Document;
                return true;
            default:
                return false;
            }
        }

        void NormalizeSideDock(SideDockMode& mode, std::string& fullPanel, std::string& topPanel,
                               std::string& bottomPanel)
        {
            if (mode != SideDockMode::Split || (!topPanel.empty() && !bottomPanel.empty()))
            {
                return;
            }

            fullPanel = topPanel.empty() ? std::move(bottomPanel) : std::move(topPanel);
            topPanel.clear();
            bottomPanel.clear();
            mode = SideDockMode::Full;
        }

        void ActivateSideDock(SideDockMode& mode, std::string& fullPanel, std::string& topPanel,
                              std::string& bottomPanel,
                              const std::optional<SideDockSlot> targetSlot, const std::string& panelId,
                              std::vector<std::string>& displacedPanelIds)
        {
            if (targetSlot.has_value() && !panelId.empty())
            {
                const std::string previousFull = fullPanel;
                if (mode == SideDockMode::Full)
                {
                    fullPanel.clear();
                    topPanel.clear();
                    bottomPanel.clear();
                    mode = SideDockMode::Split;
                    if (*targetSlot == SideDockSlot::Top)
                    {
                        topPanel = panelId;
                        if (previousFull != panelId)
                            bottomPanel = previousFull;
                    }
                    else
                    {
                        if (previousFull != panelId)
                            topPanel = previousFull;
                        bottomPanel = panelId;
                    }
                    return;
                }

                if (topPanel == panelId)
                    topPanel.clear();
                if (bottomPanel == panelId)
                    bottomPanel.clear();
                std::string& targetPanel = *targetSlot == SideDockSlot::Top ? topPanel : bottomPanel;
                displacedPanelIds.push_back(targetPanel);
                targetPanel = panelId;
                return;
            }

            displacedPanelIds.push_back(fullPanel);
            displacedPanelIds.push_back(topPanel);
            displacedPanelIds.push_back(bottomPanel);
            mode = SideDockMode::Full;
            topPanel.clear();
            bottomPanel.clear();
            fullPanel = panelId;
        }

        void NormalizeBottomDock(EditorWorkspaceViewModel& viewModel)
        {
            if (viewModel.bottomDockMode != BottomDockMode::Split ||
                (!viewModel.activeBottomLeftPanelId.empty() && !viewModel.activeBottomRightPanelId.empty()))
            {
                return;
            }

            viewModel.activeBottomPanelId = viewModel.activeBottomLeftPanelId.empty()
                                                ? std::move(viewModel.activeBottomRightPanelId)
                                                : std::move(viewModel.activeBottomLeftPanelId);
            viewModel.activeBottomLeftPanelId.clear();
            viewModel.activeBottomRightPanelId.clear();
            viewModel.bottomDockMode = BottomDockMode::Full;
        }

        void NormalizeDocks(EditorWorkspaceViewModel& viewModel)
        {
            NormalizeSideDock(viewModel.leftDockMode, viewModel.activeLeftPanelId, viewModel.activeLeftTopPanelId,
                              viewModel.activeLeftBottomPanelId);
            NormalizeSideDock(viewModel.rightDockMode, viewModel.activeRightPanelId, viewModel.activeRightTopPanelId,
                              viewModel.activeRightBottomPanelId);
            NormalizeBottomDock(viewModel);
        }

        struct ActivityLayoutRegion
        {
            WorkspaceDockArea area = WorkspaceDockArea::Document;
            std::optional<SideDockSlot> sideSlot;
            std::optional<BottomDockSlot> bottomSlot;

            friend bool operator==(const ActivityLayoutRegion&, const ActivityLayoutRegion&) = default;
        };

        [[nodiscard]] std::optional<ActivityLayoutRegion> RegionForActivitySlot(const ActivityBarSlot slot)
        {
            if (slot.rail == ActivityBarRail::DocumentTop && slot.groupIndex == 0)
            {
                return ActivityLayoutRegion{WorkspaceDockArea::Document, std::nullopt, std::nullopt};
            }
            if (slot.rail == ActivityBarRail::Left)
            {
                switch (slot.groupIndex)
                {
                case 0:
                    return ActivityLayoutRegion{WorkspaceDockArea::Left, SideDockSlot::Top, std::nullopt};
                case 1:
                    return ActivityLayoutRegion{WorkspaceDockArea::Left, SideDockSlot::Bottom, std::nullopt};
                case 2:
                    return ActivityLayoutRegion{WorkspaceDockArea::Bottom, std::nullopt, BottomDockSlot::Left};
                default:
                    return std::nullopt;
                }
            }
            if (slot.rail == ActivityBarRail::Right)
            {
                switch (slot.groupIndex)
                {
                case 0:
                    return ActivityLayoutRegion{WorkspaceDockArea::Right, SideDockSlot::Top, std::nullopt};
                case 1:
                    return ActivityLayoutRegion{WorkspaceDockArea::Right, SideDockSlot::Bottom, std::nullopt};
                case 2:
                    return ActivityLayoutRegion{WorkspaceDockArea::Bottom, std::nullopt, BottomDockSlot::Right};
                default:
                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] bool IsPanelActiveInRegion(const EditorWorkspaceViewModel& viewModel,
                                                 const std::string_view panelId,
                                                 const ActivityLayoutRegion& region)
        {
            switch (region.area)
            {
            case WorkspaceDockArea::Left:
                if (viewModel.leftDockMode == SideDockMode::Full)
                {
                    return viewModel.activeLeftPanelId == panelId;
                }
                return region.sideSlot == SideDockSlot::Top
                           ? viewModel.activeLeftTopPanelId == panelId
                           : viewModel.activeLeftBottomPanelId == panelId;
            case WorkspaceDockArea::Right:
                if (viewModel.rightDockMode == SideDockMode::Full)
                {
                    return viewModel.activeRightPanelId == panelId;
                }
                return region.sideSlot == SideDockSlot::Top
                           ? viewModel.activeRightTopPanelId == panelId
                           : viewModel.activeRightBottomPanelId == panelId;
            case WorkspaceDockArea::Bottom:
                if (viewModel.bottomDockMode == BottomDockMode::Full)
                {
                    return viewModel.activeBottomPanelId == panelId;
                }
                return region.bottomSlot == BottomDockSlot::Left
                           ? viewModel.activeBottomLeftPanelId == panelId
                           : viewModel.activeBottomRightPanelId == panelId;
            case WorkspaceDockArea::Document:
                return viewModel.activeDocumentPanelId == panelId;
            }
            return false;
        }

        [[nodiscard]] EditorWorkspaceViewCommandData MakeRegionActivationCommand(const std::string_view panelId,
            const ActivityLayoutRegion& region)
        {
            EditorWorkspaceViewCommandData command;
            command.command = EditorWorkspaceViewCommand::ChangeActivePanel;
            command.targetIndex = static_cast<int>(region.area);
            command.stringPayload = std::string(panelId);
            command.sideDockSlot = region.sideSlot;
            command.bottomDockSlot = region.bottomSlot;
            return command;
        }
    } // namespace

    EditorWorkspaceController::EditorWorkspaceController(std::string projectRoot, Runtime::RuntimeSceneService &runtimeScene)
        : m_runtimeScene(runtimeScene)
    {
        m_viewModel.projectRoot = std::move(projectRoot);
        m_viewModel.panelDockAreas = {
            {"horo.hierarchy", WorkspaceDockArea::Left},
            {"horo.viewport", WorkspaceDockArea::Document},
            {"horo.content_browser", WorkspaceDockArea::Bottom},
            {"horo.inspector", WorkspaceDockArea::Right},
            {"horo.input_mapping", WorkspaceDockArea::Right}
        };
        static_cast<void>(
            m_viewModel.activityBarLayout.Insert("horo.hierarchy", ActivityBarSlot{ActivityBarRail::Left, 0, 0}));
        static_cast<void>(
            m_viewModel.activityBarLayout.Insert("horo.viewport", ActivityBarSlot{ActivityBarRail::DocumentTop, 0, 0}));
        static_cast<void>(
            m_viewModel.activityBarLayout.Insert("horo.content_browser", ActivityBarSlot{ActivityBarRail::Left, 2, 0}));
        static_cast<void>(
            m_viewModel.activityBarLayout.Insert("horo.inspector", ActivityBarSlot{ActivityBarRail::Right, 0, 0}));
        static_cast<void>(
            m_viewModel.activityBarLayout.Insert("horo.input_mapping", ActivityBarSlot{ActivityBarRail::Right, 1, 0}));

        const Math::Quaternion pitch = Math::Quaternion::FromAxisAngle({1.0F, 0.0F, 0.0F}, -0.42F);
        const Math::Quaternion yaw = Math::Quaternion::FromAxisAngle({0.0F, 1.0F, 0.0F}, 0.55F);
        const Result<SceneCommandResult> created = m_documentCommands.Execute(CreateSceneObjectCommand{
            .name = "Box",
            .localTransform = Math::Transform{.rotation = pitch * yaw},
            .primitiveMesh = PrimitiveMeshDescriptor{},
        });
        if (created.HasError())
        {
            LOG_ERROR("editor.scene_document", "Bootstrap scene creation failed: %s",
                      created.ErrorValue().message.c_str());
        }
        else
        {
            static_cast<void>(m_document.MarkSaved(m_document.Revision(), m_document.State()));
            m_history.Clear();
        }
        RefreshSceneProjections();
    }

    void EditorWorkspaceController::UpdateFps(const float fps)
    {
        m_viewModel.fps = fps;
    }

    void EditorWorkspaceController::ProcessCommand(const EditorWorkspaceViewCommandData& cmd)
    {
        switch (cmd.command)
        {
        case EditorWorkspaceViewCommand::None:
        case EditorWorkspaceViewCommand::ReturnToWelcome:
            break;
        case EditorWorkspaceViewCommand::SaveScene:
            static_cast<void>(m_document.MarkSaved(m_document.Revision(), m_document.State()));
            m_dataBus.Publish(SceneDocumentChangedEvent{
                m_document.Revision(), m_document.State(), DocumentChangeKind::SaveStateChanged, m_document.IsDirty(),
                {}
            });
            RefreshSceneProjections();
            break;
        case EditorWorkspaceViewCommand::UndoScene:
            HandleDocumentCommandResult(m_documentCommands.Undo(), "Undo");
            break;
        case EditorWorkspaceViewCommand::RedoScene:
            HandleDocumentCommandResult(m_documentCommands.Redo(), "Redo");
            break;
        case EditorWorkspaceViewCommand::CreatePrimitive:
            if (cmd.primitivePayload.has_value())
            {
                HandleCreatePrimitive(*cmd.primitivePayload, cmd.objectPayload);
            }
            break;
        case EditorWorkspaceViewCommand::DuplicateObject:
            if (cmd.objectPayload.has_value())
                HandleDuplicateObject(*cmd.objectPayload);
            break;
        case EditorWorkspaceViewCommand::DeleteObject:
            if (cmd.objectPayload.has_value())
                HandleDeleteObject(*cmd.objectPayload);
            break;
        case EditorWorkspaceViewCommand::SelectObject:
            if (cmd.objectPayload.has_value())
            {
                const Result<void> selected = m_selection.SetObjects({*cmd.objectPayload}, *cmd.objectPayload);
                if (selected.HasError())
                {
                    LOG_ERROR("editor.selection", "Select object failed: %s", selected.ErrorValue().message.c_str());
                }
                RefreshSelectionProjection();
            }
            break;
        case EditorWorkspaceViewCommand::PickViewport:
            if (cmd.viewportPickPayload.has_value())
            {
                const ViewportPickRequest& request = *cmd.viewportPickPayload;
                const Result<EditorViewportPickResult> picked = PickEditorViewportScene(
                    m_viewportScene, EditorViewportPickQuery{request.normalizedX, request.normalizedY, request.aspect});
                if (picked.HasError())
                {
                    LOG_ERROR("editor.viewport_picking", "Viewport pick failed: %s",
                              picked.ErrorValue().message.c_str());
                    break;
                }
                const std::optional<Runtime::RuntimeSceneView> active = m_runtimeScene.ActiveScene();
                if (!active || picked.Value().runtimeScene != active->RuntimeId())
                {
                    LOG_WARN("editor.viewport_picking", "Discarded a stale runtime-scene pick result.");
                    break;
                }
                if (picked.Value().object)
                {
                    const SceneObjectId object = *picked.Value().object;
                    const Result<void> selected = m_selection.SetObjects({object}, object);
                    if (selected.HasError())
                        LOG_ERROR("editor.selection", "Viewport selection failed: %s",
                                  selected.ErrorValue().message.c_str());
                }
                else
                    m_selection.Clear();
                RefreshSelectionProjection();
            }
            break;
        case EditorWorkspaceViewCommand::NavigateViewport:
            if (cmd.viewportNavigationPayload.has_value())
            {
                const Result<void> navigated = m_viewport.Navigate(*cmd.viewportNavigationPayload);
                if (navigated.HasError())
                {
                    LOG_ERROR("editor.viewport", "Viewport navigation failed: %s",
                              navigated.ErrorValue().message.c_str());
                }
                else
                {
                    m_viewportScene.camera = m_viewport.Current().camera;
                    m_viewModel.viewportCamera = m_viewport.Current().camera;
                }
            }
            break;
        case EditorWorkspaceViewCommand::ChangeViewportProjection:
            if (cmd.viewportProjectionPayload.has_value())
            {
                const Result<void> changed = m_viewport.SetProjection(*cmd.viewportProjectionPayload);
                if (changed.HasError())
                LOG_ERROR("editor.viewport", "Viewport projection change failed: %s",
                          changed.ErrorValue().message.c_str());
                else
                {
                    m_viewportScene.camera = m_viewport.Current().camera;
                    m_viewModel.viewportCamera = m_viewport.Current().camera;
                }
            }
            break;
        case EditorWorkspaceViewCommand::FocusViewportSelection:
            if (m_viewModel.primarySelectionWorldBounds.has_value() && cmd.floatPayload.has_value())
            {
                const Result<void> focused = m_viewport.Focus(*m_viewModel.primarySelectionWorldBounds,
                                                              *cmd.floatPayload);
                if (focused.HasError())
                    LOG_ERROR("editor.viewport", "Viewport focus failed: %s", focused.ErrorValue().message.c_str());
                else
                {
                    m_viewportScene.camera = m_viewport.Current().camera;
                    m_viewModel.viewportCamera = m_viewport.Current().camera;
                }
            }
            break;
        case EditorWorkspaceViewCommand::ChangeTransformTool:
            if (cmd.transformToolPayload.has_value())
            {
                m_viewModel.activeTransformTool = *cmd.transformToolPayload;
            }
            break;
        case EditorWorkspaceViewCommand::ChangeTransformSpace:
            if (cmd.transformSpacePayload.has_value())
            {
                m_viewModel.activeTransformSpace = *cmd.transformSpacePayload;
            }
            break;
        case EditorWorkspaceViewCommand::PreviewObjectTransform:
            if (cmd.objectPayload.has_value() && cmd.transformPayload.has_value())
            {
                PreviewObjectTransform(*cmd.objectPayload, *cmd.transformPayload);
            }
            break;
        case EditorWorkspaceViewCommand::CommitObjectTransform:
            if (cmd.objectPayload.has_value() && cmd.transformPayload.has_value())
            {
                HandleDocumentCommandResult(
                    m_documentCommands.Execute(
                        SetSceneObjectTransformCommand{*cmd.objectPayload, *cmd.transformPayload}),
                    "Transform object");
            }
            break;
        case EditorWorkspaceViewCommand::CancelObjectTransformPreview:
            CancelObjectTransformPreview();
            break;
        case EditorWorkspaceViewCommand::UpdateObjectTransform:
            if (cmd.objectPayload.has_value() && cmd.transformPayload.has_value())
            {
                HandleDocumentCommandResult(
                    m_documentCommands.Execute(
                        SetSceneObjectTransformCommand{*cmd.objectPayload, *cmd.transformPayload}),
                    "Transform object");
            }
            break;
        case EditorWorkspaceViewCommand::UpdateObjectName:
            if (cmd.objectPayload.has_value() && cmd.stringPayload.has_value())
            {
                HandleDocumentCommandResult(
                    m_documentCommands.Execute(RenameSceneObjectCommand{*cmd.objectPayload, *cmd.stringPayload}),
                    "Rename object");
            }
            break;
        case EditorWorkspaceViewCommand::ChangeActivePanel:
            if (cmd.targetIndex.has_value() && cmd.stringPayload.has_value())
            {
                WorkspaceDockArea area{};
                if (!TryGetDockArea(*cmd.targetIndex, area))
                    break;
                const bool panelWasActive =
                    !cmd.stringPayload->empty() && (*cmd.stringPayload == m_viewModel.activeLeftPanelId ||
                        *cmd.stringPayload == m_viewModel.activeRightPanelId ||
                        *cmd.stringPayload == m_viewModel.activeLeftTopPanelId ||
                        *cmd.stringPayload == m_viewModel.activeLeftBottomPanelId ||
                        *cmd.stringPayload == m_viewModel.activeRightTopPanelId ||
                        *cmd.stringPayload == m_viewModel.activeRightBottomPanelId ||
                        *cmd.stringPayload == m_viewModel.activeBottomPanelId ||
                        *cmd.stringPayload == m_viewModel.activeBottomLeftPanelId ||
                        *cmd.stringPayload == m_viewModel.activeBottomRightPanelId ||
                        *cmd.stringPayload == m_viewModel.activeDocumentPanelId);
                std::vector<std::string> displacedPanelIds;
                if (!cmd.stringPayload->empty())
                {
                    const auto previousPlacement = m_viewModel.panelDockAreas.find(*cmd.stringPayload);
                    if (previousPlacement != m_viewModel.panelDockAreas.end() && previousPlacement->second != area)
                    {
                        switch (previousPlacement->second)
                        {
                        case WorkspaceDockArea::Left:
                            if (m_viewModel.activeLeftPanelId == *cmd.stringPayload)
                                m_viewModel.activeLeftPanelId.clear();
                            if (m_viewModel.activeLeftTopPanelId == *cmd.stringPayload)
                                m_viewModel.activeLeftTopPanelId.clear();
                            if (m_viewModel.activeLeftBottomPanelId == *cmd.stringPayload)
                                m_viewModel.activeLeftBottomPanelId.clear();
                            break;
                        case WorkspaceDockArea::Right:
                            if (m_viewModel.activeRightPanelId == *cmd.stringPayload)
                                m_viewModel.activeRightPanelId.clear();
                            if (m_viewModel.activeRightTopPanelId == *cmd.stringPayload)
                                m_viewModel.activeRightTopPanelId.clear();
                            if (m_viewModel.activeRightBottomPanelId == *cmd.stringPayload)
                                m_viewModel.activeRightBottomPanelId.clear();
                            break;
                        case WorkspaceDockArea::Bottom:
                            if (m_viewModel.activeBottomPanelId == *cmd.stringPayload)
                                m_viewModel.activeBottomPanelId.clear();
                            if (m_viewModel.activeBottomLeftPanelId == *cmd.stringPayload)
                                m_viewModel.activeBottomLeftPanelId.clear();
                            if (m_viewModel.activeBottomRightPanelId == *cmd.stringPayload)
                                m_viewModel.activeBottomRightPanelId.clear();
                            break;
                        case WorkspaceDockArea::Document:
                            if (m_viewModel.activeDocumentPanelId == *cmd.stringPayload)
                                m_viewModel.activeDocumentPanelId.clear();
                            break;
                        }
                        NormalizeDocks(m_viewModel);
                    }
                    m_viewModel.panelDockAreas[*cmd.stringPayload] = area;
                }

                switch (area)
                {
                case WorkspaceDockArea::Left:
                    ActivateSideDock(m_viewModel.leftDockMode, m_viewModel.activeLeftPanelId,
                                     m_viewModel.activeLeftTopPanelId, m_viewModel.activeLeftBottomPanelId,
                                     cmd.sideDockSlot, *cmd.stringPayload, displacedPanelIds);
                    break;
                case WorkspaceDockArea::Right:
                    ActivateSideDock(m_viewModel.rightDockMode, m_viewModel.activeRightPanelId,
                                     m_viewModel.activeRightTopPanelId, m_viewModel.activeRightBottomPanelId,
                                     cmd.sideDockSlot, *cmd.stringPayload, displacedPanelIds);
                    break;
                case WorkspaceDockArea::Bottom:
                    if (cmd.bottomDockSlot.has_value() && !cmd.stringPayload->empty())
                    {
                        const std::string previousFull = m_viewModel.activeBottomPanelId;
                        if (m_viewModel.bottomDockMode == BottomDockMode::Full)
                        {
                            m_viewModel.activeBottomPanelId.clear();
                            m_viewModel.activeBottomLeftPanelId.clear();
                            m_viewModel.activeBottomRightPanelId.clear();
                            m_viewModel.bottomDockMode = BottomDockMode::Split;
                            if (*cmd.bottomDockSlot == BottomDockSlot::Left)
                            {
                                m_viewModel.activeBottomLeftPanelId = *cmd.stringPayload;
                                if (previousFull != *cmd.stringPayload)
                                    m_viewModel.activeBottomRightPanelId = previousFull;
                            }
                            else
                            {
                                if (previousFull != *cmd.stringPayload)
                                    m_viewModel.activeBottomLeftPanelId = previousFull;
                                m_viewModel.activeBottomRightPanelId = *cmd.stringPayload;
                            }
                        }
                        else
                        {
                            if (m_viewModel.activeBottomLeftPanelId == *cmd.stringPayload)
                                m_viewModel.activeBottomLeftPanelId.clear();
                            if (m_viewModel.activeBottomRightPanelId == *cmd.stringPayload)
                                m_viewModel.activeBottomRightPanelId.clear();

                            std::string& targetPanel = *cmd.bottomDockSlot == BottomDockSlot::Left
                                                           ? m_viewModel.activeBottomLeftPanelId
                                                           : m_viewModel.activeBottomRightPanelId;
                            displacedPanelIds.push_back(targetPanel);
                            targetPanel = *cmd.stringPayload;
                        }
                    }
                    else
                    {
                        displacedPanelIds.push_back(m_viewModel.activeBottomPanelId);
                        displacedPanelIds.push_back(m_viewModel.activeBottomLeftPanelId);
                        displacedPanelIds.push_back(m_viewModel.activeBottomRightPanelId);
                        m_viewModel.bottomDockMode = BottomDockMode::Full;
                        m_viewModel.activeBottomLeftPanelId.clear();
                        m_viewModel.activeBottomRightPanelId.clear();
                        m_viewModel.activeBottomPanelId = *cmd.stringPayload;
                    }
                    break;
                case WorkspaceDockArea::Document:
                    displacedPanelIds.push_back(m_viewModel.activeDocumentPanelId);
                    m_viewModel.activeDocumentPanelId = *cmd.stringPayload;
                    break;
                }

                if (!cmd.stringPayload->empty())
                {
                    const char* stackId = nullptr;
                    switch (area)
                    {
                    case WorkspaceDockArea::Left:
                        stackId = "workspace.left";
                        break;
                    case WorkspaceDockArea::Document:
                        stackId = "workspace.document";
                        break;
                    case WorkspaceDockArea::Right:
                        stackId = "workspace.right";
                        break;
                    case WorkspaceDockArea::Bottom:
                        break;
                    }
                    if (stackId != nullptr)
                    {
                        const auto activateResult =
                            m_viewModel.workspacePanelHost.SetActiveTab(stackId, *cmd.stringPayload);
                        if (!activateResult.Succeeded())
                        {
                            static_cast<void>(m_viewModel.workspacePanelHost.DockPanel(
                                *cmd.stringPayload, stackId, WorkspacePanelHost::DropKind::TabCenter));
                        }
                    }
                }

                for (const std::string& displacedPanelId : displacedPanelIds)
                {
                    if (displacedPanelId.empty() || displacedPanelId == *cmd.stringPayload)
                    {
                        continue;
                    }
                    LOG_INFO("editor.workspace", "Panel closed: '%s'", displacedPanelId.c_str());
                    m_dataBus.Publish(WorkspacePanelClosedEvent{displacedPanelId, area});
                }

                if (!panelWasActive && !cmd.stringPayload->empty())
                {
                    LOG_INFO("editor.workspace", "Panel opened: '%s'", cmd.stringPayload->c_str());
                    m_dataBus.Publish(WorkspacePanelOpenedEvent{*cmd.stringPayload, area});
                }

                // Workspace allocation drops carry an append slot. Activity Bar slot drops use the
                // dedicated reorder command and therefore preserve their exact insertion index.
                if (!cmd.stringPayload->empty() && cmd.activityBarSlot.has_value())
                {
                    const auto previousSlot = m_viewModel.activityBarLayout.FindSlot(*cmd.stringPayload);
                    if (previousSlot.has_value())
                    {
                        const auto result = m_viewModel.activityBarLayout.
                                                        Move(*cmd.stringPayload, *cmd.activityBarSlot);
                        const auto resultingSlot = m_viewModel.activityBarLayout.FindSlot(*cmd.stringPayload);
                        if (result.Succeeded() && result.code != ActivityBarLayoutOperationCode::NoOp &&
                            resultingSlot.has_value())
                        {
                            m_dataBus.Publish(
                                ActivityBarItemReorderedEvent{*cmd.stringPayload, *previousSlot, *resultingSlot});
                        }
                    }
                }
            }
            break;
        case EditorWorkspaceViewCommand::ReorderActivityBarItem:
            if (cmd.stringPayload.has_value() && cmd.activityBarSlot.has_value())
            {
                const auto previousSlot = m_viewModel.activityBarLayout.FindSlot(*cmd.stringPayload);
                if (!previousSlot.has_value())
                {
                    break;
                }

                const auto previousRegion = RegionForActivitySlot(*previousSlot);
                const auto targetRegion = RegionForActivitySlot(*cmd.activityBarSlot);
                const bool activePanelChangesRegion =
                    previousRegion.has_value() && targetRegion.has_value() && *previousRegion != *targetRegion &&
                    IsPanelActiveInRegion(m_viewModel, *cmd.stringPayload, *previousRegion);

                const auto result = m_viewModel.activityBarLayout.Move(*cmd.stringPayload, *cmd.activityBarSlot);
                if (result.Succeeded() && result.code != ActivityBarLayoutOperationCode::NoOp)
                {
                    const auto resultingSlot = m_viewModel.activityBarLayout.FindSlot(*cmd.stringPayload);
                    if (!resultingSlot.has_value())
                    {
                        break;
                    }

                    if (activePanelChangesRegion)
                    {
                        const std::string sourceFallback{
                            m_viewModel.activityBarLayout.ItemAt(previousSlot->rail, previousSlot->groupIndex, 0)
                        };
                        // The activation path needs the source placement to remove the panel from
                        // its old runtime region before assigning its destination.
                        ProcessCommand(MakeRegionActivationCommand(*cmd.stringPayload, *targetRegion));
                        if (!sourceFallback.empty())
                        {
                            ProcessCommand(MakeRegionActivationCommand(sourceFallback, *previousRegion));
                        }
                        NormalizeDocks(m_viewModel);
                    }
                    else if (targetRegion.has_value())
                    {
                        m_viewModel.panelDockAreas[*cmd.stringPayload] = targetRegion->area;
                    }

                    m_dataBus.Publish(ActivityBarItemReorderedEvent{*cmd.stringPayload, *previousSlot, *resultingSlot});
                }
            }
            break;
        case EditorWorkspaceViewCommand::DockWorkspacePanel:
            if (cmd.stringPayload.has_value() && cmd.workspaceDropTarget.has_value())
            {
                const auto& target = *cmd.workspaceDropTarget;
                const auto result =
                    m_viewModel.workspacePanelHost.DockPanel(*cmd.stringPayload, target.targetNodeId, target.kind);
                if (result.Succeeded())
                {
                    m_dataBus.Publish(WorkspacePanelDockedEvent{*cmd.stringPayload, target.targetNodeId, target.kind});
                }
            }
            break;
        case EditorWorkspaceViewCommand::ResizePanel:
            if (cmd.targetIndex.has_value() && cmd.floatPayload.has_value())
            {
                WorkspaceDockArea area{};
                if (!TryGetDockArea(*cmd.targetIndex, area))
                {
                    break;
                }

                switch (area)
                {
                case WorkspaceDockArea::Left:
                    m_viewModel.leftPanelWidth = *cmd.floatPayload;
                    break;
                case WorkspaceDockArea::Right:
                    m_viewModel.rightPanelWidth = *cmd.floatPayload;
                    break;
                case WorkspaceDockArea::Bottom:
                    m_viewModel.bottomPanelHeight = *cmd.floatPayload;
                    break;
                case WorkspaceDockArea::Document:
                    break;
                }

                if (cmd.layoutPayload.has_value())
                {
                    m_dataBus.Publish(WorkspaceLayoutChangedEvent{area, *cmd.layoutPayload});
                }
            }
            break;
        }
    }

    void EditorWorkspaceController::HandleCreatePrimitive(const Runtime::PrimitiveId primitive,
                                                          const std::optional<SceneObjectId> parent)
    {
        Result<SceneCommandResult> result = m_createSceneObject.Execute(PrimitiveCreationRequest{primitive, parent});
        if (result.HasError())
        {
            HandleDocumentCommandResult(std::move(result), "Create object");
            return;
        }
        const SceneObjectId created = result.Value().object;
        const bool committed = result.Value().committed;
        HandleDocumentCommandResult(std::move(result), "Create object");
        if (committed)
        {
            m_viewModel.hierarchyRevealObject = created;
            m_viewModel.hierarchyRevealRevision = m_document.Revision();
            const Result<void> selected = m_selection.SetObjects({created}, created);
            if (selected.HasError())
            {
                LOG_ERROR("editor.selection", "Select created object failed: %s",
                          selected.ErrorValue().message.c_str());
            }
            RefreshSelectionProjection();
        }
    }

    void EditorWorkspaceController::HandleDuplicateObject(const SceneObjectId object)
    {
        const auto source = std::ranges::find(m_viewModel.objects, object, &SceneObject::id);
        if (source != m_viewModel.objects.end())
        {
            HandleDocumentCommandResult(
                m_documentCommands.Execute(DuplicateSceneObjectCommand{source->id, source->name + " Copy"}),
                "Duplicate object");
        }
    }

    void EditorWorkspaceController::HandleDeleteObject(const SceneObjectId object)
    {
        if (m_document.Contains(object))
        {
            HandleDocumentCommandResult(m_documentCommands.Execute(DeleteSceneObjectCommand{object}), "Delete object");
        }
    }

    void EditorWorkspaceController::HandleDocumentCommandResult(Result<SceneCommandResult> result,
                                                                const char* operation)
    {
        if (result.HasError())
        {
            LOG_ERROR("editor.scene_document", "%s failed: %s", operation, result.ErrorValue().message.c_str());
            return;
        }
        const SceneCommandResult& committed = result.Value();
        if (!committed.committed)
        {
            CancelObjectTransformPreview();
            return;
        }
        m_viewport.ClearTransformPreview();
        m_dataBus.Publish(SceneDocumentChangedEvent{
            committed.revision, committed.state, committed.kind,
            m_document.IsDirty(), committed.affectedObjects
        });
        m_selection.Reconcile();
        RefreshSceneProjections();
    }

    void EditorWorkspaceController::PreviewObjectTransform(const SceneObjectId object, const Math::Transform& transform)
    {
        const std::optional<Runtime::RuntimeSceneView> active = m_runtimeScene.ActiveScene();
        if (!active || m_viewportScene.runtimeSceneId != active->RuntimeId()) return;
        const SceneObjectTransformPreview preview{object, transform};
        const std::optional<SceneObjectTransformPreview> previousPreview = m_viewport.Current().transformPreview;
        Result<void> applied = ApplyEditorViewportTransformPreview(*active, preview, m_viewportScene);
        if (applied.HasError())
        {
            LOG_ERROR("editor.viewport", "Transform preview failed: %s", applied.ErrorValue().message.c_str());
            return;
        }
        Result<void> committed = m_viewport.SetTransformPreview(preview);
        if (committed.HasError())
        {
            const Result<void> restored =
                ApplyEditorViewportTransformPreview(*active, previousPreview, m_viewportScene);
            if (restored.HasError())
            {
                LOG_ERROR("editor.viewport", "Transform preview rollback failed: %s",
                          restored.ErrorValue().message.c_str());
            }
            LOG_ERROR("editor.viewport", "Transform preview state failed: %s", committed.ErrorValue().message.c_str());
            return;
        }
    }

    void EditorWorkspaceController::CancelObjectTransformPreview()
    {
        if (!m_viewport.Current().transformPreview.has_value())
        {
            return;
        }
        const std::optional<Runtime::RuntimeSceneView> active = m_runtimeScene.ActiveScene();
        if (!active || m_viewportScene.runtimeSceneId != active->RuntimeId()) return;
        const Result<void> restored = ApplyEditorViewportTransformPreview(*active, {}, m_viewportScene);
        if (restored.HasError())
        {
            LOG_ERROR("editor.viewport", "Transform preview cancellation failed: %s",
                      restored.ErrorValue().message.c_str());
            return;
        }
        m_viewport.ClearTransformPreview();
    }

    void EditorWorkspaceController::RefreshSceneProjections()
    {
        const SceneDocumentSnapshot documentSnapshot = m_document.Snapshot();
        m_viewModel.documentRevision = documentSnapshot.revision;
        m_viewModel.objects.clear();
        m_viewModel.objects.reserve(documentSnapshot.objects.size());
        for (const SceneObjectSnapshot& object : documentSnapshot.objects)
        {
            SceneObjectKind kind = SceneObjectKind::Empty;
            if (object.primitiveMesh.has_value())
            {
                kind = SceneObjectKind::Mesh;
            }
            else if (object.components.camera.has_value())
            {
                kind = SceneObjectKind::Camera;
            }
            else if (object.components.light.has_value())
            {
                kind = SceneObjectKind::Light;
            }
            else if (object.components.triggerVolume.has_value())
            {
                kind = SceneObjectKind::TriggerVolume;
            }
            else if (object.components.audioSource.has_value())
            {
                kind = SceneObjectKind::AudioSource;
            }
            m_viewModel.objects.push_back(SceneObject{
                .id = object.id,
                .parent = object.parent,
                .name = object.name,
                .kind = kind,
                .localTransform = object.localTransform
            });
        }
        m_viewModel.isDirty = m_document.IsDirty();
        m_viewModel.canUndo = m_history.CanUndo();
        m_viewModel.canRedo = m_history.CanRedo();
        QueueRuntimeScene(documentSnapshot);
        RefreshSelectionProjection();
    }

    void EditorWorkspaceController::QueueRuntimeScene(SceneDocumentSnapshot snapshot)
    {
        if (snapshot.state.value == m_activeRuntimeRevision.value ||
            snapshot.state.value == m_queuedDefinitionRevision.value ||
            (m_deferredRuntimeSnapshot && m_deferredRuntimeSnapshot->state == snapshot.state))
            return;

        Result<Runtime::RuntimeSceneDefinition> definition =
            ConvertSceneDocumentToRuntime(snapshot, m_previewSceneId);
        if (definition.HasError())
        {
            LOG_ERROR("editor.runtime_scene", "Scene conversion failed: %s",
                      definition.ErrorValue().message.c_str());
            return;
        }
        const Result<void> queued = m_runtimeScene.QueuePreparation(definition.Value());
        if (queued.HasError())
        {
            m_deferredRuntimeSnapshot = std::move(snapshot);
            return;
        }
        m_queuedRuntimeRevision = snapshot.revision;
        m_queuedDefinitionRevision = Runtime::SceneDefinitionRevision{snapshot.state.value};
    }

    void EditorWorkspaceController::SynchronizeRuntimeScenePreview()
    {
        if (std::optional<Error> operationError = m_runtimeScene.TakeOperationError())
            LOG_ERROR("editor.runtime_scene", "Runtime scene operation failed: %s",
                      operationError->message.c_str());

        const std::optional<Runtime::RuntimeSceneView> active = m_runtimeScene.ActiveScene();
        if (!active || active->DefinitionRevision() == m_activeRuntimeRevision)
            return;

        Result<EditorViewportSceneSnapshot> extracted =
            ExtractEditorViewportScene(*active, m_queuedRuntimeRevision,
                                       m_viewport.Current().camera, m_primitiveMeshCache);
        if (extracted.HasError())
        {
            LOG_ERROR("editor.viewport", "Runtime scene extraction failed: %s",
                      extracted.ErrorValue().message.c_str());
            return;
        }
        m_viewportScene = std::move(extracted).Value();
        m_activeRuntimeRevision = active->DefinitionRevision();
        if (m_queuedDefinitionRevision == m_activeRuntimeRevision)
            m_queuedDefinitionRevision = {};
        m_selection.Reconcile();
        RefreshSelectionProjection();

        if (m_deferredRuntimeSnapshot &&
            m_deferredRuntimeSnapshot->state.value != m_activeRuntimeRevision.value)
        {
            SceneDocumentSnapshot deferred = std::move(*m_deferredRuntimeSnapshot);
            m_deferredRuntimeSnapshot.reset();
            QueueRuntimeScene(std::move(deferred));
        }
    }

    void EditorWorkspaceController::RefreshSelectionProjection()
    {
        const SelectionSnapshot& selection = m_selection.Current();
        m_viewModel.primarySelection = selection.primary;
        m_viewModel.viewportCamera = m_viewportScene.camera;
        m_viewModel.primarySelectionWorldTransform.reset();
        m_viewModel.primarySelectionParentWorldTransform.reset();
        m_viewModel.primarySelectionWorldBounds.reset();
        const std::optional<Runtime::RuntimeSceneView> active = m_runtimeScene.ActiveScene();
        if (selection.primary && active && m_viewportScene.runtimeSceneId == active->RuntimeId())
        {
            const Result<SceneObjectWorldTransforms> transforms =
                ResolveSceneObjectWorldTransforms(*active, *selection.primary);
            if (transforms.HasValue())
            {
                m_viewModel.primarySelectionWorldTransform = transforms.Value().localToWorld;
                m_viewModel.primarySelectionParentWorldTransform = transforms.Value().parentToWorld;
            }
        }
        if (m_viewportScene.instances.size() != m_viewportScene.instanceObjects.size())
        {
            return;
        }
        for (std::size_t index = 0; index < m_viewportScene.instances.size(); ++index)
        {
            m_viewportScene.instances[index].presentation.tint = {0.12F, 0.72F, 1.0F};
            m_viewportScene.instances[index].presentation.tintStrength =
                std::ranges::find(selection.objects, m_viewportScene.instanceObjects[index]) != selection.objects.end()
                    ? 0.65F
                    : 0.0F;
            if (selection.primary == m_viewportScene.instanceObjects[index])
            {
                const Result<Math::Aabb> bounds = Math::TransformAabb(m_viewportScene.instances[index].localBounds,
                                                                      m_viewportScene.instances[index].localToWorld);
                if (bounds.HasValue())
                    m_viewModel.primarySelectionWorldBounds = bounds.Value();
            }
        }
    }
} // namespace Horo::Editor
