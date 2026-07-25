#include <catch2/catch_test_macros.hpp>

#include "FullEditorUiTestActions.h"
#include "FullEditorUiTestHost.h"
#include "FullEditorUiTestSetups.h"
#include "Horo/Runtime/Scene/PrimitiveMesh.h"
#include "editor/renderer/EditorViewportScene.h"

#include <imgui.h>
#include <imgui_test_engine/imgui_te_context.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
    using namespace Horo;

    TEST_CASE("Selected E2E renderer produces an editor viewport texture", "[ui][imgui][editor][e2e][renderer]")
    {
        Tests::EditorUiTestHarness harness;
        Tests::IEditorUiTestSurface& surface = harness.Surface();
        CAPTURE(std::string{surface.RendererName()});

        Runtime::PrimitiveMeshCache meshCache;
        auto acquired = meshCache.Acquire(Runtime::PrimitiveMeshDescriptor::Defaults(Runtime::PrimitiveMeshType::Box));
        REQUIRE(acquired.HasValue());
        Runtime::PrimitiveMeshLease meshLease = std::move(acquired).Value();
        const Render::MeshData& mesh = meshLease.Data();
        const Render::RenderMeshHandle meshHandle{meshLease.Id(), 1};
        const std::array meshResources{
            Editor::EditorViewportMeshResourceView{meshHandle, mesh.vertices, mesh.indices, mesh.localBounds}
        };
        const std::array instances{
            Editor::EditorViewportInstance{
                meshHandle,
                Math::Transform{}.ToMatrix(),
                mesh.localBounds,
                Render::CoreDefaultMaterial,
                {.tint = {0.12F, 0.72F, 1.0F}, .tintStrength = 0.65F}
            }
        };
        const Editor::EditorViewportSceneView scene{
            .camera = {}, .meshResources = meshResources, .instances = instances
        };

        const Tests::EditorUiScenarioResult result = harness.Run(
            "renderer_e2e", "selected_renderer_viewport",
            [&surface, scene](ImGuiTestContext*)
            {
                constexpr Editor::EditorViewportExtent extent{640, 360};
                surface.ViewportRenderer().RequestExtent(extent);
                surface.RenderViewport(scene);

                ImGui::SetNextWindowSize({680.0F, 420.0F}, ImGuiCond_Always);
                ImGui::Begin("Renderer E2E", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
                if (const Editor::EditorViewportTextureView texture = surface.ViewportRenderer().TextureView(); texture.IsValid())
                {
                    ImGui::Image(texture.textureId, {640.0F, 360.0F},
                                 {texture.u0, texture.v0}, {texture.u1, texture.v1});
                }
                else
                {
                    ImGui::TextUnformatted("Waiting for selected renderer...");
                }
                ImGui::End();
            },
            [&surface](ImGuiTestContext* context)
            {
                const bool expectsTexture = surface.RendererName() != "null";
                for (int frame = 0; frame < 120 && expectsTexture && !surface.ViewportRenderer().IsReady(); ++frame)
                    context->Yield();
                if (expectsTexture)
                {
                    IM_CHECK(surface.ViewportRenderer().IsReady());
                    IM_CHECK(surface.ViewportRenderer().TextureView().IsValid());
                }
            });

        INFO(result.testEngineLog);
        REQUIRE_FALSE(result.frameBudgetExceeded);
        REQUIRE_FALSE(result.cancelled);
        REQUIRE(result.exception == nullptr);
        REQUIRE(result.Succeeded());
        if (surface.RendererName() != "null")
            REQUIRE(surface.ViewportRenderer().IsReady());
    }

    TEST_CASE("Full editor project journey creates content and configures the viewport", "[ui][imgui][editor][e2e]")
    {
        const char* const envLocale = std::getenv("HORO_UI_TEST_LOCALE");
        const std::string locale = (envLocale != nullptr && envLocale[0] != '\0')
                                       ? std::string{envLocale}
                                       : std::string{"en-US"};
        CAPTURE(locale);
        Tests::EditorUiTestHarness harness;
        Tests::FullEditorUiTestHost editor{harness.Surface(), locale};
        CAPTURE(std::string{editor.RendererName()});
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
        if (editor.RendererName() != "null")
            REQUIRE(editor.RendererReady());
    }

    TEST_CASE("Recent project opens through the loading screen", "[ui][imgui][editor][e2e][recent-project]")
    {
        Tests::EditorUiTestHarness harness;
        Tests::FullEditorUiTestHost editor{harness.Surface(), "en-US", "RecentProject"};
        const std::filesystem::path projectRoot = editor.ProjectsRoot() / "RecentProject";

        const Tests::EditorUiScenarioResult result = harness.RunScenario(
            "full_editor", "open_recent_project",
            [&editor](ImGuiTestContext* context) { editor.DrawFrame(context); },
            [&editor](Tests::UiScenarioPipe& pipeline)
            {
                pipeline.Setup("Open recent project", [](ImGuiTestContext& ui)
                {
                    ui.SetRef("Welcome");
                    ui.ItemClick("**/Project card###welcome_project_card");
                });
                Tests::FullEditorSetups::AwaitWorkspace(pipeline, editor);
            },
            Tests::EditorUiScenarioBudget::Extended(1400), &editor.Input());

        INFO(result.testEngineLog);
        REQUIRE_FALSE(result.frameBudgetExceeded);
        REQUIRE_FALSE(result.cancelled);
        REQUIRE(result.exception == nullptr);
        REQUIRE(result.Succeeded());
        REQUIRE(editor.WasRouteDrawn(Editor::GuiRouteKind::ProjectLoading));
        REQUIRE(editor.RouteDrawCount(Editor::GuiRouteKind::ProjectLoading) >= 2);
        REQUIRE(editor.ActiveRoute() == Editor::GuiRouteKind::EditorWorkspace);
        REQUIRE(std::filesystem::exists(projectRoot / ".horo" / "project.json"));
    }
} // namespace
