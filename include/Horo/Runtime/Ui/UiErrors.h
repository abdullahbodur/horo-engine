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
    /** @brief Authored document or canvas metadata is invalid. */
    extern const ErrorCodeDescriptor DocumentInvalid;
    /** @brief A document repeats a stable canvas or root-element identity. */
    extern const ErrorCodeDescriptor DocumentDuplicateIdentity;
    /** @brief An asset dependency is malformed or conflicts with an earlier requirement. */
    extern const ErrorCodeDescriptor DependencyInvalid;
    /** @brief A bounded document or cooked payload limit was exceeded. */
    extern const ErrorCodeDescriptor CapacityExceeded;
    /** @brief Cooked bytes are empty or exceed the declared representation contract. */
    extern const ErrorCodeDescriptor PayloadInvalid;
    /** @brief A scene/component canvas reference lacks stable identity or revision evidence. */
    extern const ErrorCodeDescriptor CanvasReferenceInvalid;
    /** @brief A runtime instance cannot admit the requested lifecycle transition. */
    extern const ErrorCodeDescriptor InstanceStateInvalid;
}  // namespace Horo::Runtime::Ui::UiErrors
