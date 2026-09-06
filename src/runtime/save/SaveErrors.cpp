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
}  // namespace Horo::Runtime::SaveErrors
