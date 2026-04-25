#pragma once
#include <memory>
#include <string>

#include "core/EngineLaunchArgs.h"
#include "core/Window.h"

namespace Monolith {
    struct AppSpec {
        std::string name = "Monolith App";
        int width = 1280;
        int height = 720;
        bool vsync = true;
        // Repo-relative path to the main scene file (e.g.
        // "assets/scenes/scene.json"). Resolved to an absolute path via ProjectPath
        // at construction time.
        std::string defaultSceneFile;
        // Kept after defaultSceneFile to preserve aggregate-initialization
        // compatibility with existing starter/integration apps that pass scene path
        // as the 5th field.
        WindowGraphicsApi graphicsApi = WindowGraphicsApi::OpenGL;
    };

    class Application {
    public:
        explicit Application(const AppSpec &spec);

        virtual ~Application();

        Application(const Application &) = delete;

        Application &operator=(const Application &) = delete;

        // Parse standard engine CLI flags from main()'s argv. Call before Run().
        //   --editor   Force editor on startup
        //   --play     Force game-only startup (no editor)
        //   (no flag)  Release (NDEBUG): game; Debug: editor
        void ParseArgs(int argc, char **argv);

        // True when the app should open the editor on startup (after ParseArgs).
        bool ShouldStartWithEditor() const;

        // Deprecated alias for ShouldStartWithEditor().
        bool IsEditorModeRequested() const { return ShouldStartWithEditor(); }

        void Run();

        Window &GetWindow() { return *m_window; }

        // Absolute path to the default scene file set in AppSpec.
        // Empty if none was provided.
        const std::string &GetDefaultSceneFilePath() const {
            return m_defaultSceneFilePath;
        }

    protected:
        // Subclasses override these
        virtual void OnInit() {
            /* intentionally empty — subclasses override only what they need */
        }

        virtual void OnUpdate(float /*dt*/) {
            // intentionally empty — subclasses override only what they need
        } // variable-rate update (rendering, input)
        virtual void OnFixedUpdate(float /*dt*/) {
            /* intentionally empty — subclasses override only what they need */
        } // fixed-rate update (physics)
        virtual void OnRender(float /*alpha*/) {
            // intentionally empty — subclasses override only what they need
        } // alpha = interpolation factor [0,1]
        virtual void OnShutdown() {
            /* intentionally empty — subclasses override only what they need */
        }

    private:
        std::unique_ptr<Window> m_window;
        bool m_running = true;
        EditorStartupCli m_editorStartupCli = EditorStartupCli::Default;
        std::string m_defaultSceneFilePath;
    };
} // namespace Monolith
