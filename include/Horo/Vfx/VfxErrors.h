#pragma once

/**
 * @file VfxErrors.h
 * @brief Stable VFX identity and quality-resolution errors independent of simulation and rendering backends.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Vfx::VfxErrors {
    /** @brief An identity contains a reserved scope, slot, or generation value. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief An identity does not name the requested owner slot. */
    extern const ErrorCodeDescriptor IdentityUnknown;
    /** @brief An identity names a known slot with a retired generation. */
    extern const ErrorCodeDescriptor IdentityStale;
    /** @brief A generation cannot advance without wrapping into a reserved value. */
    extern const ErrorCodeDescriptor GenerationExhausted;
    /** @brief Canonical serialized identity bytes are malformed. */
    extern const ErrorCodeDescriptor SerializedIdentityInvalid;
    /** @brief Capability evidence is malformed, incomplete, or contradictory. */
    extern const ErrorCodeDescriptor CapabilityDataInvalid;
    /** @brief A quality policy contains an invalid profile, revision, budget, or threshold. */
    extern const ErrorCodeDescriptor QualityPolicyInvalid;
    /** @brief An effect requirement or authored fallback variant is malformed. */
    extern const ErrorCodeDescriptor RequirementInvalid;
    /** @brief Authored simulation intent conflicts with a gameplay-mandatory CPU path. */
    extern const ErrorCodeDescriptor DomainConflict;
    /** @brief No permitted path satisfies the required capability facts. */
    extern const ErrorCodeDescriptor UnsupportedCapability;
    /** @brief A required CPU or GPU simulation kernel is absent. */
    extern const ErrorCodeDescriptor MissingKernel;
    /** @brief No authored compatible fallback variant can satisfy the request. */
    extern const ErrorCodeDescriptor MissingVariant;
    /** @brief A count, memory, or work request exceeds an effective finite limit. */
    extern const ErrorCodeDescriptor LimitExceeded;
    /** @brief A prepared decision references a retired capability revision. */
    extern const ErrorCodeDescriptor CapabilityRevisionStale;
    /** @brief A prepared decision references a retired quality-policy revision. */
    extern const ErrorCodeDescriptor QualityPolicyRevisionStale;
}  // namespace Horo::Vfx::VfxErrors
