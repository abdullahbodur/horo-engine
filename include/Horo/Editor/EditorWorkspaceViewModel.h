#pragma once

#include "Horo/Editor/ActivityBarLayout.h"
#include "Horo/Editor/EditorMenuModel.h"
#include "Horo/Editor/EditorWorkspaceEvents.h"
#include "Horo/Editor/WorkspacePanelHost.h"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Horo::Editor
{
// ──────────────────────────────────────────────────────────────────────────
// Scene object model (bootstrap stub)
// ──────────────────────────────────────────────────────────────────────────
struct SceneObject
{
    std::string name;
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    std::array<float, 3> rotation{0.0F, 0.0F, 0.0F};
    std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
};

enum class EditorWorkspaceViewCommand
{
    None,
    ReturnToWelcome,
    SaveScene,
    AddObject,
    DuplicateObject,
    DeleteObject,
    SelectObject,
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

struct EditorWorkspaceViewCommandData
{
    EditorWorkspaceViewCommand command = EditorWorkspaceViewCommand::None;
    EditorMenuAction menuAction = EditorMenuAction::None;
    std::optional<int> targetIndex = std::nullopt;
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
    std::vector<SceneObject> objects;
    int selectedIndex = -1;
    bool isDirty = false;
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
