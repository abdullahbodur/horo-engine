#pragma once

#include "OpenGLBackendModule.h"

#include <cstdint>

namespace Horo::Render::Detail
{
    using OpenGLViewportFunction = void (*)(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
    using OpenGLClearColorFunction = void (*)(float red, float green, float blue, float alpha);
    using OpenGLClearFunction = void (*)(std::uint32_t mask);

    struct OpenGLCommandFunctions
    {
        OpenGLViewportFunction viewport{nullptr};
        OpenGLClearColorFunction clearColor{nullptr};
        OpenGLClearFunction clear{nullptr};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return viewport != nullptr && clearColor != nullptr && clear != nullptr;
        }
    };

    [[nodiscard]] Result<void> RegisterOpenGLRenderBackendWithFunctions(RenderBackendRegistry& registry,
                                                                        IOpenGLPresentationPort& presentationPort,
                                                                        OpenGLBackendOptions options,
                                                                        OpenGLCommandFunctions functions);
} // namespace Horo::Render::Detail
