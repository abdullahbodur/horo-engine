#pragma once

/**
 * @file PrefabErrors.h
 * @brief Stable prefab authoring identity and document validation errors.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Prefab::PrefabErrors {
    /** @brief A required prefab identity is invalid. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief A prefab-local address contains an invalid or excessive scope. */
    extern const ErrorCodeDescriptor AddressInvalid;
    /** @brief A prefab reference does not use a valid Asset Registry identity. */
    extern const ErrorCodeDescriptor ReferenceInvalid;
}  // namespace Horo::Prefab::PrefabErrors
