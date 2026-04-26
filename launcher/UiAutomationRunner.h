#pragma once

#include <memory>

struct GLFWwindow;

namespace Horo::Launcher {
    class LauncherEditorShell;
}

namespace Horo {
    class UiAutomationRunner {
    public:
        static void PrepareEnvironmentBeforeAppStart(bool runUiAutomation);

        UiAutomationRunner();

        ~UiAutomationRunner();

        UiAutomationRunner(const UiAutomationRunner &) = delete;

        UiAutomationRunner &operator=(const UiAutomationRunner &) = delete;

        void StartIfRequested(bool runUiAutomation,
                              Launcher::LauncherEditorShell *shellContext) const;

        void PostRenderFrame(GLFWwindow *nativeWindowHandle) const;

        void Shutdown() const;

        void DestroyContext() const;

        bool DidPass() const;

        bool IsActive() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Horo
