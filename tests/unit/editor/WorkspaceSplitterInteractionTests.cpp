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
    WorkspaceSplitterInteraction interaction;
    const std::array regions{kLeftRegion};

    const auto pressed = interaction.Update(
        regions,
        WorkspaceSplitterPointerInput{
            .x = 280.0F, .y = 240.0F, .deltaX = 2.0F, .deltaY = 0.0F, .primaryClicked = true, .primaryDown = true});
    assert(pressed.active == WorkspaceSplitterId::Left);
    assert(pressed.axis == WorkspaceSplitterAxis::Horizontal);
    assert(pressed.delta == 2.0F);

    const auto draggedOutside = interaction.Update(
        regions,
        WorkspaceSplitterPointerInput{
            .x = 340.0F, .y = 240.0F, .deltaX = 12.0F, .deltaY = 0.0F, .primaryClicked = false, .primaryDown = true});
    assert(draggedOutside.active == WorkspaceSplitterId::Left);
    assert(draggedOutside.axis == WorkspaceSplitterAxis::Horizontal);
    assert(draggedOutside.delta == 12.0F);

    const auto released = interaction.Update(
        regions,
        WorkspaceSplitterPointerInput{
            .x = 340.0F, .y = 240.0F, .deltaX = 0.0F, .deltaY = 0.0F, .primaryClicked = false, .primaryDown = false});
    assert(released.active == WorkspaceSplitterId::None);
    assert(released.delta == 0.0F);
}

void ReportsHoverWithoutCapturing()
{
    WorkspaceSplitterInteraction interaction;
    const std::array regions{kLeftRegion};

    const auto hovered = interaction.Update(
        regions,
        WorkspaceSplitterPointerInput{
            .x = 280.0F, .y = 240.0F, .deltaX = 0.0F, .deltaY = 0.0F, .primaryClicked = false, .primaryDown = false});
    assert(hovered.hovered == WorkspaceSplitterId::Left);
    assert(hovered.active == WorkspaceSplitterId::None);
    assert(hovered.axis == WorkspaceSplitterAxis::Horizontal);
    assert(hovered.delta == 0.0F);
}
} // namespace

int main()
{
    CapturesDragWithoutDependingOnAnImGuiWindow();
    ReportsHoverWithoutCapturing();
    return 0;
}
