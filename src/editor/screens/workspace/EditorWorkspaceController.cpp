#include "Horo/Editor/EditorWorkspaceController.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "Horo/Editor/EditorWorkspaceEvents.h"
#include "Horo/Foundation/Logging/Logger.h"
#include <format>

namespace Horo::Editor
{
EditorWorkspaceController::EditorWorkspaceController(std::string projectRoot)
{
    m_viewModel.projectRoot = std::move(projectRoot);
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
        if (cmd.targetIndex.has_value())
            m_viewModel.selectedIndex = *cmd.targetIndex;
        break;
    case EditorWorkspaceViewCommand::UpdateObjectTransform:
        m_viewModel.isDirty = true;
        break;
    case EditorWorkspaceViewCommand::UpdateObjectName:
        if (cmd.targetIndex.has_value() && cmd.stringPayload.has_value())
        {
            m_viewModel.objects[*cmd.targetIndex].name = *cmd.stringPayload;
            m_viewModel.isDirty = true;
        }
        break;
    case EditorWorkspaceViewCommand::ChangeActivePanel:
        if (cmd.targetIndex.has_value() && cmd.stringPayload.has_value())
        {
            const auto area = static_cast<WorkspaceDockArea>(*cmd.targetIndex);
            std::string previousPanelId;
            
            switch (area)
            {
            case WorkspaceDockArea::Left:
                previousPanelId = m_viewModel.activeLeftPanelId;
                m_viewModel.activeLeftPanelId = *cmd.stringPayload;
                break;
            case WorkspaceDockArea::Right:
                previousPanelId = m_viewModel.activeRightPanelId;
                m_viewModel.activeRightPanelId = *cmd.stringPayload;
                break;
            case WorkspaceDockArea::Bottom:
                previousPanelId = m_viewModel.activeBottomPanelId;
                m_viewModel.activeBottomPanelId = *cmd.stringPayload;
                break;
            case WorkspaceDockArea::Document:
                previousPanelId = m_viewModel.activeDocumentPanelId;
                m_viewModel.activeDocumentPanelId = *cmd.stringPayload;
                break;
            }

            if (!previousPanelId.empty() && previousPanelId != *cmd.stringPayload)
            {
                LOG_INFO("editor.workspace", "Panel closed: '%s'", previousPanelId.c_str());
                m_dataBus.Publish(WorkspacePanelClosedEvent{previousPanelId, area});
            }

            if (previousPanelId != *cmd.stringPayload && !cmd.stringPayload->empty())
            {
                LOG_INFO("editor.workspace", "Panel opened: '%s'", cmd.stringPayload->c_str());
                m_dataBus.Publish(WorkspacePanelOpenedEvent{*cmd.stringPayload, area});
            }
        }
        break;
    case EditorWorkspaceViewCommand::ResizePanel:
        if (cmd.targetIndex.has_value() && cmd.floatPayload.has_value())
        {
            const auto area = static_cast<WorkspaceDockArea>(*cmd.targetIndex);
            if (*cmd.targetIndex == 0) m_viewModel.leftPanelWidth = *cmd.floatPayload;
            else if (*cmd.targetIndex == 1) m_viewModel.rightPanelWidth = *cmd.floatPayload;
            else if (*cmd.targetIndex == 2) m_viewModel.bottomPanelHeight = *cmd.floatPayload;
            
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
    if (index >= 0 && index < static_cast<int>(m_viewModel.objects.size()))
    {
        auto copy = m_viewModel.objects[index];
        copy.name += " Copy";
        m_viewModel.objects.push_back(std::move(copy));
        m_viewModel.isDirty = true;
    }
}

void EditorWorkspaceController::HandleDeleteObject(const int index)
{
    if (index >= 0 && index < static_cast<int>(m_viewModel.objects.size()))
    {
        m_viewModel.objects.erase(m_viewModel.objects.begin() + index);
        m_viewModel.selectedIndex = -1;
        m_viewModel.isDirty = true;
    }
}
} // namespace Horo::Editor
