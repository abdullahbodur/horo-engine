#pragma once

/**
 * @file ReleaseErrors.h
 * @brief Stable backend-neutral release-domain failure identities.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Release::ReleaseErrors {
    /** @brief Semantic version text is invalid, ambiguous, non-canonical, or too large. */
    extern const ErrorCodeDescriptor VersionInvalid;
    /** @brief Release tag does not contain one lowercase `v` plus canonical SemVer. */
    extern const ErrorCodeDescriptor TagInvalid;
    /** @brief Authority input is incomplete, malformed, or outside its bounded contract. */
    extern const ErrorCodeDescriptor AuthorityInvalid;
    /** @brief Duplicate sources or exact version identities conflict. */
    extern const ErrorCodeDescriptor AuthorityConflict;
    /** @brief Engine, game, and persistent-contract claim kinds were mixed. */
    extern const ErrorCodeDescriptor ProductKindMismatch;
    /** @brief Engine core or prerelease conflicts with durable compatibility metadata. */
    extern const ErrorCodeDescriptor PersistentContractMismatch;
    /** @brief A distribution product, build, package, or installation identity is malformed. */
    extern const ErrorCodeDescriptor DistributionIdentityInvalid;
    /** @brief A product, platform, artifact class, and package format combination is unsupported. */
    extern const ErrorCodeDescriptor DistributionCombinationUnsupported;
}  // namespace Horo::Release::ReleaseErrors
