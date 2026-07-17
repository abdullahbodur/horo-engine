#include "SdlOpenGLPresentationPort.h"

#include <string>

namespace Horo::Editor
{
namespace
{
[[nodiscard]] Error MakeSdlRenderError(const char *code, const char *operation)
{
    return Error{.code = ErrorCode{code},
                 .domain = ErrorDomainId{"horo.editor.sdl.opengl"},
                 .severity = ErrorSeverity::Error,
                 .message = std::string{operation} + ": " + SDL_GetError()};
}
} // namespace

/** @copydoc SdlOpenGLPresentationPort::SdlOpenGLPresentationPort */
SdlOpenGLPresentationPort::SdlOpenGLPresentationPort(SDL_Window &window) noexcept : window_(&window)
{
}

/** @copydoc SdlOpenGLPresentationPort::CreateContext */
Result<void> SdlOpenGLPresentationPort::CreateContext(const Render::OpenGLContextDescriptor &descriptor)
{
    if (context_ != nullptr)
    {
        return Result<void>::Failure(
            MakeSdlRenderError("render.sdl.context_exists", "An OpenGL context is already retained"));
    }

    const int contextFlags = descriptor.enableDebugContext ? SDL_GL_CONTEXT_DEBUG_FLAG : 0;
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, contextFlags) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, descriptor.majorVersion) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, descriptor.minorVersion))
    {
        return Result<void>::Failure(
            MakeSdlRenderError("render.sdl.context_attributes_failed", "SDL_GL_SetAttribute failed"));
    }

    context_ = SDL_GL_CreateContext(window_);
    if (context_ == nullptr)
    {
        return Result<void>::Failure(
            MakeSdlRenderError("render.sdl.context_creation_failed", "SDL_GL_CreateContext failed"));
    }
    return Result<void>::Success();
}

/** @copydoc SdlOpenGLPresentationPort::MakeCurrent */
Result<void> SdlOpenGLPresentationPort::MakeCurrent()
{
    if (context_ == nullptr || !SDL_GL_MakeCurrent(window_, context_))
    {
        return Result<void>::Failure(MakeSdlRenderError("render.sdl.make_current_failed", "SDL_GL_MakeCurrent failed"));
    }
    return Result<void>::Success();
}

/** @copydoc SdlOpenGLPresentationPort::SetPresentMode */
Result<void> SdlOpenGLPresentationPort::SetPresentMode(const Render::PresentMode mode)
{
    int interval = 0;
    switch (mode)
    {
    case Render::PresentMode::Fifo:
        interval = 1;
        break;
    case Render::PresentMode::Immediate:
        interval = 0;
        break;
    default:
        return Result<void>::Failure(Error{.code = ErrorCode{"render.sdl.invalid_present_mode"},
                                           .domain = ErrorDomainId{"horo.editor.sdl.opengl"},
                                           .severity = ErrorSeverity::Error,
                                           .message = "Unsupported presentation mode."});
    }

    if (!SDL_GL_SetSwapInterval(interval))
    {
        return Result<void>::Failure(
            MakeSdlRenderError("render.sdl.present_mode_failed", "SDL_GL_SetSwapInterval failed"));
    }
    return Result<void>::Success();
}

/** @copydoc SdlOpenGLPresentationPort::SwapBuffers */
Result<void> SdlOpenGLPresentationPort::SwapBuffers()
{
    if (context_ == nullptr || !SDL_GL_SwapWindow(window_))
    {
        return Result<void>::Failure(MakeSdlRenderError("render.sdl.swap_failed", "SDL_GL_SwapWindow failed"));
    }
    return Result<void>::Success();
}

/** @copydoc SdlOpenGLPresentationPort::DestroyContext */
void SdlOpenGLPresentationPort::DestroyContext() noexcept
{
    if (context_ != nullptr)
    {
        SDL_GL_DestroyContext(context_);
        context_ = nullptr;
    }
}

/** @copydoc SdlOpenGLPresentationPort::Context */
SDL_GLContext SdlOpenGLPresentationPort::Context() const noexcept
{
    return context_;
}
} // namespace Horo::Editor
