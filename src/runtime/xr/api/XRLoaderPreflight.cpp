#include "Horo/XR/XRLoaderPreflight.h"

#include "Horo/XR/XRErrors.h"

namespace Horo::XR {
    namespace {
        /** @brief Creates a failed result from one stable XR descriptor. */
        template <typename Value> [[nodiscard]] Result<Value> Reject(const ErrorCodeDescriptor &descriptor) {
            return Result<Value>::Failure(MakeError(descriptor));
        }

        /** @brief Checks one closed inclusive loader API interval. */
        [[nodiscard]] bool ValidVersionRange(const XRLoaderApiVersionRange &range) noexcept {
            return range.minimum.IsValid() && range.maximum.IsValid() && range.minimum.major == range.maximum.major &&
                   range.minimum <= range.maximum;
        }

        /** @brief Checks the canonical absence of downstream evidence after an upstream failure. */
        [[nodiscard]] bool ConsistentEvidence(const XRLoaderProbeEvidence &evidence) noexcept {
            using enum XRLoaderAvailability;
            if (evidence.loader == Absent || evidence.loader == OpenFailed) {
                return !evidence.loaderApiVersion.IsValid() && evidence.runtime == XRRuntimeAvailability::Unavailable &&
                       evidence.system == XRSystemAvailability::Unsupported && !evidence.runtimeGeneration.IsValid();
            }
            if (evidence.loader == Incompatible) {
                return evidence.loaderApiVersion.IsValid() && evidence.runtime == XRRuntimeAvailability::Unavailable &&
                       evidence.system == XRSystemAvailability::Unsupported && !evidence.runtimeGeneration.IsValid();
            }
            if (!evidence.loaderApiVersion.IsValid())
                return false;
            if (evidence.runtime != XRRuntimeAvailability::Available) {
                return evidence.system == XRSystemAvailability::Unsupported && !evidence.runtimeGeneration.IsValid();
            }
            return evidence.system == XRSystemAvailability::Supported ? evidence.runtimeGeneration.IsValid()
                                                                      : !evidence.runtimeGeneration.IsValid();
        }

        /** @brief Maps loader discovery state to its stable failure identity. */
        [[nodiscard]] const ErrorCodeDescriptor *LoaderFailure(const XRLoaderAvailability availability) noexcept {
            using enum XRLoaderAvailability;
            switch (availability) {
                case Absent:
                    return &XRErrors::LoaderAbsent;
                case Incompatible:
                    return &XRErrors::LoaderIncompatible;
                case OpenFailed:
                    return &XRErrors::LoaderOpenFailed;
                case Available:
                case Count:
                    break;
            }
            return nullptr;
        }

        /** @brief Maps active-runtime discovery state to its stable failure identity. */
        [[nodiscard]] const ErrorCodeDescriptor *RuntimeFailure(const XRRuntimeAvailability availability) noexcept {
            using enum XRRuntimeAvailability;
            switch (availability) {
                case Unavailable:
                    return &XRErrors::RuntimeUnavailable;
                case Rejected:
                    return &XRErrors::RuntimeRejected;
                case Available:
                case Count:
                    break;
            }
            return nullptr;
        }

        /** @brief Maps system discovery state to its stable failure identity. */
        [[nodiscard]] const ErrorCodeDescriptor *SystemFailure(const XRSystemAvailability availability) noexcept {
            using enum XRSystemAvailability;
            switch (availability) {
                case Unsupported:
                    return &XRErrors::SystemUnsupported;
                case TemporarilyUnavailable:
                    return &XRErrors::SystemTemporarilyUnavailable;
                case Supported:
                case Count:
                    break;
            }
            return nullptr;
        }

        /** @brief Maps the first failed discovery layer to its stable actionable identity. */
        [[nodiscard]] const ErrorCodeDescriptor *DiscoveryFailure(const XRLoaderProbeEvidence &evidence) noexcept {
            if (const auto *failure = LoaderFailure(evidence.loader); failure != nullptr)
                return failure;
            if (const auto *failure = RuntimeFailure(evidence.runtime); failure != nullptr)
                return failure;
            return SystemFailure(evidence.system);
        }
    }  // namespace

    /** @copydoc CreateXRLoaderPreflightSnapshot */
    Result<XRLoaderPreflightSnapshot> CreateXRLoaderPreflightSnapshot(const XRLoaderPreflightRequest &request,
                                                                      const XRLoaderProbeEvidence &evidence) {
        if (auto version = RequireXRContractVersion(CurrentXRContractVersion, request.contractVersion); version.HasError())
            return Result<XRLoaderPreflightSnapshot>::Failure(version.ErrorValue());
        if (!request.attempt.IsValid() || !request.backend.IsValid() || !request.installRecord.IsValid() ||
            !request.productProfile.IsValid() || request.loaderSource >= XRLoaderSourcePolicy::Count ||
            request.productMode >= XRPreflightProductMode::Count || request.runtimeSelection >= XRRuntimeSelectionPolicy::Count ||
            !ValidVersionRange(request.admittedLoaderVersions))
            return Reject<XRLoaderPreflightSnapshot>(XRErrors::LoaderPreflightInvalid);
        if (request.cancellationRequested)
            return Reject<XRLoaderPreflightSnapshot>(XRErrors::LoaderPreflightCancelled);
        if (request.runtimeSelection == XRRuntimeSelectionPolicy::ApprovedDeveloperOverride &&
            (request.productMode != XRPreflightProductMode::Development || !request.developerOverrideApproved))
            return Reject<XRLoaderPreflightSnapshot>(XRErrors::RuntimeOverrideRejected);
        if (request.runtimeSelection == XRRuntimeSelectionPolicy::SystemDefault && request.developerOverrideApproved)
            return Reject<XRLoaderPreflightSnapshot>(XRErrors::LoaderPreflightInvalid);
        if (!evidence.attempt.IsValid() || evidence.attempt != request.attempt)
            return Reject<XRLoaderPreflightSnapshot>(XRErrors::LoaderPreflightStale);
        if (evidence.loader >= XRLoaderAvailability::Count || evidence.runtime >= XRRuntimeAvailability::Count ||
            evidence.system >= XRSystemAvailability::Count || evidence.consumedProbeSteps == 0 ||
            evidence.consumedProbeSteps > MaximumXRLoaderPreflightSteps || !ConsistentEvidence(evidence))
            return Reject<XRLoaderPreflightSnapshot>(XRErrors::LoaderPreflightInvalid);
        if (const auto *failure = DiscoveryFailure(evidence); failure != nullptr)
            return Reject<XRLoaderPreflightSnapshot>(*failure);
        if (evidence.loaderApiVersion < request.admittedLoaderVersions.minimum ||
            evidence.loaderApiVersion > request.admittedLoaderVersions.maximum)
            return Reject<XRLoaderPreflightSnapshot>(XRErrors::LoaderIncompatible);
        return Result<XRLoaderPreflightSnapshot>::Success(XRLoaderPreflightSnapshot{request, evidence});
    }

    XRLoaderPreflightSnapshot::XRLoaderPreflightSnapshot(const XRLoaderPreflightRequest &request,
                                                         const XRLoaderProbeEvidence &evidence) noexcept
        : contractVersion_(request.contractVersion), attempt_(request.attempt), backend_(request.backend),
          installRecord_(request.installRecord), productProfile_(request.productProfile), loaderSource_(request.loaderSource),
          runtimeSelection_(request.runtimeSelection), loaderApiVersion_(evidence.loaderApiVersion),
          runtimeGeneration_(evidence.runtimeGeneration), consumedProbeSteps_(evidence.consumedProbeSteps) {}

    /** @copydoc XRLoaderPreflightSnapshot::ContractVersion */
    XRContractVersion XRLoaderPreflightSnapshot::ContractVersion() const noexcept {
        return contractVersion_;
    }

    /** @copydoc XRLoaderPreflightSnapshot::Attempt */
    XRLoaderPreflightAttempt XRLoaderPreflightSnapshot::Attempt() const noexcept {
        return attempt_;
    }

    /** @copydoc XRLoaderPreflightSnapshot::Backend */
    XRBackendId XRLoaderPreflightSnapshot::Backend() const noexcept {
        return backend_;
    }

    /** @copydoc XRLoaderPreflightSnapshot::InstallRecord */
    XRInstallRecordId XRLoaderPreflightSnapshot::InstallRecord() const noexcept {
        return installRecord_;
    }

    /** @copydoc XRLoaderPreflightSnapshot::ProductProfile */
    XRProductProfileId XRLoaderPreflightSnapshot::ProductProfile() const noexcept {
        return productProfile_;
    }

    /** @copydoc XRLoaderPreflightSnapshot::LoaderSource */
    XRLoaderSourcePolicy XRLoaderPreflightSnapshot::LoaderSource() const noexcept {
        return loaderSource_;
    }

    /** @copydoc XRLoaderPreflightSnapshot::RuntimeSelection */
    XRRuntimeSelectionPolicy XRLoaderPreflightSnapshot::RuntimeSelection() const noexcept {
        return runtimeSelection_;
    }

    /** @copydoc XRLoaderPreflightSnapshot::LoaderApiVersion */
    XRLoaderApiVersion XRLoaderPreflightSnapshot::LoaderApiVersion() const noexcept {
        return loaderApiVersion_;
    }

    /** @copydoc XRLoaderPreflightSnapshot::RuntimeGeneration */
    XRRuntimeGeneration XRLoaderPreflightSnapshot::RuntimeGeneration() const noexcept {
        return runtimeGeneration_;
    }

    /** @copydoc XRLoaderPreflightSnapshot::ConsumedProbeSteps */
    std::uint8_t XRLoaderPreflightSnapshot::ConsumedProbeSteps() const noexcept {
        return consumedProbeSteps_;
    }

    /** @copydoc ValidateXRLoaderPreflight */
    Result<void> ValidateXRLoaderPreflight(const XRLoaderPreflightSnapshot &snapshot, const XRLoaderPreflightAttempt activeAttempt,
                                           const XRBackendId expectedBackend, const XRInstallRecordId expectedInstallRecord,
                                           const XRProductProfileId expectedProductProfile) {
        if (!activeAttempt.IsValid() || !expectedBackend.IsValid() || !expectedInstallRecord.IsValid() || !expectedProductProfile.IsValid())
            return Reject<void>(XRErrors::LoaderPreflightInvalid);
        if (snapshot.Attempt() != activeAttempt || snapshot.Backend() != expectedBackend ||
            snapshot.InstallRecord() != expectedInstallRecord || snapshot.ProductProfile() != expectedProductProfile)
            return Reject<void>(XRErrors::LoaderPreflightStale);
        return Result<void>::Success();
    }
}  // namespace Horo::XR
