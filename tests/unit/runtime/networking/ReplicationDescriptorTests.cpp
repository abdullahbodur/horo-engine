#include "ReplicationDescriptorTestSupport.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <type_traits>

namespace Horo::Network {
    using namespace TestSupport;

    static_assert(!std::is_same_v<ReplicationSchemaId, FieldId>);
    static_assert(!std::is_same_v<FieldId, ReplicationValueTypeId>);
    static_assert(sizeof(FieldId) == sizeof(std::uint32_t));

    TEST_CASE("Replication identities reserve zero and preserve domain type", "[unit][network][replication][descriptor]") {
        REQUIRE(ReplicationSchemaId::Create(0).HasError());
        REQUIRE(FieldId::Create(0).HasError());
        REQUIRE(ReplicationValueTypeId::Create(0).HasError());
        REQUIRE(ReplicationCodecId::Create(0).HasError());
        REQUIRE(FieldIdValue(7).Value() == 7);
        REQUIRE(SchemaId(7).Value() == 7);
    }

    TEST_CASE("Replication descriptor accepts bounded stable semantic metadata", "[unit][network][replication][descriptor]") {
        const ReplicationSchemaDescriptor descriptor = Schema();
        REQUIRE(ValidateReplicationSchemaDescriptor(descriptor, Limits).HasValue());
        REQUIRE(descriptor.fields.front().id == FieldIdValue(1));
    }

    TEST_CASE("Replication descriptor rejects malformed identity version owner and policy", "[unit][network][replication][descriptor]") {
        auto descriptor = Schema();
        descriptor.id = {};
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorInvalid);

        descriptor = Schema();
        descriptor.version = {};
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorInvalid);

        descriptor = Schema();
        descriptor.compatibility.maximum = {1, 1};
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorInvalid);

        descriptor = Schema();
        descriptor.owner.value = "Game Player";
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorInvalid);

        descriptor = Schema();
        descriptor.fields.front().condition = ReplicationCondition::Count;
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorInvalid);

        descriptor = Schema();
        descriptor.fields.front().writePolicy = ReplicationWritePolicy::Count;
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorInvalid);
    }

    TEST_CASE("Replication descriptor validates finite field limits and canonical defaults", "[unit][network][replication][descriptor]") {
        auto descriptor = Schema();
        descriptor.fields.front().limits.maximumEncodedBytes = 0;
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorInvalid);

        descriptor = Schema();
        descriptor.fields.front().requirement = ReplicationFieldRequirement::Optional;
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorInvalid);

        descriptor.fields.front().canonicalDefault = ReplicationFieldDefault{{std::byte{0x01}}};
        REQUIRE(ValidateReplicationSchemaDescriptor(descriptor, Limits).HasValue());

        descriptor.fields.front().canonicalDefault->canonicalBytes.resize(9);
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorInvalid);

        descriptor = Schema();
        descriptor.fields.front().canonicalDefault = ReplicationFieldDefault{};
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorInvalid);
    }

    TEST_CASE("Replication descriptor rejects conflicting fields tombstones and finite capacities",
              "[unit][network][replication][descriptor]") {
        auto descriptor = Schema();
        descriptor.fields = {Field(1), Field(2), Field(1)};
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorConflict);

        descriptor = Schema();
        descriptor.tombstonedFields = {FieldIdValue(2), FieldIdValue(2)};
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorConflict);

        descriptor = Schema();
        descriptor.tombstonedFields = {FieldIdValue(1)};
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorConflict);

        descriptor = Schema();
        ReplicationDescriptorLimits narrow = Limits;
        narrow.maximumFieldsPerSchema = 0;
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, narrow), NetworkErrors::ReplicationDescriptorInvalid);

        narrow = Limits;
        narrow.maximumOwnerIdentityBytes = 3;
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, narrow), NetworkErrors::ReplicationCapacityExceeded);

        descriptor = Schema();
        descriptor.fields.clear();
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorInvalid);
    }

    TEST_CASE("Compatible minor fields are optional and defaulted", "[unit][network][replication][descriptor]") {
        auto descriptor = Schema();
        descriptor.version = {1, 1};
        descriptor.compatibility.maximum = descriptor.version;
        auto added = Field(2);
        added.introducedVersion = {1, 1};
        descriptor.fields.push_back(added);
        RequireError(ValidateReplicationSchemaDescriptor(descriptor, Limits), NetworkErrors::ReplicationDescriptorIncompatible);

        descriptor.fields.back().requirement = ReplicationFieldRequirement::Optional;
        descriptor.fields.back().canonicalDefault = ReplicationFieldDefault{{std::byte{0x00}}};
        REQUIRE(ValidateReplicationSchemaDescriptor(descriptor, Limits).HasValue());
    }
}  // namespace Horo::Network
