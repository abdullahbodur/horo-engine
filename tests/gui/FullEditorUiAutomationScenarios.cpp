#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "FullEditorUiTestActions.h"
#include "FullEditorUiTestHost.h"
#include "FullEditorUiTestSetups.h"

#include <imgui_test_engine/imgui_te_context.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace
{
    using namespace Horo;

    TEST_CASE("Full editor project journey creates content and configures the viewport", "[ui][imgui][editor][e2e]")
    {
        const std::string locale = GENERATE(std::string{"en-US"}, std::string{"tr-TR"});
        CAPTURE(locale);
        Tests::EditorUiTestHarness harness;
        Tests::FullEditorUiTestHost editor{locale};
        const Tests::FullEditorProjectSetup project{.name = "ProjectJourney", .templateId = "empty"};
        const std::filesystem::path projectRoot = editor.ProjectsRoot() / project.name;

        const Tests::EditorUiScenarioResult result = harness.RunScenario(
            "full_editor", "project_journey_" + locale,
            [&editor](ImGuiTestContext* context) { editor.DrawFrame(context); },
            [&editor, project](Tests::UiScenarioPipe& pipeline)
            {
                Tests::FullEditorSetups::CreateProjectAndOpenWorkspace(pipeline, editor, project);
                Tests::FullEditorActions::CreateRootBox(pipeline);
                Tests::FullEditorActions::SelectOrthographicProjection(pipeline, editor);
            },
            Tests::EditorUiScenarioBudget::Extended(1800), &editor.Input());

        INFO(result.testEngineLog);
        REQUIRE_FALSE(result.frameBudgetExceeded);
        REQUIRE_FALSE(result.cancelled);
        REQUIRE(result.exception == nullptr);
        REQUIRE(result.Succeeded());
        REQUIRE(result.steps.size() == 8);
        REQUIRE(std::all_of(result.steps.begin(), result.steps.begin() + 4, [](const Tests::UiScenarioStepResult& step)
            {
            return step.kind == Tests::UiScenarioStepKind::Setup && step.status == Tests::UiScenarioStepStatus::Passed;
            }));
        REQUIRE(std::all_of(result.steps.begin() + 4, result.steps.end(), [](const Tests::UiScenarioStepResult& step)
            {
            return step.kind == Tests::UiScenarioStepKind::Test && step.status == Tests::UiScenarioStepStatus::Passed;
            }));
        REQUIRE(std::filesystem::exists(projectRoot / ".horo" / "project.json"));
        REQUIRE(editor.ViewportProjection() == Runtime::CameraProjection::Orthographic);
    }
} // namespace
