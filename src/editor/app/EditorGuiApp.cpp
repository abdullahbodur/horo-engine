#include "Horo/Editor/EditorGuiApp.h"

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/WelcomeScreen.h"
#include "Horo/Foundation/Logging/Logger.h"

#include "editor/screens/welcome/WelcomeScreenGui.h"
#include "editor/modals/new_project/NewProjectModal.h"
#include "editor/modals/settings/SettingsModal.h"

#include <SDL.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <SDL_opengl.h>
#endif

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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
                std::fprintf(stderr, "logo fail: %s\n", path.c_str());
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
        Log::Logger::DumpStartupInfo();

        auto opts = ParseOptions(argc, argv);
        WelcomeScreenController ctrl{BuildBootstrapRecentProjects()};
        auto vm = ctrl.BuildViewModel();

        if (opts.textPreview)
        {
            std::fputs(RenderWelcomeScreenText(vm).c_str(), stdout);
            return 0;
        }

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER))
        {
            const char *err = SDL_GetError();
            HORO_LOG_CRITICAL("platform.sdl", "SDL_Init failed: %s", err);
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
            std::fprintf(stderr, "Window: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        auto *gl = SDL_GL_CreateContext(w);
        if (!gl)
        {
            std::fprintf(stderr, "GL: %s\n", SDL_GetError());
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
        HORO_LOG_INFO("editor.startup", "Editor initialised — entering main loop");

        constexpr auto *glsl = "#version 150";
        ImGui_ImplSDL2_InitForOpenGL(w, gl);
        ImGui_ImplOpenGL3_Init(glsl);

        bool run = true;
        NewProjectState np;
        SettingsState sets;
        while (run)
        {
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

            if (const WelcomeScreenGuiCommand command = DrawWelcomeScreenGui(
                    vm, fonts, WelcomeScreenGuiAssets{textures.logo});
                command == WelcomeScreenGuiCommand::NewProject)
            {
                np.open = true;
                ImGui::OpenPopup("New Project");
            }
            else if (command == WelcomeScreenGuiCommand::OpenSettings)
            {
                sets.open = true;
                ImGui::OpenPopup("Settings");
            }

            DrawNewProjectModal(np, fonts, textures.logo);
            DrawSettingsModal(sets, fonts, textures.logo);

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