#include "Horo/Release/ReleaseErrors.h"

namespace Horo::Release::ReleaseErrors {
    namespace {
        const ErrorDomainId Domain{"horo.release"};
    }

    const ErrorCodeDescriptor VersionInvalid{.domain = Domain,
                                             .code = ErrorCode{"release.version.invalid"},
                                             .defaultSeverity = ErrorSeverity::Error,
                                             .summary = "The release semantic version is invalid or non-canonical.",
                                             .remediationHint = "Provide bounded canonical SemVer 2.0 text.",
                                             .retryable = false,
                                             .userActionable = true};
    const ErrorCodeDescriptor TagInvalid{.domain = Domain,
                                         .code = ErrorCode{"release.version.tag_invalid"},
                                         .defaultSeverity = ErrorSeverity::Error,
                                         .summary = "The release tag is invalid or non-canonical.",
                                         .remediationHint = "Provide a lowercase v prefix followed by canonical SemVer.",
                                         .retryable = false,
                                         .userActionable = true};
    const ErrorCodeDescriptor AuthorityInvalid{.domain = Domain,
                                               .code = ErrorCode{"release.version.authority_invalid"},
                                               .defaultSeverity = ErrorSeverity::Error,
                                               .summary = "The release version authority input is incomplete or invalid.",
                                               .remediationHint =
                                                   "Provide one requested claim, bounded unique sources and a source revision.",
                                               .retryable = false,
                                               .userActionable = true};
    const ErrorCodeDescriptor AuthorityConflict{.domain = Domain,
                                                .code = ErrorCode{"release.version.authority_conflict"},
                                                .defaultSeverity = ErrorSeverity::Error,
                                                .summary = "Release version claims do not identify the same exact version.",
                                                .remediationHint = "Align the request, tag, manifest and release notes before building.",
                                                .retryable = false,
                                                .userActionable = true};
    const ErrorCodeDescriptor ProductKindMismatch{.domain = Domain,
                                                  .code = ErrorCode{"release.version.product_kind_mismatch"},
                                                  .defaultSeverity = ErrorSeverity::Error,
                                                  .summary = "Engine and game product version claims were mixed.",
                                                  .remediationHint = "Use one explicit product-version type throughout the candidate.",
                                                  .retryable = false,
                                                  .userActionable = true};
    const ErrorCodeDescriptor
        PersistentContractMismatch{.domain = Domain,
                                   .code = ErrorCode{"release.version.persistent_contract_mismatch"},
                                   .defaultSeverity = ErrorSeverity::Error,
                                   .summary = "The engine product version conflicts with persistent-contract metadata.",
                                   .remediationHint = "Align the engine core and prerelease with the compatibility release identity.",
                                   .retryable = false,
                                   .userActionable = true};
}  // namespace Horo::Release::ReleaseErrors
