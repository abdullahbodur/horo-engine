#include "EditorUiTestSurface.h"

#include "editor/renderer/opengl/EditorGuiRendererOpenGL.h"

#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <imgui_impl_sdl3.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace Horo::Tests
{
namespace
{
[[nodiscard]] std::runtime_error MakeSdlError(const char *operation)
{
    return std::runtime_error(std::string{operation} + ": " + SDL_GetError());
}

class InteractiveOpenGlEditorUiTestSurface final : public IEditorUiTestSurface
{
  public:
    ~InteractiveOpenGlEditorUiTestSurface() override
    {
        Shutdown();
    }

    void Initialize(ImGuiContext &context) override
    {
        if (initialized_)
            throw std::logic_error("Interactive UI test surface is already initialized.");
        ImGui::SetCurrentContext(&context);

        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            throw MakeSdlError("SDL_InitSubSystem(SDL_INIT_VIDEO) failed");
        ownsVideoSubsystem_ = true;
        if (!SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) || !SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24) ||
            !SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8) ||
            !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) ||
            !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) ||
            !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2))
        {
            throw MakeSdlError("SDL_GL_SetAttribute failed");
        }

        window_ = SDL_CreateWindow("Horo Editor UI Test", 1280, 800,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (window_ == nullptr)
            throw MakeSdlError("SDL_CreateWindow failed");
        context_ = SDL_GL_CreateContext(window_);
        if (context_ == nullptr)
            throw MakeSdlError("SDL_GL_CreateContext failed");
        if (!SDL_GL_MakeCurrent(window_, context_))
            throw MakeSdlError("SDL_GL_MakeCurrent failed");
        if (gladLoadGL(SDL_GL_GetProcAddress) == 0)
            throw std::runtime_error("Unable to load OpenGL functions for the interactive UI test surface.");
        static_cast<void>(SDL_GL_SetSwapInterval(1));

        renderer_ = std::make_unique<Editor::EditorGuiRendererOpenGL>(*window_, context_);
        if (const auto initialized = renderer_->Initialize(); initialized.HasError())
            throw std::runtime_error(initialized.ErrorValue().message);
        initialized_ = true;
    }

    [[nodiscard]] bool BeginFrame() override
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window_)))
            {
                return false;
            }
        }
        if (const auto begun = renderer_->BeginFrame(); begun.HasError())
            throw std::runtime_error(begun.ErrorValue().message);
        return true;
    }

    void Present() override
    {
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSizeInPixels(window_, &width, &height))
            throw MakeSdlError("SDL_GetWindowSizeInPixels failed");
        glViewport(0, 0, width, height);
        glClearColor(0.035F, 0.045F, 0.065F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        if (const auto rendered = renderer_->RenderDrawData(); rendered.HasError())
            throw std::runtime_error(rendered.ErrorValue().message);
        if (!SDL_GL_SwapWindow(window_))
            throw MakeSdlError("SDL_GL_SwapWindow failed");
    }

    void Shutdown() noexcept override
    {
        if (renderer_ != nullptr)
        {
            renderer_->Shutdown();
            renderer_.reset();
        }
        if (context_ != nullptr)
        {
            SDL_GL_DestroyContext(context_);
            context_ = nullptr;
        }
        if (window_ != nullptr)
        {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if (ownsVideoSubsystem_)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            ownsVideoSubsystem_ = false;
        }
        initialized_ = false;
    }

    [[nodiscard]] bool IsInteractive() const noexcept override
    {
        return true;
    }

  private:
    SDL_Window *window_{nullptr};
    SDL_GLContext context_{nullptr};
    std::unique_ptr<Editor::EditorGuiRendererOpenGL> renderer_;
    bool ownsVideoSubsystem_{false};
    bool initialized_{false};
};
} // namespace

std::unique_ptr<IEditorUiTestSurface> CreateInteractiveOpenGlEditorUiTestSurface()
{
    return std::make_unique<InteractiveOpenGlEditorUiTestSurface>();
}
} // namespace Horo::Tests
