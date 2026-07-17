#include "WorkspaceSplitterInteraction.h"

#include <array>
#include <cassert>

namespace
{
using namespace Horo::Editor;

constexpr WorkspaceSplitterRegion kLeftRegion{
    .id = WorkspaceSplitterId::Left,
    .axis = WorkspaceSplitterAxis::Horizontal,
    .minX = 276.0F,
    .minY = 0.0F,
    .maxX = 284.0F,
    .maxY = 600.0F,
};

void CapturesDragWithoutDependingOnAnImGuiWindow()
{
    Horo::Input::InputRouter router;
    auto context = router.PushContext(Horo::Input::InputContextId{"workspace"}, Horo::Input::InputContextKind::EditorWorkspace);
    WorkspaceSplitterInteraction interaction;
    const std::array regions{kLeftRegion};

    const auto pressed = interaction.Update(
        regions,
        WorkspaceSplitterPointerInput{
            .x = 280.0F, .y = 240.0F, .deltaX = 2.0F, .deltaY = 0.0F, .primaryClicked = true, .primaryDown = true}, router, context);
    assert(pressed.active == WorkspaceSplitterId::Left);
    assert(pressed.axis == WorkspaceSplitterAxis::Horizontal);
    assert(pressed.delta == 2.0F);
    assert(interaction.OwnsPrimaryPointer());

    const auto draggedOutside = interaction.Update(
        regions,
        WorkspaceSplitterPointerInput{
            .x = 340.0F, .y = 240.0F, .deltaX = 12.0F, .deltaY = 0.0F, .primaryClicked = false, .primaryDown = true}, router, context);
    assert(draggedOutside.active == WorkspaceSplitterId::Left);
    assert(draggedOutside.axis == WorkspaceSplitterAxis::Horizontal);
    assert(draggedOutside.delta == 12.0F);

    const auto released = interaction.Update(
        regions,
        WorkspaceSplitterPointerInput{
            .x = 340.0F, .y = 240.0F, .deltaX = 0.0F, .deltaY = 0.0F, .primaryClicked = false, .primaryDown = false}, router, context);
    assert(released.active == WorkspaceSplitterId::None);
    assert(released.delta == 0.0F);
    assert(!interaction.OwnsPrimaryPointer());
}

void ReportsHoverWithoutCapturing()
{
    Horo::Input::InputRouter router;
    auto context = router.PushContext(Horo::Input::InputContextId{"workspace"}, Horo::Input::InputContextKind::EditorWorkspace);
    WorkspaceSplitterInteraction interaction;
    const std::array regions{kLeftRegion};

    const auto hovered = interaction.Update(
        regions,
        WorkspaceSplitterPointerInput{
            .x = 280.0F, .y = 240.0F, .deltaX = 0.0F, .deltaY = 0.0F, .primaryClicked = false, .primaryDown = false}, router, context);
    assert(hovered.hovered == WorkspaceSplitterId::Left);
    assert(hovered.active == WorkspaceSplitterId::None);
    assert(hovered.axis == WorkspaceSplitterAxis::Horizontal);
    assert(hovered.delta == 0.0F);
}

void BottomResizeOwnsPointerOutsideTheOverlappingSeam()
{
    Horo::Input::InputRouter router;
    auto context = router.PushContext(Horo::Input::InputContextId{"workspace"}, Horo::Input::InputContextKind::EditorWorkspace);
    WorkspaceSplitterInteraction interaction;
    const std::array regions{WorkspaceSplitterRegion{
        .id = WorkspaceSplitterId::Bottom,
        .axis = WorkspaceSplitterAxis::Vertical,
        .minX = 36.0F,
        .minY = 496.0F,
        .maxX = 1244.0F,
        .maxY = 504.0F,
    }};

    const auto pressed = interaction.Update(
        regions,
        WorkspaceSplitterPointerInput{
            .x = 640.0F, .y = 502.0F, .deltaX = 0.0F, .deltaY = -3.0F, .primaryClicked = true, .primaryDown = true}, router, context);
    assert(pressed.active == WorkspaceSplitterId::Bottom);
    assert(pressed.axis == WorkspaceSplitterAxis::Vertical);
    assert(pressed.delta == -3.0F);
    assert(interaction.OwnsPrimaryPointer());

    const auto draggedIntoPanel = interaction.Update(
        regions,
        WorkspaceSplitterPointerInput{
            .x = 640.0F, .y = 470.0F, .deltaX = 0.0F, .deltaY = -12.0F, .primaryClicked = false, .primaryDown = true}, router, context);
    assert(draggedIntoPanel.active == WorkspaceSplitterId::Bottom);
    assert(draggedIntoPanel.delta == -12.0F);
    assert(interaction.OwnsPrimaryPointer());
}
} // namespace

int main()
{
    CapturesDragWithoutDependingOnAnImGuiWindow();
    ReportsHoverWithoutCapturing();
    BottomResizeOwnsPointerOutsideTheOverlappingSeam();
    return 0;
}
