#include "Horo/Network/NetworkErrors.h"
#include "Horo/Network/NetworkObjectIdentity.h"
#include "Horo/Network/NetworkObjectMapping.h"
#include "NetworkTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>

namespace Horo::Network {
    using TestSupport::RequireError;

    namespace {
        ReplicationAuthorityEpoch Epoch(const std::uint64_t value = 1) {
            return ReplicationAuthorityEpoch::Create(value).Value();
        }

        NetworkObjectId Object(const std::uint64_t slot, const std::uint32_t generation = 1, const std::uint64_t epoch = 1) {
            return NetworkObjectId::Create(Epoch(epoch), slot, generation).Value();
        }

        ReplicationSchemaId Schema(const std::uint64_t value = 7) {
            return ReplicationSchemaId::Create(value).Value();
        }

        Runtime::EntityRef Entity(const std::uint32_t index, const std::uint32_t generation = 1, const std::uint64_t scene = 9) {
            return {Runtime::SceneRuntimeId{scene}, Runtime::EntityId{index, generation}};
        }

        NetworkObjectMappingEntry Entry(const NetworkObjectId object, const Runtime::EntityRef entity, const std::uint16_t schemaMinor = 0,
                                        const std::optional<Runtime::SceneObjectId> authored = std::nullopt) {
            return {object, entity, {Schema(), {1, schemaMinor}, authored}};
        }
    }  // namespace

    TEST_CASE("Network object identities reject zero and never wrap generations", "[unit][network][replication][identity]") {
        RequireError(ReplicationAuthorityEpoch::Create(0), NetworkErrors::NetworkObjectIdentityInvalid);
        RequireError(NetworkObjectId::Create({}, 1, 1), NetworkErrors::NetworkObjectIdentityInvalid);
        RequireError(NetworkObjectId::Create(Epoch(), 0, 1), NetworkErrors::NetworkObjectIdentityInvalid);
        RequireError(NetworkObjectId::Create(Epoch(), 1, 0), NetworkErrors::NetworkObjectIdentityInvalid);
        RequireError(NetworkObjectId{}.NextGeneration(), NetworkErrors::NetworkObjectIdentityInvalid);

        const auto current = Object(17, 3);
        REQUIRE(current.NextGeneration().Value() == Object(17, 4));
        const auto exhausted = Object(17, std::numeric_limits<std::uint32_t>::max());
        RequireError(exhausted.NextGeneration(), NetworkErrors::NetworkObjectGenerationExhausted);
    }

    TEST_CASE("Network object mapping owns exact bidirectional scene-qualified associations", "[unit][network][replication][mapping]") {
        auto mapping = NetworkObjectMapping::Create(Epoch(4), Runtime::SceneRuntimeId{9}, 3).Value();
        const auto authored = Runtime::SceneObjectId{33};
        const auto entry = Entry(Object(8, 1, 4), Entity(2, 5), 2, authored);
        REQUIRE(mapping.Register(entry).HasValue());
        REQUIRE(mapping.Resolve(entry.object).Value() == entry.entity);
        REQUIRE(mapping.Find(entry.entity).Value() == entry.object);
        REQUIRE(mapping.Size() == 1);

        auto snapshot = mapping.Snapshot().Value();
        REQUIRE(snapshot.Epoch() == Epoch(4));
        REQUIRE(snapshot.Scene() == Runtime::SceneRuntimeId{9});
        REQUIRE(snapshot.State() == NetworkObjectMappingState::Active);
        REQUIRE(snapshot.Entries().size() == 1);
        REQUIRE(snapshot.Entries().front() == entry);
        REQUIRE(snapshot.Entries().front().provenance.schema == Schema());
        REQUIRE(snapshot.Entries().front().provenance.schemaVersion == ReplicationSchemaVersion{1, 2});

        REQUIRE(mapping.Retire(entry.object).HasValue());
        REQUIRE(mapping.Size() == 0);
        REQUIRE(snapshot.Entries().size() == 1);
        REQUIRE(snapshot.Entries().front().provenance.authoredObject == authored);
    }

    TEST_CASE("Network object mapping rejects duplicate slots entities and malformed provenance", "[unit][network][replication][mapping]") {
        auto mapping = NetworkObjectMapping::Create(Epoch(), Runtime::SceneRuntimeId{9}, 3).Value();
        const auto first = Entry(Object(1), Entity(1));
        REQUIRE(mapping.Register(first).HasValue());
        RequireError(mapping.Register(Entry(Object(1), Entity(2))), NetworkErrors::NetworkObjectMappingConflict);
        RequireError(mapping.Register(Entry(Object(2), Entity(1))), NetworkErrors::NetworkObjectMappingConflict);
        RequireError(mapping.Register(Entry(Object(2), Entity(2, 1, 10))), NetworkErrors::NetworkObjectMappingInvalid);
        RequireError(mapping.Register(Entry(Object(2, 1, 2), Entity(2))), NetworkErrors::NetworkObjectMappingInvalid);

        auto malformed = Entry(Object(2), Entity(2));
        malformed.provenance.schema = {};
        RequireError(mapping.Register(malformed), NetworkErrors::NetworkObjectMappingInvalid);
        malformed = Entry(Object(2), Entity(2));
        malformed.provenance.schemaVersion = {};
        RequireError(mapping.Register(malformed), NetworkErrors::NetworkObjectMappingInvalid);
        malformed = Entry(Object(2), Entity(2));
        malformed.provenance.authoredObject = Runtime::SceneObjectId{};
        RequireError(mapping.Register(malformed), NetworkErrors::NetworkObjectMappingInvalid);
    }

    TEST_CASE("Bidirectional indexes stay canonical when slot and entity registration orders differ",
              "[unit][network][replication][mapping]") {
        auto mapping = NetworkObjectMapping::Create(Epoch(), Runtime::SceneRuntimeId{9}, 3).Value();
        const std::array entries{Entry(Object(9), Entity(2)), Entry(Object(3), Entity(7)), Entry(Object(5), Entity(1))};
        for (const auto &entry : entries)
            REQUIRE(mapping.Register(entry).HasValue());

        for (const auto &entry : entries) {
            REQUIRE(mapping.Resolve(entry.object).Value() == entry.entity);
            REQUIRE(mapping.Find(entry.entity).Value() == entry.object);
        }
        const auto snapshot = mapping.Snapshot().Value();
        REQUIRE(snapshot.Entries()[0].object == Object(3));
        REQUIRE(snapshot.Entries()[1].object == Object(5));
        REQUIRE(snapshot.Entries()[2].object == Object(9));
    }

    TEST_CASE("Retirement admits only the exact next generation and stale identities never alias reuse",
              "[unit][network][replication][mapping]") {
        auto mapping = NetworkObjectMapping::Create(Epoch(), Runtime::SceneRuntimeId{9}, 2).Value();
        const auto first = Object(5, 12);
        REQUIRE(mapping.Register(Entry(first, Entity(1, 4))).HasValue());
        REQUIRE(mapping.Retire(first).HasValue());
        RequireError(mapping.Retire(first), NetworkErrors::NetworkObjectMappingUnknown);
        RequireError(mapping.Register(Entry(Object(5, 14), Entity(2))), NetworkErrors::NetworkObjectMappingUnknown);
        RequireError(mapping.Register(Entry(first, Entity(2))), NetworkErrors::NetworkObjectMappingUnknown);

        const auto successor = first.NextGeneration().Value();
        REQUIRE(mapping.Register(Entry(successor, Entity(1, 5), 3)).HasValue());
        RequireError(mapping.Resolve(first), NetworkErrors::NetworkObjectMappingUnknown);
        REQUIRE(mapping.Resolve(successor).Value() == Entity(1, 5));
        RequireError(mapping.Find(Entity(1, 4)), NetworkErrors::NetworkObjectMappingUnknown);
    }

    TEST_CASE("Authority epoch replacement rejects identities from an older session even when slot generations match",
              "[unit][network][replication][mapping]") {
        auto replacement = NetworkObjectMapping::Create(Epoch(2), Runtime::SceneRuntimeId{10}, 1).Value();
        const auto current = Object(5, 1, 2);
        REQUIRE(replacement.Register(Entry(current, Entity(1, 1, 10))).HasValue());
        RequireError(replacement.Resolve(Object(5)), NetworkErrors::NetworkObjectMappingUnknown);
        RequireError(replacement.Retire(Object(5)), NetworkErrors::NetworkObjectMappingUnknown);
        REQUIRE(replacement.Resolve(current).Value() == Entity(1, 1, 10));
    }

    TEST_CASE("Retired tombstones consume bounded slot capacity while same-slot reuse remains admitted",
              "[unit][network][replication][mapping]") {
        auto mapping = NetworkObjectMapping::Create(Epoch(), Runtime::SceneRuntimeId{9}, 1).Value();
        const auto first = Object(4);
        REQUIRE(mapping.Register(Entry(first, Entity(1))).HasValue());
        REQUIRE(mapping.Retire(first).HasValue());
        RequireError(mapping.Register(Entry(Object(7), Entity(2))), NetworkErrors::NetworkObjectMappingCapacityExceeded);
        REQUIRE(mapping.Register(Entry(first.NextGeneration().Value(), Entity(2))).HasValue());
    }

    TEST_CASE("Exhausted retired slots stay tombstoned and cannot alias a later occurrence", "[unit][network][replication][mapping]") {
        auto mapping = NetworkObjectMapping::Create(Epoch(), Runtime::SceneRuntimeId{9}, 1).Value();
        const auto exhausted = Object(4, std::numeric_limits<std::uint32_t>::max());
        REQUIRE(mapping.Register(Entry(exhausted, Entity(1))).HasValue());
        REQUIRE(mapping.Retire(exhausted).HasValue());
        RequireError(mapping.Register(Entry(Object(4), Entity(2))), NetworkErrors::NetworkObjectGenerationExhausted);
        RequireError(mapping.Resolve(exhausted), NetworkErrors::NetworkObjectMappingUnknown);
    }

    TEST_CASE("Scene invalidation and shutdown are explicit terminal owner-thread lifecycle states",
              "[unit][network][replication][mapping]") {
        RequireError(NetworkObjectMapping::Create({}, Runtime::SceneRuntimeId{9}, 1), NetworkErrors::NetworkObjectMappingInvalid);
        RequireError(NetworkObjectMapping::Create(Epoch(), {}, 1), NetworkErrors::NetworkObjectMappingInvalid);
        RequireError(NetworkObjectMapping::Create(Epoch(), Runtime::SceneRuntimeId{9}, 0), NetworkErrors::NetworkObjectMappingInvalid);

        auto invalidated = NetworkObjectMapping::Create(Epoch(), Runtime::SceneRuntimeId{9}, 1).Value();
        const auto entry = Entry(Object(1), Entity(1));
        REQUIRE(invalidated.Register(entry).HasValue());
        RequireError(invalidated.InvalidateScene(Runtime::SceneRuntimeId{10}), NetworkErrors::NetworkObjectMappingInvalid);
        REQUIRE(invalidated.InvalidateScene(Runtime::SceneRuntimeId{9}).HasValue());
        REQUIRE(invalidated.State() == NetworkObjectMappingState::SceneInvalidated);
        REQUIRE(invalidated.Size() == 0);
        RequireError(invalidated.Resolve(entry.object), NetworkErrors::NetworkObjectMappingTerminal);
        RequireError(invalidated.Register(entry), NetworkErrors::NetworkObjectMappingTerminal);
        RequireError(invalidated.InvalidateScene(Runtime::SceneRuntimeId{9}), NetworkErrors::NetworkObjectMappingTerminal);
        REQUIRE(invalidated.Snapshot().Value().Entries().empty());

        auto shuttingDown = NetworkObjectMapping::Create(Epoch(2), Runtime::SceneRuntimeId{9}, 1).Value();
        const auto shutdownEntry = Entry(Object(1, 1, 2), Entity(1));
        REQUIRE(shuttingDown.Register(shutdownEntry).HasValue());
        shuttingDown.BeginShutdown();
        shuttingDown.BeginShutdown();
        REQUIRE(shuttingDown.State() == NetworkObjectMappingState::ShuttingDown);
        RequireError(shuttingDown.Find(shutdownEntry.entity), NetworkErrors::NetworkObjectMappingTerminal);
        RequireError(shuttingDown.Retire(shutdownEntry.object), NetworkErrors::NetworkObjectMappingTerminal);
        REQUIRE(shuttingDown.Snapshot().Value().Entries().size() == 1);
    }
}  // namespace Horo::Network
