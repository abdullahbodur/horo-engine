#pragma once

/**
 * @file ShaderManifestErrors.h
 * @brief Stable typed failures for shader manifest and target validation.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Render::ShaderManifestErrors {
    /** @brief Caller-supplied validation limits are zero, inconsistent, or exceed engine hard bounds. */
    extern const ErrorCodeDescriptor InvalidLimits;
    /** @brief Manifest identity, version, entry points, or collection bounds are invalid. */
    extern const ErrorCodeDescriptor InvalidManifest;
    /** @brief A logical identity is duplicated or records are not in canonical identity order. */
    extern const ErrorCodeDescriptor NonCanonicalIdentity;
    /** @brief A parameter or stage visibility references an undeclared owner. */
    extern const ErrorCodeDescriptor InvalidReference;
    /** @brief Inline constant ranges overlap, overflow, or exceed the declared bound. */
    extern const ErrorCodeDescriptor InvalidInlineConstants;
    /** @brief A target descriptor is malformed or cannot support the manifest requirements. */
    extern const ErrorCodeDescriptor UnsupportedTarget;
    /** @brief Compatibility identity construction could not allocate its bounded temporary storage. */
    extern const ErrorCodeDescriptor CompatibilityIdentityUnavailable;
}  // namespace Horo::Render::ShaderManifestErrors
