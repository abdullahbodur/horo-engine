#include "Horo/Prefab/PrefabErrors.h"

namespace Horo::Prefab::PrefabErrors {
    namespace {
        const ErrorDomainId Domain{"horo.prefab"};

        /** @brief Builds an immutable descriptor in the prefab error domain. */
        [[nodiscard]] ErrorCodeDescriptor Describe(const char *code, const char *summary, const char *remediationHint) {
            return {
                .domain = Domain,
                .code = ErrorCode{code},
                .defaultSeverity = ErrorSeverity::Error,
                .summary = summary,
                .remediationHint = remediationHint,
                .retryable = false,
                .userActionable = true,
            };
        }
    }  // namespace

    const ErrorCodeDescriptor IdentityInvalid = Describe("prefab.identity.invalid", "A prefab identity is invalid.",
                                                         "Provide the stable persisted identity required by the prefab contract.");

    const ErrorCodeDescriptor AddressInvalid = Describe("prefab.address.invalid", "A prefab-local address is invalid.",
                                                        "Use a bounded non-root placement scope and valid registered target identities.");

    const ErrorCodeDescriptor ReferenceInvalid = Describe("prefab.reference.invalid", "A prefab asset reference is invalid.",
                                                          "Use the non-zero AssetId from the Asset Registry sidecar.");
}  // namespace Horo::Prefab::PrefabErrors
