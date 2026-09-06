#include "Horo/Editor/GuiScreen.h"
#include "Horo/Editor/ProjectCreationController.h"
#include "Horo/Runtime/Input.h"
#include "editor/project_model/RendererAvailability.h"
#include "editor/screens/project_creation/ProjectCreationView.h"
#include "helpers/editor_ui/HeadlessEditorGuiFixture.h"

#include <array>
#include <catch2/catch_test_macros.hpp>

namespace {
    Horo::Editor::ProjectCreationViewCommand DrawProjectCreationFrame(Horo::Editor::Tests::EditorGuiContextFixture &fixture,
                                                                      Horo::Editor::ProjectCreationController &controller,
                                                                      Horo::Editor::ProjectCreationViewState &state,
                                                                      const Horo::Editor::RendererAvailabilitySnapshot &renderers,
                                                                      const Horo::Editor::GuiContentRegion &content) {
        fixture.imgui.BeginFrame();
        const auto command = Horo::Editor::DrawProjectCreationView(controller, state, fixture.context, fixture.input, renderers, content);
        fixture.imgui.EndFrame();
        return command;
    }
}  // namespace

TEST_CASE("Project creation presentation renders every wizard step and template-specific review", "[unit][editor][gui][project-creation]") {
    using namespace Horo;
    using namespace Horo::Editor;

    Tests::EditorGuiContextFixture fixture;
    const RendererAvailabilitySnapshot renderers{{RendererBackendAvailability{"opengl", "OpenGL", RendererAvailabilityState::Active, {}},
                                                  RendererBackendAvailability{"metal", "Metal", RendererAvailabilityState::Available, {}},
                                                  RendererBackendAvailability{"vulkan", "Vulkan", RendererAvailabilityState::NotInstalled,
                                                                              "Not installed"}},
                                                 "opengl"};
    ProjectCreationController controller{renderers};
    controller.SetProjectName("PresentationProject");
    controller.SetProjectPath("/tmp/horo-presentation-project");
    const GuiContentRegion content{0.0F, 0.0F, 1280.0F, 800.0F};

    ProjectCreationViewState state;
    for (int step = 1; step <= 4; ++step) {
        state.step = step;
        const ProjectCreationViewCommand command = DrawProjectCreationFrame(fixture, controller, state, renderers, content);
        REQUIRE(command == ProjectCreationViewCommand::None);
        REQUIRE(state.step == step);
    }

    static constexpr std::array templates{"package-based", "first-person", "tech-demo", "custom"};
    for (const char *templateId : templates) {
        controller.SetTemplateId(templateId);
        state.step = 4;
        const ProjectCreationViewCommand command = DrawProjectCreationFrame(fixture, controller, state, renderers, content);
        REQUIRE(command == ProjectCreationViewCommand::None);
        REQUIRE(controller.Draft().templateId == templateId);
    }
}

TEST_CASE("Project creation presentation normalizes an out-of-range step", "[unit][editor][gui][project-creation]") {
    using namespace Horo;
    using namespace Horo::Editor;

    Tests::EditorGuiContextFixture fixture{{640.0F, 480.0F}};
    const RendererAvailabilitySnapshot renderers{{RendererBackendAvailability{"opengl", "OpenGL", RendererAvailabilityState::Active, {}}},
                                                 "opengl"};
    ProjectCreationController controller{renderers};
    ProjectCreationViewState state;
    state.step = 99;

    static_cast<void>(DrawProjectCreationFrame(fixture, controller, state, renderers, GuiContentRegion{0.0F, 0.0F, 640.0F, 480.0F}));

    REQUIRE(state.step == 1);
}
