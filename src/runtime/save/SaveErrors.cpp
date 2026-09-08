#include "Horo/Runtime/Save/SaveErrors.h"

namespace Horo::Runtime::SaveErrors {
    namespace {
        const ErrorDomainId kDomain{"horo.save"};
        constexpr auto kError = ErrorSeverity::Error;
    }  // namespace

    const ErrorCodeDescriptor IdentityInvalid{kDomain, ErrorCode{"save.identity.invalid"}, kError,
                                              "A required save identity is missing or reserved.",
                                              "Supply a non-zero identity allocated by the owning authority."};
    const ErrorCodeDescriptor IdentityMalformed{kDomain, ErrorCode{"save.identity.malformed"}, kError,
                                                "A save identity is not in canonical form.",
                                                "Use the exact lowercase UUID representation."};
    const ErrorCodeDescriptor IdentityDuplicate{kDomain, ErrorCode{"save.identity.duplicate"}, kError,
                                                "A save identity occurs more than once.", "Provide each identity exactly once."};
    const ErrorCodeDescriptor ParticipantIdInvalid{kDomain, ErrorCode{"save.participant_id.invalid"}, kError,
                                                   "A save participant identity is not canonical.",
                                                   "Use a bounded lowercase dotted identity with letter-led segments."};
    const ErrorCodeDescriptor VersionInvalid{kDomain, ErrorCode{"save.version.invalid"}, kError,
                                             "A save version uses the reserved zero value.",
                                             "Supply an explicitly declared non-zero version."};
    const ErrorCodeDescriptor VersionUnsupportedNewer{kDomain, ErrorCode{"save.version.unsupported_newer"}, kError,
                                                      "The save version is newer than this reader supports.",
                                                      "Use a compatible reader or an explicit supported migration."};
    const ErrorCodeDescriptor ParticipantDescriptorInvalid{kDomain, ErrorCode{"save.participant.descriptor_invalid"}, kError,
                                                           "A save participant descriptor is invalid.",
                                                           "Correct its identity, version, roles, dependencies, ownership, and bounds."};
    const ErrorCodeDescriptor ParticipantAdapterMissing{kDomain, ErrorCode{"save.participant.adapter_missing"}, kError,
                                                        "A save participant adapter lease is missing.",
                                                        "Bind an owned adapter during explicit host composition."};
    const ErrorCodeDescriptor ParticipantDuplicate{kDomain, ErrorCode{"save.participant.duplicate"}, kError,
                                                   "A save participant identity is already registered.",
                                                   "Register each semantic participant identity exactly once."};
    const ErrorCodeDescriptor ParticipantRecordOwnershipDuplicate{kDomain, ErrorCode{"save.participant.record_ownership_duplicate"}, kError,
                                                                  "A canonical save record has more than one semantic owner.",
                                                                  "Assign each record identity to exactly one participant."};
    const ErrorCodeDescriptor ParticipantRegistryClosed{kDomain, ErrorCode{"save.participant.registry_closed"}, kError,
                                                        "The save participant registry is closed.",
                                                        "Register or rebind participants before the owning lifecycle closes."};
    const ErrorCodeDescriptor ParticipantRegistryCapacityExceeded{kDomain, ErrorCode{"save.participant.registry_capacity_exceeded"}, kError,
                                                                  "The save participant registry reached its bounded capacity.",
                                                                  "Reduce participant count or revise the explicit product limit."};
    const ErrorCodeDescriptor ParticipantDependencyMissing{kDomain, ErrorCode{"save.participant.dependency_missing"}, kError,
                                                           "A required save participant dependency is absent.",
                                                           "Register every declared dependency before publishing a snapshot."};
    const ErrorCodeDescriptor ParticipantDependencyCycle{kDomain, ErrorCode{"save.participant.dependency_cycle"}, kError,
                                                         "Save participant dependencies contain a cycle.",
                                                         "Remove the cycle so semantic ownership has an acyclic dependency graph."};
    const ErrorCodeDescriptor ParticipantRegistryGenerationExhausted{kDomain, ErrorCode{"save.participant.registry_generation_exhausted"},
                                                                     ErrorSeverity::Critical,
                                                                     "The save participant registry generation is exhausted.",
                                                                     "Stop the owning runtime instead of reusing a registry generation."};
    const ErrorCodeDescriptor CaptureContextInvalid{kDomain, ErrorCode{"save.capture.context_invalid"}, kError,
                                                    "Runtime save capture evidence or bounds are invalid.",
                                                    "Capture at one issued safe point with finite qualified limits."};
    const ErrorCodeDescriptor CaptureRegistryStale{kDomain, ErrorCode{"save.capture.registry_stale"}, kError,
                                                   "Runtime save capture addressed a different participant registry generation.",
                                                   "Restart capture with the exact currently pinned registry snapshot."};
    const ErrorCodeDescriptor CaptureRecordInvalid{kDomain, ErrorCode{"save.capture.record_invalid"}, kError,
                                                   "A canonical capture record contradicts its participant registration.",
                                                   "Use a registered capture owner, exact schema, and owned record identity."};
    const ErrorCodeDescriptor CaptureBudgetExceeded{kDomain, ErrorCode{"save.capture.budget_exceeded"}, kError,
                                                    "Runtime save capture would exceed a declared participant or operation bound.",
                                                    "Reject or defer capture before allocating detached payload storage."};
    const ErrorCodeDescriptor CaptureRecordDuplicate{kDomain, ErrorCode{"save.capture.record_duplicate"}, kError,
                                                     "A canonical record occurs more than once in one runtime save capture.",
                                                     "Supply each participant-owned record exactly once."};
    const ErrorCodeDescriptor CaptureIncomplete{kDomain, ErrorCode{"save.capture.incomplete"}, kError,
                                                "Runtime save capture is missing required participant records.",
                                                "Capture every owned record for required or participating optional owners at one epoch."};
    const ErrorCodeDescriptor CaptureAlreadySealed{kDomain, ErrorCode{"save.capture.already_sealed"}, kError,
                                                   "The runtime save capture builder is sealed, spent, or moved from.",
                                                   "Create a new owner-safe-point builder for another capture."};
    const ErrorCodeDescriptor CaptureAllocationFailed{kDomain,
                                                      ErrorCode{"save.capture.allocation_failed"},
                                                      kError,
                                                      "Host-owned runtime save capture storage could not be allocated.",
                                                      "Release admitted capture memory and retry at a later safe point.",
                                                      true};
    const ErrorCodeDescriptor
        CaptureAdapterContractInvalid{kDomain, ErrorCode{"save.capture.adapter_contract_invalid"}, kError,
                                      "A runtime save participant violated its scoped capture contract.",
                                      "Fix the adapter to honor its exact context, sink failures, and omission disposition."};
    const ErrorCodeDescriptor ArchiveHeaderInvalid{kDomain, ErrorCode{"save.archive.header_invalid"}, kError,
                                                   "Save archive header metadata is invalid.",
                                                   "Use the exact bounded canonical header schema and required fields."};
    const ErrorCodeDescriptor ArchiveManifestInvalid{kDomain, ErrorCode{"save.archive.manifest_invalid"}, kError,
                                                     "Save archive manifest metadata is invalid.",
                                                     "Use unique stable-sorted participant and chunk identities with valid schemas."};
    const ErrorCodeDescriptor ArchiveMetadataLimitExceeded{kDomain, ErrorCode{"save.archive.metadata_limit_exceeded"}, kError,
                                                           "Save archive metadata exceeds an admission bound.",
                                                           "Reduce metadata size or revise the trusted product limits."};
    const ErrorCodeDescriptor ArchiveDirectoryInvalid{kDomain, ErrorCode{"save.archive.directory_invalid"}, kError,
                                                      "Save archive chunk directory framing is invalid.",
                                                      "Use bounded, contiguous, aligned, uniquely owned chunk records."};
    const ErrorCodeDescriptor ArchiveFramingLimitExceeded{kDomain, ErrorCode{"save.archive.framing_limit_exceeded"}, kError,
                                                          "Save archive framing exceeds an admission bound.",
                                                          "Reduce entry or payload size or revise trusted product limits."};
    const ErrorCodeDescriptor ArchivePayloadTruncated{kDomain, ErrorCode{"save.archive.payload_truncated"}, kError,
                                                      "Save archive payload length contradicts its validated directory.",
                                                      "Provide the exact complete payload region before selecting a chunk."};
    const ErrorCodeDescriptor ArchiveChunkHashMismatch{kDomain, ErrorCode{"save.archive.chunk_hash_mismatch"}, kError,
                                                       "Decoded save chunk bytes do not match their declared digest.",
                                                       "Reject the archive and retain it for corruption diagnostics."};
    const ErrorCodeDescriptor SaveRootConfigurationInvalid{kDomain, ErrorCode{"save.root.configuration_invalid"}, kError,
                                                           "The platform save-root configuration is invalid.",
                                                           "Provide the required absolute platform state directory."};
    const ErrorCodeDescriptor SaveRootPlatformUnsupported{kDomain, ErrorCode{"save.root.platform_unsupported"}, kError,
                                                          "The selected save-root platform convention is unsupported.",
                                                          "Compose a Windows, macOS, Linux, or explicit test resolver."};
    const ErrorCodeDescriptor SaveRootUnavailable{kDomain,
                                                  ErrorCode{"save.root.unavailable"},
                                                  kError,
                                                  "The product save root is unavailable.",
                                                  "Check user-state storage availability and directory permissions.",
                                                  true,
                                                  true};
    const ErrorCodeDescriptor SaveRootContainmentViolation{kDomain, ErrorCode{"save.root.containment_violation"}, kError,
                                                           "The product save root failed containment validation.",
                                                           "Remove redirected or unexpected entries beneath the approved state root."};
    const ErrorCodeDescriptor NamespaceInvalid{kDomain, ErrorCode{"save.namespace.invalid"}, kError,
                                               "A save namespace identity or revision is invalid.",
                                               "Supply complete non-zero typed namespace identities and revisions."};
    const ErrorCodeDescriptor NamespaceUnavailable{kDomain, ErrorCode{"save.namespace.unavailable"}, kError,
                                                   "No save namespace is available for the requested operation.",
                                                   "Bind an available user/profile or server-world namespace first."};
    const ErrorCodeDescriptor NamespaceStale{kDomain, ErrorCode{"save.namespace.stale"}, kError,
                                             "The captured save namespace binding is stale.",
                                             "Reject the operation and acquire the current namespace binding."};
    const ErrorCodeDescriptor SlotMetadataInvalid{kDomain, ErrorCode{"save.slot.metadata_invalid"}, kError,
                                                  "Trusted save-slot publication metadata is invalid.",
                                                  "Supply complete typed catalog facts from a committed publication."};
    const ErrorCodeDescriptor SlotMetadataLimitExceeded{kDomain, ErrorCode{"save.slot.metadata_limit_exceeded"}, kError,
                                                        "Trusted save-slot publication metadata exceeds an admission bound.",
                                                        "Reduce bounded provenance metadata or revise the trusted product limit."};
    const ErrorCodeDescriptor SlotDisplayMetadataInvalid{kDomain, ErrorCode{"save.slot.display_metadata_invalid"}, kError,
                                                         "Save-slot presentation metadata is invalid.",
                                                         "Use optional bounded well-formed UTF-8 presentation text."};
    const ErrorCodeDescriptor SlotGenerationConflict{kDomain, ErrorCode{"save.slot.generation_conflict"}, kError,
                                                     "A save-slot replacement does not advance the same logical slot.",
                                                     "Keep the slot identity and allocate a new publication generation."};
    const ErrorCodeDescriptor CompositionUnsupported{kDomain, ErrorCode{"save.composition.unsupported"}, kError,
                                                     "The selected save composition does not support persistence.",
                                                     "Select a save-capable product composition before admission."};
    const ErrorCodeDescriptor CompositionInvalid{kDomain, ErrorCode{"save.composition.invalid"}, kError,
                                                 "A deterministic save composition input is invalid.",
                                                 "Supply finite positive limits and a valid typed operation request."};
    const ErrorCodeDescriptor CompositionCapacityExceeded{kDomain, ErrorCode{"save.composition.capacity_exceeded"}, kError,
                                                          "The deterministic save composition reached a declared bound.",
                                                          "Retire test state or increase the explicit test-only capacity."};
    const ErrorCodeDescriptor CompositionCancelled{kDomain, ErrorCode{"save.composition.cancelled"}, kError,
                                                   "The deterministic save operation was cancelled before publication.",
                                                   "Retry only when the owning operation remains valid."};
    const ErrorCodeDescriptor CompositionInjectedFailure{kDomain, ErrorCode{"save.composition.injected_failure"}, kError,
                                                         "The deterministic save operation reached its injected failure.",
                                                         "Remove the deliberate test fault before retrying."};
    const ErrorCodeDescriptor CompositionObjectMissing{kDomain, ErrorCode{"save.composition.object_missing"}, kError,
                                                       "No deterministic save object exists at the requested address.",
                                                       "Store the object before loading or removing it."};
}  // namespace Horo::Runtime::SaveErrors
