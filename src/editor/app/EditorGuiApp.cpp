#include "Horo/Editor/EditorGuiApp.h"

#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorSettingsEvents.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/GuiRoute.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "Horo/Editor/Localization/LocalizationService.h"
#include "Horo/Editor/ProjectCreationController.h"
#include "Horo/Editor/ProjectCreationService.h"
#include "Horo/Editor/WelcomeController.h"
#include "Horo/Foundation/DataBus.h"
#include "Horo/Foundation/JobSystem.h"
#include "Horo/Foundation/Logging/Logger.h"

#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/SettingsModal.h"
#include "editor/screens/project_creation/ProjectCreationView.h"
#include "editor/screens/project_loading/ProjectLoadingView.h"
#include "editor/screens/welcome/WelcomeView.h"

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
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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

[[nodiscard]] bool LoadEditorCatalogResources(LocalizationService &localization)
{
    const std::filesystem::path root = std::filesystem::path{HORO_EDITOR_ASSET_ROOT} / "localization" / "editor";
    bool loadedAny = false;
    std::error_code error;
    if (!std::filesystem::exists(root, error))
        return false;
    for (const auto &entry : std::filesystem::directory_iterator(root, error))
    {
        if (error || !entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        LocalizationError loadError;
        loadedAny = localization.LoadCatalogFile(entry.path(), &loadError) || loadedAny;
    }
    return loadedAny;
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
        // Add Latin Extended-A for Turkish and other European characters
        static constexpr std::array<ImWchar, 3> latinExtendedA = {0x0100, 0x017F, 0};
        builder.AddRanges(latinExtendedA.data());

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
    f.sans = io.Fonts->AddFontFromFileTTF(AssetPath("fonts/inter/InterVariable.ttf").c_str(), Theme::FontPx::Sans,
                                          &sansCfg, ranges);
    f.mono = io.Fonts->AddFontFromFileTTF(AssetPath("fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf").c_str(),
                                          Theme::FontPx::Mono, &monoCfg, ranges);
    f.monoSemiBold = io.Fonts->AddFontFromFileTTF(AssetPath("fonts/ibm-plex-mono/IBMPlexMono-SemiBold.ttf").c_str(),
                                                  Theme::FontPx::MonoSemiBold, &monoSemiBoldCfg, ranges);
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
        LOG_WARN("platform.assets", "Logo texture not found at '%s' — sidebar will render without image.",
                 path.c_str());
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

[[nodiscard]] static EditorGuiOptions ParseOptions(const std::span<char *> args) noexcept
{
    EditorGuiOptions opts;
    for (std::size_t i = 1; i < args.size(); ++i)
    {
        std::string_view a{args[i]};
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

namespace
{
[[nodiscard]] bool InitializeSdlAndOpenGl(SDL_Window *&window, SDL_GLContext &glContext)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER))
    {
        const char *err = SDL_GetError();
        LOG_CRITICAL("platform.sdl", "SDL_Init failed: %s", err);
        std::fprintf(stderr, "SDL_Init: %s\n", err);
        Log::Logger::Shutdown();
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    window = SDL_CreateWindow("Horo Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1000, 760,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window)
    {
        LOG_CRITICAL("platform.sdl", "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    glContext = SDL_GL_CreateContext(window);
    if (!glContext)
    {
        LOG_CRITICAL("platform.gl", "SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1);
    return true;
}

void ApplySavedThemePreference()
{
    auto doc = LoadEditorSettingsDocument();
    if (doc.loadedFromDisk && !doc.parseError)
    {
        const auto savedIndex = static_cast<int>(doc.settings.themePreset);
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

void ActivateInitialLocale(const EditorSettings &initialSettings, LocalizationService &localization)
{
    LocalizationError localizationError;
    if (const auto locale = LocaleTag::Parse(initialSettings.languageTag); locale.has_value())
    {
        if (localization.Prepare(*locale, &localizationError))
        {
            (void)localization.ActivatePrepared(&localizationError);
            LOG_INFO("editor.startup", "Activated language tag: '%s'", locale->value.c_str());
        }
        else
        {
            LOG_WARN("editor.startup", "Failed to prepare language tag '%s': %s", locale->value.c_str(),
                     localizationError.message.c_str());
        }
    }
    else
    {
        LOG_WARN("editor.startup", "Failed to parse language tag: '%s'", initialSettings.languageTag.c_str());
    }
}

struct RunEditorMainLoopParams
{
    bool exitAfterFirstFrame;
    SDL_Window *window;
    ImGuiIO &io;
    const Fonts &fonts;
    const EditorTextures &textures;
    ProjectCreationService &projectCreationService;
    EditorSettingsService &settings;
    EngineDataBus &engineEvents;
    EditorDataBus &editorEvents;
    LocalizationService &localization;
    EditorModalHost &modalHost;
};

void RunEditorMainLoop(RunEditorMainLoopParams &p)
{
    ThemeContext themeContext{p.fonts};
    EditorSettingsSnapshot settingsSnapshot = p.settings.Snapshot();
    EditorGuiContext guiContext{p.engineEvents, p.editorEvents, p.localization, themeContext, settingsSnapshot};

    GuiScreenHost screenHost{guiContext,
                             p.modalHost,
                             p.settings,
                             p.localization,
                             p.engineEvents,
                             p.projectCreationService,
                             (std::uintptr_t)(void *)(intptr_t)p.textures.logo};
    static_cast<void>(screenHost.Navigate(GuiRoute{GuiRouteKind::Welcome, WelcomeRouteParameters{}}));

    while (!screenHost.IsApplicationCloseRequested())
    {
        p.projectCreationService.PumpMainThread();
        p.engineEvents.DispatchQueued();
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT || (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE &&
                                        ev.window.windowID == SDL_GetWindowID(p.window)))
            {
                static_cast<void>(screenHost.RequestCloseApplication());
            }
        }

        if (screenHost.IsApplicationCloseRequested())
        {
            break;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        settingsSnapshot = p.settings.Snapshot();
        p.modalHost.OnUpdate(p.io.DeltaTime);
        screenHost.OnUpdate(p.io.DeltaTime);
        screenHost.Draw();
        p.modalHost.Draw();

        ImGui::Render();
        int drawableW = 0;
        int drawableH = 0;
        SDL_GL_GetDrawableSize(p.window, &drawableW, &drawableH);
        glViewport(0, 0, drawableW, drawableH);
        glClearColor(Theme::Bg0().x, Theme::Bg0().y, Theme::Bg0().z, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(p.window);

        if (p.exitAfterFirstFrame)
        {
            static_cast<void>(screenHost.RequestCloseApplication());
            break;
        }
    }
}
} // namespace

// ── public entry ─────────────────────────────────────────────────────────

/** @copydoc RunEditorGuiApp */
int RunEditorGuiApp(const int argc, char **argv)
{
    // ── Bootstrap logging before any subsystem ───────────────────────
    Log::Logger::Init("~/.horo/logs", "horo-editor");

    // Setup base MDC for the whole application run
    Horo::Log::LogContext appCtx("app", "horo-editor", "run_id", "1");

    Log::Logger::DumpStartupInfo();

    auto opts = ParseOptions(std::span<char *>{argv, static_cast<std::size_t>(argc)});
    std::vector<RecentProjectEntry> recentProjects = LoadRecentProjectsFromDisk();
    WelcomeScreenController ctrl{recentProjects};
    auto vm = ctrl.BuildViewModel();

    if (opts.textPreview)
    {
        std::fputs(RenderWelcomeScreenText(vm).c_str(), stdout);
        return 0;
    }

    SDL_Window *w = nullptr;
    SDL_GLContext gl = nullptr;
    if (!InitializeSdlAndOpenGl(w, gl))
        return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    auto fonts = LoadEditorFonts(io, QueryRasterizerDensity(w));
    auto textures = LoadEditorTextures();
    Theme::Apply(ImGui::GetStyle());

    ApplySavedThemePreference();

    LOG_INFO("editor.startup", "Editor initialised — entering main loop");

    constexpr auto *glsl = "#version 150";
    ImGui_ImplSDL2_InitForOpenGL(w, gl);
    ImGui_ImplOpenGL3_Init(glsl);

    EngineDataBus engineEvents;
    JobSystem jobSystem{JobSystemConfig{.workerCount = 2, .maxQueuedJobs = 256}};
    ProjectCreationService projectCreationService{jobSystem, engineEvents};
    EditorDataBus editorEvents;
    const EditorSettings initialSettings = LoadEditorSettingsDocument().settings;
    LOG_INFO("editor.startup", "Loaded language tag from disk: '%s'", initialSettings.languageTag.c_str());
    LocalizationService localization{LocaleTag{"en-US"}};
    const bool loadedCatalogs = LoadEditorCatalogResources(localization);
    LOG_INFO("editor.startup", "Catalog resources loaded: %s", loadedCatalogs ? "true" : "false");
    ActivateInitialLocale(initialSettings, localization);

    ConfigurationService configuration = CreateEditorConfigurationService(initialSettings, &engineEvents);
    EditorSettingsService settings{initialSettings, configuration, editorEvents, localization};

    const Subscription localizationSub = editorEvents.Subscribe<EditorSettingsChangedEvent>(
        [&settings, &localization](const EditorSettingsChangedEvent &event) {
            if (event.phase == SettingsChangePhase::Committed)
            {
                if (const auto loc = LocaleTag::Parse(settings.Snapshot().settings.languageTag);
                    loc.has_value() && localization.ActiveLocale() != *loc && localization.Prepare(*loc))
                {
                    (void)localization.ActivatePrepared();
                }
            }
        });

    EditorModalHost modalHost{editorEvents};
    RunEditorMainLoopParams loopParams{opts.exitAfterFirstFrame,
                                       w,
                                       io,
                                       fonts,
                                       textures,
                                       projectCreationService,
                                       settings,
                                       engineEvents,
                                       editorEvents,
                                       localization,
                                       modalHost};
    RunEditorMainLoop(loopParams);

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
