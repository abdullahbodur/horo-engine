#include "SdlMetalPresentationPort.h"

#include <SDL3/SDL_metal.h>

#import <QuartzCore/CAMetalLayer.h>

#include <string>
#include <utility>

namespace Horo::Editor
{
namespace
{
[[nodiscard]] Error MakeMetalSurfaceError(const char *code, std::string message)
{
    return Error{.code = ErrorCode{code},
                 .domain = ErrorDomainId{"horo.editor.sdl.metal"},
                 .severity = ErrorSeverity::Error,
                 .message = std::move(message)};
}
} // namespace

struct SdlMetalPresentationPort::Impl
{
    explicit Impl(SDL_Window &borrowedWindow) noexcept : window(&borrowedWindow)
    {
    }

    SDL_Window *window{nullptr};
    SDL_MetalView view{nullptr};
    __strong CAMetalLayer *layer{nil};
};

/** @copydoc SdlMetalPresentationPort::SdlMetalPresentationPort */
SdlMetalPresentationPort::SdlMetalPresentationPort(SDL_Window &window) noexcept : impl_(std::make_unique<Impl>(window))
{
}

/** @copydoc SdlMetalPresentationPort::~SdlMetalPresentationPort */
SdlMetalPresentationPort::~SdlMetalPresentationPort()
{
    DestroySurface();
}

/** @copydoc SdlMetalPresentationPort::CreateSurface */
Result<void> SdlMetalPresentationPort::CreateSurface()
{
    if (impl_->view != nullptr)
    {
        return Result<void>::Failure(MakeMetalSurfaceError(
            "render.metal.surface_exists", "An SDL Metal surface attachment is already retained."));
    }

    impl_->view = SDL_Metal_CreateView(impl_->window);
    if (impl_->view == nullptr)
    {
        return Result<void>::Failure(MakeMetalSurfaceError(
            "render.metal.view_creation_failed", std::string{"SDL_Metal_CreateView failed: "} + SDL_GetError()));
    }

    impl_->layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(impl_->view);
    if (impl_->layer == nil)
    {
        SDL_Metal_DestroyView(impl_->view);
        impl_->view = nullptr;
        return Result<void>::Failure(MakeMetalSurfaceError(
            "render.metal.layer_unavailable", "SDL Metal view did not expose a CAMetalLayer."));
    }
    return Result<void>::Success();
}

/** @copydoc SdlMetalPresentationPort::Layer */
void *SdlMetalPresentationPort::Layer() const noexcept
{
    return (__bridge void *)impl_->layer;
}

/** @copydoc SdlMetalPresentationPort::DestroySurface */
void SdlMetalPresentationPort::DestroySurface() noexcept
{
    impl_->layer = nil;
    if (impl_->view != nullptr)
    {
        SDL_Metal_DestroyView(impl_->view);
        impl_->view = nullptr;
    }
}
} // namespace Horo::Editor
