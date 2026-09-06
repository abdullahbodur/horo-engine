#include "Horo/Network/NetworkErrors.h"
#include "Horo/Network/TransportCapabilities.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>

namespace Horo::Network {
    namespace {
        constexpr std::size_t Index(const DeliveryPolicy policy) {
            return static_cast<std::size_t>(policy);
        }

        TransportCapabilities AvailableCapabilities() {
            TransportCapabilities capabilities;
            capabilities.revision = 7;
            capabilities.delivery.fill(TransportSupport::Unsupported);
            capabilities.delivery[Index(DeliveryPolicy::UnreliableUnordered)] = TransportSupport::Available;
            capabilities.delivery[Index(DeliveryPolicy::ReliableOrdered)] = TransportSupport::Available;
            capabilities.maximumChannels = 4;
            capabilities.maximumMessageBytes = 1200;
            capabilities.deadlines = TransportSupport::Available;
            capabilities.maximumDeadlineMilliseconds = 5000;
            return capabilities;
        }

        TransportRequirements Requirements() {
            TransportRequirements requirements;
            requirements.requiredDelivery[Index(DeliveryPolicy::ReliableOrdered)] = true;
            requirements.requiredChannels = 2;
            requirements.requiredMaximumMessageBytes = 1024;
            requirements.deadline = DeadlineRequirement::Required;
            requirements.requiredMaximumDeadlineMilliseconds = 1000;
            return requirements;
        }

        template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &error) {
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == error.code.Value());
        }
    }  // namespace

    static_assert(static_cast<std::size_t>(DeliveryPolicy::Count) == 4);

    TEST_CASE("Each transport delivery mode is an independent exact semantic", "[unit][network][transport][capabilities]") {
        constexpr std::array modes{DeliveryPolicy::UnreliableUnordered, DeliveryPolicy::UnreliableSequenced,
                                   DeliveryPolicy::ReliableOrdered, DeliveryPolicy::ReliableUnordered};
        for (const DeliveryPolicy mode : modes) {
            auto capabilities = AvailableCapabilities();
            capabilities.delivery.fill(TransportSupport::Unsupported);
            capabilities.delivery[Index(mode)] = TransportSupport::Available;
            auto requirements = Requirements();
            requirements.requiredDelivery.fill(false);
            requirements.requiredDelivery[Index(mode)] = true;

            const auto result = ResolveTransportCapabilities(capabilities, capabilities.revision, requirements);
            REQUIRE(result.HasValue());
            REQUIRE(result.Value().admittedDelivery[Index(mode)]);
            REQUIRE(std::ranges::count(result.Value().admittedDelivery, true) == 1);
        }
    }

    TEST_CASE("Transport capabilities validate exact delivery and finite optional-feature evidence",
              "[unit][network][transport][capabilities]") {
        REQUIRE(ValidateTransportCapabilities(AvailableCapabilities()));

        auto malformed = AvailableCapabilities();
        malformed.revision = 0;
        REQUIRE_FALSE(ValidateTransportCapabilities(malformed));

        malformed = AvailableCapabilities();
        malformed.delivery.front() = TransportSupport::Count;
        REQUIRE_FALSE(ValidateTransportCapabilities(malformed));

        malformed = AvailableCapabilities();
        malformed.maximumChannels = 0;
        REQUIRE_FALSE(ValidateTransportCapabilities(malformed));

        malformed = AvailableCapabilities();
        malformed.deadlines = TransportSupport::Unsupported;
        REQUIRE_FALSE(ValidateTransportCapabilities(malformed));

        malformed.maximumDeadlineMilliseconds = 0;
        REQUIRE(ValidateTransportCapabilities(malformed));
    }

    TEST_CASE("Transport negotiation preserves exact delivery channel size and revision", "[unit][network][transport][capabilities]") {
        const auto capabilities = AvailableCapabilities();
        const auto requirements = Requirements();
        const auto result = ResolveTransportCapabilities(capabilities, capabilities.revision, requirements);

        REQUIRE(result.HasValue());
        REQUIRE(result.Value().capabilityRevision == capabilities.revision);
        REQUIRE(result.Value().admittedDelivery == requirements.requiredDelivery);
        REQUIRE(result.Value().channelCount == requirements.requiredChannels);
        REQUIRE(result.Value().maximumMessageBytes == requirements.requiredMaximumMessageBytes);
        REQUIRE(result.Value().deadlinesEnabled);
        REQUIRE(result.Value().maximumDeadlineMilliseconds == requirements.requiredMaximumDeadlineMilliseconds);

        auto maximum = requirements;
        maximum.requiredChannels = capabilities.maximumChannels;
        maximum.requiredMaximumMessageBytes = capabilities.maximumMessageBytes;
        REQUIRE(ResolveTransportCapabilities(capabilities, capabilities.revision, maximum).HasValue());
    }

    TEST_CASE("Transport negotiation never downgrades required reliability or ordering", "[unit][network][transport][capabilities]") {
        auto capabilities = AvailableCapabilities();
        auto requirements = Requirements();
        requirements.requiredDelivery.fill(false);
        requirements.requiredDelivery[Index(DeliveryPolicy::ReliableUnordered)] = true;

        RequireError(ResolveTransportCapabilities(capabilities, capabilities.revision, requirements),
                     NetworkErrors::TransportDeliveryUnsupported);

        capabilities.delivery[Index(DeliveryPolicy::ReliableUnordered)] = TransportSupport::Unavailable;
        RequireError(ResolveTransportCapabilities(capabilities, capabilities.revision, requirements),
                     NetworkErrors::TransportCapabilityUnavailable);
    }

    TEST_CASE("Only an explicitly optional deadline may be disabled during negotiation", "[unit][network][transport][capabilities]") {
        auto capabilities = AvailableCapabilities();
        capabilities.deadlines = TransportSupport::Unsupported;
        capabilities.maximumDeadlineMilliseconds = 0;
        auto requirements = Requirements();

        RequireError(ResolveTransportCapabilities(capabilities, capabilities.revision, requirements),
                     NetworkErrors::TransportDeliveryUnsupported);

        requirements.deadline = DeadlineRequirement::Optional;
        const auto optional = ResolveTransportCapabilities(capabilities, capabilities.revision, requirements);
        REQUIRE(optional.HasValue());
        REQUIRE_FALSE(optional.Value().deadlinesEnabled);
        REQUIRE(optional.Value().maximumDeadlineMilliseconds == 0);

        capabilities.deadlines = TransportSupport::Available;
        capabilities.maximumDeadlineMilliseconds = 500;
        REQUIRE(ResolveTransportCapabilities(capabilities, capabilities.revision, requirements).HasValue());
        requirements.deadline = DeadlineRequirement::Required;
        RequireError(ResolveTransportCapabilities(capabilities, capabilities.revision, requirements),
                     NetworkErrors::TransportLimitExceeded);
    }

    TEST_CASE("Transport negotiation rejects malformed stale and over-limit requirements", "[unit][network][transport][capabilities]") {
        const auto capabilities = AvailableCapabilities();
        auto requirements = Requirements();

        RequireError(ResolveTransportCapabilities(capabilities, capabilities.revision + 1, requirements),
                     NetworkErrors::TransportCapabilityStale);

        requirements.requiredDelivery.fill(false);
        RequireError(ResolveTransportCapabilities(capabilities, capabilities.revision, requirements),
                     NetworkErrors::TransportCapabilityDescriptorInvalid);

        requirements = Requirements();
        requirements.requiredChannels = capabilities.maximumChannels + 1;
        RequireError(ResolveTransportCapabilities(capabilities, capabilities.revision, requirements),
                     NetworkErrors::TransportLimitExceeded);

        requirements = Requirements();
        requirements.requiredMaximumMessageBytes = capabilities.maximumMessageBytes + 1;
        RequireError(ResolveTransportCapabilities(capabilities, capabilities.revision, requirements),
                     NetworkErrors::TransportLimitExceeded);

        requirements = Requirements();
        requirements.deadline = DeadlineRequirement::Count;
        RequireError(ResolveTransportCapabilities(capabilities, capabilities.revision, requirements),
                     NetworkErrors::TransportCapabilityDescriptorInvalid);
    }

    TEST_CASE("Transport send admission accepts zero and maximum payload boundaries before queue mutation",
              "[unit][network][transport][capabilities]") {
        const auto capabilities = AvailableCapabilities();
        const auto selection = ResolveTransportCapabilities(capabilities, capabilities.revision, Requirements()).Value();

        TransportSendRequirement send{
            .delivery = DeliveryPolicy::ReliableOrdered,
            .channel = {.value = selection.channelCount - 1},
            .payloadBytes = 0,
            .deadlineMilliseconds = selection.maximumDeadlineMilliseconds,
        };
        REQUIRE(AdmitTransportSend(selection, selection.capabilityRevision, send, TransportAdmissionState::Accepting).HasValue());

        send.payloadBytes = selection.maximumMessageBytes;
        REQUIRE(AdmitTransportSend(selection, selection.capabilityRevision, send, TransportAdmissionState::Accepting).HasValue());
    }

    TEST_CASE("Transport send admission rejects stale unsupported channel size and deadline requests",
              "[unit][network][transport][capabilities]") {
        const auto capabilities = AvailableCapabilities();
        const auto selection = ResolveTransportCapabilities(capabilities, capabilities.revision, Requirements()).Value();
        TransportSendRequirement send{.delivery = DeliveryPolicy::ReliableOrdered};

        RequireError(AdmitTransportSend(selection, selection.capabilityRevision + 1, send, TransportAdmissionState::Accepting),
                     NetworkErrors::TransportCapabilityStale);

        send.delivery = DeliveryPolicy::UnreliableUnordered;
        RequireError(AdmitTransportSend(selection, selection.capabilityRevision, send, TransportAdmissionState::Accepting),
                     NetworkErrors::TransportDeliveryUnsupported);

        send.delivery = DeliveryPolicy::ReliableOrdered;
        send.channel.value = selection.channelCount;
        RequireError(AdmitTransportSend(selection, selection.capabilityRevision, send, TransportAdmissionState::Accepting),
                     NetworkErrors::TransportLimitExceeded);

        send.channel.value = 0;
        send.payloadBytes = selection.maximumMessageBytes + 1;
        RequireError(AdmitTransportSend(selection, selection.capabilityRevision, send, TransportAdmissionState::Accepting),
                     NetworkErrors::TransportLimitExceeded);

        send.payloadBytes = 1;
        send.deadlineMilliseconds = selection.maximumDeadlineMilliseconds + 1;
        RequireError(AdmitTransportSend(selection, selection.capabilityRevision, send, TransportAdmissionState::Accepting),
                     NetworkErrors::TransportLimitExceeded);
    }

    TEST_CASE("Transport admission treats cancellation and shutdown as terminal caller-owned values",
              "[unit][network][transport][capabilities]") {
        const auto capabilities = AvailableCapabilities();
        const auto selection = ResolveTransportCapabilities(capabilities, capabilities.revision, Requirements()).Value();
        const TransportSendRequirement send{.delivery = DeliveryPolicy::ReliableOrdered};

        RequireError(AdmitTransportSend(selection, selection.capabilityRevision, send, TransportAdmissionState::Cancelled),
                     NetworkErrors::TransportOperationCancelled);
        RequireError(AdmitTransportSend(selection, selection.capabilityRevision, send, TransportAdmissionState::ShuttingDown),
                     NetworkErrors::TransportShuttingDown);
        RequireError(AdmitTransportSend(selection, selection.capabilityRevision, send, TransportAdmissionState::Count),
                     NetworkErrors::TransportCapabilityDescriptorInvalid);
    }
}  // namespace Horo::Network
