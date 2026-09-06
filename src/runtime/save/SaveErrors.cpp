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
}  // namespace Horo::Runtime::SaveErrors
