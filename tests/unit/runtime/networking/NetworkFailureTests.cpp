#include "Horo/Network/NetworkErrors.h"
#include "Horo/Network/NetworkFailure.h"
#include "NetworkTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace Horo::Network {
    using TestSupport::RequireError;
    using TestSupport::WireIdentity;

    namespace {
        ConnectionHandle Connection(const std::uint32_t generation = 7) {
            return ConnectionHandle::Create(3, generation).Value();
        }

        NetworkTerminalRecord Terminal(const NetworkFailureKind kind = NetworkFailureKind::SessionCancelled) {
            return MakeNetworkTerminalRecord(NetworkFailureLayer::Session, kind).Value();
        }

        void RequireTerminalFailure(const Result<NetworkTerminalRecord> &result, const ErrorCodeDescriptor &descriptor) {
            RequireError(result, descriptor);
        }
    }  // namespace

    static_assert(!std::is_convertible_v<NetworkFailureLayer, NetworkFailureDisposition>);
    static_assert(!std::is_convertible_v<NetworkFailureKind, CloseReasonKind>);
    static_assert(!std::is_default_constructible_v<NetworkTerminalRecord>);
    static_assert(
        std::is_same_v<decltype(std::declval<const NetworkTerminalRecord &>().Context()), std::span<const NetworkFailureContextEntry>>);
    static_assert(std::is_same_v<decltype(std::declval<const NetworkTerminalOwner &>().Terminal()), const NetworkTerminalRecord *>);

    TEST_CASE("Network failure kinds map to one canonical layer disposition and stable code", "[unit][network][failure]") {
        struct Case final {
            NetworkFailureLayer layer;
            NetworkFailureKind kind;
            NetworkFailureDisposition disposition;
            const ErrorCodeDescriptor *descriptor;
        };

        const std::array cases{
            Case{NetworkFailureLayer::Transport, NetworkFailureKind::NameResolutionFailed, NetworkFailureDisposition::Retryable,
                 &NetworkErrors::NameResolutionFailed},
            Case{NetworkFailureLayer::Transport, NetworkFailureKind::TransportUnavailable, NetworkFailureDisposition::Retryable,
                 &NetworkErrors::TransportCapabilityUnavailable},
            Case{NetworkFailureLayer::Transport, NetworkFailureKind::TransportSaturated, NetworkFailureDisposition::Retryable,
                 &NetworkErrors::PacketQueueFull},
            Case{NetworkFailureLayer::Protocol, NetworkFailureKind::ProtocolMalformed, NetworkFailureDisposition::RemoteRejection,
                 &NetworkErrors::MessageEnvelopeInvalid},
            Case{NetworkFailureLayer::Protocol, NetworkFailureKind::ProtocolIncompatible, NetworkFailureDisposition::Incompatible,
                 &NetworkErrors::ProtocolVersionIncompatible},
            Case{NetworkFailureLayer::Session, NetworkFailureKind::SessionLocalPolicyRejected, NetworkFailureDisposition::LocalPolicy,
                 &NetworkErrors::SessionPolicyRejected},
            Case{NetworkFailureLayer::Session, NetworkFailureKind::SessionRemoteRejected, NetworkFailureDisposition::RemoteRejection,
                 &NetworkErrors::SessionRemoteRejected},
            Case{NetworkFailureLayer::Session, NetworkFailureKind::SessionCancelled, NetworkFailureDisposition::LocalPolicy,
                 &NetworkErrors::SessionCancelled},
            Case{NetworkFailureLayer::Session, NetworkFailureKind::SessionTimedOut, NetworkFailureDisposition::Retryable,
                 &NetworkErrors::SessionTimedOut},
            Case{NetworkFailureLayer::Session, NetworkFailureKind::SessionShutdown, NetworkFailureDisposition::LocalPolicy,
                 &NetworkErrors::SessionShuttingDown},
            Case{NetworkFailureLayer::Replication, NetworkFailureKind::ReplicationIncompatible, NetworkFailureDisposition::Incompatible,
                 &NetworkErrors::ReplicationDescriptorIncompatible},
            Case{NetworkFailureLayer::GameplayDispatch, NetworkFailureKind::GameplayDispatchRejected,
                 NetworkFailureDisposition::RemoteRejection, &NetworkErrors::GameplayDispatchRejected},
            Case{NetworkFailureLayer::GameplayDispatch, NetworkFailureKind::FatalInternal, NetworkFailureDisposition::Fatal,
                 &NetworkErrors::FatalFailure},
        };
        for (const auto &entry : cases) {
            const auto record = MakeNetworkTerminalRecord(entry.layer, entry.kind);
            REQUIRE(record.HasValue());
            REQUIRE(record.Value().Layer() == entry.layer);
            REQUIRE(record.Value().Disposition() == entry.disposition);
            REQUIRE(record.Value().Descriptor().code.Value() == entry.descriptor->code.Value());
            REQUIRE(record.Value().ToError().code.Value() == entry.descriptor->code.Value());
            REQUIRE_FALSE(record.Value().Descriptor().remediationHint.empty());
            REQUIRE(record.Value().Descriptor().retryable == (entry.disposition == NetworkFailureDisposition::Retryable));
        }
    }

    TEST_CASE("Network terminal mapping rejects unknown and cross-layer kinds", "[unit][network][failure]") {
        RequireTerminalFailure(MakeNetworkTerminalRecord(NetworkFailureLayer::Session, NetworkFailureKind::ProtocolMalformed),
                               NetworkErrors::TerminalRecordInvalid);
        RequireTerminalFailure(MakeNetworkTerminalRecord(NetworkFailureLayer::Count, NetworkFailureKind::FatalInternal),
                               NetworkErrors::TerminalRecordInvalid);
        RequireTerminalFailure(MakeNetworkTerminalRecord(NetworkFailureLayer::Session, NetworkFailureKind::Count),
                               NetworkErrors::TerminalRecordInvalid);
        RequireTerminalFailure(MakeNetworkTerminalRecord(static_cast<NetworkFailureLayer>(255), NetworkFailureKind::FatalInternal),
                               NetworkErrors::TerminalRecordInvalid);
    }

    TEST_CASE("Network terminal context is typed ordered bounded and snapshot-owned", "[unit][network][failure]") {
        const auto protocol = WireIdentity<ProtocolId>(1);
        const auto closeReason = WireIdentity<CloseReasonId>(2);
        const auto message = WireIdentity<MessageTypeId>(3);
        const auto epoch = ReplicationAuthorityEpoch::Create(4).Value();
        const auto object = NetworkObjectId::Create(epoch, 5, 6).Value();
        std::array context{
            NetworkFailureContextEntry{NetworkFailureContextKey::Connection, Connection().Diagnostic()},
            NetworkFailureContextEntry{NetworkFailureContextKey::Protocol, protocol},
            NetworkFailureContextEntry{NetworkFailureContextKey::CloseReason, closeReason},
            NetworkFailureContextEntry{NetworkFailureContextKey::NetworkObject, object},
            NetworkFailureContextEntry{NetworkFailureContextKey::SessionGeneration, std::uint64_t{7}},
            NetworkFailureContextEntry{NetworkFailureContextKey::MessageType, message},
            NetworkFailureContextEntry{NetworkFailureContextKey::RequestedBytes, std::uint64_t{0}},
            NetworkFailureContextEntry{NetworkFailureContextKey::Capacity, std::uint64_t{0}},
        };
        const auto record = MakeNetworkTerminalRecord(NetworkFailureLayer::Session, NetworkFailureKind::SessionRemoteRejected, context);
        REQUIRE(record.HasValue());
        REQUIRE(record.Value().Context().size() == MaximumNetworkFailureContextEntries);
        context[0].value = TransportHandleDiagnostic{99, 99};
        REQUIRE(std::get<TransportHandleDiagnostic>(record.Value().Context()[0].value) == Connection().Diagnostic());
    }

    TEST_CASE("Network terminal context rejects exact one-over duplicate unordered unknown and incompatible values",
              "[unit][network][failure]") {
        std::array<NetworkFailureContextEntry, MaximumNetworkFailureContextEntries + 1> oversized{};
        RequireTerminalFailure(MakeNetworkTerminalRecord(NetworkFailureLayer::Session, NetworkFailureKind::SessionCancelled, oversized),
                               NetworkErrors::TerminalRecordInvalid);
        const std::array duplicate{
            NetworkFailureContextEntry{NetworkFailureContextKey::Attempt, std::uint64_t{1}},
            NetworkFailureContextEntry{NetworkFailureContextKey::Attempt, std::uint64_t{2}},
        };
        RequireTerminalFailure(MakeNetworkTerminalRecord(NetworkFailureLayer::Session, NetworkFailureKind::SessionCancelled, duplicate),
                               NetworkErrors::TerminalRecordInvalid);
        const std::array unordered{
            NetworkFailureContextEntry{NetworkFailureContextKey::Capacity, std::uint64_t{2}},
            NetworkFailureContextEntry{NetworkFailureContextKey::RequestedBytes, std::uint64_t{1}},
        };
        RequireTerminalFailure(MakeNetworkTerminalRecord(NetworkFailureLayer::Session, NetworkFailureKind::SessionCancelled, unordered),
                               NetworkErrors::TerminalRecordInvalid);
        const std::array wrongType{NetworkFailureContextEntry{NetworkFailureContextKey::Protocol, std::uint64_t{1}}};
        RequireTerminalFailure(MakeNetworkTerminalRecord(NetworkFailureLayer::Protocol, NetworkFailureKind::ProtocolMalformed, wrongType),
                               NetworkErrors::TerminalRecordInvalid);
        const std::array invalidGeneration{NetworkFailureContextEntry{NetworkFailureContextKey::SessionGeneration, std::uint64_t{0}}};
        RequireTerminalFailure(MakeNetworkTerminalRecord(NetworkFailureLayer::Session, NetworkFailureKind::SessionCancelled,
                                                         invalidGeneration),
                               NetworkErrors::TerminalRecordInvalid);
        const std::array reasonWithoutProtocol{
            NetworkFailureContextEntry{NetworkFailureContextKey::CloseReason, WireIdentity<CloseReasonId>(1)}};
        RequireTerminalFailure(MakeNetworkTerminalRecord(NetworkFailureLayer::Session, NetworkFailureKind::SessionRemoteRejected,
                                                         reasonWithoutProtocol),
                               NetworkErrors::TerminalRecordInvalid);
    }

    TEST_CASE("Private backend text is never retained and evidence inspection is bounded", "[unit][network][failure]") {
        const std::string secret = "token=super-secret native socket failure";
        const auto normal =
            NormalizePrivateBackendFailure(NetworkFailureLayer::Transport, NetworkFailureKind::NameResolutionFailed, secret);
        REQUIRE(normal.HasValue());
        REQUIRE(normal.Value().BackendEvidence().observed);
        REQUIRE_FALSE(normal.Value().BackendEvidence().truncated);
        REQUIRE_FALSE(normal.Value().BackendEvidence().malformed);
        REQUIRE(normal.Value().ToError().message.find("super-secret") == std::string::npos);

        const std::string exact(MaximumPrivateBackendDetailBytes, 'x');
        const auto exactBoundary =
            NormalizePrivateBackendFailure(NetworkFailureLayer::Transport, NetworkFailureKind::TransportUnavailable, exact);
        REQUIRE(exactBoundary.HasValue());
        REQUIRE(exactBoundary.Value().BackendEvidence().observedBytes == MaximumPrivateBackendDetailBytes);
        REQUIRE_FALSE(exactBoundary.Value().BackendEvidence().truncated);

        std::string hostile(MaximumPrivateBackendDetailBytes + 1, 'x');
        hostile[2] = '\0';
        const auto bounded =
            NormalizePrivateBackendFailure(NetworkFailureLayer::Transport, NetworkFailureKind::TransportUnavailable, hostile);
        REQUIRE(bounded.HasValue());
        REQUIRE(bounded.Value().BackendEvidence().observedBytes == MaximumPrivateBackendDetailBytes + 1);
        REQUIRE(bounded.Value().BackendEvidence().truncated);
        REQUIRE(bounded.Value().BackendEvidence().malformed);

        const auto malformed =
            NormalizePrivateBackendFailure(NetworkFailureLayer::Transport, NetworkFailureKind::TransportUnavailable, "\xf0\x28\x8c\x28");
        REQUIRE(malformed.HasValue());
        REQUIRE(malformed.Value().BackendEvidence().malformed);

        const auto empty = NormalizePrivateBackendFailure(NetworkFailureLayer::Transport, NetworkFailureKind::TransportUnavailable, {});
        REQUIRE(empty.HasValue());
        REQUIRE_FALSE(empty.Value().BackendEvidence().observed);
        REQUIRE_FALSE(empty.Value().BackendEvidence().malformed);
    }

    TEST_CASE("Disabled instrumentation cannot change terminal identity disposition or context", "[unit][network][failure]") {
        const std::array context{
            NetworkFailureContextEntry{NetworkFailureContextKey::Connection, Connection().Diagnostic()},
        };
        const auto enabled = NormalizePrivateBackendFailure(NetworkFailureLayer::Transport, NetworkFailureKind::TransportSaturated,
                                                            "private", context, true);
        const auto disabled = NormalizePrivateBackendFailure(NetworkFailureLayer::Transport, NetworkFailureKind::TransportSaturated,
                                                             "private", context, false);
        REQUIRE(enabled.HasValue());
        REQUIRE(disabled.HasValue());
        REQUIRE(enabled.Value().Kind() == disabled.Value().Kind());
        REQUIRE(enabled.Value().Disposition() == disabled.Value().Disposition());
        REQUIRE(enabled.Value().Context().size() == disabled.Value().Context().size());
        REQUIRE(enabled.Value().BackendEvidence().observed);
        REQUIRE_FALSE(disabled.Value().BackendEvidence().observed);
    }

    TEST_CASE("Terminal owner publishes exactly once across success cancellation and shutdown ordering", "[unit][network][failure]") {
        auto successFirst = NetworkTerminalOwner::Create(Connection()).Value();
        REQUIRE(successFirst.Resolve(Connection(), Terminal(NetworkFailureKind::SessionCancelled)).HasValue());
        REQUIRE_FALSE(successFirst.IsAccepting());
        REQUIRE(successFirst.Terminal()->Kind() == NetworkFailureKind::SessionCancelled);
        RequireError(successFirst.Resolve(Connection(), Terminal(NetworkFailureKind::SessionShutdown)),
                     NetworkErrors::TerminalAlreadyResolved);
        REQUIRE(successFirst.Terminal()->Kind() == NetworkFailureKind::SessionCancelled);

        auto shutdownFirst = NetworkTerminalOwner::Create(Connection()).Value();
        REQUIRE(shutdownFirst.Resolve(Connection(), Terminal(NetworkFailureKind::SessionShutdown)).HasValue());
        RequireError(shutdownFirst.Resolve(Connection(), Terminal(NetworkFailureKind::SessionTimedOut)),
                     NetworkErrors::TerminalAlreadyResolved);
        REQUIRE(shutdownFirst.Terminal()->Kind() == NetworkFailureKind::SessionShutdown);
    }

    TEST_CASE("Terminal owner replacement rejects stale late callbacks and generation gaps", "[unit][network][failure]") {
        const auto first = Connection();
        const auto next = first.NextGeneration().Value();
        auto owner = NetworkTerminalOwner::Create(first).Value();
        REQUIRE(owner.Resolve(first, Terminal()).HasValue());
        RequireError(owner.Replace(first, Connection(first.Generation() + 2)), NetworkErrors::TerminalGenerationStale);
        REQUIRE(owner.Connection() == first);
        REQUIRE(owner.Replace(first, next).HasValue());
        REQUIRE(owner.IsAccepting());
        REQUIRE(owner.Terminal() == nullptr);
        RequireError(owner.Resolve(first, Terminal()), NetworkErrors::TerminalGenerationStale);
        REQUIRE(owner.Resolve(next, Terminal(NetworkFailureKind::SessionTimedOut)).HasValue());

        const auto exhausted = Connection(std::numeric_limits<std::uint32_t>::max());
        auto exhaustedOwner = NetworkTerminalOwner::Create(exhausted).Value();
        REQUIRE(exhaustedOwner.Resolve(exhausted, Terminal()).HasValue());
        RequireError(exhaustedOwner.Replace(exhausted, Connection()), NetworkErrors::TransportGenerationExhausted);
    }

    TEST_CASE("Terminal owner fails closed for malformed identities and pre-terminal replacement", "[unit][network][failure]") {
        RequireError(NetworkTerminalOwner::Create({}), NetworkErrors::TerminalRecordInvalid);
        auto owner = NetworkTerminalOwner::Create(Connection()).Value();
        RequireError(owner.Resolve({}, Terminal()), NetworkErrors::TerminalGenerationStale);
        const std::array foreignContext{
            NetworkFailureContextEntry{NetworkFailureContextKey::Connection, Connection(8).Diagnostic()},
        };
        auto foreignTerminal =
            MakeNetworkTerminalRecord(NetworkFailureLayer::Session, NetworkFailureKind::SessionCancelled, foreignContext).Value();
        RequireError(owner.Resolve(Connection(), std::move(foreignTerminal)), NetworkErrors::TerminalGenerationStale);
        RequireError(owner.Replace(Connection(), Connection(8)), NetworkErrors::TerminalGenerationStale);
        REQUIRE(owner.IsAccepting());
        REQUIRE(owner.Terminal() == nullptr);
    }
}  // namespace Horo::Network
