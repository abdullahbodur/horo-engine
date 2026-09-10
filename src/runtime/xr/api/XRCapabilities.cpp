#include "Horo/XR/XRCapabilities.h"

#include "Horo/XR/XRErrors.h"

#include <algorithm>

namespace Horo::XR {
    namespace {
        template <typename Enum> bool IsKnown(const Enum value, const Enum count) noexcept {
            return value < count;
        }

        bool AreLimitsValid(const XRSystemLimits &limits) noexcept {
            return limits.maximumViews > 0 && limits.maximumViews <= XRHardLimits::MaximumViews && limits.maximumSpaces > 0 &&
                   limits.maximumSpaces <= XRHardLimits::MaximumSpaces && limits.maximumActions > 0 &&
                   limits.maximumActions <= XRHardLimits::MaximumActions && limits.maximumDevices > 0 &&
                   limits.maximumDevices <= XRHardLimits::MaximumDevices;
        }

        bool AreStatesValid(const XRCapabilityDescriptor &descriptor) noexcept {
            return std::ranges::all_of(descriptor.states, [](const XRCapabilityState state) {
                return IsKnown(state, XRCapabilityState::Count);
            });
        }

        bool Fits(const XRCapabilityRequirement &request, const XRSystemLimits &limits) noexcept {
            return request.views <= limits.maximumViews && request.spaces <= limits.maximumSpaces &&
                   request.actions <= limits.maximumActions && request.devices <= limits.maximumDevices;
        }

        Result<void> AdmitState(const XRCapabilityState state) {
            using enum XRCapabilityState;
            if (state == Available)
                return Result<void>::Success();
            if (state == Unsupported)
                return Result<void>::Failure(MakeError(XRErrors::OperationUnsupported));
            if (state == Incompatible)
                return Result<void>::Failure(MakeError(XRErrors::OperationIncompatible));
            return Result<void>::Failure(MakeError(XRErrors::OperationUnavailable));
        }
    }  // namespace

    /** @copydoc XRCapabilitySnapshot::Create */
    Result<XRCapabilitySnapshot> XRCapabilitySnapshot::Create(const XRCapabilityDescriptor &descriptor) {
        if (const auto version = RequireXRContractVersion(CurrentXRContractVersion, descriptor.contractVersion); version.HasError())
            return Result<XRCapabilitySnapshot>::Failure(version.ErrorValue());
        if (!descriptor.system.IsValid() || !descriptor.revision.IsValid() || !AreStatesValid(descriptor) ||
            !AreLimitsValid(descriptor.limits))
            return Result<XRCapabilitySnapshot>::Failure(MakeError(XRErrors::CapabilityDescriptorInvalid));
        return Result<XRCapabilitySnapshot>::Success(XRCapabilitySnapshot{descriptor});
    }

    /** @copydoc XRCapabilitySnapshot::XRCapabilitySnapshot */
    XRCapabilitySnapshot::XRCapabilitySnapshot(const XRCapabilityDescriptor &descriptor) noexcept
        : contractVersion_(descriptor.contractVersion), system_(descriptor.system), revision_(descriptor.revision),
          states_(descriptor.states), limits_(descriptor.limits) {}

    /** @copydoc XRCapabilitySnapshot::ContractVersion */
    XRContractVersion XRCapabilitySnapshot::ContractVersion() const noexcept {
        return contractVersion_;
    }

    /** @copydoc XRCapabilitySnapshot::System */
    const XRSystemId &XRCapabilitySnapshot::System() const noexcept {
        return system_;
    }

    /** @copydoc XRCapabilitySnapshot::Revision */
    XRCapabilityRevision XRCapabilitySnapshot::Revision() const noexcept {
        return revision_;
    }

    /** @copydoc XRCapabilitySnapshot::State */
    XRCapabilityState XRCapabilitySnapshot::State(const XRCapability capability) const noexcept {
        if (!IsKnown(capability, XRCapability::Count))
            return XRCapabilityState::Unsupported;
        return states_[static_cast<std::size_t>(capability)];
    }

    /** @copydoc XRCapabilitySnapshot::Limits */
    const XRSystemLimits &XRCapabilitySnapshot::Limits() const noexcept {
        return limits_;
    }

    /** @copydoc AdmitXRCapability */
    Result<void> AdmitXRCapability(const XRCapabilitySnapshot &snapshot, const XRSystemId &activeSystem,
                                   const XRCapabilityRevision expectedRevision, const XRCapabilityRequirement &requirement) {
        if (!expectedRevision.IsValid() || !IsKnown(requirement.capability, XRCapability::Count))
            return Result<void>::Failure(MakeError(XRErrors::OperationInvalid));
        if (const auto owner = ValidateXRSystem(snapshot.System(), activeSystem); owner.HasError())
            return owner;
        if (snapshot.Revision() != expectedRevision)
            return Result<void>::Failure(MakeError(XRErrors::CapabilityStale));
        if (const auto support = AdmitState(snapshot.State(requirement.capability)); support.HasError())
            return support;
        if (!Fits(requirement, snapshot.Limits()))
            return Result<void>::Failure(MakeError(XRErrors::CapacityExceeded));
        return Result<void>::Success();
    }
}  // namespace Horo::XR
