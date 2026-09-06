#include "Horo/Network/NetworkErrors.h"
#include "Horo/Network/ProtocolIdentityRegistry.h"
#include "NetworkTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace Horo::Network {
    using TestSupport::RequireError;
    using TestSupport::WireIdentity;

    namespace {
        struct Fixture final {
            std::array<ProtocolIdentityDescriptor, 2> protocols{
                ProtocolIdentityDescriptor{WireIdentity<ProtocolId>(1), {{1, 0}, {1, 4}}},
                ProtocolIdentityDescriptor{WireIdentity<ProtocolId>(0x8000), {{2, 0}, {2, 2}}},
            };
            std::array<MessageIdentityDescriptor, 2> messages{
                MessageIdentityDescriptor{protocols[0].id, WireIdentity<MessageTypeId>(2), WireIdentity<MessageSchemaId>(3), {1, 0}, false},
                MessageIdentityDescriptor{protocols[1].id,
                                          WireIdentity<MessageTypeId>(0x8001),
                                          WireIdentity<MessageSchemaId>(0x8002),
                                          {2, 0},
                                          true},
            };
            std::array<FeatureIdentityDescriptor, 2> features{
                FeatureIdentityDescriptor{protocols[0].id, WireIdentity<ProtocolFeatureId>(4), {1, 2}},
                FeatureIdentityDescriptor{protocols[1].id, WireIdentity<ProtocolFeatureId>(0x8003), {2, 1}},
            };
            std::array<CloseReasonIdentityDescriptor, 4> closeReasons{
                CloseReasonIdentityDescriptor{protocols[0].id, WireIdentity<CloseReasonId>(5), CloseReasonKind::Normal},
                CloseReasonIdentityDescriptor{protocols[0].id, WireIdentity<CloseReasonId>(6), CloseReasonKind::ProtocolError},
                CloseReasonIdentityDescriptor{protocols[0].id, WireIdentity<CloseReasonId>(7), CloseReasonKind::Timeout},
                CloseReasonIdentityDescriptor{protocols[0].id, WireIdentity<CloseReasonId>(8), CloseReasonKind::Shutdown},
            };

            ProtocolIdentityContributions Contributions() const {
                return {protocols, messages, features, closeReasons};
            }
        };
    }  // namespace

    static_assert(sizeof(ProtocolId::ValueType) == 2);
    static_assert(sizeof(MessageTypeId::ValueType) == 2);
    static_assert(sizeof(MessageSchemaId::ValueType) == 2);
    static_assert(sizeof(ProtocolFeatureId::ValueType) == 2);
    static_assert(sizeof(CloseReasonId::ValueType) == 2);
    static_assert(ProtocolId::EngineMaximum + 1U == ProtocolId::GameMinimum);
    static_assert(ProtocolId::GameMaximum == UINT16_MAX);

    TEST_CASE("Protocol identities preserve exact fixed-width namespace values across local renames", "[unit][network][protocol]") {
        REQUIRE_FALSE(ProtocolId::Create(0).HasValue());
        REQUIRE(ProtocolId{}.Namespace() == ProtocolIdentityNamespace::Count);
        const auto engine = ProtocolId::Create(ProtocolId::EngineMaximum).Value();
        const auto game = ProtocolId::Create(ProtocolId::GameMinimum).Value();
        const auto lastGame = ProtocolId::Create(ProtocolId::GameMaximum).Value();
        REQUIRE(engine.Namespace() == ProtocolIdentityNamespace::Engine);
        REQUIRE(game.Namespace() == ProtocolIdentityNamespace::Game);
        REQUIRE(lastGame.Namespace() == ProtocolIdentityNamespace::Game);
        const auto renamedCppSymbol = engine;
        REQUIRE(renamedCppSymbol.Value() == ProtocolId::EngineMaximum);
    }

    TEST_CASE("Protocol registry owns canonical inert descriptors and exact terminal reasons", "[unit][network][protocol]") {
        Fixture fixture;
        const auto registry = ProtocolIdentityRegistry::Create(fixture.Contributions());
        REQUIRE(registry.HasValue());
        fixture.protocols[0].versions.maximum = {9, 9};

        REQUIRE(registry.Value().FindProtocol(WireIdentity<ProtocolId>(1))->versions.maximum == ProtocolVersion{1, 4});
        REQUIRE(registry.Value().FindMessage(WireIdentity<ProtocolId>(1), WireIdentity<MessageTypeId>(2))->schema ==
                WireIdentity<MessageSchemaId>(3));
        REQUIRE(registry.Value().FindFeature(WireIdentity<ProtocolId>(1), WireIdentity<ProtocolFeatureId>(4)).has_value());
        REQUIRE(registry.Value().FindCloseReason(WireIdentity<ProtocolId>(1), WireIdentity<CloseReasonId>(7))->kind ==
                CloseReasonKind::Timeout);
        REQUIRE(registry.Value().FindCloseReason(WireIdentity<ProtocolId>(1), WireIdentity<CloseReasonId>(8))->kind ==
                CloseReasonKind::Shutdown);
        REQUIRE_FALSE(registry.Value().FindMessage(WireIdentity<ProtocolId>(1), WireIdentity<MessageTypeId>(99)).has_value());
    }

    TEST_CASE("Protocol registry rejects duplicate protocol message feature and close identities", "[unit][network][protocol]") {
        Fixture fixture;
        fixture.protocols[1] = fixture.protocols[0];
        RequireError(ProtocolIdentityRegistry::Create(fixture.Contributions()), NetworkErrors::ProtocolIdentityConflict);

        fixture = Fixture{};
        fixture.messages[1] = fixture.messages[0];
        RequireError(ProtocolIdentityRegistry::Create(fixture.Contributions()), NetworkErrors::ProtocolIdentityConflict);

        fixture = Fixture{};
        fixture.features[1] = fixture.features[0];
        RequireError(ProtocolIdentityRegistry::Create(fixture.Contributions()), NetworkErrors::ProtocolIdentityConflict);

        fixture = Fixture{};
        fixture.closeReasons[1] = fixture.closeReasons[0];
        RequireError(ProtocolIdentityRegistry::Create(fixture.Contributions()), NetworkErrors::ProtocolIdentityConflict);
    }

    TEST_CASE("Protocol registry permits shared schema identity and zero optional category capacities", "[unit][network][protocol]") {
        Fixture fixture;
        fixture.messages[1] = fixture.messages[0];
        fixture.messages[1].id = WireIdentity<MessageTypeId>(9);
        REQUIRE(ProtocolIdentityRegistry::Create(fixture.Contributions()).HasValue());

        ProtocolIdentityRegistryLimits limits;
        limits.maximumMessages = 0;
        limits.maximumFeatures = 0;
        limits.maximumCloseReasons = 0;
        const ProtocolIdentityContributions protocolOnly{fixture.protocols, {}, {}, {}};
        REQUIRE(ProtocolIdentityRegistry::Create(protocolOnly, limits).HasValue());

        limits.maximumProtocols = 0;
        RequireError(ProtocolIdentityRegistry::Create(protocolOnly, limits), NetworkErrors::ProtocolIdentityDescriptorInvalid);
    }

    TEST_CASE("Protocol registry rejects malformed foreign hostile and over-capacity contributions", "[unit][network][protocol]") {
        Fixture fixture;
        fixture.messages[0].id = WireIdentity<MessageTypeId>(ProtocolId::GameMinimum);
        RequireError(ProtocolIdentityRegistry::Create(fixture.Contributions()), NetworkErrors::ProtocolIdentityDescriptorInvalid);

        fixture = Fixture{};
        fixture.features[0].introduced = {1, 5};
        RequireError(ProtocolIdentityRegistry::Create(fixture.Contributions()), NetworkErrors::ProtocolIdentityDescriptorInvalid);

        fixture = Fixture{};
        fixture.closeReasons[0].kind = CloseReasonKind::Count;
        RequireError(ProtocolIdentityRegistry::Create(fixture.Contributions()), NetworkErrors::ProtocolIdentityDescriptorInvalid);

        fixture = Fixture{};
        ProtocolIdentityRegistryLimits limits;
        limits.maximumMessages = 1;
        RequireError(ProtocolIdentityRegistry::Create(fixture.Contributions(), limits), NetworkErrors::ProtocolIdentityCapacityExceeded);
    }

    TEST_CASE("Protocol version selection is highest mutual exact and rejects skew or unknown identity", "[unit][network][protocol]") {
        const Fixture fixture;
        const auto registry = ProtocolIdentityRegistry::Create(fixture.Contributions()).Value();
        REQUIRE(registry.SelectVersion(WireIdentity<ProtocolId>(1), {{1, 2}, {1, 9}}).Value() == ProtocolVersion{1, 4});
        REQUIRE(registry.SelectVersion(WireIdentity<ProtocolId>(1), {{1, 0}, {1, 2}}).Value() == ProtocolVersion{1, 2});
        RequireError(registry.SelectVersion(WireIdentity<ProtocolId>(1), {{2, 0}, {2, 1}}), NetworkErrors::ProtocolVersionIncompatible);
        RequireError(registry.SelectVersion(WireIdentity<ProtocolId>(99), {{1, 0}, {1, 1}}), NetworkErrors::ProtocolIdentityUnknown);
        RequireError(registry.SelectVersion(WireIdentity<ProtocolId>(1), {{1, 4}, {1, 3}}),
                     NetworkErrors::ProtocolIdentityDescriptorInvalid);
    }
}  // namespace Horo::Network
