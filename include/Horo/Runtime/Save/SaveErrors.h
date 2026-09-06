#pragma once

/**
 * @file SaveErrors.h
 * @brief Stable errors for runtime-save value validation and bounded snapshot construction.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Runtime::SaveErrors {
    /** @brief A persistent save identity was missing or used the reserved all-zero value. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief Persistent identity text or bytes were not in the canonical representation. */
    extern const ErrorCodeDescriptor IdentityMalformed;
    /** @brief A collection contained the same persistent identity more than once. */
    extern const ErrorCodeDescriptor IdentityDuplicate;
    /** @brief A participant type identity was empty, oversized, or noncanonical. */
    extern const ErrorCodeDescriptor ParticipantIdInvalid;
    /** @brief A schema or format version used the reserved zero value. */
    extern const ErrorCodeDescriptor VersionInvalid;
    /** @brief Input requires a newer schema or format version than the reader supports. */
    extern const ErrorCodeDescriptor VersionUnsupportedNewer;
    /** @brief A save participant descriptor is incomplete or internally contradictory. */
    extern const ErrorCodeDescriptor ParticipantDescriptorInvalid;
    /** @brief A participant registration omitted its owned adapter lease. */
    extern const ErrorCodeDescriptor ParticipantAdapterMissing;
    /** @brief The registry already contains the participant identity. */
    extern const ErrorCodeDescriptor ParticipantDuplicate;
    /** @brief Two participants claim ownership of the same canonical record identity. */
    extern const ErrorCodeDescriptor ParticipantRecordOwnershipDuplicate;
    /** @brief Participant registration or publication was attempted after registry shutdown. */
    extern const ErrorCodeDescriptor ParticipantRegistryClosed;
    /** @brief The bounded participant registry has no remaining capacity. */
    extern const ErrorCodeDescriptor ParticipantRegistryCapacityExceeded;
    /** @brief A participant declares a dependency absent from the registry snapshot. */
    extern const ErrorCodeDescriptor ParticipantDependencyMissing;
    /** @brief Participant dependencies contain a cycle. */
    extern const ErrorCodeDescriptor ParticipantDependencyCycle;
    /** @brief The registry generation cannot advance without reusing a value. */
    extern const ErrorCodeDescriptor ParticipantRegistryGenerationExhausted;
    /** @brief Save header JSON was malformed, noncanonical, or had an invalid exact shape. */
    extern const ErrorCodeDescriptor ArchiveHeaderInvalid;
    /** @brief Save manifest JSON was malformed, noncanonical, duplicated, or out of order. */
    extern const ErrorCodeDescriptor ArchiveManifestInvalid;
    /** @brief Save metadata exceeded an explicit byte, string, participant, or chunk bound. */
    extern const ErrorCodeDescriptor ArchiveMetadataLimitExceeded;
    /** @brief A chunk directory has unsafe bounds, ordering, ownership, alignment, or correspondence. */
    extern const ErrorCodeDescriptor ArchiveDirectoryInvalid;
    /** @brief A chunk directory or payload exceeds a trusted framing admission bound. */
    extern const ErrorCodeDescriptor ArchiveFramingLimitExceeded;
    /** @brief The stored payload is shorter or longer than its validated directory declares. */
    extern const ErrorCodeDescriptor ArchivePayloadTruncated;
    /** @brief A selected decoded chunk does not match its manifest checksum. */
    extern const ErrorCodeDescriptor ArchiveChunkHashMismatch;
}  // namespace Horo::Runtime::SaveErrors
