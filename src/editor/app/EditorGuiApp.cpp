#include "Horo/Editor/EditorGuiApp.h"

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Foundation/DataBus.h"
#include "Horo/Editor/WelcomeScreen.h"
#include "Horo/Editor/GuiRoute.h"
#include "Horo/Editor/ProjectCreationScreen.h"
#include "Horo/Editor/ProjectCreationService.h"
#include "Horo/Foundation/JobSystem.h"
#include "Horo/Foundation/Logging/Logger.h"

#include "editor/screens/welcome/WelcomeScreenGui.h"
#include "editor/screens/project_creation/ProjectCreationScreenGui.h"
#include "editor/screens/project_loading/ProjectLoadingScreenGui.h"
#include "Horo/Editor/SettingsModal.h"
#include "Horo/Editor/EditorUiComponents.h"

#include <SDL.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <SDL_opengl.h>
#endif

#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <portable-file-dialogs.h>
#include <filesystem>

namespace Horo::Editor
{
    namespace
    {

        using Theme::Fonts;


        struct EditorGuiOptions
        {
            bool textPreview = false;
            bool exitAfterFirstFrame = false;
        };

        struct EditorTextures
        {
            ImTextureID logo = 0;
        };

        [[nodiscard]] std::string AssetPath(const char *rel)
        {
            return std::string{HORO_EDITOR_ASSET_ROOT} + "/" + rel;
        }

        [[nodiscard]] const ImWchar *BuildEditorGlyphRanges(ImGuiIO &io)
        {
            // ImGui default ranges do not always include the small UI glyphs used
            // by the HTML mockups (arrows, multiplication sign, square icon,
            // middle dot). Without these, they render as '?' in the editor.
            static ImVector<ImWchar> ranges;
            if (ranges.empty())
            {
                ImFontGlyphRangesBuilder builder;
                builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
                builder.AddChar(0x00B7); // ·
                builder.AddChar(0x00D7); // ×
                builder.AddChar(0x2013); // –
                builder.AddChar(0x2014); // —
                builder.AddChar(0x2026); // …
                builder.AddChar(0x2190); // ←
                builder.AddChar(0x2191); // ↑
                builder.AddChar(0x2192); // →
                builder.AddChar(0x2193); // ↓
                builder.AddChar(0x25A1); // □
                builder.AddChar(0x25AA); // ▪
                builder.AddChar(0x25AB); // ▫
                builder.AddChar(0x2713); // ✓
                builder.BuildRanges(&ranges);
            }
            return ranges.Data;
        }

        [[nodiscard]] float QueryRasterizerDensity(SDL_Window *window)
        {
            int windowW = 0;
            int windowH = 0;
            int drawableW = 0;
            int drawableH = 0;
            SDL_GetWindowSize(window, &windowW, &windowH);
            SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);
            if (windowW <= 0 || windowH <= 0)
            {
                return 1.0F;
            }
            const float scaleX = static_cast<float>(drawableW) / static_cast<float>(windowW);
            const float scaleY = static_cast<float>(drawableH) / static_cast<float>(windowH);
            const float scale = (scaleX < scaleY) ? scaleX : scaleY;
            return (scale > 1.0F) ? scale : 1.0F;
        }

        [[nodiscard]] Fonts LoadEditorFonts(ImGuiIO &io, const float rasterizerDensity)
        {
            Fonts f;
            const ImWchar *ranges = BuildEditorGlyphRanges(io);
            ImFontConfig sansCfg{};
            sansCfg.OversampleH = 3;
            sansCfg.OversampleV = 2;
            sansCfg.RasterizerDensity = rasterizerDensity;
            ImFontConfig monoCfg{};
            monoCfg.OversampleH = 3;
            monoCfg.OversampleV = 2;
            monoCfg.RasterizerDensity = rasterizerDensity;
            ImFontConfig monoSemiBoldCfg{};
            monoSemiBoldCfg.OversampleH = 3;
            monoSemiBoldCfg.OversampleV = 2;
            monoSemiBoldCfg.RasterizerDensity = rasterizerDensity;
            f.sans = io.Fonts->AddFontFromFileTTF(
                AssetPath("fonts/inter/InterVariable.ttf").c_str(), Theme::FontPx::Sans, &sansCfg, ranges);
            f.mono = io.Fonts->AddFontFromFileTTF(
                AssetPath("fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf").c_str(), Theme::FontPx::Mono, &monoCfg, ranges);
            f.monoSemiBold = io.Fonts->AddFontFromFileTTF(
                AssetPath("fonts/ibm-plex-mono/IBMPlexMono-SemiBold.ttf").c_str(), Theme::FontPx::MonoSemiBold, &monoSemiBoldCfg, ranges);
            if (f.sans)
                io.FontDefault = f.sans;
            return f;
        }

        [[nodiscard]] EditorTextures LoadEditorTextures()
        {
            EditorTextures t;
            auto path = AssetPath("launcher/logo.png");
            int w = 0;
            int h = 0;
            int c = 0;
            auto *px = stbi_load(path.c_str(), &w, &h, &c, 4);
            if (!px)
            {
                LOG_WARN("platform.assets", "Logo texture not found at '%s' — sidebar will render without image.", path.c_str());
                return t;
            }
            GLuint tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            stbi_image_free(px);
            t.logo = (ImTextureID)(intptr_t)tex;
            return t;
        }

        // Shared components and the welcome screen renderer live under src/editor/design_system and src/editor/screens.
        // Modal implementations live under src/editor/modals.

    } // namespace

    [[nodiscard]] static EditorGuiOptions ParseOptions(int argc, char **argv) noexcept
    {
        EditorGuiOptions opts;
        for (int i = 1; i < argc; ++i)
        {
            std::string_view a{argv[i]};
            if (a == "--text-preview")
                opts.textPreview = true;
            else if (a == "--exit-after-first-frame")
                opts.exitAfterFirstFrame = true;
        }
        return opts;
    }

    [[nodiscard]] static std::vector<RecentProjectEntry> BuildBootstrapRecentProjects()
    {
        return {{{"Desert Run", "~/projects/desert-run", "2h ago", "desert-run"},
                 {"Arena Prototype", "~/projects/arena-proto", "yesterday", "arena-prototype"},
                 {"Tech Demo", "~/projects/tech-demo", "3 days ago", "tech-demo"}}};
    }

    // ── public entry ─────────────────────────────────────────────────────────

    /** @copydoc RunEditorGuiApp */
    int RunEditorGuiApp(const int argc, char **argv)
    {
        // ── Bootstrap logging before any subsystem ───────────────────────
        Log::Logger::Init("~/.horo/logs", "horo-editor");
        
        // Setup base MDC for the whole application run
        Horo::Log::LogContext appCtx("app", "horo-editor", "run_id", "1");
        
        Log::Logger::DumpStartupInfo();

        auto opts = ParseOptions(argc, argv);
        std::vector<RecentProjectEntry> recentProjects = LoadRecentProjectsFromDisk();
        WelcomeScreenController ctrl{recentProjects};
        auto vm = ctrl.BuildViewModel();

        if (opts.textPreview)
        {
            std::fputs(RenderWelcomeScreenText(vm).c_str(), stdout);
            return 0;
        }

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER))
        {
            const char *err = SDL_GetError();
            LOG_CRITICAL("platform.sdl", "SDL_Init failed: %s", err);
            std::fprintf(stderr, "SDL_Init: %s\n", err);
            Log::Logger::Shutdown();
            return 1;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        auto *w = SDL_CreateWindow("Horo Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   1000, 760, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!w)
        {
            LOG_CRITICAL("platform.sdl", "SDL_CreateWindow failed: %s", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        auto *gl = SDL_GL_CreateContext(w);
        if (!gl)
        {
            LOG_CRITICAL("platform.gl", "SDL_GL_CreateContext failed: %s", SDL_GetError());
            SDL_DestroyWindow(w);
            SDL_Quit();
            return 1;
        }
        SDL_GL_MakeCurrent(w, gl);
        SDL_GL_SetSwapInterval(1);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        auto fonts = LoadEditorFonts(io, QueryRasterizerDensity(w));
        auto textures = LoadEditorTextures();
        Theme::Apply(ImGui::GetStyle());

        // Apply saved theme preference from disk, if available.
        {
            auto doc = LoadEditorSettingsDocument();
            if (doc.loadedFromDisk && !doc.parseError)
            {
                const int savedIndex = static_cast<int>(doc.settings.themePreset);
                Theme::SelectThemeByIndex(savedIndex);
                LOG_DEBUG("editor.settings", "Restored theme preset index %d from disk.", savedIndex);
            }
            else if (doc.loadedFromDisk && doc.parseError)
            {
                LOG_WARN("editor.settings", "editor_settings.json exists but failed to parse — using defaults.");
            }
            else
            {
                LOG_DEBUG("editor.settings", "No editor_settings.json on disk — using defaults.");
            }
        }

        LOG_INFO("editor.startup", "Editor initialised — entering main loop");

        constexpr auto *glsl = "#version 150";
        ImGui_ImplSDL2_InitForOpenGL(w, gl);
        ImGui_ImplOpenGL3_Init(glsl);

        bool run = true;
        GuiRoute activeRoute{GuiRouteKind::Welcome, WelcomeRouteParameters{}};
        std::optional<GuiRoute> pendingRoute;
        ProjectCreationController projectCreation;
        ProjectCreationScreenGuiState projectCreationGuiState;
        ProjectLoadingScreenGuiState projectLoadingState;
        EngineDataBus engineEvents;
        JobSystem jobSystem{JobSystemConfig{.workerCount = 2, .maxQueuedJobs = 256}};
        ProjectCreationService projectCreationService{jobSystem, engineEvents};
        EditorDataBus editorEvents;
        const EditorSettings initialSettings = LoadEditorSettingsDocument().settings;
        ConfigurationService configuration = CreateEditorConfigurationService(initialSettings, &engineEvents);
        EditorSettingsService settings{initialSettings, configuration, editorEvents};
        EditorModalHost modalHost{editorEvents};
        while (run)
        {
            projectCreationService.PumpMainThread();
            engineEvents.DispatchQueued();
            SDL_Event ev;
            while (SDL_PollEvent(&ev))
            {
                ImGui_ImplSDL2_ProcessEvent(&ev);
                if (ev.type == SDL_QUIT)
                    run = false;
                if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE &&
                    ev.window.windowID == SDL_GetWindowID(w))
                    run = false;
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            const auto drawWelcomeRoute = [&] {
            if (activeRoute.kind == GuiRouteKind::Welcome)
            {
                const WelcomeScreenGuiResult guiResult = DrawWelcomeScreenGui(
                    vm, fonts, WelcomeScreenGuiAssets{textures.logo});

                if (guiResult.command == WelcomeScreenGuiCommand::NewProject)
                {
                    pendingRoute = GuiRoute{GuiRouteKind::ProjectCreation, ProjectCreationRouteParameters{}};
                }
                else if (guiResult.command == WelcomeScreenGuiCommand::OpenSettings)
                {
                    (void)modalHost.OpenRoot(std::make_unique<SettingsModal>(settings, fonts, textures.logo));
                }
                else if (guiResult.command == WelcomeScreenGuiCommand::OpenRecentProject)
                {
                    const int idx = guiResult.openRecentIndex;
                    if (const auto action = ctrl.RequestOpenRecentProject(static_cast<std::size_t>(idx)))
                    {
                        LOG_INFO("editor.welcome", "Opening recent project at index %d", idx);
                        // Convert to Project Loading instead of jumping directly
                        if (std::holds_alternative<EditorWorkspaceRouteParameters>(action->route.parameters)) {
                            const auto& wsParams = std::get<EditorWorkspaceRouteParameters>(action->route.parameters);
                            projectLoadingState = ProjectLoadingScreenGuiState{};
                            projectLoadingState.projectName = recentProjects[idx].name; // Best effort 
                            projectLoadingState.projectRoot = wsParams.projectRoot;
                            pendingRoute = GuiRoute{
                                GuiRouteKind::ProjectLoading,
                                ProjectLoadingRouteParameters{wsParams.projectRoot, recentProjects[idx].name}};
                        } else {
                            pendingRoute = action->route;
                        }
                    }
                }
                else if (guiResult.command == WelcomeScreenGuiCommand::OpenProject)
                {
                    LOG_INFO("editor.welcome", "Opening native folder picker for project selection.");
                    pfd::select_folder dialog("Select Horo Engine Project", "");
                    std::string folderPath = dialog.result();

                    if (!folderPath.empty())
                    {
                        std::filesystem::path path{folderPath};
                        std::string projectName = path.filename().string();
                        if (projectName.empty()) projectName = "Unknown Project";

                        LOG_INFO("editor.welcome", "Selected project folder: %s (%s)", projectName.c_str(), folderPath.c_str());

                        // Update recent projects
                        for (auto it = recentProjects.begin(); it != recentProjects.end();)
                        {
                            if (it->rootPath == folderPath)
                                it = recentProjects.erase(it);
                            else
                                ++it;
                        }
                        recentProjects.insert(recentProjects.begin(), RecentProjectEntry{projectName, folderPath, "Just now", "empty"});
                        SaveRecentProjectsToDisk(recentProjects);
                        
                        ctrl = WelcomeScreenController{recentProjects};
                        vm = ctrl.BuildViewModel();

                        // Navigate to Project Loading
                        projectLoadingState = ProjectLoadingScreenGuiState{};
                        projectLoadingState.projectName = projectName;
                        projectLoadingState.projectRoot = folderPath;
                        pendingRoute = GuiRoute{
                            GuiRouteKind::ProjectLoading,
                            ProjectLoadingRouteParameters{folderPath, projectName}};
                    }
                    else
                    {
                        LOG_INFO("editor.welcome", "Open Project cancelled by user.");
                    }
                }

                modalHost.OnUpdate(io.DeltaTime);
                modalHost.Draw();
            }
            };
            drawWelcomeRoute();

            const auto drawProjectCreationRoute = [&] {
            if (activeRoute.kind == GuiRouteKind::ProjectCreation)
            {
                const ProjectCreationScreenGuiCommand cmd = DrawProjectCreationScreenGui(projectCreation, projectCreationGuiState, fonts, textures.logo);
                if (cmd == ProjectCreationScreenGuiCommand::ReturnToWelcome)
                {
                    pendingRoute = GuiRoute{GuiRouteKind::Welcome, WelcomeRouteParameters{}};
                }
                else if (cmd == ProjectCreationScreenGuiCommand::CreateProject)
                {
                    if (const auto req = projectCreation.BuildCreationRequest())
                    {
                        if (auto handleRes = projectCreationService.StartCreate(*req); handleRes.HasValue())
                        {
                            LOG_INFO("editor.project_creation", "Starting project creation for '%s' at '%s'", req->projectName.c_str(), req->projectRoot.string().c_str());
                            while (true)
                            {
                                projectCreationService.PumpMainThread();
                                engineEvents.DispatchQueued();
                                auto snap = projectCreationService.Query(handleRes.Value().id);
                                if (!snap)
                                    break;
                                if (snap->state == ProjectCreationOperationState::Succeeded)
                                {
                                    LOG_INFO("editor.project_creation", "Project '%s' created successfully.", req->projectName.c_str());
                                    for (auto it = recentProjects.begin(); it != recentProjects.end();)
                                    {
                                        if (it->rootPath == req->projectRoot.string())
                                            it = recentProjects.erase(it);
                                        else
                                            ++it;
                                    }
                                    recentProjects.insert(recentProjects.begin(), RecentProjectEntry{req->projectName, req->projectRoot.string(), "Just now", req->templateId});
                                    SaveRecentProjectsToDisk(recentProjects);
                                    ctrl = WelcomeScreenController{recentProjects};
                                    vm = ctrl.BuildViewModel();
                                    // Navigate to Project Loading
                                    projectLoadingState = ProjectLoadingScreenGuiState{};
                                    projectLoadingState.projectName = req->projectName;
                                    projectLoadingState.projectRoot = req->projectRoot.string();
                                    pendingRoute = GuiRoute{
                                        GuiRouteKind::ProjectLoading,
                                        ProjectLoadingRouteParameters{req->projectRoot.string(), req->projectName}};
                                    break;
                                }
                                if (snap->state == ProjectCreationOperationState::Failed ||
                                    snap->state == ProjectCreationOperationState::Cancelled)
                                {
                                    if (snap->error)
                                    {
                                        LOG_ERROR("editor.project_creation", "Project creation failed: [code %d] %s", static_cast<int>(snap->error->code), snap->error->message.c_str());
                                    }
                                    else
                                    {
                                        LOG_ERROR("editor.project_creation", "Project creation operation %s.", snap->state == ProjectCreationOperationState::Cancelled ? "cancelled" : "failed");
                                    }
                                    break;
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                            }
                        }
                        else
                        {
                            LOG_ERROR("editor.project_creation", "StartCreate failed: [%s] %s", handleRes.ErrorValue().code.Value().c_str(), handleRes.ErrorValue().message.c_str());
                        }
                    }
                    else
                    {
                        LOG_ERROR("editor.project_creation", "BuildCreationRequest failed due to validation errors.");
                        for (const auto &diag : projectCreation.Validate().diagnostics)
                        {
                            LOG_ERROR("editor.project_creation", " - %s", diag.message.c_str());
                        }
                    }
                }
            }
            };
            drawProjectCreationRoute();

            const auto drawProjectLoadingRoute = [&] {
            if (activeRoute.kind == GuiRouteKind::ProjectLoading)
            {
                // Simulate loading process
                if (!projectLoadingState.isCancelled)
                {
                    projectLoadingState.progress += io.DeltaTime * 35.0f; // Approx 3 seconds to load
                    
                    if (projectLoadingState.progress < 20.0f) projectLoadingState.statusText = "Initializing asset database...";
                    else if (projectLoadingState.progress < 50.0f) projectLoadingState.statusText = "Parsing project manifests...";
                    else if (projectLoadingState.progress < 80.0f) projectLoadingState.statusText = "Loading default scene...";
                    else projectLoadingState.statusText = "Finalizing workspace...";

                    if (projectLoadingState.progress >= 100.0f)
                    {
                        projectLoadingState.progress = 100.0f;
                        LOG_INFO("editor.loading", "Project loading complete. Transitioning to EditorWorkspace.");
                        pendingRoute = GuiRoute{
                            GuiRouteKind::EditorWorkspace,
                            EditorWorkspaceRouteParameters{projectLoadingState.projectRoot, std::nullopt}};
                    }
                }

                const ProjectLoadingScreenGuiCommand cmd = DrawProjectLoadingScreenGui(projectLoadingState, fonts);
                
                if (cmd == ProjectLoadingScreenGuiCommand::Cancel)
                {
                    LOG_INFO("editor.loading", "Project loading cancelled by user.");
                    pendingRoute = GuiRoute{GuiRouteKind::Welcome, WelcomeRouteParameters{}};
                }
            }
            };
            drawProjectLoadingRoute();

            const auto drawEditorWorkspaceRoute = [&] {
            if (activeRoute.kind == GuiRouteKind::EditorWorkspace)
            {
                // ── Editor Workspace Placeholder ─────────────────────────────────
                // Full EditorWorkspaceScreen (EditorLayer, EditorPanelHost, etc.)
                // is not yet implemented. This placeholder keeps the route alive
                // and provides a minimal "Return to Welcome" escape hatch.
                const auto *viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->WorkPos);
                ImGui::SetNextWindowSize(viewport->WorkSize);
                constexpr auto wsFlags =
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

                ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg0());
                ImGui::Begin("EditorWorkspace", nullptr, wsFlags);

                const std::string *projectRoot = nullptr;
                if (std::holds_alternative<EditorWorkspaceRouteParameters>(activeRoute.parameters))
                {
                    projectRoot = &std::get<EditorWorkspaceRouteParameters>(activeRoute.parameters).projectRoot;
                }

                const ImVec2 centre = ImGui::GetContentRegionAvail();
                const float labelY = ImGui::GetCursorPosY() + centre.y * 0.4F;
                ImGui::SetCursorPosY(labelY);

                {
                    Theme::ScopedTextStyle ts(fonts.monoSemiBold, 18.0F, Theme::FontPx::MonoSemiBold);
                    const char *title = "Editor Workspace";
                    const float tw = ImGui::CalcTextSize(title).x;
                    ImGui::SetCursorPosX((centre.x - tw) * 0.5F);
                    ImGui::TextDisabled("%s", title);
                }
                ImGui::Dummy({0.0F, 8.0F});
                if (projectRoot)
                {
                    Theme::ScopedTextStyle ts(fonts.mono, 12.0F, Theme::FontPx::Mono);
                    const float pw = ImGui::CalcTextSize(projectRoot->c_str()).x;
                    ImGui::SetCursorPosX((centre.x - pw) * 0.5F);
                    ImGui::TextDisabled("%s", projectRoot->c_str());
                }
                ImGui::Dummy({0.0F, 20.0F});
                {
                    const float bw = 160.0F;
                    ImGui::SetCursorPosX((centre.x - bw) * 0.5F);
                    if (Ui::Button(Ui::ButtonProps{"← Return to Welcome",
                                                  {bw, 34.0F},
                                                  Ui::ButtonVariant::Secondary,
                                                  true,
                                                  13.0F,
                                                  fonts.sans,
                                                  Theme::FontPx::Sans}))
                    {
                        pendingRoute = GuiRoute{GuiRouteKind::Welcome, WelcomeRouteParameters{}};
                    }
                }

                ImGui::End();
                ImGui::PopStyleColor();
            }
            };
            drawEditorWorkspaceRoute();

            if (pendingRoute.has_value())
            {
                activeRoute = std::move(*pendingRoute);
                pendingRoute.reset();

                // ── Route transition log ──────────────────────────────────────
                const char *routeName = "Unknown";
                switch (activeRoute.kind)
                {
                case GuiRouteKind::Welcome:          routeName = "Welcome";          break;
                case GuiRouteKind::ProjectBrowser:   routeName = "ProjectBrowser";   break;
                case GuiRouteKind::ProjectCreation:  routeName = "ProjectCreation";  break;
                case GuiRouteKind::ProjectLoading:   routeName = "ProjectLoading";   break;
                case GuiRouteKind::EditorWorkspace:  routeName = "EditorWorkspace";  break;
                }
                LOG_DEBUG("editor.routing", "Route committed: %s", routeName);

                if (activeRoute.kind == GuiRouteKind::ProjectCreation)
                {
                    projectCreation = ProjectCreationController{};
                    projectCreationGuiState = ProjectCreationScreenGuiState{};
                }
                else if (activeRoute.kind == GuiRouteKind::Welcome)
                {
                    recentProjects = LoadRecentProjectsFromDisk();
                    ctrl = WelcomeScreenController{recentProjects};
                    vm = ctrl.BuildViewModel();
                }
                else if (activeRoute.kind == GuiRouteKind::EditorWorkspace)
                {
                    if (std::holds_alternative<EditorWorkspaceRouteParameters>(activeRoute.parameters))
                    {
                        const auto &wsParams = std::get<EditorWorkspaceRouteParameters>(activeRoute.parameters);
                        LOG_INFO("editor.workspace", "Entering workspace for project at '%s'", wsParams.projectRoot.c_str());
                    }
                }
            }

            ImGui::Render();
            int drawableW = 0;
            int drawableH = 0;
            SDL_GL_GetDrawableSize(w, &drawableW, &drawableH);
            glViewport(0, 0, drawableW, drawableH);
            glClearColor(Theme::Bg0().x, Theme::Bg0().y, Theme::Bg0().z, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(w);
            if (opts.exitAfterFirstFrame)
                run = false;
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        if (textures.logo)
        {
            auto t = (GLuint)(intptr_t)textures.logo;
            glDeleteTextures(1, &t);
        }
        SDL_GL_DeleteContext(gl);
        SDL_DestroyWindow(w);
        SDL_Quit();

        Log::Logger::Shutdown();
        return 0;
    }

} // namespace Horo::Editor