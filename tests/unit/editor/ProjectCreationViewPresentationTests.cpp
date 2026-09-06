#include "Horo/Editor/GuiScreen.h"
#include "Horo/Editor/ProjectCreationController.h"
#include "Horo/Runtime/Input.h"
#include "editor/project_model/RendererAvailability.h"
#include "editor/screens/project_creation/ProjectCreationView.h"
#include "helpers/editor_ui/HeadlessEditorGuiFixture.h"

#include <array>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Project creation presentation renders every wizard step and template-specific review", "[unit][editor][gui][project-creation]") {
    using namespace Horo;
    using namespace Horo::Editor;

    Tests::HeadlessEditorGuiFixture imgui;
    EngineDataBus engineEvents;
    EditorDataBus editorEvents;
    Tests::KeyLocalization localization;
    const ThemeContext theme{imgui.Fonts()};
    const EditorSettingsSnapshot settings{};
    const EditorGuiContext context{engineEvents, editorEvents, localization, theme, settings};
    Input::InputRouter input;
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
        imgui.BeginFrame();
        const ProjectCreationViewCommand command = DrawProjectCreationView(controller, state, context, input, renderers, content);
        imgui.EndFrame();
        REQUIRE(command == ProjectCreationViewCommand::None);
        REQUIRE(state.step == step);
    }

    static constexpr std::array templates{"package-based", "first-person", "tech-demo", "custom"};
    for (const char *templateId : templates) {
        controller.SetTemplateId(templateId);
        state.step = 4;
        imgui.BeginFrame();
        const ProjectCreationViewCommand command = DrawProjectCreationView(controller, state, context, input, renderers, content);
        imgui.EndFrame();
        REQUIRE(command == ProjectCreationViewCommand::None);
        REQUIRE(controller.Draft().templateId == templateId);
    }
}

TEST_CASE("Project creation presentation normalizes an out-of-range step", "[unit][editor][gui][project-creation]") {
    using namespace Horo;
    using namespace Horo::Editor;

    Tests::HeadlessEditorGuiFixture imgui{{640.0F, 480.0F}};
    EngineDataBus engineEvents;
    EditorDataBus editorEvents;
    Tests::KeyLocalization localization;
    const ThemeContext theme{imgui.Fonts()};
    const EditorSettingsSnapshot settings{};
    const EditorGuiContext context{engineEvents, editorEvents, localization, theme, settings};
    Input::InputRouter input;
    const RendererAvailabilitySnapshot renderers{{RendererBackendAvailability{"opengl", "OpenGL", RendererAvailabilityState::Active, {}}},
                                                 "opengl"};
    ProjectCreationController controller{renderers};
    ProjectCreationViewState state;
    state.step = 99;

    imgui.BeginFrame();
    static_cast<void>(DrawProjectCreationView(controller, state, context, input, renderers, GuiContentRegion{0.0F, 0.0F, 640.0F, 480.0F}));
    imgui.EndFrame();

    REQUIRE(state.step == 1);
}
