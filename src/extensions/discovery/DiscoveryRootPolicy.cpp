#include "DiscoveryRootPolicy.h"

#include "DiscoveryPaths.h"

#include <algorithm>
#include <array>
#include <utility>

namespace Horo::Extensions::Discovery {
    namespace {
        const ErrorCodeDescriptor InvalidRootPolicy{
            .domain = ErrorDomainId{"horo.extensions"},
            .code = ErrorCode{"invalid_discovery_root_policy"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "The discovery root policy is invalid.",
            .remediationHint = "Supply no more than 64 roots with unique nonempty stable IDs.",
            .retryable = false,
            .userActionable = true,
        };

        /** @brief Require product ownership for system roots and local ownership for overrides. */
        bool HasValidAuthority(const RootRequest &request) {
            using enum RootKind;
            using enum ConfigurationOrigin;
            constexpr std::array allowedAuthorities{std::pair{System, ProductPolicy}, std::pair{User, UserLocal},
                                                    std::pair{Development, UserLocal}, std::pair{Project, ProjectPortable}};
            return std::ranges::find(allowedAuthorities, std::pair{request.kind, request.configuration}) != allowedAuthorities.end();
        }

        /** @brief Determine ignored requests without probing their paths. */
        RootDisposition Disposition(const RootRequest &request, const RootPolicy &policy) {
            using enum RootDisposition;
            if (!HasValidAuthority(request))
                return InvalidAuthority;
            if (request.approval != RootApproval::Approved)
                return ApprovalRequired;
            if (request.kind != RootKind::Development)
                return Accepted;
            if (policy.profile != DiscoveryProfile::Development || !policy.enableDevelopmentOverrides)
                return DevelopmentDisabled;
            return DevelopmentNonPortable;
        }

        /** @brief Validate identities before sorting so malformed inputs cannot affect accepted roots. */
        bool HasInvalidIds(std::span<const RootRequest> requests) {
            return std::ranges::any_of(requests, [](const RootRequest &request) {
                return request.id.empty();
            });
        }
    }  // namespace

    /** @copydoc SelectApprovedRoots */
    Result<RootSelection> SelectApprovedRoots(std::span<const RootRequest> requests, const RootPolicy &policy) {
        if (requests.size() > 64 || HasInvalidIds(requests))
            return Result<RootSelection>::Failure(MakeError(InvalidRootPolicy));
        std::vector<RootRequest> ordered(requests.begin(), requests.end());
        std::ranges::sort(ordered, {}, &RootRequest::id);
        if (std::ranges::adjacent_find(ordered, {}, &RootRequest::id) != ordered.end())
            return Result<RootSelection>::Failure(MakeError(InvalidRootPolicy, "Duplicate discovery root IDs."));
        RootSelection selection;
        for (const auto &request : ordered) {
            using enum RootDisposition;
            const auto disposition = Disposition(request, policy);
            selection.diagnostics.emplace_back(request.id, disposition);
            if (disposition != Accepted && disposition != DevelopmentNonPortable)
                continue;
            auto canonical = CanonicalRoot(request.path);
            if (canonical.HasError())
                return Result<RootSelection>::Failure(canonical.ErrorValue());
            selection.roots.emplace_back(request.id, std::move(canonical).Value(), request.kind);
            selection.nonPortable = selection.nonPortable || disposition == DevelopmentNonPortable;
        }
        return Result<RootSelection>::Success(std::move(selection));
    }
}  // namespace Horo::Extensions::Discovery
