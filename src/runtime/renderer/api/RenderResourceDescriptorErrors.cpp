#include "Horo/Runtime/Render/RenderResourceDescriptorErrors.h"

#include "RenderErrorDescriptor.h"

namespace Horo::Render::RenderResourceDescriptorErrors {
    namespace {
        const ErrorDomainId Domain{"render.resource_descriptor"};
    }  // namespace

    const ErrorCodeDescriptor BufferInitialDataInvalid =
        Detail::MakeErrorDescriptor(Domain, "render.resource_descriptor.buffer_initial_data_invalid", ErrorSeverity::Error,
                                    "Buffer initial data does not match the immutable buffer descriptor.",
                                    "Provide no initial bytes or exactly the declared buffer byte size.");
    const ErrorCodeDescriptor BufferInvalid =
        Detail::MakeErrorDescriptor(Domain, "render.resource_descriptor.buffer_invalid", ErrorSeverity::Error,
                                    "The buffer descriptor is structurally invalid.",
                                    "Use a non-zero size, declared usage flags, and a declared memory-access policy.");
    const ErrorCodeDescriptor SamplerInvalid =
        Detail::MakeErrorDescriptor(Domain, "render.resource_descriptor.sampler_invalid", ErrorSeverity::Error,
                                    "The sampler descriptor is structurally invalid.",
                                    "Use declared filter, address, comparison, finite LOD, and anisotropy values.");
    const ErrorCodeDescriptor TextureInitialDataInvalid =
        Detail::MakeErrorDescriptor(Domain, "render.resource_descriptor.texture_initial_data_invalid", ErrorSeverity::Error,
                                    "Texture initial-data records are malformed or incompatible with the descriptor.",
                                    "Provide canonical unique subresources with sufficient checked row, slice, and byte extents.");
    const ErrorCodeDescriptor TextureInvalid =
        Detail::MakeErrorDescriptor(Domain, "render.resource_descriptor.texture_invalid", ErrorSeverity::Error,
                                    "The texture descriptor is structurally invalid.",
                                    "Use declared dimensions, formats, usages, and non-zero bounded extents and ranges.");
    const ErrorCodeDescriptor TextureViewIncompatible =
        Detail::MakeErrorDescriptor(Domain, "render.resource_descriptor.texture_view_incompatible", ErrorSeverity::Error,
                                    "The texture view is incompatible with its immutable source texture descriptor.",
                                    "Use a compatible format and aspect with mip and layer ranges inside the source texture.");
    const ErrorCodeDescriptor TextureViewInvalid =
        Detail::MakeErrorDescriptor(Domain, "render.resource_descriptor.texture_view_invalid", ErrorSeverity::Error,
                                    "The texture-view descriptor is structurally invalid.",
                                    "Use a valid exact texture handle, declared format and aspect, and non-empty ranges.");
}  // namespace Horo::Render::RenderResourceDescriptorErrors
