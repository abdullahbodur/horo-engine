#include "FullEditorUiTestActions.h"

#include "FullEditorUiTestHost.h"

#include <imgui_test_engine/imgui_te_context.h>

namespace Horo::Tests::FullEditorActions
{
void CreateRootBox(UiScenarioPipe& pipeline)
{
    pipeline.Step("Create a Box from the hierarchy root menu", [](ImGuiTestContext& ui)
    {
        ui.ItemClick("**/##HierarchyRootDrop", ImGuiMouseButton_Right);
        ui.SetRef("//$FOCUSED");
        ui.MenuClick("###hierarchy_create_root/"
            "###hierarchy_create_workspace.create.group.objects_3d/"
            "###hierarchy_create_primitive.mesh.box");
    });
    pipeline.Step("Observe the created object across the workspace", [](ImGuiTestContext& ui)
    {
        IM_CHECK(ui.ItemInfo("//**/Hierarchy object###hierarchy_object_row").ID != 0);
    });
}

void SelectOrthographicProjection(UiScenarioPipe& pipeline, FullEditorUiTestHost& editor)
{
    pipeline.Step("Choose Orthographic projection", [](ImGuiTestContext& ui)
    {
        ui.ItemClick("//**/Combo###viewport_projection");
        ui.ItemClick("//**/###combo_option_1");
    });
    pipeline.Step("Observe projection through the shared viewport handoff", [&editor](ImGuiTestContext&)
    {
        IM_CHECK(editor.ViewportProjection() == Runtime::CameraProjection::Orthographic);
    });
}
} // namespace Horo::Tests::FullEditorActions
