#include "FullEditorUiTestHost.h"

#include "Horo/Editor/DefaultScreenFactories.h"
#include "Horo/Editor/DefaultWorkspacePanels.h"
#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "Horo/Editor/Localization/LocalizationService.h"
#include "Horo/Editor/ProjectCreationService.h"
#include "Horo/Editor/ProjectOpenService.h"
#include "Horo/Editor/RecentProjectInspectionService.h"
#include "Horo/Foundation/DataBus.h"
#include "Horo/Foundation/JobSystem.h"
#include "Horo/Foundation/Platform.h"
#include "Horo/Runtime/Scene/RuntimeScene.h"
#include "editor/document/EditorViewportSceneExtractor.h"
#include "editor/input/EditorInputActions.h"
#include "editor/project_model/RendererAvailability.h"
#include "editor/renderer/EditorViewportRenderer.h"

#include <imgui.h>
#include <imgui_test_engine/imgui_te_context.h>

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace Horo::Tests
{
namespace
{
class ScopedHome final
{
  public:
    explicit ScopedHome(const std::filesystem::path& home)
    {
#if defined(_WIN32)
        if (const char* value = std::getenv("USERPROFILE"))
            previous_ = value;
        _putenv_s("USERPROFILE", home.string().c_str());
#else
        if (const char* value = std::getenv("HOME"))
            previous_ = value;
        setenv("HOME", home.string().c_str(), 1);
#endif
    }

    ~ScopedHome()
    {
#if defined(_WIN32)
        _putenv_s("USERPROFILE", previous_.c_str());
#else
        if (previous_.empty())
            unsetenv("HOME");
        else
            setenv("HOME", previous_.c_str(), 1);
#endif
    }

  private:
    std::string previous_;
};

class NullViewportRenderer final : public Editor::IEditorViewportRenderer
{
  public:
    void RequestExtent(const Editor::EditorViewportExtent extent) noexcept override
    {
        extent_ = extent;
    }
    [[nodiscard]] Editor::EditorViewportExtent RequestedExtent() const noexcept override
    {
        return extent_;
    }
    [[nodiscard]] Result<void> ExecuteStaticMeshPass(const Render::StaticMeshPassDescriptor&) override
    {
        return Result<void>::Success();
    }
    [[nodiscard]] Editor::EditorViewportTextureView TextureView() const noexcept override
    {
        return {};
    }
    [[nodiscard]] bool IsReady() const noexcept override
    {
        return false;
    }

  private:
    Editor::EditorViewportExtent extent_{};
};

[[nodiscard]] std::filesystem::path MakeIsolatedRoot()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("horo-full-editor-ui-" + std::to_string(nonce));
}

void LoadLocalization(Editor::LocalizationService& localization, const std::string_view locale)
{
    Editor::LocalizationError error;
    const std::filesystem::path root = std::filesystem::path{HORO_PROJECT_SOURCE_DIR} / "assets/localization/editor";
    if (!localization.LoadCatalogFile(root / "en-US.json", &error) ||
        !localization.LoadCatalogFile(root / "tr-TR.json", &error) ||
        !localization.Prepare(Editor::LocaleTag{std::string{locale}}, &error) || !localization.ActivatePrepared(&error))
    {
        throw std::runtime_error("Unable to load deterministic editor localization catalogs.");
    }
}
} // namespace

struct FullEditorUiTestHost::State
{
    explicit State(std::string locale)
        : root(MakeIsolatedRoot()), home(root / "home"), projectsRoot(root / "projects"), scopedHome(home),
          jobs(JobSystemConfig{2, 256}), creation(jobs, engineEvents), localization(Editor::LocaleTag{"en-US"}),
          configuration(Editor::CreateEditorConfigurationService(Editor::DefaultEditorSettings())),
          settings(Editor::DefaultEditorSettings(), configuration, editorEvents, localization),
          modals(editorEvents, input), mutations(files), transactions(files, wallClock, mutations, jobs),
          preflight(transactions), recentInspection(jobs, preflight),
          rendererAvailability(
              {Editor::RendererBackendAvailability{"opengl", "OpenGL", Editor::RendererAvailabilityState::Active, {}}},
              "opengl"),
          open(jobs, files, preflight, mutations, transactions, rendererAvailability),
          fonts{ImGui::GetIO().Fonts->Fonts.front(), ImGui::GetIO().Fonts->Fonts.front(),
                ImGui::GetIO().Fonts->Fonts.front()},
          theme{fonts}, settingsSnapshot(settings.Snapshot()),
          gui{engineEvents, editorEvents, localization, theme, settingsSnapshot}
    {
        std::filesystem::create_directories(home);
        std::filesystem::create_directories(projectsRoot);
        LoadLocalization(localization, locale);
        static_cast<void>(input.SetActionMap(Editor::BuildEditorInputActions()));
        Editor::ScreenRegistry screens;
        Editor::RegisterWelcomeScreen(screens);
        Editor::RegisterProjectCreationScreen(screens);
        Editor::RegisterProjectLoadingScreen(screens);
        Editor::RegisterEditorWorkspaceScreen(screens);
        Editor::WorkspacePanelRegistry panels;
        Editor::RegisterDefaultWorkspacePanels(panels);
        screenHost =
            std::make_unique<Editor::GuiScreenHost>(gui, modals, settings, localization, engineEvents, creation, input,
                                                    rendererAvailability, std::move(screens), std::move(panels));
        screenHost->Services().Register<Editor::IEditorViewportRenderer>(viewportRenderer);
        screenHost->Services().Register<Editor::EditorViewportSceneState>(viewportScene);
        screenHost->Services().Register<Runtime::RuntimeSceneService>(runtimeScene);
        screenHost->Services().Register<Editor::ProjectOpenService>(open);
        screenHost->Services().Register<Editor::RecentProjectInspectionService>(recentInspection);
        const Result<void> started =
            screenHost->Start({Editor::GuiRouteKind::Welcome, Editor::WelcomeRouteParameters{}});
        if (started.HasError())
            throw std::runtime_error(started.ErrorValue().message);
    }

    ~State()
    {
        if (screenHost)
            screenHost->Shutdown();
        recentInspection.Shutdown();
        open.Shutdown();
        jobs.Shutdown(ShutdownPolicy::Cancel);
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path root;
    std::filesystem::path home;
    std::filesystem::path projectsRoot;
    ScopedHome scopedHome;
    EngineDataBus engineEvents;
    Editor::EditorDataBus editorEvents;
    JobSystem jobs;
    Editor::ProjectCreationService creation;
    Editor::LocalizationService localization;
    ConfigurationService configuration;
    Editor::EditorSettingsService settings;
    Input::InputRouter input;
    Editor::EditorModalHost modals;
    NativeDurableFileSystem files;
    SystemWallClock wallClock;
    Editor::ProjectMutationCoordinator mutations;
    Editor::ProjectMigrationTransactionService transactions;
    Editor::ProjectOpenPreflightService preflight;
    Editor::RecentProjectInspectionService recentInspection;
    Editor::RendererAvailabilitySnapshot rendererAvailability;
    Editor::ProjectOpenService open;
    Runtime::RuntimeSceneService runtimeScene;
    Editor::EditorViewportSceneState viewportScene;
    NullViewportRenderer viewportRenderer;
    Editor::Theme::Fonts fonts;
    Editor::ThemeContext theme;
    Editor::EditorSettingsSnapshot settingsSnapshot;
    Editor::EditorGuiContext gui;
    std::unique_ptr<Editor::GuiScreenHost> screenHost;
};

FullEditorUiTestHost::FullEditorUiTestHost(std::string locale) : state_(std::make_unique<State>(std::move(locale)))
{
}

FullEditorUiTestHost::~FullEditorUiTestHost() = default;

void FullEditorUiTestHost::DrawFrame(ImGuiTestContext*)
{
    state_->engineEvents.DispatchQueued();
    state_->settingsSnapshot = state_->settings.Snapshot();
    state_->modals.OnUpdate(1.0F / 60.0F);
    state_->screenHost->OnUpdate(1.0F / 60.0F);
    state_->screenHost->Draw();
    state_->modals.Draw();
}

Editor::GuiRouteKind FullEditorUiTestHost::ActiveRoute() const noexcept
{
    return state_->screenHost->ActiveRoute().kind;
}

const std::filesystem::path& FullEditorUiTestHost::ProjectsRoot() const noexcept
{
    return state_->projectsRoot;
}

Editor::GuiScreenHost& FullEditorUiTestHost::Screens() noexcept
{
    return *state_->screenHost;
}

Input::InputRouter& FullEditorUiTestHost::Input() noexcept
{
    return state_->input;
}

Runtime::CameraProjection FullEditorUiTestHost::ViewportProjection() const noexcept
{
    return state_->viewportScene.View().camera.projection;
}

} // namespace Horo::Tests
