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
    /** @brief A retained element tree is malformed, disconnected, cyclic, or exceeds its declared depth. */
    extern const ErrorCodeDescriptor ElementTreeInvalid;
    /** @brief A retained element tree repeats a stable authored identity. */
    extern const ErrorCodeDescriptor ElementTreeIdentityConflict;
    /** @brief A structural command has an invalid safe point, identity, or child position. */
    extern const ErrorCodeDescriptor StructuralCommandInvalid;
    /** @brief A structural command would remove the root or create a cyclic/conflicting topology. */
    extern const ErrorCodeDescriptor StructuralCommandConflict;
    /** @brief A retained element tree is retiring, stopped, or otherwise unavailable for the request. */
    extern const ErrorCodeDescriptor ElementTreeLifecycleUnavailable;
    /** @brief Immutable Runtime UI render snapshot evidence or table topology is malformed. */
    extern const ErrorCodeDescriptor RenderSnapshotInvalid;
    /** @brief A Runtime UI draw command contains invalid geometry, paint, or table references. */
    extern const ErrorCodeDescriptor RenderCommandInvalid;
    /** @brief A Runtime UI render resource identity, role, or revision is invalid. */
    extern const ErrorCodeDescriptor RenderResourceReferenceInvalid;
    /** @brief Runtime UI diagnostic evidence is malformed or exceeds its fixed bounds. */
    extern const ErrorCodeDescriptor DiagnosticInvalid;
    /** @brief A Runtime UI diagnostic category or source error is not part of the declared contract. */
    extern const ErrorCodeDescriptor DiagnosticUnsupported;
}  // namespace Horo::Runtime::Ui::UiErrors
