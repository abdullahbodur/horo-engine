#include "Horo/Runtime/Render/ShaderManifestErrors.h"

#include "RenderErrorDescriptor.h"

namespace Horo::Render::ShaderManifestErrors {
    namespace {
        const ErrorDomainId Domain{"render.shader_manifest"};
    }

    const ErrorCodeDescriptor InvalidLimits =
        Detail::MakeErrorDescriptor(Domain, "render.shader_manifest.invalid_limits", ErrorSeverity::Error,
                                    "Shader manifest validation limits are invalid.",
                                    "Provide non-zero finite limits within the engine shader-manifest bounds.");
    const ErrorCodeDescriptor InvalidManifest =
        Detail::MakeErrorDescriptor(Domain, "render.shader_manifest.invalid", ErrorSeverity::Error,
                                    "The shader manifest identity, version, entry points, or collection bounds are invalid.",
                                    "Use the supported schema with bounded canonical manifest records.");
    const ErrorCodeDescriptor NonCanonicalIdentity =
        Detail::MakeErrorDescriptor(Domain, "render.shader_manifest.noncanonical_identity", ErrorSeverity::Error,
                                    "Logical shader identities are duplicated or not canonically ordered.",
                                    "Publish each non-zero identity once in strictly increasing order.");
    const ErrorCodeDescriptor InvalidReference =
        Detail::MakeErrorDescriptor(Domain, "render.shader_manifest.invalid_reference", ErrorSeverity::Error,
                                    "Shader interface metadata references an undeclared binding or stage.",
                                    "Reference a declared buffer binding and only stages with declared entry points.");
    const ErrorCodeDescriptor InvalidInlineConstants =
        Detail::MakeErrorDescriptor(Domain, "render.shader_manifest.invalid_inline_constants", ErrorSeverity::Error,
                                    "Inline constant ranges overlap, overflow, or exceed a finite bound.",
                                    "Use canonical non-overlapping byte ranges inside every declared target capacity.");
    const ErrorCodeDescriptor UnsupportedTarget = Detail::
        MakeErrorDescriptor(Domain, "render.shader_manifest.unsupported_target", ErrorSeverity::Error,
                            "A shader target descriptor is malformed or cannot support the manifest.",
                            "Declare the canonical payload route and sufficient exact stage, resource, binding, and constant support.");
    const ErrorCodeDescriptor CompatibilityIdentityUnavailable =
        Detail::MakeErrorDescriptor(Domain, "render.shader_manifest.compatibility_identity_unavailable", ErrorSeverity::Error,
                                    "The shader interface compatibility identity could not be constructed.",
                                    "Reduce the manifest within its finite limits and retry at a preparation boundary.");
}  // namespace Horo::Render::ShaderManifestErrors
