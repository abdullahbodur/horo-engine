#include "Horo/Network/NetworkErrors.h"
#include "Horo/Network/ProtocolIdentityRegistry.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace Horo::Network {
    namespace {
        template <typename T> T Id(const std::uint16_t value) {
            return T::Create(value).Value();
        }

        template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &error) {
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == error.code.Value());
        }

        struct Fixture final {
            std::array<ProtocolIdentityDescriptor, 2> protocols{
                ProtocolIdentityDescriptor{Id<ProtocolId>(1), {{1, 0}, {1, 4}}},
                ProtocolIdentityDescriptor{Id<ProtocolId>(0x8000), {{2, 0}, {2, 2}}},
            };
            std::array<MessageIdentityDescriptor, 2> messages{
                MessageIdentityDescriptor{protocols[0].id, Id<MessageTypeId>(2), Id<MessageSchemaId>(3), {1, 0}, false},
                MessageIdentityDescriptor{protocols[1].id, Id<MessageTypeId>(0x8001), Id<MessageSchemaId>(0x8002), {2, 0}, true},
            };
            std::array<FeatureIdentityDescriptor, 2> features{
                FeatureIdentityDescriptor{protocols[0].id, Id<ProtocolFeatureId>(4), {1, 2}},
                FeatureIdentityDescriptor{protocols[1].id, Id<ProtocolFeatureId>(0x8003), {2, 1}},
            };
            std::array<CloseReasonIdentityDescriptor, 4> closeReasons{
                CloseReasonIdentityDescriptor{protocols[0].id, Id<CloseReasonId>(5), CloseReasonKind::Normal},
                CloseReasonIdentityDescriptor{protocols[0].id, Id<CloseReasonId>(6), CloseReasonKind::ProtocolError},
                CloseReasonIdentityDescriptor{protocols[0].id, Id<CloseReasonId>(7), CloseReasonKind::Timeout},
                CloseReasonIdentityDescriptor{protocols[0].id, Id<CloseReasonId>(8), CloseReasonKind::Shutdown},
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

        REQUIRE(registry.Value().FindProtocol(Id<ProtocolId>(1))->versions.maximum == ProtocolVersion{1, 4});
        REQUIRE(registry.Value().FindMessage(Id<ProtocolId>(1), Id<MessageTypeId>(2))->schema == Id<MessageSchemaId>(3));
        REQUIRE(registry.Value().FindFeature(Id<ProtocolId>(1), Id<ProtocolFeatureId>(4)).has_value());
        REQUIRE(registry.Value().FindCloseReason(Id<ProtocolId>(1), Id<CloseReasonId>(7))->kind == CloseReasonKind::Timeout);
        REQUIRE(registry.Value().FindCloseReason(Id<ProtocolId>(1), Id<CloseReasonId>(8))->kind == CloseReasonKind::Shutdown);
        REQUIRE_FALSE(registry.Value().FindMessage(Id<ProtocolId>(1), Id<MessageTypeId>(99)).has_value());
    }

    TEST_CASE("Protocol registry rejects duplicate message schema feature and close identities", "[unit][network][protocol]") {
        Fixture fixture;
        fixture.protocols[1] = fixture.protocols[0];
        RequireError(ProtocolIdentityRegistry::Create(fixture.Contributions()), NetworkErrors::ProtocolIdentityConflict);

        fixture = Fixture{};
        fixture.messages[1] = fixture.messages[0];
        RequireError(ProtocolIdentityRegistry::Create(fixture.Contributions()), NetworkErrors::ProtocolIdentityConflict);

        fixture = Fixture{};
        fixture.messages[1] = fixture.messages[0];
        fixture.messages[1].id = Id<MessageTypeId>(9);
        RequireError(ProtocolIdentityRegistry::Create(fixture.Contributions()), NetworkErrors::ProtocolIdentityConflict);

        fixture = Fixture{};
        fixture.features[1] = fixture.features[0];
        RequireError(ProtocolIdentityRegistry::Create(fixture.Contributions()), NetworkErrors::ProtocolIdentityConflict);

        fixture = Fixture{};
        fixture.closeReasons[1] = fixture.closeReasons[0];
        RequireError(ProtocolIdentityRegistry::Create(fixture.Contributions()), NetworkErrors::ProtocolIdentityConflict);
    }

    TEST_CASE("Protocol registry rejects malformed foreign hostile and over-capacity contributions", "[unit][network][protocol]") {
        Fixture fixture;
        fixture.messages[0].id = Id<MessageTypeId>(ProtocolId::GameMinimum);
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
        REQUIRE(registry.SelectVersion(Id<ProtocolId>(1), {{1, 2}, {1, 9}}).Value() == ProtocolVersion{1, 4});
        REQUIRE(registry.SelectVersion(Id<ProtocolId>(1), {{1, 0}, {1, 2}}).Value() == ProtocolVersion{1, 2});
        RequireError(registry.SelectVersion(Id<ProtocolId>(1), {{2, 0}, {2, 1}}), NetworkErrors::ProtocolVersionIncompatible);
        RequireError(registry.SelectVersion(Id<ProtocolId>(99), {{1, 0}, {1, 1}}), NetworkErrors::ProtocolIdentityUnknown);
        RequireError(registry.SelectVersion(Id<ProtocolId>(1), {{1, 4}, {1, 3}}), NetworkErrors::ProtocolIdentityDescriptorInvalid);
    }
}  // namespace Horo::Network
