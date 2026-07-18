#include "SdlOpenGLPresentationPort.h"
#include "editor/renderer/EditorRendererErrors.h"

#include <string>

namespace Horo::Editor
{
namespace
{
[[nodiscard]] Error MakeSdlRenderError(const ErrorCodeDescriptor &descriptor, const char *operation)
{
    return MakeError(descriptor, std::string{operation} + ": " + SDL_GetError());
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
            MakeSdlRenderError(RendererErrors::SdlContextExists, "An OpenGL context is already retained"));
    }

    const int contextFlags = descriptor.enableDebugContext ? SDL_GL_CONTEXT_DEBUG_FLAG : 0;
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, contextFlags) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, descriptor.majorVersion) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, descriptor.minorVersion))
    {
        return Result<void>::Failure(
            MakeSdlRenderError(RendererErrors::SdlContextAttributesFailed, "SDL_GL_SetAttribute failed"));
    }

    context_ = SDL_GL_CreateContext(window_);
    if (context_ == nullptr)
    {
        return Result<void>::Failure(
            MakeSdlRenderError(RendererErrors::SdlContextCreationFailed, "SDL_GL_CreateContext failed"));
    }
    return Result<void>::Success();
}

/** @copydoc SdlOpenGLPresentationPort::MakeCurrent */
Result<void> SdlOpenGLPresentationPort::MakeCurrent()
{
    if (context_ == nullptr || !SDL_GL_MakeCurrent(window_, context_))
    {
        return Result<void>::Failure(
            MakeSdlRenderError(RendererErrors::SdlMakeCurrentFailed, "SDL_GL_MakeCurrent failed"));
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
        return Result<void>::Failure(
            MakeError(RendererErrors::SdlInvalidPresentMode, "Unsupported presentation mode."));
    }

    if (!SDL_GL_SetSwapInterval(interval))
    {
        return Result<void>::Failure(
            MakeSdlRenderError(RendererErrors::SdlPresentModeFailed, "SDL_GL_SetSwapInterval failed"));
    }
    return Result<void>::Success();
}

/** @copydoc SdlOpenGLPresentationPort::SwapBuffers */
Result<void> SdlOpenGLPresentationPort::SwapBuffers()
{
    if (context_ == nullptr || !SDL_GL_SwapWindow(window_))
    {
        return Result<void>::Failure(MakeSdlRenderError(RendererErrors::SdlSwapFailed, "SDL_GL_SwapWindow failed"));
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
