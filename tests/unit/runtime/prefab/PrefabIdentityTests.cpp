#include "Horo/Prefab/PrefabIdentity.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace Horo::Prefab {
    namespace {
        Assets::AssetId Asset(const std::uint8_t suffix = 1) {
            std::array<std::uint8_t, 16> bytes{};
            bytes.back() = suffix;
            return Assets::AssetId::FromBytes(bytes);
        }

        Gameplay::ComponentTypeId ComponentType() {
            return Gameplay::ComponentTypeId::Parse("game.tests.prefab_component").Value();
        }

        TEST_CASE("Prefab identity domains reject zero without becoming interchangeable", "[unit][prefab][identity]") {
            REQUIRE(PrefabInstanceId::Create(0).HasError());
            REQUIRE(PrefabComponentInstanceId::Create(0).HasError());
            REQUIRE(PrefabPropertyId::Create(0).HasError());
            REQUIRE(PrefabInstanceId::Create(std::numeric_limits<std::uint64_t>::max()).HasValue());
            static_assert(!std::is_same_v<PrefabInstanceId, PrefabComponentInstanceId>);
            static_assert(!std::is_same_v<PrefabComponentInstanceId, PrefabPropertyId>);
        }

        TEST_CASE("Prefab assets retain Asset Registry identity without path authority", "[unit][prefab][identity]") {
            REQUIRE(PrefabAssetReference::Create({}).HasError());
            const auto reference = PrefabAssetReference::Create(Asset()).Value();
            REQUIRE(reference.IsValid());
            REQUIRE(reference.Asset() == Asset());
        }

        TEST_CASE("Prefab object addresses preserve exact nested placement scope", "[unit][prefab][identity]") {
            const std::array scope{LocalObjectId{4}, LocalObjectId{9}};
            const auto address = PrefabObjectAddress::Create(scope, LocalObjectId{7}).Value();
            REQUIRE(std::ranges::equal(address.NestedInstanceScope(), scope));
            REQUIRE(address.SourceObject() == LocalObjectId{7});
            REQUIRE_FALSE(address.SourceObject().IsRoot());

            const auto root = PrefabObjectAddress::Create({}, {}).Value();
            REQUIRE(root.SourceObject().IsRoot());
        }

        TEST_CASE("Prefab object addresses reject root placement segments and excessive depth", "[unit][prefab][identity]") {
            REQUIRE(PrefabObjectAddress::Create(std::array{LocalObjectId{}}, {}).HasError());
            const std::array<LocalObjectId, MaximumPrefabObjectScopeDepth + 1> excessive{};
            REQUIRE(PrefabObjectAddress::Create(excessive, {}).HasError());
        }

        TEST_CASE("Expanded object keys distinguish repeated instances and nested scopes", "[unit][prefab][identity]") {
            const auto instanceOne = PrefabInstanceId::Create(1).Value();
            const auto instanceTwo = PrefabInstanceId::Create(2).Value();
            const auto outer = PrefabObjectAddress::Create(std::array{LocalObjectId{3}}, LocalObjectId{5}).Value();
            const auto nested = PrefabObjectAddress::Create(std::array{LocalObjectId{7}, LocalObjectId{3}}, LocalObjectId{5}).Value();

            const ExpandedPrefabObjectKey first{instanceOne, outer};
            REQUIRE(first.IsValid());
            REQUIRE(first != ExpandedPrefabObjectKey{instanceTwo, outer});
            REQUIRE(first != ExpandedPrefabObjectKey{instanceOne, nested});
        }

        TEST_CASE("Property addresses require every registered identity dimension", "[unit][prefab][identity]") {
            const auto object = PrefabObjectAddress::Create({}, LocalObjectId{2}).Value();
            const auto component = PrefabComponentInstanceId::Create(7).Value();
            const auto property = PrefabPropertyId::Create(11).Value();
            const auto address = PrefabPropertyAddress::Create(object, ComponentType(), component, property);
            REQUIRE(address.HasValue());
            REQUIRE(address.Value().Object() == object);
            REQUIRE(address.Value().ComponentInstance() == component);
            REQUIRE(address.Value().Property() == property);
            REQUIRE(PrefabPropertyAddress::Create(object, {}, component, property).HasError());
            REQUIRE(PrefabPropertyAddress::Create(object, ComponentType(), {}, property).HasError());
            REQUIRE(PrefabPropertyAddress::Create(object, ComponentType(), component, {}).HasError());
        }

        TEST_CASE("Opaque prefab components preserve unknown project bytes verbatim", "[unit][prefab][identity]") {
            const std::vector<std::byte> bytes{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}};
            const RawComponentPayload payload{
                .instance = PrefabComponentInstanceId::Create(19).Value(),
                .component = {.typeId = ComponentType(), .schemaVersion = 7, .payload = bytes},
            };
            REQUIRE(ValidateRawComponentPayload(payload).HasValue());
            REQUIRE(payload.component.payload == bytes);

            auto invalid = payload;
            invalid.instance = {};
            REQUIRE(ValidateRawComponentPayload(invalid).HasError());
        }
    }  // namespace
}  // namespace Horo::Prefab
