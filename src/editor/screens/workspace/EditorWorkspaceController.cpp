#include "Horo/Editor/EditorWorkspaceController.h"
#include "Horo/Editor/EditorWorkspaceEvents.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "Horo/Foundation/Logging/Logger.h"
#include <format>

namespace Horo::Editor
{
namespace
{
[[nodiscard]] bool IsValidObjectIndex(const EditorWorkspaceViewModel &viewModel, const int index) noexcept
{
    return index >= 0 && index < static_cast<int>(viewModel.objects.size());
}

[[nodiscard]] bool TryGetDockArea(const int value, WorkspaceDockArea &area) noexcept
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

void NormalizeSideDock(SideDockMode &mode, std::string &fullPanel, std::string &topPanel, std::string &bottomPanel)
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

void ActivateSideDock(SideDockMode &mode, std::string &fullPanel, std::string &topPanel, std::string &bottomPanel,
                      const std::optional<SideDockSlot> targetSlot, const std::string &panelId,
                      std::vector<std::string> &displacedPanelIds)
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
        std::string &targetPanel = *targetSlot == SideDockSlot::Top ? topPanel : bottomPanel;
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

void NormalizeBottomDock(EditorWorkspaceViewModel &viewModel)
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

void NormalizeDocks(EditorWorkspaceViewModel &viewModel)
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

    friend bool operator==(const ActivityLayoutRegion &, const ActivityLayoutRegion &) = default;
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

[[nodiscard]] bool IsPanelActiveInRegion(const EditorWorkspaceViewModel &viewModel, const std::string_view panelId,
                                         const ActivityLayoutRegion &region)
{
    switch (region.area)
    {
    case WorkspaceDockArea::Left:
        if (viewModel.leftDockMode == SideDockMode::Full)
        {
            return viewModel.activeLeftPanelId == panelId;
        }
        return region.sideSlot == SideDockSlot::Top ? viewModel.activeLeftTopPanelId == panelId
                                                    : viewModel.activeLeftBottomPanelId == panelId;
    case WorkspaceDockArea::Right:
        if (viewModel.rightDockMode == SideDockMode::Full)
        {
            return viewModel.activeRightPanelId == panelId;
        }
        return region.sideSlot == SideDockSlot::Top ? viewModel.activeRightTopPanelId == panelId
                                                    : viewModel.activeRightBottomPanelId == panelId;
    case WorkspaceDockArea::Bottom:
        if (viewModel.bottomDockMode == BottomDockMode::Full)
        {
            return viewModel.activeBottomPanelId == panelId;
        }
        return region.bottomSlot == BottomDockSlot::Left ? viewModel.activeBottomLeftPanelId == panelId
                                                         : viewModel.activeBottomRightPanelId == panelId;
    case WorkspaceDockArea::Document:
        return viewModel.activeDocumentPanelId == panelId;
    }
    return false;
}

[[nodiscard]] EditorWorkspaceViewCommandData MakeRegionActivationCommand(const std::string_view panelId,
                                                                         const ActivityLayoutRegion &region)
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

EditorWorkspaceController::EditorWorkspaceController(std::string projectRoot)
{
    m_viewModel.projectRoot = std::move(projectRoot);
    m_viewModel.panelDockAreas = {{"horo.hierarchy", WorkspaceDockArea::Left},
                                  {"horo.viewport", WorkspaceDockArea::Document},
                                  {"horo.content_browser", WorkspaceDockArea::Bottom},
                                  {"horo.inspector", WorkspaceDockArea::Right}};
    static_cast<void>(
        m_viewModel.activityBarLayout.Insert("horo.hierarchy", ActivityBarSlot{ActivityBarRail::Left, 0, 0}));
    static_cast<void>(
        m_viewModel.activityBarLayout.Insert("horo.viewport", ActivityBarSlot{ActivityBarRail::DocumentTop, 0, 0}));
    static_cast<void>(
        m_viewModel.activityBarLayout.Insert("horo.content_browser", ActivityBarSlot{ActivityBarRail::Left, 2, 0}));
    static_cast<void>(
        m_viewModel.activityBarLayout.Insert("horo.inspector", ActivityBarSlot{ActivityBarRail::Right, 0, 0}));
}

void EditorWorkspaceController::UpdateFps(const float fps)
{
    m_viewModel.fps = fps;
}

void EditorWorkspaceController::ProcessCommand(const EditorWorkspaceViewCommandData &cmd)
{
    switch (cmd.command)
    {
    case EditorWorkspaceViewCommand::None:
    case EditorWorkspaceViewCommand::ReturnToWelcome:
        break;
    case EditorWorkspaceViewCommand::SaveScene:
        m_viewModel.isDirty = false;
        break;
    case EditorWorkspaceViewCommand::AddObject:
        HandleAddObject();
        break;
    case EditorWorkspaceViewCommand::DuplicateObject:
        if (cmd.targetIndex.has_value())
            HandleDuplicateObject(*cmd.targetIndex);
        break;
    case EditorWorkspaceViewCommand::DeleteObject:
        if (cmd.targetIndex.has_value())
            HandleDeleteObject(*cmd.targetIndex);
        break;
    case EditorWorkspaceViewCommand::SelectObject:
        if (cmd.targetIndex.has_value() && IsValidObjectIndex(m_viewModel, *cmd.targetIndex))
            m_viewModel.selectedIndex = *cmd.targetIndex;
        break;
    case EditorWorkspaceViewCommand::UpdateObjectTransform:
        m_viewModel.isDirty = true;
        break;
    case EditorWorkspaceViewCommand::UpdateObjectName:
        if (cmd.targetIndex.has_value() && cmd.stringPayload.has_value() &&
            IsValidObjectIndex(m_viewModel, *cmd.targetIndex))
        {
            m_viewModel.objects[*cmd.targetIndex].name = *cmd.stringPayload;
            m_viewModel.isDirty = true;
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

                        std::string &targetPanel = *cmd.bottomDockSlot == BottomDockSlot::Left
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
                const char *stackId = nullptr;
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

            for (const std::string &displacedPanelId : displacedPanelIds)
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
                    const auto result = m_viewModel.activityBarLayout.Move(*cmd.stringPayload, *cmd.activityBarSlot);
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
                        m_viewModel.activityBarLayout.ItemAt(previousSlot->rail, previousSlot->groupIndex, 0)};
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
            const auto &target = *cmd.workspaceDropTarget;
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

void EditorWorkspaceController::HandleAddObject()
{
    SceneObject obj;
    obj.name = std::format("Object {}", m_viewModel.objects.size() + 1);
    m_viewModel.objects.push_back(std::move(obj));
    m_viewModel.isDirty = true;
}

void EditorWorkspaceController::HandleDuplicateObject(const int index)
{
    if (IsValidObjectIndex(m_viewModel, index))
    {
        auto copy = m_viewModel.objects[index];
        copy.name += " Copy";
        m_viewModel.objects.push_back(std::move(copy));
        m_viewModel.isDirty = true;
    }
}

void EditorWorkspaceController::HandleDeleteObject(const int index)
{
    if (IsValidObjectIndex(m_viewModel, index))
    {
        m_viewModel.objects.erase(m_viewModel.objects.begin() + index);
        m_viewModel.selectedIndex = -1;
        m_viewModel.isDirty = true;
    }
}
} // namespace Horo::Editor
