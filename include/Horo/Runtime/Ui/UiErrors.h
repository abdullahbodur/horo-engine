#pragma once

/**
 * @file UiErrors.h
 * @brief Stable errors for backend-neutral Runtime UI contracts.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Runtime::Ui::UiErrors {
    /** @brief A stable authored identity is the reserved all-zero value. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief A runtime ownership generation is the reserved zero value. */
    extern const ErrorCodeDescriptor OwnershipGenerationInvalid;
    /** @brief A runtime handle has an invalid owner, slot, or slot generation. */
    extern const ErrorCodeDescriptor HandleMalformed;
    /** @brief A runtime handle belongs to another service, scope, or instance incarnation. */
    extern const ErrorCodeDescriptor HandleOwnerMismatch;
    /** @brief A runtime handle names an absent, retired, or replaced slot generation. */
    extern const ErrorCodeDescriptor HandleStale;
    /** @brief A revision is the reserved zero value. */
    extern const ErrorCodeDescriptor RevisionInvalid;
    /** @brief An expected revision no longer matches the owner-published revision. */
    extern const ErrorCodeDescriptor RevisionStale;
    /** @brief A generation or revision cannot advance without wrapping. */
    extern const ErrorCodeDescriptor GenerationExhausted;
}  // namespace Horo::Runtime::Ui::UiErrors
