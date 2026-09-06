#include "Horo/Network/TransportCapabilities.h"

#include "Horo/Network/NetworkErrors.h"

#include <algorithm>

namespace Horo::Network {
    namespace {
        template <typename Enum> [[nodiscard]] constexpr bool IsKnown(const Enum value, const Enum count) noexcept {
            return value < count;
        }

        [[nodiscard]] bool HasAvailableDelivery(const TransportCapabilities &capabilities) noexcept {
            return std::ranges::find(capabilities.delivery, TransportSupport::Available) != capabilities.delivery.end();
        }

        [[nodiscard]] bool ValidDeadlineEvidence(const TransportCapabilities &capabilities) noexcept {
            if (!IsKnown(capabilities.deadlines, TransportSupport::Count))
                return false;
            if (capabilities.deadlines == TransportSupport::Available)
                return capabilities.maximumDeadlineMilliseconds > 0;
            return capabilities.maximumDeadlineMilliseconds == 0;
        }

        [[nodiscard]] bool ValidateRequirements(const TransportRequirements &requirements) noexcept {
            if (std::ranges::find(requirements.requiredDelivery, true) == requirements.requiredDelivery.end())
                return false;
            if (requirements.requiredChannels == 0 || requirements.requiredMaximumMessageBytes == 0 ||
                !IsKnown(requirements.deadline, DeadlineRequirement::Count))
                return false;
            const bool hasDeadline = requirements.deadline != DeadlineRequirement::None;
            return hasDeadline == (requirements.requiredMaximumDeadlineMilliseconds > 0);
        }

        template <typename T> [[nodiscard]] Result<T> Unsupported() {
            return Result<T>::Failure(MakeError(NetworkErrors::TransportDeliveryUnsupported));
        }

        template <typename T> [[nodiscard]] Result<T> Unavailable() {
            return Result<T>::Failure(MakeError(NetworkErrors::TransportCapabilityUnavailable));
        }

        [[nodiscard]] Result<void> RequireSupport(const TransportSupport support) {
            if (support == TransportSupport::Available)
                return Result<void>::Success();
            if (support == TransportSupport::Unsupported)
                return Unsupported<void>();
            return Unavailable<void>();
        }

        [[nodiscard]] Result<bool> ResolveDeadline(const TransportCapabilities &capabilities, const TransportRequirements &requirements) {
            if (requirements.deadline == DeadlineRequirement::None)
                return Result<bool>::Success(false);

            if (const bool supported = capabilities.deadlines == TransportSupport::Available &&
                                       requirements.requiredMaximumDeadlineMilliseconds <= capabilities.maximumDeadlineMilliseconds;
                supported)
                return Result<bool>::Success(true);
            if (requirements.deadline == DeadlineRequirement::Optional)
                return Result<bool>::Success(false);
            if (const Result<void> support = RequireSupport(capabilities.deadlines); support.HasError())
                return Result<bool>::Failure(support.ErrorValue());
            return Result<bool>::Failure(MakeError(NetworkErrors::TransportLimitExceeded));
        }

        [[nodiscard]] Result<void> RequireDeliveryModes(const TransportCapabilities &capabilities,
                                                        const TransportRequirements &requirements) {
            for (std::size_t index = 0; index < requirements.requiredDelivery.size(); ++index) {
                if (!requirements.requiredDelivery[index])
                    continue;
                if (const Result<void> support = RequireSupport(capabilities.delivery[index]); support.HasError())
                    return support;
            }
            return Result<void>::Success();
        }

        [[nodiscard]] bool FitsSessionLimits(const TransportCapabilities &capabilities,
                                             const TransportRequirements &requirements) noexcept {
            return requirements.requiredChannels <= capabilities.maximumChannels &&
                   requirements.requiredMaximumMessageBytes <= capabilities.maximumMessageBytes;
        }

        [[nodiscard]] bool ValidateSelection(const TransportSelectionEvidence &selection) noexcept {
            if (selection.capabilityRevision == 0 || selection.channelCount == 0 || selection.maximumMessageBytes == 0 ||
                std::ranges::find(selection.admittedDelivery, true) == selection.admittedDelivery.end())
                return false;
            return selection.deadlinesEnabled == (selection.maximumDeadlineMilliseconds > 0);
        }

        [[nodiscard]] Result<void> AdmitState(const TransportAdmissionState state) {
            using enum TransportAdmissionState;
            if (!IsKnown(state, Count))
                return Result<void>::Failure(MakeError(NetworkErrors::TransportCapabilityDescriptorInvalid));
            if (state == Cancelled)
                return Result<void>::Failure(MakeError(NetworkErrors::TransportOperationCancelled));
            if (state == ShuttingDown)
                return Result<void>::Failure(MakeError(NetworkErrors::TransportShuttingDown));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> AdmitSendBounds(const TransportSelectionEvidence &selection,
                                                   const TransportSendRequirement &requirement) {
            if (requirement.channel.value >= selection.channelCount || requirement.payloadBytes > selection.maximumMessageBytes)
                return Result<void>::Failure(MakeError(NetworkErrors::TransportLimitExceeded));
            if (requirement.deadlineMilliseconds > 0 && !selection.deadlinesEnabled)
                return Result<void>::Failure(MakeError(NetworkErrors::TransportDeliveryUnsupported));
            if (requirement.deadlineMilliseconds > selection.maximumDeadlineMilliseconds)
                return Result<void>::Failure(MakeError(NetworkErrors::TransportLimitExceeded));
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc ValidateTransportCapabilities */
    bool ValidateTransportCapabilities(const TransportCapabilities &capabilities) noexcept {
        if (capabilities.contractVersion != 1 || capabilities.revision == 0)
            return false;
        if (!std::ranges::all_of(capabilities.delivery, [](const TransportSupport support) {
            return IsKnown(support, TransportSupport::Count);
        }))
            return false;
        const bool deliveryAvailable = HasAvailableDelivery(capabilities);
        if (deliveryAvailable != (capabilities.maximumChannels > 0) || deliveryAvailable != (capabilities.maximumMessageBytes > 0))
            return false;
        if (!deliveryAvailable && capabilities.deadlines == TransportSupport::Available)
            return false;
        return ValidDeadlineEvidence(capabilities);
    }

    /** @copydoc ResolveTransportCapabilities */
    Result<TransportSelectionEvidence> ResolveTransportCapabilities(const TransportCapabilities &capabilities,
                                                                    const std::uint64_t expectedRevision,
                                                                    const TransportRequirements &requirements) {
        if (!ValidateTransportCapabilities(capabilities) || !ValidateRequirements(requirements) || expectedRevision == 0)
            return Result<TransportSelectionEvidence>::Failure(MakeError(NetworkErrors::TransportCapabilityDescriptorInvalid));
        if (capabilities.revision != expectedRevision)
            return Result<TransportSelectionEvidence>::Failure(MakeError(NetworkErrors::TransportCapabilityStale));

        if (const Result<void> delivery = RequireDeliveryModes(capabilities, requirements); delivery.HasError())
            return Result<TransportSelectionEvidence>::Failure(delivery.ErrorValue());
        if (!FitsSessionLimits(capabilities, requirements))
            return Result<TransportSelectionEvidence>::Failure(MakeError(NetworkErrors::TransportLimitExceeded));

        Result<bool> deadlines = ResolveDeadline(capabilities, requirements);
        if (deadlines.HasError())
            return Result<TransportSelectionEvidence>::Failure(deadlines.ErrorValue());
        return Result<TransportSelectionEvidence>::Success({
            .capabilityRevision = capabilities.revision,
            .admittedDelivery = requirements.requiredDelivery,
            .channelCount = requirements.requiredChannels,
            .maximumMessageBytes = requirements.requiredMaximumMessageBytes,
            .deadlinesEnabled = deadlines.Value(),
            .maximumDeadlineMilliseconds = deadlines.Value() ? requirements.requiredMaximumDeadlineMilliseconds : 0,
        });
    }

    /** @copydoc AdmitTransportSend */
    Result<void> AdmitTransportSend(const TransportSelectionEvidence &selection, const std::uint64_t expectedRevision,
                                    const TransportSendRequirement &requirement, const TransportAdmissionState state) {
        if (!ValidateSelection(selection) || expectedRevision == 0 || !IsKnown(requirement.delivery, DeliveryPolicy::Count))
            return Result<void>::Failure(MakeError(NetworkErrors::TransportCapabilityDescriptorInvalid));
        if (const Result<void> admittedState = AdmitState(state); admittedState.HasError())
            return admittedState;
        if (selection.capabilityRevision != expectedRevision)
            return Result<void>::Failure(MakeError(NetworkErrors::TransportCapabilityStale));

        if (const auto deliveryIndex = static_cast<std::size_t>(requirement.delivery); !selection.admittedDelivery[deliveryIndex])
            return Unsupported<void>();
        return AdmitSendBounds(selection, requirement);
    }
}  // namespace Horo::Network
