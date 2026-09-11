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
    const ErrorCodeDescriptor DescriptorInvalid{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.descriptor.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A shared Terrain/Foliage descriptor is malformed or internally inconsistent.",
        .remediationHint =
            "Provide valid identities, non-zero revisions, ordered bounds, bounded dimensions, and coherent footprint facts.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor TierInvalid{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.tier.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A Terrain feature tier is outside the closed provider-neutral vocabulary.",
        .remediationHint = "Select Baseline, Standard, High, or Ultra through the typed product configuration.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor TierUnsupported{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.tier.unsupported"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The exact requested Terrain feature tier is unavailable in the captured plan.",
        .remediationHint = "Install compatible cooked and provider capabilities or explicitly select another product configuration.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor LimitProfileInvalid{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.limits.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A Terrain limit profile is empty, inconsistent, or exceeds its exact tier ceiling.",
        .remediationHint =
            "Use finite required limits and an all-zero or fully positive foliage group no wider than the selected versioned tier profile.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor LimitExceeded{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.limits.exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Terrain/Foliage descriptor dimensions, counts, bytes, or work exceed captured project limits.",
        .remediationHint = "Reject or recook the content under an explicitly larger compatible configuration; do not clamp or drop data.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor RevisionStale{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.revision.stale"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "Terrain descriptor admission references an outdated immutable revision.",
        .remediationHint = "Capture the current content, bounds, configuration, and capability revisions before retrying.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor ReplacementInvalid{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.replacement.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Terrain descriptor insert or replacement state contradicts the current publication.",
        .remediationHint = "Use Insert only without current state and Replace only with a complete exact current generation expectation.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor FoliageDefinitionInvalid{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.foliage.definition_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A foliage type definition is malformed or internally inconsistent.",
        .remediationHint = "Provide a valid type revision, bounded scales, and complete stable mesh and material asset references.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor FoliageFeatureUnsupported{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.foliage.feature_unsupported"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The exact foliage definition requires a feature absent from the captured capability plan.",
        .remediationHint = "Provide the required culling, impostor, wind, collision, or navigation capability; do not silently downgrade.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor FoliagePlacementInvalid{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.foliage.placement_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Foliage placement constraints or deterministic algorithm metadata are invalid.",
        .remediationHint =
            "Use the supported algorithm version and ordered bounded density, altitude, slope, separation, and quantization values.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor FoliageCullingInvalid{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.foliage.culling_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Foliage LOD, impostor, transition, or culling thresholds are invalid.",
        .remediationHint = "Provide strictly increasing populated LOD thresholds and a farther finite cull distance.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor FoliageWindInvalid{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.foliage.wind_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Foliage wind parameters contradict the selected fixed-point deformation model.",
        .remediationHint = "Zero every wind field for None, or provide bounded strengths, frequencies, flexibility, and required flutter.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor FoliageCollisionInvalid{
        .domain = TerrainDomain,
        .code = ErrorCode{"terrain.foliage.collision_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Foliage collision dimensions or response flags contradict the selected primitive.",
        .remediationHint = "Use no dimensions or flags for visual-only foliage, or a positive bounded cylinder or capsule.",
        .retryable = false,
        .userActionable = true,
    };

    /** @copydoc Descriptors */
    std::span<const ErrorCodeDescriptor *const> Descriptors() noexcept {
        static constexpr std::array descriptors{
            &IdentityInvalid,    &SerializedIdentityInvalid, &DerivationInvalid,         &IdentityConflict,        &IdentityUnknown,
            &GenerationStale,    &GenerationExhausted,       &CapacityExceeded,          &LifecycleUnavailable,    &DescriptorInvalid,
            &TierInvalid,        &TierUnsupported,           &LimitProfileInvalid,       &LimitExceeded,           &RevisionStale,
            &ReplacementInvalid, &FoliageDefinitionInvalid,  &FoliageFeatureUnsupported, &FoliagePlacementInvalid, &FoliageCullingInvalid,
            &FoliageWindInvalid, &FoliageCollisionInvalid,
        };
        return descriptors;
    }
}  // namespace Horo::Terrain::TerrainErrors
