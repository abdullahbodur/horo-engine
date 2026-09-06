#pragma once

/**
 * @file RenderResourceDescriptorErrors.h
 * @brief Stable typed failures for renderer resource descriptor validation.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Render::RenderResourceDescriptorErrors {
    /** @brief Initial buffer bytes are neither empty nor the declared byte size. */
    extern const ErrorCodeDescriptor BufferInitialDataInvalid;
    /** @brief Buffer size, usage, or access structure is invalid. */
    extern const ErrorCodeDescriptor BufferInvalid;
    /** @brief Sampler enums or finite numeric policy are invalid. */
    extern const ErrorCodeDescriptor SamplerInvalid;
    /** @brief Texture subresource initial-data layout is invalid. */
    extern const ErrorCodeDescriptor TextureInitialDataInvalid;
    /** @brief Texture dimension, format, range, or usage structure is invalid. */
    extern const ErrorCodeDescriptor TextureInvalid;
    /** @brief Texture-view format, aspect, or range conflicts with its source. */
    extern const ErrorCodeDescriptor TextureViewIncompatible;
    /** @brief Texture-view source identity or structure is invalid. */
    extern const ErrorCodeDescriptor TextureViewInvalid;
}  // namespace Horo::Render::RenderResourceDescriptorErrors
