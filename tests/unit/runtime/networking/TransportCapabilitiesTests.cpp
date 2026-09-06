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

        struct DeadlineCase final {
            TransportSupport support;
            DeadlineRequirement requirement;
            const ErrorCodeDescriptor *error;
            bool enabled;
        };

        const std::array DeadlineCases{
            DeadlineCase{TransportSupport::Unknown, DeadlineRequirement::None, nullptr, false},
            DeadlineCase{TransportSupport::Unknown, DeadlineRequirement::Optional, nullptr, false},
            DeadlineCase{TransportSupport::Unknown, DeadlineRequirement::Required, &NetworkErrors::TransportCapabilityUnavailable, false},
            DeadlineCase{TransportSupport::Unknown, DeadlineRequirement::Count, &NetworkErrors::TransportCapabilityDescriptorInvalid,
                         false},
            DeadlineCase{TransportSupport::Unsupported, DeadlineRequirement::None, nullptr, false},
            DeadlineCase{TransportSupport::Unsupported, DeadlineRequirement::Optional, nullptr, false},
            DeadlineCase{TransportSupport::Unsupported, DeadlineRequirement::Required, &NetworkErrors::TransportDeliveryUnsupported, false},
            DeadlineCase{TransportSupport::Unsupported, DeadlineRequirement::Count, &NetworkErrors::TransportCapabilityDescriptorInvalid,
                         false},
            DeadlineCase{TransportSupport::Unavailable, DeadlineRequirement::None, nullptr, false},
            DeadlineCase{TransportSupport::Unavailable, DeadlineRequirement::Optional, nullptr, false},
            DeadlineCase{TransportSupport::Unavailable, DeadlineRequirement::Required, &NetworkErrors::TransportCapabilityUnavailable,
                         false},
            DeadlineCase{TransportSupport::Unavailable, DeadlineRequirement::Count, &NetworkErrors::TransportCapabilityDescriptorInvalid,
                         false},
            DeadlineCase{TransportSupport::Available, DeadlineRequirement::None, nullptr, false},
            DeadlineCase{TransportSupport::Available, DeadlineRequirement::Optional, nullptr, true},
            DeadlineCase{TransportSupport::Available, DeadlineRequirement::Required, nullptr, true},
            DeadlineCase{TransportSupport::Available, DeadlineRequirement::Count, &NetworkErrors::TransportCapabilityDescriptorInvalid,
                         false},
            DeadlineCase{TransportSupport::Count, DeadlineRequirement::None, &NetworkErrors::TransportCapabilityDescriptorInvalid, false},
            DeadlineCase{TransportSupport::Count, DeadlineRequirement::Optional, &NetworkErrors::TransportCapabilityDescriptorInvalid,
                         false},
            DeadlineCase{TransportSupport::Count, DeadlineRequirement::Required, &NetworkErrors::TransportCapabilityDescriptorInvalid,
                         false},
            DeadlineCase{TransportSupport::Count, DeadlineRequirement::Count, &NetworkErrors::TransportCapabilityDescriptorInvalid, false},
        };

        void VerifyDeadlineCase(const DeadlineCase &testCase) {
            CAPTURE(testCase.support, testCase.requirement);
            auto capabilities = AvailableCapabilities();
            capabilities.deadlines = testCase.support;
            capabilities.maximumDeadlineMilliseconds = testCase.support == TransportSupport::Available ? 5000 : 0;
            auto requirements = Requirements();
            requirements.deadline = testCase.requirement;
            requirements.requiredMaximumDeadlineMilliseconds = testCase.requirement == DeadlineRequirement::None ? 0 : 1000;

            const auto result = ResolveTransportCapabilities(capabilities, capabilities.revision, requirements);
            if (testCase.error != nullptr)
                RequireError(result, *testCase.error);
            else {
                REQUIRE(result.HasValue());
                REQUIRE(result.Value().deadlinesEnabled == testCase.enabled);
            }
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
        malformed.maximumMessageBytes = 0;
        REQUIRE_FALSE(ValidateTransportCapabilities(malformed));

        malformed = AvailableCapabilities();
        malformed.delivery.fill(TransportSupport::Unsupported);
        malformed.maximumChannels = 0;
        REQUIRE_FALSE(ValidateTransportCapabilities(malformed));

        malformed = AvailableCapabilities();
        malformed.delivery.fill(TransportSupport::Unsupported);
        malformed.maximumMessageBytes = 0;
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

    TEST_CASE("Every deadline support and requirement combination has an explicit outcome", "[unit][network][transport][capabilities]") {
        for (const DeadlineCase &testCase : DeadlineCases)
            VerifyDeadlineCase(testCase);
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

    TEST_CASE("Every transport admission state has an explicit caller-owned outcome", "[unit][network][transport][capabilities]") {
        struct AdmissionCase final {
            TransportAdmissionState state;
            const ErrorCodeDescriptor *error;
        };

        const std::array cases{
            AdmissionCase{TransportAdmissionState::Accepting, nullptr},
            AdmissionCase{TransportAdmissionState::Cancelled, &NetworkErrors::TransportOperationCancelled},
            AdmissionCase{TransportAdmissionState::ShuttingDown, &NetworkErrors::TransportShuttingDown},
            AdmissionCase{TransportAdmissionState::Count, &NetworkErrors::TransportCapabilityDescriptorInvalid},
        };
        const auto capabilities = AvailableCapabilities();
        const auto selection = ResolveTransportCapabilities(capabilities, capabilities.revision, Requirements()).Value();
        const TransportSendRequirement send{.delivery = DeliveryPolicy::ReliableOrdered};

        for (const AdmissionCase &testCase : cases) {
            CAPTURE(testCase.state);
            const auto result = AdmitTransportSend(selection, selection.capabilityRevision, send, testCase.state);
            if (testCase.error != nullptr)
                RequireError(result, *testCase.error);
            else
                REQUIRE(result.HasValue());
        }
    }
}  // namespace Horo::Network
