#include "Horo/WorldStreaming/WorldAuthoringContract.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingInternal.h"

namespace Horo::WorldStreaming {
    namespace {
        [[nodiscard]] constexpr bool IsSupported(const WorldAuthoringGranularity value) noexcept {
            return value == WorldAuthoringGranularity::SpatialPage;
        }

        [[nodiscard]] constexpr bool IsSupported(const WorldAuthoringCollaborationMode value) noexcept {
            return value == WorldAuthoringCollaborationMode::RevisionChecked;
        }

        [[nodiscard]] constexpr bool IsKnown(const WorldAuthoringOwnerState value) noexcept {
            return value >= WorldAuthoringOwnerState::Active && value <= WorldAuthoringOwnerState::Closed;
        }

        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Internal::Failure<T>(descriptor);
        }

        [[nodiscard]] Result<void> ValidateContext(const WorldAuthoringContract &contract, const WorldAuthoringAdmissionContext &context) {
            if (!context.expectedPartition.IsValid() || !IsKnown(context.ownerState) ||
                context.openPageCount > contract.Limits().maximumOpenPages) {
                return Failure<void>(WorldStreamingErrors::AuthoringContractInvalid);
            }
            if (context.currentPage.has_value() &&
                (!context.currentPage->IsValid() || context.currentPage->partition != context.expectedPartition)) {
                return Failure<void>(WorldStreamingErrors::AuthoringContractInvalid);
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<WorldAuthoringAdmissionKind> ValidateOpen(const WorldAuthoringContract &contract,
                                                                       const WorldAuthoringPageRequest &request,
                                                                       const WorldAuthoringAdmissionContext &context) {
            if (request.expectedRevision.has_value()) {
                return Failure<WorldAuthoringAdmissionKind>(WorldStreamingErrors::AuthoringRevisionStale);
            }
            if (context.openPageCount == contract.Limits().maximumOpenPages) {
                return Failure<WorldAuthoringAdmissionKind>(WorldStreamingErrors::AuthoringCapacityExceeded);
            }
            return Result<WorldAuthoringAdmissionKind>::Success(WorldAuthoringAdmissionKind::Open);
        }

        [[nodiscard]] Result<WorldAuthoringAdmissionKind> ValidateReplacement(const WorldAuthoringPageRequest &request,
                                                                              const WorldAuthoringPageDescriptor &current) {
            if (current.sourceAsset != request.candidate.sourceAsset) {
                return Failure<WorldAuthoringAdmissionKind>(WorldStreamingErrors::AuthoringIdentityConflict);
            }
            if (!request.expectedRevision.has_value() || *request.expectedRevision != current.revision) {
                return Failure<WorldAuthoringAdmissionKind>(WorldStreamingErrors::AuthoringRevisionStale);
            }
            const auto next = NextWorldAuthoringRevision(current.revision);
            if (next.HasError()) {
                return Result<WorldAuthoringAdmissionKind>::Failure(next.ErrorValue());
            }
            if (request.candidate.revision != next.Value()) {
                return Failure<WorldAuthoringAdmissionKind>(WorldStreamingErrors::AuthoringRevisionStale);
            }
            return Result<WorldAuthoringAdmissionKind>::Success(WorldAuthoringAdmissionKind::Replace);
        }
    }  // namespace

    /** @copydoc WorldAuthoringContract::Create */
    Result<WorldAuthoringContract> WorldAuthoringContract::Create(const WorldAuthoringContractVersion version,
                                                                  const WorldAuthoringGranularity granularity,
                                                                  const WorldAuthoringCollaborationMode collaboration,
                                                                  const WorldAuthoringContractLimits limits) {
        if (version.major != WorldAuthoringContractVersion::CurrentMajor || version.minor != WorldAuthoringContractVersion::CurrentMinor) {
            return Failure<WorldAuthoringContract>(WorldStreamingErrors::AuthoringVersionUnsupported);
        }
        if (!IsSupported(granularity) || !IsSupported(collaboration)) {
            return Failure<WorldAuthoringContract>(WorldStreamingErrors::AuthoringPolicyUnsupported);
        }
        if (limits.maximumOpenPages == 0) {
            return Failure<WorldAuthoringContract>(WorldStreamingErrors::AuthoringContractInvalid);
        }
        return Result<WorldAuthoringContract>::Success(WorldAuthoringContract{version, granularity, collaboration, limits});
    }

    /** @copydoc WorldAuthoringPageDescriptor::IsValid */
    bool WorldAuthoringPageDescriptor::IsValid() const noexcept {
        return partition.IsValid() && sourceAsset.IsValid() && revision.IsValid();
    }

    /** @copydoc ValidateWorldAuthoringAdmission */
    Result<WorldAuthoringAdmissionKind> ValidateWorldAuthoringAdmission(const WorldAuthoringContract &contract,
                                                                        const WorldAuthoringPageRequest &request,
                                                                        const WorldAuthoringAdmissionContext &context) {
        if (!request.candidate.IsValid() || (request.expectedRevision.has_value() && !request.expectedRevision->IsValid())) {
            return Failure<WorldAuthoringAdmissionKind>(WorldStreamingErrors::AuthoringContractInvalid);
        }
        if (const auto valid = ValidateContext(contract, context); valid.HasError()) {
            return Result<WorldAuthoringAdmissionKind>::Failure(valid.ErrorValue());
        }
        if (request.candidate.partition != context.expectedPartition) {
            return Failure<WorldAuthoringAdmissionKind>(WorldStreamingErrors::AuthoringIdentityConflict);
        }
        if (context.ownerState != WorldAuthoringOwnerState::Active) {
            return Failure<WorldAuthoringAdmissionKind>(WorldStreamingErrors::AuthoringLifecycleUnavailable);
        }
        if (!context.currentPage.has_value()) {
            return ValidateOpen(contract, request, context);
        }
        return ValidateReplacement(request, *context.currentPage);
    }
}  // namespace Horo::WorldStreaming
