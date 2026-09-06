#pragma once

#include "Horo/Network/NetworkErrors.h"
#include "Horo/Network/ReplicationDescriptor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Horo::Network::TestSupport {
    inline constexpr ReplicationDescriptorLimits Limits{
        .maximumSchemas = 4,
        .maximumFieldsPerSchema = 8,
        .maximumOwnerIdentityBytes = 64,
        .maximumDefaultBytesPerField = 16,
        .maximumTotalDefaultBytes = 32,
    };

    inline ReplicationSchemaId SchemaId(const std::uint64_t value) {
        return ReplicationSchemaId::Create(value).Value();
    }

    inline FieldId FieldIdValue(const std::uint32_t value) {
        return FieldId::Create(value).Value();
    }

    inline ReplicationValueTypeId ValueType(const std::uint32_t value) {
        return ReplicationValueTypeId::Create(value).Value();
    }

    inline ReplicationCodecId Codec(const std::uint32_t value) {
        return ReplicationCodecId::Create(value).Value();
    }

    inline ReplicationFieldDescriptor Field(const std::uint32_t id = 1, const ReplicationSchemaVersion introduced = {1, 0}) {
        return {.id = FieldIdValue(id),
                .valueType = ValueType(1),
                .codec = Codec(1),
                .introducedVersion = introduced,
                .condition = ReplicationCondition::Always,
                .requirement = ReplicationFieldRequirement::Required,
                .writePolicy = ReplicationWritePolicy::AuthorityServerOnly,
                .limits = {.maximumEncodedBytes = 8, .maximumElementCount = 1}};
    }

    inline ReplicationSchemaDescriptor Schema(const std::uint64_t id = 10, std::vector<ReplicationFieldDescriptor> fields = {Field()}) {
        return {.id = SchemaId(id),
                .version = {1, 0},
                .compatibility = {.minimum = {1, 0}, .maximum = {1, 0}},
                .owner = {.value = "game.replication"},
                .fields = std::move(fields)};
    }

    template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &descriptor) {
        REQUIRE(result.HasError());
        REQUIRE(result.ErrorValue().code.Value() == descriptor.code.Value());
    }
}  // namespace Horo::Network::TestSupport
