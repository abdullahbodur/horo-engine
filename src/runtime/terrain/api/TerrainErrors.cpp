#include "Horo/Terrain/TerrainErrors.h"

#include <array>

namespace Horo::Terrain::TerrainErrors {
    namespace {
        const ErrorDomainId TerrainDomain{"horo.terrain"};
    }

    const ErrorCodeDescriptor IdentityInvalid{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.identity.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A terrain identity uses a reserved or incomplete representation.",
        .remediationHint = "Use canonical stable IDs or complete generation-safe handles issued by the owning Terrain runtime.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor SerializedIdentityInvalid{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.identity.serialized_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Serialized terrain identity bytes are invalid for the requested typed domain.",
        .remediationHint = "Restore the exact fixed-width canonical identity representation from verified project or cooked metadata.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor DerivationInvalid{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.identity.derivation_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Stable terrain identity derivation input is malformed or exceeds its hard bound.",
        .remediationHint = "Provide a valid project/tile/type owner and a non-empty canonical semantic key of at most 256 bytes.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor IdentityConflict{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.identity.conflict"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A terrain identity is duplicated within its typed catalog domain.",
        .remediationHint = "Assign a unique canonical authored key or remove the duplicate manifest entry before publication.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor IdentityUnknown{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.identity.unknown"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "A runtime foliage identity names a different terrain owner or slot.",
        .remediationHint = "Resolve the instance again from the exact active terrain runtime registry.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor GenerationStale{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.generation.stale"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "A terrain or foliage handle belongs to a replaced runtime generation.",
        .remediationHint = "Discard the stale handle and resolve the logical identity through the active generation.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor GenerationExhausted{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.generation.exhausted"},
        .defaultSeverity = ErrorSeverity::Critical,
        .summary = "A non-wrapping terrain generation or revision has reached its maximum value.",
        .remediationHint = "Close admission and retire the affected runtime owner; never wrap or reuse the generation.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor CapacityExceeded{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.identity.capacity_exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A terrain identity catalog exceeds its fixed validation ceiling.",
        .remediationHint = "Split the bounded publication operation without truncating or silently dropping identities.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor LifecycleUnavailable{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.lifecycle.unavailable"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "The terrain runtime is closing or closed and rejects identity access.",
        .remediationHint = "Stop submitting work and wait for the owning runtime to complete retirement.",
        .retryable = false,
        .userActionable = false,
    };

    /** @copydoc Descriptors */
    std::span<const ErrorCodeDescriptor *const> Descriptors() noexcept {
        static constexpr std::array descriptors{
            &IdentityInvalid, &SerializedIdentityInvalid, &DerivationInvalid, &IdentityConflict,     &IdentityUnknown,
            &GenerationStale, &GenerationExhausted,       &CapacityExceeded,  &LifecycleUnavailable,
        };
        return descriptors;
    }
}  // namespace Horo::Terrain::TerrainErrors
