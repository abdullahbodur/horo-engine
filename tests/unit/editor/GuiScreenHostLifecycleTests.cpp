#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "Horo/Editor/Localization/LocalizationService.h"
#include "Horo/Editor/ProjectCreationService.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "Horo/Foundation/DataBus.h"
#include "Horo/Foundation/JobSystem.h"
#include "editor/project_model/RendererAvailability.h"

#include <cassert>
#include <cstdint>
#include <memory>

namespace Horo::Editor::Theme
{
struct Fonts;
}

namespace
{
using namespace Horo;
using namespace Horo::Editor;

struct ScreenStats
{
    int enters = 0;
    int leaves = 0;
    int destructions = 0;
};

class RecordingScreen final : public GuiScreen
{
  public:
    explicit RecordingScreen(ScreenStats &stats) : stats_(stats) {}
    ~RecordingScreen() override { ++stats_.destructions; }
    [[nodiscard]] ScreenId Id() const override { return 1; }
    [[nodiscard]] Result<void> OnEnter(const GuiRoute &) override
    {
        ++stats_.enters;
        return Result<void>::Success();
    }
    void OnUpdate(float) override {}
    void Draw(const GuiContentRegion &) override {}
    [[nodiscard]] LeaveDecision CanLeave(const LeaveTarget &) const override
    {
        return {.disposition = LeaveDisposition::Allow, .requirement = std::nullopt};
    }
    [[nodiscard]] Result<LeaveDecision> ResolveLeave(const LeaveTarget &, const LeaveResolution &) override
    {
        return Result<LeaveDecision>::Success(
            {.disposition = LeaveDisposition::Allow, .requirement = std::nullopt});
    }
    void OnLeave() override { ++stats_.leaves; }

  private:
    ScreenStats &stats_;
};

void ShutdownLeavesOnceDestroysScreenAndRevokesServices()
{
    EngineDataBus engineEvents;
    EditorDataBus editorEvents;
    Input::InputRouter input;
    JobSystem jobs{JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 8}};
    ProjectCreationService creation{jobs, engineEvents};
    LocalizationService localization{LocaleTag{"en-US"}};
    ConfigurationService configuration = CreateEditorConfigurationService(DefaultEditorSettings());
    EditorSettingsService settings{DefaultEditorSettings(), configuration, editorEvents, localization};
    EditorModalHost modals{editorEvents, input};
    const Theme::Fonts &fonts = *reinterpret_cast<const Theme::Fonts *>(static_cast<std::uintptr_t>(1));
    ThemeContext theme{fonts};
    EditorSettingsSnapshot settingsSnapshot = settings.Snapshot();
    EditorGuiContext gui{engineEvents, editorEvents, localization, theme, settingsSnapshot};
    RendererAvailabilitySnapshot renderers{
        {RendererBackendAvailability{"opengl", "OpenGL", RendererAvailabilityState::Active, {}}}, "opengl"};
    ScreenStats stats;
    ScreenRegistry screens;
    screens.Register(GuiRouteKind::Welcome, [&stats](const EditorServiceRegistry &, const GuiRoute &) {
        return std::make_unique<RecordingScreen>(stats);
    });
    WorkspacePanelRegistry panels;

    GuiScreenHost host{gui, modals, settings, localization, engineEvents, creation, input, renderers,
                       std::move(screens), std::move(panels)};
    assert(stats.enters == 1);
    assert(!host.Services().Empty());

    const Result<void> invalidRoute =
        host.Navigate(GuiRoute{GuiRouteKind::Welcome, ProjectCreationRouteParameters{}});
    assert(invalidRoute.HasError());
    assert(invalidRoute.ErrorValue().domain.Value() == "horo.editor.screens");
    assert(invalidRoute.ErrorValue().code.Value() == "navigation.invalid_route_parameters");

    host.Shutdown();
    assert(host.IsShutdown());
    assert(stats.leaves == 1);
    assert(stats.destructions == 1);
    assert(host.Services().Empty());

    host.Shutdown();
    assert(stats.leaves == 1);
    assert(stats.destructions == 1);
    const Result<void> navigation = host.Navigate(GuiRoute{GuiRouteKind::Welcome, WelcomeRouteParameters{}});
    assert(navigation.HasError());
    assert(navigation.ErrorValue().domain.Value() == "horo.editor.screens");
    assert(navigation.ErrorValue().code.Value() == "navigation.host_shutdown");
    jobs.Shutdown(ShutdownPolicy::Cancel);
}
} // namespace

int main()
{
    ShutdownLeavesOnceDestroysScreenAndRevokesServices();
    return 0;
}
