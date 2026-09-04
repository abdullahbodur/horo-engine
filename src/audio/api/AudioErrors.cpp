#include "Horo/Audio/AudioErrors.h"

namespace Horo::Audio::AudioErrors {
    namespace {
        const ErrorDomainId AudioDomain{"horo.audio"};
    }

    const ErrorCodeDescriptor ResamplerInvalid{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.resampler.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The audio resampler request is invalid or outside supported bounds.",
        .remediationHint = "Provide supported rates, pitch, quality, channel and output-frame limits.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor ResamplerBudgetExceeded{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.resampler.budget_exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The audio resampler exceeds its admitted processing budget.",
        .remediationHint = "Reserve sufficient work, history and latency capacity before publication.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor MemoryInvalid{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.memory.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The audio memory reservation is invalid.",
        .remediationHint = "Supply a valid runtime owner and bounded aligned memory dimensions.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor MemoryBudgetExceeded{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.memory.budget_exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The audio memory reservation exceeds its admitted budget.",
        .remediationHint = "Reduce requested capacity or explicitly admit a supported memory profile before publication.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor MemoryAllocationFailed{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.memory.allocation_failed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Audio memory preparation could not allocate its backing storage.",
        .remediationHint = "Release quiescent resources before retrying preparation outside the callback.",
        .retryable = true,
        .userActionable = true,
    };
    const ErrorCodeDescriptor IdentityInvalid{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.identity.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The audio identity is invalid.",
        .remediationHint = "Provide a non-zero stable value or persistent asset identity from its owning registry.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor HandleMalformed{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.handle.malformed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The audio handle is malformed.",
        .remediationHint = "Use a handle issued by the active audio runtime registry.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor HandleOwnerMismatch{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.handle.owner_mismatch"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The audio handle belongs to another runtime generation.",
        .remediationHint = "Resolve the stable audio identity again after runtime replacement.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor HandleStale{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.handle.stale"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The audio handle generation is stale or retired.",
        .remediationHint = "Discard the handle and resolve the stable audio identity against the current runtime.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor HandleCapacityExhausted{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.handle.capacity_exhausted"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The bounded audio handle registry has no available slot.",
        .remediationHint = "Release inactive objects or increase the validated control-runtime capacity.",
        .retryable = true,
        .userActionable = true,
    };
    const ErrorCodeDescriptor HandleGenerationExhausted{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.handle.generation_exhausted"},
        .defaultSeverity = ErrorSeverity::Critical,
        .summary = "Every remaining audio handle slot exhausted its generation range.",
        .remediationHint = "Replace the audio runtime; exhausted slots are never reused.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor CapabilityUnavailable{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.capability.unavailable"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A required audio capability is unavailable.",
        .remediationHint = "Select a composition that explicitly provides the required capability.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor OperationUnsupported{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.operation.unsupported"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The selected audio composition does not support this operation.",
        .remediationHint = "Choose a supported operation or an audio provider that declares the capability.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor OperationCancelled{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.operation.cancelled"},
        .defaultSeverity = ErrorSeverity::Info,
        .summary = "The audio operation was cancelled.",
        .remediationHint = "Retry only if the owning runtime and producer generation remain active.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor RuntimeInactive{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.runtime.inactive"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The audio runtime is not active.",
        .remediationHint = "Submit work only after successful runtime activation and before shutdown begins.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor DeviceUnavailable{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.device.unavailable"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The selected audio device is unavailable.",
        .remediationHint = "Re-enumerate through the active backend or select an explicitly supported composition.",
        .retryable = true,
        .userActionable = true,
    };
    const ErrorCodeDescriptor BackendFailed{
        .domain = AudioDomain,
        .code = ErrorCode{"audio.backend.failed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The selected audio backend failed.",
        .remediationHint = "Inspect the bounded backend diagnostic cause and follow host recovery policy.",
        .retryable = true,
        .userActionable = false,
    };
}  // namespace Horo::Audio::AudioErrors
