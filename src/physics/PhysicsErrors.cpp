#include "Horo/Physics/PhysicsErrors.h"

namespace Horo::Physics::PhysicsErrors {
    namespace {
        const ErrorDomainId PhysicsDomain{"horo.physics"};
    }

    const ErrorCodeDescriptor WorldInvalid{
        .domain = PhysicsDomain,
        .code = ErrorCode{"physics.world.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The physics world identity is invalid.",
        .remediationHint = "Use the non-zero world generation published by the owning scene activation.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor HandleMalformed{
        .domain = PhysicsDomain,
        .code = ErrorCode{"physics.handle.malformed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The physics handle is malformed.",
        .remediationHint = "Use a typed handle issued by the owning world with a valid index and non-zero slot generation.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor HandleWorldMismatch{
        .domain = PhysicsDomain,
        .code = ErrorCode{"physics.handle.world_mismatch"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The physics handle belongs to another world generation.",
        .remediationHint = "Resolve the stable scene binding again against the active physics world.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor HandleStale{
        .domain = PhysicsDomain,
        .code = ErrorCode{"physics.handle.stale"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The physics object slot is absent or its generation has retired.",
        .remediationHint = "Discard the handle and resolve its stable binding before submitting new work.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor GenerationExhausted{
        .domain = PhysicsDomain,
        .code = ErrorCode{"physics.generation.exhausted"},
        .defaultSeverity = ErrorSeverity::Critical,
        .summary = "The physics identity generation range is exhausted.",
        .remediationHint = "Retire exhausted object slots; never wrap identities or reuse a published world generation.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor CapabilityUnavailable{
        .domain = PhysicsDomain,
        .code = ErrorCode{"physics.capability.unavailable"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A required physics capability is unavailable.",
        .remediationHint =
            "Select a composition and qualified profile providing the required capability; do not silently substitute a null world.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor OperationUnsupported{
        .domain = PhysicsDomain,
        .code = ErrorCode{"physics.operation.unsupported"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The physics profile does not support this operation.",
        .remediationHint = "Use a supported operation or explicitly admit a qualified profile that supports it.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor InvalidState{
        .domain = PhysicsDomain,
        .code = ErrorCode{"physics.state.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The physics world lifecycle phase cannot admit this operation.",
        .remediationHint = "Submit work only during its declared owner-thread phase and before world admission closes.",
        .retryable = false,
        .userActionable = false,
    };
}  // namespace Horo::Physics::PhysicsErrors
