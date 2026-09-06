#pragma once

#include "Horo/WorldStreaming/StreamingSourceDescriptor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace Horo::WorldStreaming::TestSupport {
    template <typename Identity> Identity IdentityFrom(const std::uint64_t value) {
        const auto result = Identity::Create(value);
        REQUIRE(result.HasValue());
        return result.Value();
    }

    inline WorldPartitionId World(const std::uint8_t discriminator = 1) {
        SerializedWorldPartitionId bytes{};
        bytes.back() = discriminator;
        return WorldPartitionId::Create(bytes).Value();
    }

    inline StreamingSourceOwnerToken Owner(const std::uint32_t generation = 1,
                                           const PartitionEpoch epoch = IdentityFrom<PartitionEpoch>(1)) {
        return {.partition = World(), .epoch = epoch, .slot = 3, .generation = generation};
    }

    inline StreamingSourceDescriptor Descriptor(const StreamingSourceRevision revision = IdentityFrom<StreamingSourceRevision>(1)) {
        return {.id = IdentityFrom<StreamingSourceId>(11),
                .owner = Owner(),
                .intent = StreamingSourceIntent::Camera,
                .priority = StreamingSourcePriority::Create(2.5F).Value(),
                .revision = revision};
    }

    inline StreamingSourceAdmissionContext Context() {
        return {.expectedOwner = Owner(),
                .currentRevision = std::nullopt,
                .activeSourceCount = 2,
                .sourceCapacity = 3,
                .ownerState = StreamingSourceOwnerState::Active};
    }
    template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &expected) {
        REQUIRE(result.HasError());
        REQUIRE(result.ErrorValue().code.Value() == expected.code.Value());
    }
}  // namespace Horo::WorldStreaming::TestSupport
