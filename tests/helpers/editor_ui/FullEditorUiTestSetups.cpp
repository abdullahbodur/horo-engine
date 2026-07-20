#include "FullEditorUiTestSetups.h"

#include "FullEditorUiTestHost.h"

#include <imgui_test_engine/imgui_te_context.h>

#include <filesystem>
#include <utility>

namespace Horo::Tests::FullEditorSetups
{
void OpenProjectCreation(UiScenarioPipe& pipeline, FullEditorUiTestHost& editor)
{
    pipeline.Setup("Open project creation", [&editor](ImGuiTestContext& ui) {
        ui.SetRef("Welcome");
        ui.ItemClick("**/New Project###welcome_new_project");
        for (int frame = 0; frame < 60 && editor.ActiveRoute() != Editor::GuiRouteKind::ProjectCreation; ++frame)
            ui.Yield();
        IM_CHECK(editor.ActiveRoute() == Editor::GuiRouteKind::ProjectCreation);
    });
}

void SubmitProjectCreation(UiScenarioPipe& pipeline, FullEditorUiTestHost& editor, FullEditorProjectSetup setup)
{
    const std::filesystem::path projectRoot = editor.ProjectsRoot() / setup.name;
    pipeline.Setup("Select project template", [templateId = std::move(setup.templateId)](ImGuiTestContext& ui) {
        ui.SetRef("ProjectCreationScreen");
        ui.ItemClick(("**/###project_template_" + templateId).c_str());
        ui.ItemClick("**/Next###project_creation_next");
    });
    pipeline.Setup("Enter project identity",
                   [name = std::move(setup.name), path = projectRoot.string()](ImGuiTestContext& ui) {
                       ui.SetRef("ProjectCreationScreen");
                       ui.ItemInputValue("**/###project_creation_name", name.c_str());
                       ui.ItemInputValue("**/###project_creation_location", path.c_str());
                       ui.ItemClick("**/Next###project_creation_next");
                       ui.ItemClick("**/Next###project_creation_next");
                       ui.ItemClick("**/Create###project_creation_create");
                   });
}

void AwaitWorkspace(UiScenarioPipe& pipeline, FullEditorUiTestHost& editor)
{
    pipeline.Setup("Wait for complete workspace", [&editor](ImGuiTestContext& ui) {
        for (int frame = 0; frame < 1200 && editor.ActiveRoute() != Editor::GuiRouteKind::EditorWorkspace; ++frame)
            ui.Yield();
        IM_CHECK(editor.ActiveRoute() == Editor::GuiRouteKind::EditorWorkspace);
    });
}

void CreateProjectAndOpenWorkspace(UiScenarioPipe& pipeline, FullEditorUiTestHost& editor, FullEditorProjectSetup setup)
{
    OpenProjectCreation(pipeline, editor);
    SubmitProjectCreation(pipeline, editor, std::move(setup));
    AwaitWorkspace(pipeline, editor);
}
} // namespace Horo::Tests::FullEditorSetups
