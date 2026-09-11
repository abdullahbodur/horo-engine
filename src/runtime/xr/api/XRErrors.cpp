#include "Horo/XR/XRErrors.h"

#include <array>

namespace Horo::XR::XRErrors {
    namespace {
        const ErrorDomainId XRDomain{"horo.xr"};
    }

    const ErrorCodeDescriptor ContractVersionInvalid{
        .domain = XRDomain,
        .code = ErrorCode{"xr.contract.version_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The Horo XR contract version uses an invalid representation.",
        .remediationHint = "Use a non-zero Horo XRApi major version; do not substitute a native OpenXR API version.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor ContractVersionIncompatible{
        .domain = XRDomain,
        .code = ErrorCode{"xr.contract.version_incompatible"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The provided Horo XR contract cannot satisfy the required contract.",
        .remediationHint = "Compose an XR producer with the same major version and a sufficient minor version.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor IdentityInvalid{
        .domain = XRDomain,
        .code = ErrorCode{"xr.identity.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "An XR identity or its active owner uses an invalid representation.",
        .remediationHint = "Use only non-zero owner generations and generation-safe slots issued by the active XR owner.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor IdentityStale{
        .domain = XRDomain,
        .code = ErrorCode{"xr.identity.stale"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "An XR identity belongs to a replaced runtime, system, or session.",
        .remediationHint = "Resolve a new identity from the active XR generation before repeating the operation.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor CapabilityDescriptorInvalid{
        .domain = XRDomain,
        .code = ErrorCode{"xr.capability.descriptor_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "XR capability evidence contains an invalid identity, state, revision, or limit.",
        .remediationHint = "Publish complete typed evidence with a current system, known states, and limits within XRApi hard ceilings.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor CapabilityStale{
        .domain = XRDomain,
        .code = ErrorCode{"xr.capability.stale"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "XR capability evidence changed after it was captured.",
        .remediationHint = "Capture the current immutable capability snapshot and repeat complete admission.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor OperationUnsupported{
        .domain = XRDomain,
        .code = ErrorCode{"xr.operation.unsupported"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The selected XR system permanently does not support the requested operation.",
        .remediationHint = "Select a supported capability or use the product's explicitly admitted non-XR fallback.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor OperationUnavailable{
        .domain = XRDomain,
        .code = ErrorCode{"xr.operation.unavailable"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "The requested XR operation is known but not currently available.",
        .remediationHint = "Resolve the reported permission, policy, dependency, temporary-loss, or lifecycle state before retrying.",
        .retryable = true,
        .userActionable = true,
    };
    const ErrorCodeDescriptor OperationIncompatible{
        .domain = XRDomain,
        .code = ErrorCode{"xr.operation.incompatible"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The requested XR operation is incompatible with the selected system configuration.",
        .remediationHint = "Choose a product profile and composition whose declared XR capabilities are compatible.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor CapacityExceeded{
        .domain = XRDomain,
        .code = ErrorCode{"xr.operation.capacity_exceeded"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "The XR request exceeds an immutable system capacity limit.",
        .remediationHint = "Reduce the bounded request or select a qualified system with sufficient declared capacity.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor OperationInvalid{
        .domain = XRDomain,
        .code = ErrorCode{"xr.operation.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The XR operation request is malformed.",
        .remediationHint = "Use a known capability, non-zero captured revision, and finite typed bounds.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor CoordinateSpaceIncompatible{
        .domain = XRDomain,
        .code = ErrorCode{"xr.coordinate_space.incompatible"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "XR coordinate spaces cannot be related under the declared Horo convention.",
        .remediationHint = "Use a contiguous current-session path in metres and right-handed Y-up negative-Z-forward space.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor TimeDomainIncompatible{
        .domain = XRDomain,
        .code = ErrorCode{"xr.time_domain.incompatible"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "XR pose timing evidence mixes or omits required clock domains.",
        .remediationHint = "Correlate runtime samples explicitly with either simulation time or render prediction time.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor PoseInvalid{
        .domain = XRDomain,
        .code = ErrorCode{"xr.pose.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "XR pose values conflict with their validity, confidence, or loss evidence.",
        .remediationHint = "Publish finite unit pose values only for independently valid components and clear lost components.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor OriginRevisionStale{
        .domain = XRDomain,
        .code = ErrorCode{"xr.origin_revision.stale"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "XR coordinate evidence references a retired world-origin revision.",
        .remediationHint = "Relocate the pose through the current world-origin adapter before use.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor TrackingSnapshotInvalid{
        .domain = XRDomain,
        .code = ErrorCode{"xr.tracking_snapshot.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "XR tracking snapshot evidence is malformed, contradictory, or not canonically ordered.",
        .remediationHint = "Publish bounded canonical device and simulation-pose evidence from one exact session, time, and origin.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor TrackingSnapshotStale{
        .domain = XRDomain,
        .code = ErrorCode{"xr.tracking_snapshot.stale"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "The retained XR tracking snapshot revision has been replaced.",
        .remediationHint = "Acquire the current immutable tracking snapshot before querying device pose evidence.",
        .retryable = true,
        .userActionable = false,
    };

    /** @copydoc Descriptors */
    std::span<const ErrorCodeDescriptor *const> Descriptors() noexcept {
        static constexpr std::array descriptors{
            &ContractVersionInvalid,
            &ContractVersionIncompatible,
            &IdentityInvalid,
            &IdentityStale,
            &CapabilityDescriptorInvalid,
            &CapabilityStale,
            &OperationUnsupported,
            &OperationUnavailable,
            &OperationIncompatible,
            &CapacityExceeded,
            &OperationInvalid,
            &CoordinateSpaceIncompatible,
            &TimeDomainIncompatible,
            &PoseInvalid,
            &OriginRevisionStale,
            &TrackingSnapshotInvalid,
            &TrackingSnapshotStale,
        };
        return descriptors;
    }
}  // namespace Horo::XR::XRErrors
